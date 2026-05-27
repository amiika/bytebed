#include <WiFi.h>
#include <esp_task_wdt.h> 
#include <nvs_flash.h> 
#include "state.h"
#include "vm.h"
#include "fast_math.h"
#include "ui.h"
#include "fasti2s.h" 
#include "sync.h"
#include "usb_midi_handler.h"

// Global System Configuration
const bool WIPE_NVRAM = false; 
const bool FORCE_REWRITE_PRESETS = true;

// Global Sync State
bool is_sync_master = false;
bool is_sync_initialized = false;
volatile bool pending_code_update = false;
char pending_code_buffer[2048] = {0};
uint8_t pending_flags = 0;

// UI Visualization Buffer
#define UI_RING_SIZE 4096
volatile uint8_t ui_sample_ring[UI_RING_SIZE];
volatile uint32_t ui_ring_head = 0;
uint32_t ui_ring_tail = 0;

FastI2S audioOut;

// Native USB MIDI State
USBMidiHandler midiHandler;
bool usb_midi_enabled = false;
float current_midi_freq = 440.0f;
int current_midi_note = 0; 
TaskHandle_t midiTaskHandle = NULL; 

// Secure State Machine for Confirmations
enum ConfirmType { CONF_NONE, CONF_BANK, CONF_LOAD_PATCH, CONF_SAVE_PATCH };
static ConfirmType pending_confirm = CONF_NONE;
static int confirm_bank = -1;
static int confirm_slot = -1;

// Backup Buffers for Live Preview
static String backup_input_buffer = "";
static int backup_cursor_pos = 0;
static bool patch_edited = false;

extern volatile int32_t alloc_requested_size;
extern volatile bool alloc_request_pending;

/**
 * @brief Periodically checks and handles dynamic array requests from Core 1
 * without blocking the real-time audio thread.
 */
void handle_deferred_allocations() {
    if (__builtin_expect(alloc_request_pending, 0)) {
        int32_t target_size = alloc_requested_size;
        if (target_size > global_array_capacity) {
            ensureGlobalArray(target_size);
        }
        alloc_request_pending = false; 
    }
}

/**
 * @brief Helper to keep the VM 'sr' and 'mn' variables in sync with hardware.
 */
void updateRuntimeVars() {
    // Dynamic fetching ensures perfect execution context post-compilation wipe
    int i_sr = getVarId("sr");
    vars[i_sr].type = 0;
    vars[i_sr].f = (float)current_sample_rate;

    int i_mn = getVarId("mn");
    vars[i_mn].type = 0; 
    vars[i_mn].f = (float)current_midi_note;
}

void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!usb_midi_enabled) return;
    current_midi_freq = 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
    float gate = (float)velocity / 127.0f;
    updateMIDIVars(current_midi_freq, gate, (float)note);
}

void onNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (!usb_midi_enabled) return;
    float released_freq = 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
    if (fabsf(current_midi_freq - released_freq) < 0.1f) {
        updateMIDIVars(current_midi_freq, 0.0f, (float)note);
    }
}

/**
 * @brief FreeRTOS task for polling the Native USB hardware buffer.
 */
void midiTask(void *pvParameters) {
    while(1) {
        if (usb_midi_enabled) {
            midiHandler.poll();
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Keyboard helpers */
void doEditorInsert(char c) {
    input_buffer = input_buffer.substring(0, cursor_pos) + c + input_buffer.substring(cursor_pos);
    cursor_pos++;
    patch_edited = true;
}

void doEditorBackspace() {
    if (cursor_pos > 0) {
        input_buffer.remove(cursor_pos - 1, 1);
        cursor_pos--;
        patch_edited = true;
    }
}

void doEditorLeft() { if (cursor_pos > 0) cursor_pos--; }
void doEditorRight() { if (cursor_pos < (int)input_buffer.length()) cursor_pos++; }

void doEditorEnter() {
    saveUndo();
    bg_sprite.fillScreen(theme.bg);
    bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false);
    extern void applyCompilationResult(bool valid, String prefix = "");
    applyCompilationResult(valid);
}

void doEditorTab() {
    rpn_mode = !rpn_mode;
    status_msg = rpn_mode ? "STACK" : "INFIX";
    status_timer = millis() + 1500;
    input_buffer = decompile(rpn_mode);
    cursor_pos = input_buffer.length();
}

void doEditorStop() {
    is_playing = false;
    t_raw = 0;
    status_msg = "STOPPED";
    status_timer = millis() + 1500;
    if (is_sync_master) broadcastStop();
}

void doEditorClearAll() {
    is_playing = false;
    t_raw = 0;
    input_buffer = "";
    cursor_pos = 0;
    bg_sprite.fillScreen(theme.bg);
    if (is_sync_master) broadcastStop();
}

/**
 * Core 1 DSP Task. Features automatic 2x VM acceleration on 44.1kHz/48kHz buffers
 * using smooth linear float interpolation across native hardware registers.
 * @param pvParameters FreeRTOS task params
 */
#pragma GCC optimize ("O3")
void IRAM_ATTR playBytebeat(void *pvParameters) {
    int16_t pcm_buffer[AUDIO_BUF_SIZE * 2]; 
    uint32_t block_buf[AUDIO_BUF_SIZE]; 
    int ui_tick = 0;
    uint32_t wave_ptr = 0; 
    
    float dc_block_in = 0.0f;
    float dc_block_out = 0.0f;
    
    // Extrapolator hold buffers
    static float last_frame_output = 0.0f;
    static float current_frame_output = 0.0f;

    esp_task_wdt_add(NULL); 

    while(1) {
        if (is_playing) {
            int mod = std::max(1, 1 << drawScale); 
            
            // AUTOMATIC VM DOWNSAMPLING
            bool use_downsample = (current_sample_rate >= 44100);
            int virtual_block_size = use_downsample ? (AUDIO_BUF_SIZE / 2) : AUDIO_BUF_SIZE;
            float vm_t_step = use_downsample ? 2.0f : 1.0f;
            
            // Safely execute VM with clean strided fractional stepping
            executeVmBlock((float)t_raw, vm_t_step, virtual_block_size, block_buf);
            
            int virtual_read_ptr = 0;

            // HARDWARE INTERPOLATION & DISPATCH
            for(int i = 0; i < AUDIO_BUF_SIZE; i++) {
                uint32_t raw_sample = 0;
                
                if (!use_downsample) {
                    raw_sample = block_buf[i];
                } else {
                    if ((i & 1) == 0) { 
                        last_frame_output = current_frame_output;
                        uint32_t packed = block_buf[virtual_read_ptr++];
                        
                        if (current_play_mode == MODE_BYTEBEAT) {
                            current_frame_output = (float)((int16_t)(packed & 255) - 128) * 256.0f;
                        } else {
                            int32_t signed_32 = (int32_t)(packed - 2147483648U);
                            current_frame_output = (float)((int16_t)(signed_32 >> 16));
                        }
                    }
                    
                    // Lean arithmetic: Step is 0.0f on even frames, 0.5f on odd frames.
                    float step_acc = (i & 1) ? 0.5f : 0.0f;
                    float blended_float = last_frame_output + (current_frame_output - last_frame_output) * step_acc;
                    
                    if (current_play_mode == MODE_BYTEBEAT) {
                        raw_sample = (uint32_t)((int32_t)(blended_float / 256.0f) + 128) & 255;
                    } else {
                        raw_sample = (uint32_t)((int16_t)blended_float) << 16;
                    }
                }

                uint8_t ui_sample = 0;
                float raw_float = 0.0f;
                
                if (current_play_mode == MODE_BYTEBEAT) {
                    ui_sample = (uint8_t)(raw_sample & 255);
                    raw_float = (float)((int16_t)(ui_sample - 128) << 8); 
                } else {
                    int16_t signed_16 = (int16_t)(raw_sample >> 16); 
                    raw_float = (float)signed_16; 
                    ui_sample = (uint8_t)((signed_16 >> 8) + 128);
                }
                
                if (current_vis == VIS_WAV_WIRE) {
                    if (++ui_tick >= mod) {
                        ui_tick = 0;
                        wave_buf[wave_ptr] = ui_sample; 
                        if (++wave_ptr >= 240) wave_ptr = 0; 
                    }
                } else if (current_vis != VIS_HISTORY) {
                    uint32_t h = ui_ring_head;
                    uint32_t next_h = (h + 1) % UI_RING_SIZE;
                    if (next_h != ui_ring_tail) { 
                        ui_sample_ring[h] = ui_sample;
                        ui_ring_head = next_h;
                    }
                }
                
                t_raw++; // Universal hardware clock synchronization
                
                dc_block_out = raw_float - dc_block_in + 0.995f * dc_block_out;
                dc_block_in = raw_float;
                
                float log_vol = volume_perc * volume_perc * volume_perc;
                int16_t final_pcm = (int16_t)(dc_block_out * log_vol);
                
                pcm_buffer[i * 2]     = final_pcm; 
                pcm_buffer[i * 2 + 1] = final_pcm; 
            }
            
            audioOut.pushStereoBlock(pcm_buffer, AUDIO_BUF_SIZE);
            esp_task_wdt_reset(); 

        } else {
            memset(pcm_buffer, 0, sizeof(pcm_buffer));
            audioOut.pushStereoBlock(pcm_buffer, AUDIO_BUF_SIZE);
            vTaskDelay(pdMS_TO_TICKS(10));
            esp_task_wdt_reset();
        }
    }
}

/**
 * Updates UI and evaluator state based on compilation results.
 * @param valid Boolean indicating if the compilation was successful
 * @param prefix Optional prefix string for the status message
 */
void applyCompilationResult(bool valid, String prefix = "") {
    if (valid) {
        is_playing = true;
        active_eval_formula = rpn_mode ? decompile(false) : input_buffer; 
        status_msg = prefix + "OK"; 
    } else {
        status_msg = prefix + (last_vm_error != "" ? last_vm_error : "ERR"); 
    }
    status_timer = millis() + 1500;
}

/**
 * Handles incoming audio history for the persistent visualizers.
 */
void processUIRingBuffer() {
    int processed = 0;
    static uint32_t local_ui_t = 0;
    
    while (ui_ring_tail != ui_ring_head && processed < 4000) {
        updatePersistentVis(ui_sample_ring[ui_ring_tail], local_ui_t++);
        ui_ring_tail = (ui_ring_tail + 1) % UI_RING_SIZE;
        processed++;
    }
    if (ui_ring_tail != ui_ring_head && processed >= 4000) {
        ui_ring_tail = ui_ring_head;
    }
}

/**
 * Loads a patch from NVRAM/Memory into the active workspace and triggers compilation.
 * @param bank The target bank index (0-9)
 * @param slot The target patch slot index (0-9)
 * @param is_bank_switch True if switching banks (loads slot 1), False if loading a specific slot
 */
void executeLoadPatch(int bank, int slot, bool is_bank_switch) {
    current_bank = bank;
    int target = is_bank_switch ? 1 : slot;
    
    input_buffer = slots[current_bank][target].formula; 
    current_sample_rate = slots[current_bank][target].sample_rate;
    current_play_mode = slots[current_bank][target].mode;
    
    audioOut.setRate(current_sample_rate);
    updateRuntimeVars();
    cursor_pos = input_buffer.length(); 
    rpn_mode = false; 
    saveUndo(); 
    t_raw = 0; 
    patch_edited = false; 
    
    bool valid = compileInfix(input_buffer, false); 
    bg_sprite.fillScreen(theme.bg);
    
    String prefix = is_bank_switch ? 
        ("B" + String(current_bank) + " P" + String(target) + " ") : 
        ("LOAD " + String(target) + " @ B" + String(current_bank) + " ");
        
    applyCompilationResult(valid, prefix);
    
    if (is_sync_master) {
        broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT);
        broadcastPlay(0); 
    }
}

/**
 * Scans the current keyboard state to find if a numeric key (0-9) was pressed.
 * @param st The current M5Cardputer keyboard state struct
 * @return The integer value of the pressed digit, or -1 if no digit was pressed
 */
int getPressedDigit(const Keyboard_Class::KeysState& st) {
    const char* ctrl_symbols = ")!@#$%^&*("; 
    
    for (auto c : st.word) {
        for (int i = 0; i < 10; i++) {
            if (c == ctrl_symbols[i] || c == '0' + i) return i;
        }
    }
    for (int i = 0; i < 10; i++) {
        if (M5Cardputer.Keyboard.isKeyPressed('0' + i)) return i;
    }
    return -1;
}

/**
 * Core 0 UI/Input Task. Handles Keyboard, IMU, Screen, and Networking.
 * @param pvParameters FreeRTOS task params
 */
void uiTask(void *pvParameters) {
    static uint32_t last_sensor_poll = 0; 
    
    enum RepeatAction { REP_NONE, REP_CHAR, REP_BACKSPACE, REP_LEFT, REP_RIGHT };
    static RepeatAction rep_action = REP_NONE;
    static char rep_char = 0;
    static uint32_t rep_timer = 0;

    while(1) {
        handle_deferred_allocations();
        processUIRingBuffer();
        
        if (pending_code_update) {
            input_buffer = String((char*)pending_code_buffer);
            cursor_pos = input_buffer.length();
            patch_edited = true;
            
            rpn_mode = (pending_flags & 1) != 0;
            current_play_mode = (pending_flags & 2) != 0 ? MODE_FLOATBEAT : MODE_BYTEBEAT;

            var_count = 0;
            memset(vars, 0, sizeof(vars));
            clearGlobalArray();
            updateRuntimeVars();

            bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false);
            applyCompilationResult(valid, "SYNC RX ");
            bg_sprite.fillScreen(theme.bg);
            
            pending_code_update = false;
        }

        if (millis() - last_sensor_poll > 50) {
            last_sensor_poll = millis();
            updateRuntimeVars();

            M5.Imu.update();
            auto imu = M5.Imu.getImuData();
            updateIMUVars(imu.accel.x, imu.accel.y, imu.accel.z, 
                          imu.gyro.x, imu.gyro.y, imu.gyro.z);
        }
        
        M5.update(); 
        M5Cardputer.update();
        
        auto st = M5Cardputer.Keyboard.keysState();
        
        if (pending_confirm == CONF_BANK) {
            snprintf(current_top_text, 63, "BANK %d: %s", confirm_bank, String(bankNames[confirm_bank]).c_str());
            status_msg = patch_edited ? "UNSAVED - OK?" : "";
            status_timer = millis() + 1000;
        } else if (pending_confirm == CONF_LOAD_PATCH) {
            snprintf(current_top_text, 63, "PATCH %d PREVIEW", confirm_slot);
            status_msg = patch_edited ? "UNSAVED - OK?" : "";
            status_timer = millis() + 1000;
        } else if (pending_confirm == CONF_SAVE_PATCH) {
            snprintf(current_top_text, 63, "BANK %d: PATCH %d", confirm_bank, confirm_slot);
            status_msg = "OVERWRITE "+String(confirm_slot)+"?";
            status_timer = millis() + 1000;
        } else if (st.opt) snprintf(current_top_text, 63, "B%d LOAD: 0-9", current_bank);
        else if (st.alt) snprintf(current_top_text, 63, "B%d SAVE: 0-9", current_bank);
        else if (st.ctrl) snprintf(current_top_text, 63, "SWITCH BANK: 0-9");
        else if (st.fn) strncpy(current_top_text, "FN: W/T/S/F/L/M/P/R/Arr", 63); 
        else strncpy(current_top_text, "BYTEBED", 63); 
        current_top_text[63] = '\0'; 

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            
            const char* ctrl_symbols = ")!@#$%^&*("; 

            if (pending_confirm != CONF_NONE) {
                bool enterPressed = false;
                if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) && !st.ctrl) enterPressed = true;
                if (!patch_edited && pending_confirm != CONF_SAVE_PATCH) enterPressed = true;

                int new_digit = getPressedDigit(st);
                
                if (new_digit != -1) {
                    // IF CTRL IS HELD: Switch Bank, keep confirmation active
                    if (st.ctrl) {
                        confirm_bank = new_digit;
                        // Reload preview formula for the new bank, same slot
                        input_buffer = slots[confirm_bank][confirm_slot].formula;
                        cursor_pos = input_buffer.length();
                        status_msg = "BANK " + String(confirm_bank) + " SELECTED";
                        status_timer = millis() + 1000;
                    } 
                    // IF OPT IS HELD: Switch Patch Slot, keep confirmation active
                    else if (st.opt) {
                        confirm_slot = new_digit;
                        // Reload preview formula for the current bank, new slot
                        input_buffer = slots[confirm_bank][confirm_slot].formula;
                        cursor_pos = input_buffer.length();
                        status_msg = "PATCH " + String(confirm_slot) + " SELECTED";
                        status_timer = millis() + 1000;
                    }
                    // IF NO MODIFIER HELD: Use digit to select slot, or confirm
                    else if (!st.alt) {
                        if (new_digit == (pending_confirm == CONF_BANK ? confirm_bank : confirm_slot)) {
                            enterPressed = true;
                        } else {
                            confirm_slot = new_digit;
                            input_buffer = slots[confirm_bank][confirm_slot].formula;
                            cursor_pos = input_buffer.length();
                        }
                    }
                }
                

                if (enterPressed) {
                    if (pending_confirm == CONF_BANK) {
                        executeLoadPatch(confirm_bank, 1, true);
                    } 
                    else if (pending_confirm == CONF_LOAD_PATCH) {
                        executeLoadPatch(confirm_bank, confirm_slot, false);
                    } 
                    else if (pending_confirm == CONF_SAVE_PATCH) {
                        // Commit to the browsed context parameters
                        slots[confirm_bank][confirm_slot].formula = rpn_mode ? decompile(false) : backup_input_buffer; 
                        slots[confirm_bank][confirm_slot].sample_rate = current_sample_rate;
                        slots[confirm_bank][confirm_slot].mode = current_play_mode;
                        
                        String keySuffix = String(confirm_bank) + String(confirm_slot);
                        String packed = String((int)slots[confirm_bank][confirm_slot].mode) + "|" + String(current_sample_rate) + "|" + slots[confirm_bank][confirm_slot].formula;
                        prefs.putString(("s" + keySuffix).c_str(), packed);
                        
                        input_buffer = backup_input_buffer;
                        cursor_pos = backup_cursor_pos;
                        
                        status_msg = "SAVE P" + String(confirm_slot) + " @ B" + String(confirm_bank) + " OK"; 
                        status_timer = millis() + 1500;
                    }
                    pending_confirm = CONF_NONE;

                } else if (new_digit == -1) {
                    char expectedChar = '0' + ((pending_confirm == CONF_BANK) ? confirm_bank : confirm_slot);
                    
                    bool cancel = false;
                    for (auto i : st.word) {
                        if (i != expectedChar) cancel = true;
                    }
                    
                    // Whitelist navigation modifiers to prevent cancellation traps
                    if (st.ctrl || st.opt) cancel = false; 

                    if (pending_confirm == CONF_BANK && (st.alt || st.opt)) cancel = true;
                    if (pending_confirm == CONF_LOAD_PATCH && (st.alt)) cancel = true;
                    if (pending_confirm == CONF_SAVE_PATCH) cancel = false; // Overridden above by modifiers safely
                    
                    if (st.fn || st.shift || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) || M5Cardputer.Keyboard.isKeyPressed('`')) cancel = true;
                    if (cancel) {
                        input_buffer = backup_input_buffer;
                        cursor_pos = backup_cursor_pos;
                        status_msg = "CANCELLED";
                        status_timer = millis() + 1200;
                        pending_confirm = CONF_NONE;
                    }
                }
                continue; 
            }

            int pressed_digit = getPressedDigit(st);

            // 1. SETUP PENDING CONFIRMATIONS (CTRL, OPT, ALT + 0-9)
            if (pressed_digit != -1 && (st.ctrl || (st.opt && !st.shift) || (st.alt && !st.shift))) {
                
                backup_input_buffer = input_buffer;
                backup_cursor_pos = cursor_pos;
                
                if (st.ctrl) {
                    pending_confirm = CONF_BANK;
                    confirm_bank = pressed_digit;
                    confirm_slot = 1; // Explicit initial alignment target tracking
                    input_buffer = slots[confirm_bank][confirm_slot].formula; 
                } else if (st.opt && !st.shift) {
                    pending_confirm = CONF_LOAD_PATCH;
                    confirm_slot = pressed_digit;
                    confirm_bank = current_bank;
                    input_buffer = slots[confirm_bank][confirm_slot].formula; 
                } else if (st.alt && !st.shift) {
                    pending_confirm = CONF_SAVE_PATCH;
                    confirm_slot = pressed_digit;
                    confirm_bank = current_bank; // Lock context workspace index parameters immediately
                    input_buffer = slots[confirm_bank][confirm_slot].formula; 
                }
                
                cursor_pos = input_buffer.length();
            } 
            
            // 2. SYSTEM COMMANDS (CTRL without numbers)
            else if (st.ctrl) {
                if (M5Cardputer.Keyboard.isKeyPressed('z') || M5Cardputer.Keyboard.isKeyPressed('Z')) { 
                    int prev = (undo_ptr - 1 + UNDO_DEPTH) % UNDO_DEPTH; 
                    if (undo_stack[prev] != "") { 
                        undo_ptr = prev; 
                        input_buffer = undo_stack[undo_ptr]; 
                        cursor_pos = input_buffer.length(); 
                        patch_edited = true;
                    } 
                }
                if (M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y')) { 
                    if (undo_ptr != undo_max) { 
                        undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; 
                        input_buffer = undo_stack[undo_ptr]; 
                        cursor_pos = input_buffer.length(); 
                        patch_edited = true;
                    } 
                }
            }
            
            // 3. FN COMMANDS
            else if (st.fn && !st.shift) {
                for (auto c : st.word) {
                    if (c == '`') { doEditorClearAll(); }
                    if (c == 'l' || c == 'L') {
                        if (!is_sync_initialized) { initESPNowSync(); is_sync_initialized = true; }
                        is_sync_master = false; status_msg = "SYNC: LISTENING"; status_timer = millis() + 1500;
                    }
                    if (c == 'p' || c == 'P') {
                        if (!is_sync_initialized) { initESPNowSync(); is_sync_initialized = true; }
                        if (!is_sync_master) {
                            is_sync_master = true; broadcastPlay(t_raw); broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT); status_msg = "SYNC: MASTER UP";
                        } else {
                            broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT); broadcastSync(t_raw); status_msg = "SYNC: PUSH CODE";
                        }
                        status_timer = millis() + 1500;
                    }
                    
                    // USB MIDI BOOT
                    if (c == 'm' || c == 'M') {
                        usb_midi_enabled = !usb_midi_enabled;
                        if (usb_midi_enabled) {
                            if (midiTaskHandle == NULL) {
                                midiHandler.setNoteOnCallback(onNoteOn);
                                midiHandler.setNoteOffCallback(onNoteOff);
                                midiHandler.begin();
                                USB.begin(); 
                                xTaskCreatePinnedToCore(midiTask, "MidiTask", 4096, NULL, 5, &midiTaskHandle, 0);
                            } else {
                                vTaskResume(midiTaskHandle);
                            }
                            status_msg = "USB MIDI: ON";
                        } else {
                            if (midiTaskHandle != NULL) vTaskSuspend(midiTaskHandle);
                            updateMIDIVars(current_midi_freq, 0.0f, 0);
                            status_msg = "USB MIDI: OFF";
                        }
                        status_timer = millis() + 1500;
                    }
                    
                    if (c == 'r' || c == 'R') {
                        t_raw = 0; status_msg = "TIMELINE RESET"; status_timer = millis() + 1500;
                        if (is_sync_master) broadcastPlay(0);
                    }
                    if (c == 't' || c == 'T') { current_theme_idx = (current_theme_idx + 1) % 3; bg_sprite.fillScreen(theme.bg); }
                    if (c == 's' || c == 'S') { 
                        if (current_sample_rate == 8000) current_sample_rate = 11025;
                        else if (current_sample_rate == 11025) current_sample_rate = 22050;
                        else if (current_sample_rate == 22050) current_sample_rate = 32000;
                        else if (current_sample_rate == 32000) current_sample_rate = 44100;
                        else if (current_sample_rate == 44100) current_sample_rate = 48000;
                        else current_sample_rate = 8000;
                        audioOut.setRate(current_sample_rate); status_msg = "RATE: " + String(current_sample_rate) + "Hz"; status_timer = millis() + 1500;
                        updateRuntimeVars();
                    }
                    if (c == 'f' || c == 'F') {
                        current_play_mode = (current_play_mode == MODE_BYTEBEAT) ? MODE_FLOATBEAT : MODE_BYTEBEAT;
                        var_count = 0; memset(vars, 0, sizeof(vars)); clearGlobalArray(); 
                        updateRuntimeVars();
                        bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false);
                        status_msg = (current_play_mode == MODE_BYTEBEAT) ? "MODE: BYTEBEAT" : "MODE: FLOATBEAT"; status_timer = millis() + 1500;
                    }
                    if (c == '1') { current_vis = VIS_WAV_WIRE; bg_sprite.fillScreen(theme.bg); }
                    if (c == '2') { current_vis = VIS_DIA_AMP;  bg_sprite.fillScreen(theme.bg); }
                    if (c == '3') { current_vis = VIS_DIA_BIT;  bg_sprite.fillScreen(theme.bg); }
                    if (c == '4') { current_vis = VIS_WAV_ORIG; bg_sprite.fillScreen(theme.bg); }
                    if (c == '0') { current_vis = VIS_HISTORY; }
                    if (c == '=') { drawScale = std::min(10, drawScale + 1); bg_sprite.fillScreen(theme.bg); }
                    if (c == '-') { drawScale = std::max(0, drawScale - 1); bg_sprite.fillScreen(theme.bg); }
                    if (c == ';') { volume_perc = std::min(1.0f, volume_perc + VOL_STEP); }
                    if (c == '.') { volume_perc = std::max(0.0f, volume_perc - VOL_STEP); }
                    
                    if (c == ',') { doEditorLeft(); rep_action = REP_LEFT; rep_timer = millis() + 400; }
                    if (c == '/') { doEditorRight(); rep_action = REP_RIGHT; rep_timer = millis() + 400; }
                }

                if (M5Cardputer.Keyboard.isKeyPressed('`')) {
                    doEditorClearAll();
                }
            } 
            
            // 4. STANDARD TYPING
            else if (!st.ctrl && !st.alt && !st.opt && !st.fn) {
                bool key_handled = false;

                if (st.word.size() > 0) {
                    for (auto i : st.word) { 
                        if (i == '`') {
                            doEditorStop();
                            key_handled = true;
                        } else if (i == '\b' || i == 127) {
                            doEditorBackspace();
                            rep_action = REP_BACKSPACE; rep_timer = millis() + 400; 
                            key_handled = true;
                        } else if (i == '\r' || i == '\n') {
                            doEditorEnter();
                            key_handled = true;
                        } else if (i == '\t') {
                            doEditorTab();
                            key_handled = true;
                        } else {
                            doEditorInsert(i);
                            rep_action = REP_CHAR; rep_char = i; rep_timer = millis() + 800;
                            key_handled = true;
                        }
                    }
                }

                if (!key_handled) {
                    if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
                        doEditorTab();
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                        doEditorEnter();
                    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                        doEditorBackspace();
                        rep_action = REP_BACKSPACE; rep_timer = millis() + 400; 
                    } else if (M5Cardputer.Keyboard.isKeyPressed('`')) {
                        doEditorStop();
                    }
                }
            }
        }
        
        bool keep_repeating = false;
        bool no_mods_or_shift = (!st.ctrl || st.shift) && (!st.alt || st.shift) && (!st.opt || st.shift) && (!st.fn || st.shift);
        
        if (rep_action == REP_LEFT) {
            if (st.fn && !st.shift && M5Cardputer.Keyboard.isKeyPressed(',')) {
                keep_repeating = true;
                if (millis() > rep_timer) { doEditorLeft(); rep_timer = millis() + 40; }
            }
        } else if (rep_action == REP_RIGHT) {
            if (st.fn && !st.shift && M5Cardputer.Keyboard.isKeyPressed('/')) {
                keep_repeating = true;
                if (millis() > rep_timer) { doEditorRight(); rep_timer = millis() + 40; }
            }
        } else if (rep_action == REP_BACKSPACE) {
            if (no_mods_or_shift && (M5Cardputer.Keyboard.isKeyPressed('\b') || M5Cardputer.Keyboard.isKeyPressed(127) || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE))) {
                keep_repeating = true;
                if (millis() > rep_timer) { doEditorBackspace(); rep_timer = millis() + 40; }
            }
        } else if (rep_action == REP_CHAR) {
            if (no_mods_or_shift && M5Cardputer.Keyboard.isKeyPressed(rep_char)) {
                keep_repeating = true;
                if (millis() > rep_timer) { doEditorInsert(rep_char); rep_timer = millis() + 40; }
            }
        }
        
        if (!keep_repeating) {
            rep_action = REP_NONE; 
        }
        
        if (millis() - last_draw > UI_REFRESH_MS) { 
            draw(); 
            last_draw = millis(); 
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * Standard Setup. 
 */
void setup() {
    delay(1000); 

    is_playing = false; 
    
    if (WIPE_NVRAM) { 
        nvs_flash_erase(); 
        nvs_flash_init(); 
    }
    
    auto cfg = M5.config(); 
    M5Cardputer.begin(cfg, true); 
    M5.update();
    
    input_buffer.reserve(2048);
    canvas.setColorDepth(8); 
    canvas.createSprite(240, 135); 
    bg_sprite.setColorDepth(8); 
    bg_sprite.createSprite(240, 135); 

    init_fast_math(); 
    
    WiFi.persistent(false); 
    WiFi.mode(WIFI_STA); 
    WiFi.disconnect(); 
    M5.Speaker.begin(); 
    M5.Speaker.end(); 
    M5.Mic.end();     
    
    prefs.begin("bytebeat", false);
    
    int boot_idx = prefs.getInt("boot_idx", 0);
    prefs.putInt("boot_idx", (boot_idx + 1) % 10);
    
    for (int b = 0; b < 10; b++) {
        for (int i = 0; i < 10; i++) {
            const PresetConfig* defaultPreset = &defaultBanks[b][i];
            String keySuffix = String(b) + String(i);
            String sKey = "s" + keySuffix;

            if (FORCE_REWRITE_PRESETS) { 
                slots[b][i].formula = defaultPreset->formula;
                slots[b][i].sample_rate = defaultPreset->sample_rate;
                slots[b][i].mode = defaultPreset->mode;
                
                String packed = String((int)slots[b][i].mode) + "|" + 
                                String(slots[b][i].sample_rate) + "|" + 
                                slots[b][i].formula;
                prefs.putString(sKey.c_str(), packed); 
            } else {
                if (prefs.isKey(sKey.c_str())) {
                    String packed = prefs.getString(sKey.c_str(), "");
                    int p1 = packed.indexOf('|');
                    int p2 = packed.indexOf('|', p1 + 1);
                    
                    if (p1 > 0 && p2 > p1) {
                        slots[b][i].mode = (PlayMode)packed.substring(0, p1).toInt();
                        slots[b][i].sample_rate = packed.substring(p1 + 1, p2).toInt();
                        slots[b][i].formula = packed.substring(p2 + 1);
                    } else {
                        slots[b][i].formula = defaultPreset->formula;
                        slots[b][i].sample_rate = defaultPreset->sample_rate;
                        slots[b][i].mode = defaultPreset->mode;
                    }
                } else {
                    slots[b][i].formula = defaultPreset->formula;
                    slots[b][i].sample_rate = defaultPreset->sample_rate;
                    slots[b][i].mode = defaultPreset->mode;
                }
            }
        }
    }
    
    current_bank = 1; 
    input_buffer = slots[1][boot_idx].formula; 
    current_sample_rate = slots[1][boot_idx].sample_rate;
    current_play_mode = slots[1][boot_idx].mode;
    cursor_pos = input_buffer.length(); 
    
    audioOut.begin(current_sample_rate);
    updateRuntimeVars();
    
    undo_stack[0] = input_buffer;
    compileInfix(input_buffer, true);
    active_eval_formula = input_buffer; 
    
    xTaskCreatePinnedToCore(playBytebeat, "audio", 8192, NULL, 24, NULL, 1);
    
    uint32_t splash_start = millis();
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setTextFont(1);
    M5Cardputer.Display.setTextSize(4);
    
    while (millis() - splash_start < 1500) {
        if (random(10) > 7) {
            M5Cardputer.Display.fillScreen(0x0000);
        }
        int ox = random(-4, 5);
        int oy = random(-2, 3);
        uint16_t glitch_colors[] = {0x07E0, 0x0600};
        
        M5Cardputer.Display.setTextColor(glitch_colors[random(2)]);
        M5Cardputer.Display.drawString("BYTEBED", 120 + ox, 67 + oy);
        
        if (random(10) > 5) {
            M5Cardputer.Display.fillRect(0, random(135), 240, random(2, 6), 0x0000);
        }
        delay(random(20, 80)); 
    }
    
    M5Cardputer.Display.fillScreen(theme.bg); 
    
    xTaskCreatePinnedToCore(uiTask, "ui_task", 8192, NULL, 5, NULL, 0);

    t_raw = 0;
    is_playing = true; 
}

void loop() { 
    vTaskDelay(portMAX_DELAY); 
}
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h> 
#include <nvs_flash.h> 
#include "state.h"
#include "vm.h"
#include "fast_math.h"
#include "ui.h"
#include "captive.h"
#include "fasti2s.h" 
#include "sync.h"

const bool WIPE_NVRAM = false; 
const bool FORCE_REWRITE_PRESETS = true;
bool is_sync_master = false;
bool is_sync_initialized = false;
volatile bool pending_code_update = false;
char pending_code_buffer[2048] = {0};
uint8_t pending_flags = 0;

// Lock-Free UI Decoupling
#define UI_RING_SIZE 4096
volatile uint8_t ui_sample_ring[UI_RING_SIZE];
volatile int32_t ui_t_ring[UI_RING_SIZE];
volatile uint32_t ui_ring_head = 0;
uint32_t ui_ring_tail = 0;

FastI2S audioOut;

void IRAM_ATTR playBytebeat(void *pvParameters) {
    int16_t pcm_buffer[AUDIO_BUF_SIZE * 2]; 
    uint8_t block_buf[AUDIO_BUF_SIZE];
    int ui_tick = 0;
    uint32_t wave_ptr = 0; 
    
    // Core DSP State Variables for DC Blocking
    float dc_block_in = 0.0f;
    float dc_block_out = 0.0f;
    
    esp_task_wdt_add(NULL); 

    while(1) {
        if (is_playing) {
            int mod = std::max(1, 1 << drawScale); 
            
            // 1. EXECUTE: Bytecode crunching
            execute_vm_block(t_raw, AUDIO_BUF_SIZE, block_buf);
            
            // 2. TIGHT INLINE DSP & DISPATCH
            for(int i = 0; i < AUDIO_BUF_SIZE; i++) {
                uint8_t sample = block_buf[i];
                
                // UI FEEDING LOGIC
                if (current_vis == VIS_WAV_WIRE) {
                    if (++ui_tick >= mod) {
                        ui_tick = 0;
                        wave_buf[wave_ptr] = sample; 
                        if (++wave_ptr >= 240) wave_ptr = 0; 
                    }
                } else if (current_vis != VIS_HISTORY) {
                    uint32_t h = ui_ring_head;
                    uint32_t next_h = (h + 1) % UI_RING_SIZE;
                    if (next_h != ui_ring_tail) { 
                        ui_sample_ring[h] = sample;
                        ui_t_ring[h] = t_raw;
                        ui_ring_head = next_h;
                    }
                }
                
                t_raw++;
                
                // DC Blocker (1-Pole High-Pass)
                float raw_float = (sample - 128.0f) * 255.0f; 
                dc_block_out = raw_float - dc_block_in + 0.995f * dc_block_out;
                dc_block_in = raw_float;
                
                // TRUE LOGARITHMIC VOLUME SCALING
                float log_vol = volume_perc * volume_perc * volume_perc;
                int16_t final_pcm = (int16_t)(dc_block_out * log_vol);
                
                pcm_buffer[i * 2]     = final_pcm; 
                pcm_buffer[i * 2 + 1] = final_pcm; 
            }
            
            // 3. PUSH DIRECT TO DMA
            audioOut.pushStereoBlock(pcm_buffer, AUDIO_BUF_SIZE);
            esp_task_wdt_reset(); 

        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
            esp_task_wdt_reset();
        }
    }
}

void applyCompilationResult(bool valid, String prefix = "") {
    if (valid) {
        is_playing = true;
        active_eval_formula = rpn_mode ? decompile(false) : input_buffer; 
        status_msg = prefix + "OK"; 
    } else {
        status_msg = prefix + "ERR"; 
    }
    status_timer = millis() + 1500;
}

void processUIRingBuffer() {
    int processed = 0;
    while (ui_ring_tail != ui_ring_head && processed < 4000) {
        updatePersistentVis(ui_sample_ring[ui_ring_tail], ui_t_ring[ui_ring_tail]);
        ui_ring_tail = (ui_ring_tail + 1) % UI_RING_SIZE;
        processed++;
    }
    if (ui_ring_tail != ui_ring_head && processed >= 4000) {
        ui_ring_tail = ui_ring_head;
    }
}

void uiTask(void *pvParameters) {
    
    static uint32_t last_imu_poll = 0; 

    while(1) {
        processUIRingBuffer();
        
        // INCOMING CODE SYNC HANDLER
        if (pending_code_update) {
            input_buffer = String((char*)pending_code_buffer);
            cursor_pos = input_buffer.length();
            
            rpn_mode = (pending_flags & 1) != 0;
            current_play_mode = (pending_flags & 2) != 0 ? MODE_FLOATBEAT : MODE_BYTEBEAT;

            var_count = 0;
            memset(vars, 0, sizeof(vars));
            clear_global_array();

            bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false);
            applyCompilationResult(valid, "SYNC RX ");
            bg_sprite.fillScreen(theme.bg);
            
            pending_code_update = false;
        }

        // 20Hz NON-BLOCKING IMU POLLING
        if (millis() - last_imu_poll > 50) {
            last_imu_poll = millis();
            M5.Imu.update();
            auto imu = M5.Imu.getImuData();
            updateIMUVars(imu.accel.x, imu.accel.y, imu.accel.z, 
                          imu.gyro.x, imu.gyro.y, imu.gyro.z);
        }
        
        M5.update(); 
        M5Cardputer.update();
        
        auto st = M5Cardputer.Keyboard.keysState();
        
        if (st.opt) snprintf(current_top_text, 63, "B%d LOAD: 0-9", current_bank);
        else if (st.alt) snprintf(current_top_text, 63, "B%d SAVE: 0-9", current_bank);
        else if (st.ctrl) snprintf(current_top_text, 63, "SWITCH BANK: 0-9");
        else if (st.fn) strncpy(current_top_text, "FN: W/T/S/F/L/M/R/Arr/+-", 63); // Added R here
        else strncpy(current_top_text, "BYTENATOR", 63); 
        current_top_text[63] = '\0'; 

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            
            // 1. BANK SWITCHING
            if (st.ctrl) {
                const char* ctrl_symbols = ")!@#$%^&*("; 
                bool bankChanged = false;

                for (auto c : st.word) {
                    for (int i = 0; i < 10; i++) {
                        if (c == ctrl_symbols[i]) {
                            current_bank = i;
                            bankChanged = true;
                        }
                    }
                }

                if (bankChanged) {
                    int target_patch = 1;
                    input_buffer = slots[current_bank][target_patch].formula; 
                    current_sample_rate = slots[current_bank][target_patch].sample_rate;
                    current_play_mode = slots[current_bank][target_patch].mode;
                    audioOut.setRate(current_sample_rate);
                    cursor_pos = input_buffer.length(); 
                    rpn_mode = false; 
                    saveUndo(); 
                    
                    t_raw = 0; // RESET TIMELINE TO ZERO
                    
                    bool valid = compileInfix(input_buffer, false); 
                    bg_sprite.fillScreen(theme.bg);
                    applyCompilationResult(valid, "B" + String(current_bank) + " P" + String(target_patch) + " ");
                    
                    // Push to swarm if we are the master
                    if (is_sync_master) {
                        broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT);
                        broadcastPlay(0); 
                    }
                    
                    goto end_keyboard_logic;
                }

                if (M5Cardputer.Keyboard.isKeyPressed('z') || M5Cardputer.Keyboard.isKeyPressed('Z')) { 
                    int prev = (undo_ptr - 1 + UNDO_DEPTH) % UNDO_DEPTH; 
                    if (undo_stack[prev] != "") { undo_ptr = prev; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); } 
                }
                if (M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y')) { 
                    if (undo_ptr != undo_max) { undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); } 
                }
            } 
            
            // 2. LOAD PATCH (OPT + 0-9)
            else if (st.opt) {
                for (int i = 0; i < 10; i++) {
                    if (M5Cardputer.Keyboard.isKeyPressed('0' + i)) {
                        input_buffer = slots[current_bank][i].formula; 
                        current_sample_rate = slots[current_bank][i].sample_rate;
                        current_play_mode = slots[current_bank][i].mode;
                        audioOut.setRate(current_sample_rate);
                        cursor_pos = input_buffer.length(); 
                        rpn_mode = false; 
                        saveUndo(); 
                        t_raw = 0;
                        
                        bool valid = compileInfix(input_buffer, false); 
                        bg_sprite.fillScreen(theme.bg);
                        applyCompilationResult(valid, "LOAD " + String(i) + " @ B" + String(current_bank) + " ");
                        
                        // Push to swarm if we are the master
                        if (is_sync_master) {
                            broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT);
                            broadcastPlay(0); 
                        }
                    }
                }
            } 
            
            // 3. SAVE PATCH (ALT + 0-9)
            else if (st.alt) {
                for (int i = 0; i < 10; i++) {
                    if (M5Cardputer.Keyboard.isKeyPressed('0' + i)) {
                        slots[current_bank][i].formula = rpn_mode ? decompile(false) : input_buffer; 
                        slots[current_bank][i].sample_rate = current_sample_rate;
                        slots[current_bank][i].mode = current_play_mode;
                        
                        String keySuffix = String(current_bank) + String(i);
                        
                        String packed = String((int)slots[current_bank][i].mode) + "|" + 
                                        String(slots[current_bank][i].sample_rate) + "|" + 
                                        slots[current_bank][i].formula;
                        prefs.putString(("s" + keySuffix).c_str(), packed);
                        
                        status_msg = "SAVE " + String(i) + " @ B" + String(current_bank) + " OK"; 
                        status_timer = millis() + 1500;
                    }
                }
            } 
            
            // 4. FN COMMANDS
            else if (st.fn) {
                if (M5Cardputer.Keyboard.isKeyPressed('l') || M5Cardputer.Keyboard.isKeyPressed('L')) {
                    if (is_streaming) { WiFi.softAPdisconnect(true); is_streaming = false; delay(50); }
                    if (!is_sync_initialized) { initESPNowSync(); is_sync_initialized = true; }
                    
                    is_sync_master = false;
                    status_msg = "SYNC: LISTENING";
                    status_timer = millis() + 1500;
                }

                if (M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) {
                    if (is_streaming) { WiFi.softAPdisconnect(true); is_streaming = false; delay(50); }
                    if (!is_sync_initialized) { initESPNowSync(); is_sync_initialized = true; }

                    if (!is_sync_master) {
                        is_sync_master = true;
                        broadcastPlay(t_raw);
                        broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT);
                        status_msg = "SYNC: MASTER UP";
                    } else {
                        broadcastCode(input_buffer, rpn_mode, current_play_mode == MODE_FLOATBEAT);
                        broadcastSync(t_raw);
                        status_msg = "SYNC: PUSH CODE";
                    }
                    status_timer = millis() + 1500;
                }

                // RESET TIMELINE
                if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
                    t_raw = 0;
                    status_msg = "TIMELINE RESET";
                    status_timer = millis() + 1500;
                    
                    if (is_sync_master) {
                        broadcastPlay(0);
                    }
                }

                if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
                    if (is_sync_initialized) { esp_now_deinit(); WiFi.disconnect(true); is_sync_initialized = false; is_sync_master = false; delay(50); }
                    if (!is_streaming) {
                        WiFi.persistent(false); 
                        WiFi.mode(WIFI_AP);
                        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
                        if (WiFi.softAP("BYTENATOR")) {
                            MDNS.begin("bytebeat"); 
                            is_streaming = true; initBytebeatServer();
                            xTaskCreatePinnedToCore(startDnsHijack, "DNS", 2048, NULL, 1, NULL, 1);
                            status_msg = "WIFI ACTIVE";
                        }
                    } else { 
                        WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); is_streaming = false; status_msg = "WIFI OFF"; 
                    }
                    status_timer = millis() + 1500;
                }
                
                if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) { 
                    current_theme_idx = (current_theme_idx + 1) % 3; bg_sprite.fillScreen(theme.bg); 
                }
                if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) { 
                    if (current_sample_rate == 8000) current_sample_rate = 11025;
                    else if (current_sample_rate == 11025) current_sample_rate = 22050;
                    else if (current_sample_rate == 22050) current_sample_rate = 32000;
                    else if (current_sample_rate == 32000) current_sample_rate = 44100;
                    else if (current_sample_rate == 44100) current_sample_rate = 48000;
                    else current_sample_rate = 8000;
                    audioOut.setRate(current_sample_rate);
                    status_msg = "RATE: " + String(current_sample_rate) + "Hz";
                    status_timer = millis() + 1500;
                }
                if (M5Cardputer.Keyboard.isKeyPressed('f') || M5Cardputer.Keyboard.isKeyPressed('F')) {
                    current_play_mode = (current_play_mode == MODE_BYTEBEAT) ? MODE_FLOATBEAT : MODE_BYTEBEAT;
                    
                    var_count = 0; 
                    memset(vars, 0, sizeof(vars));
                    clear_global_array(); 
                    
                    bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false);
                    status_msg = (current_play_mode == MODE_BYTEBEAT) ? "MODE: BYTEBEAT" : "MODE: FLOATBEAT";
                    status_timer = millis() + 1500;
                }
                if (M5Cardputer.Keyboard.isKeyPressed('1')) { current_vis = VIS_WAV_WIRE; bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('2')) { current_vis = VIS_DIA_AMP;  bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('3')) { current_vis = VIS_DIA_BIT;  bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('4')) { current_vis = VIS_WAV_ORIG; bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('0')) { current_vis = VIS_HISTORY; }
                if (M5Cardputer.Keyboard.isKeyPressed('=')) { drawScale = std::min(10, drawScale + 1); bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('-')) { drawScale = std::max(0, drawScale - 1); bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed(';')) { volume_perc = std::min(1.0f, volume_perc + VOL_STEP); }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) { volume_perc = std::max(0.0f, volume_perc - VOL_STEP); }
                if (M5Cardputer.Keyboard.isKeyPressed(',')) { if (cursor_pos > 0) cursor_pos--; }
                if (M5Cardputer.Keyboard.isKeyPressed('/')) { if (cursor_pos < (int)input_buffer.length()) cursor_pos++; }
                if (M5Cardputer.Keyboard.isKeyPressed('`')) { 
                    is_playing = false; t_raw = 0; input_buffer = ""; cursor_pos = 0; bg_sprite.fillScreen(theme.bg); 
                    if(is_sync_master) broadcastStop(); 
                }
            } 
            
            // 5. STANDARD TYPING
            else {
                if (st.word.size() > 0) {
                    for (auto i : st.word) { input_buffer = input_buffer.substring(0, cursor_pos) + i + input_buffer.substring(cursor_pos); cursor_pos++; }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
                    rpn_mode = !rpn_mode; input_buffer = decompile(rpn_mode); cursor_pos = input_buffer.length();
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    saveUndo(); bg_sprite.fillScreen(theme.bg); 
                    bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false); 
                    applyCompilationResult(valid);
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                    if (cursor_pos > 0) { input_buffer.remove(cursor_pos - 1, 1); cursor_pos--; }
                }
            }
        }
        
        end_keyboard_logic:
        if (millis() - last_draw > UI_REFRESH_MS) { draw(); last_draw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    if (WIPE_NVRAM) { nvs_flash_erase(); nvs_flash_init(); }
    auto cfg = M5.config(); 
    M5Cardputer.begin(cfg, true); 
    M5.update();
    input_buffer.reserve(2048);
    canvas.setColorDepth(8); canvas.createSprite(240, 135); 
    bg_sprite.setColorDepth(8); bg_sprite.createSprite(240, 135); bg_sprite.fillScreen(theme.bg);
    init_fast_math(); 
    
    WiFi.persistent(false); 
    WiFi.mode(WIFI_STA); WiFi.disconnect(); 
    M5.Speaker.begin(); M5.Speaker.end(); M5.Mic.end();     
    
    prefs.begin("bytebeat", false);
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
    
    // SET BANK 1 AS DEFAULT
    current_bank = 1; 
    input_buffer = slots[1][0].formula; 
    current_sample_rate = slots[1][0].sample_rate;
    current_play_mode = slots[1][0].mode;
    
    audioOut.begin(current_sample_rate);
    
    undo_stack[0] = input_buffer;
    compileInfix(input_buffer, true);
    active_eval_formula = input_buffer; 
    
    xTaskCreatePinnedToCore(playBytebeat, "audio", 8192, NULL, 24, NULL, 1);
    xTaskCreatePinnedToCore(uiTask, "ui_task", 8192, NULL, 5, NULL, 0);
}

void loop() { vTaskDelay(portMAX_DELAY); }
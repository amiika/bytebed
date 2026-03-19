#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h> 
#include "state.h"
#include "vm.h"
#include "fast_math.h"
#include "ui.h"
#include "captive.h"

// Feature Flags
const bool TEST_PRESETS = false; 
const bool REWRITE_PRESETS = false; 

// --- Lock-Free UI Decoupling Buffer ---
// Necessary to bridge 48kHz audio math with 30fps display updates
#define UI_RING_SIZE 4096
volatile uint8_t ui_sample_ring[UI_RING_SIZE];
volatile int32_t ui_t_ring[UI_RING_SIZE];
volatile uint32_t ui_ring_head = 0;
uint32_t ui_ring_tail = 0;

// --- UNINTERRUPTED AUDIO ENGINE (Core 1, Priority 24) ---
// This task owns Core 1. It uses busy-waiting to ensure zero-latency handoff to the I2S DMA.
void IRAM_ATTR playBytebeat(void *pvParameters) {
    const int BUF_COUNT = 12; 
    int16_t** out_buffers = new int16_t*[BUF_COUNT];
    for (int i = 0; i < BUF_COUNT; i++) {
        out_buffers[i] = new int16_t[AUDIO_BUF_SIZE];
    }
    
    uint8_t block_buf[AUDIO_BUF_SIZE];
    int buf_idx = 0;
    int ui_tick = 0;
    
    // Subscribe Core 1 to the Watchdog so we can monopolize the core safely
    esp_task_wdt_add(NULL); 

    while(1) {
        if (is_playing) {
            int mod = std::max(1, 1 << drawScale); 
            
            // 1. EXECUTE: Bytecode crunching via Vectorized VM Engine
            execute_vm_block(t_raw, AUDIO_BUF_SIZE, block_buf);
            
            // 2. DISPATCH: UI Ring Buffer feeding and I2S formatting
            for(int i = 0; i < AUDIO_BUF_SIZE; i++) {
                uint8_t sample = block_buf[i];
                if (++ui_tick >= mod) {
                    ui_tick = 0;
                    if (current_vis == VIS_WAV_WIRE) { 
                        wave_buf[((uint32_t)(t_raw / mod)) % 240] = sample; 
                    } else {
                        uint32_t h = ui_ring_head;
                        uint32_t next_h = (h + 1) % UI_RING_SIZE;
                        if (next_h != ui_ring_tail) { 
                            ui_sample_ring[h] = sample;
                            ui_t_ring[h] = t_raw;
                            ui_ring_head = next_h;
                        }
                    }
                }
                t_raw++;
                out_buffers[buf_idx][i] = (int16_t)((sample - 128) << 8); 
            }
            
            // 3. BUSY-WAIT: Zero-latency polling.
            // We spin checking the hardware queue status.
            while (M5.Speaker.isPlaying(0) >= (BUF_COUNT - 2)) {
                esp_task_wdt_reset(); 
            }
            
            // 4. PUSH: Instant delivery to the ES8311 Codec
            M5.Speaker.playRaw(out_buffers[buf_idx], AUDIO_BUF_SIZE, current_sample_rate, false, 1, 0); 
            buf_idx = (buf_idx + 1) % BUF_COUNT;
            esp_task_wdt_reset(); 

        } else {
            // Idle state: Yield to allow background OS tasks on Core 1
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
    // Drain up to 4000 pixels to maintain visual sync with 48kHz audio
    while (ui_ring_tail != ui_ring_head && processed < 4000) {
        updatePersistentVis(ui_sample_ring[ui_ring_tail], ui_t_ring[ui_ring_tail]);
        ui_ring_tail = (ui_ring_tail + 1) % UI_RING_SIZE;
        processed++;
    }
    if (ui_ring_tail != ui_ring_head && processed >= 4000) {
        ui_ring_tail = ui_ring_head;
    }
}

// --- UI & SYSTEM TASK (Core 0, Priority 5) ---
void uiTask(void *pvParameters) {
    while(1) {
        processUIRingBuffer();
        M5.update(); 
        M5Cardputer.update();
        
        auto st = M5Cardputer.Keyboard.keysState();
        
        // Status Bar Feedback Logic
        if (st.opt) strncpy(current_top_text, "LOAD: 0-9", 63);
        else if (st.alt) strncpy(current_top_text, "SAVE: 0-9", 63);
        else if (st.ctrl) strncpy(current_top_text, "CTRL: Z / Y", 63);
        else if (st.fn) strncpy(current_top_text, "FN: 1-4/W/T/S/F/Arr/+-", 63);
        else strncpy(current_top_text, "BYTEBED", 63);
        current_top_text[63] = '\0'; 

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            // --- OPT: PRESET LOADING ---
            if (st.opt) {
                for (int i = 0; i < 10; i++) {
                    if (M5Cardputer.Keyboard.isKeyPressed('0' + i) && slots[i] != "") {
                        input_buffer = slots[i]; 
                        cursor_pos = input_buffer.length(); 
                        rpn_mode = false; 
                        saveUndo(); 
                        bool valid = compileInfix(input_buffer, false); 
                        bg_sprite.fillScreen(theme.bg);
                        applyCompilationResult(valid, "LOAD " + String(i) + " ");
                    }
                }
            } 
            // --- ALT: PRESET SAVING ---
            else if (st.alt) {
                for (int i = 0; i < 10; i++) {
                    if (M5Cardputer.Keyboard.isKeyPressed('0' + i)) {
                        slots[i] = rpn_mode ? decompile(false) : input_buffer; 
                        prefs.putString(("s" + String(i)).c_str(), slots[i]);
                        status_msg = "SAVED " + String(i); 
                        status_timer = millis() + 1500;
                    }
                }
            } 
            // --- CTRL: UNDO/REDO ---
            else if (st.ctrl) {
                if (M5Cardputer.Keyboard.isKeyPressed('z') || M5Cardputer.Keyboard.isKeyPressed('Z')) { 
                    int prev = (undo_ptr - 1 + UNDO_DEPTH) % UNDO_DEPTH; 
                    if (undo_stack[prev] != "") { 
                        undo_ptr = prev; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); 
                    } 
                }
                if (M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y')) { 
                    if (undo_ptr != undo_max) { 
                        undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); 
                    } 
                }
            } 
            // --- FN: MODIFIER COMMANDS ---
            else if (st.fn) {
                // WiFi Server Toggle (W)
                if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
                    if (!is_streaming) {
                        WiFi.mode(WIFI_AP);
                        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
                        if (WiFi.softAP("BYTEBED")) {
                            is_streaming = true;
                            initBytebeatServer();
                            xTaskCreatePinnedToCore(startDnsHijack, "DNS", 2048, NULL, 1, NULL, 1);
                            status_msg = "WIFI ACTIVE";
                        }
                    } else { ESP.restart(); }
                    status_timer = millis() + 1500;
                }
                // Theme Toggle (T)
                if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) { 
                    current_theme_idx = (current_theme_idx + 1) % 3; 
                    bg_sprite.fillScreen(theme.bg); 
                }
                // Sample Rate Cycling (S)
                if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) { 
                    if (current_sample_rate == 8000) current_sample_rate = 11025;
                    else if (current_sample_rate == 11025) current_sample_rate = 22050;
                    else if (current_sample_rate == 22050) current_sample_rate = 32000;
                    else if (current_sample_rate == 32000) current_sample_rate = 44100;
                    else if (current_sample_rate == 44100) current_sample_rate = 48000;
                    else current_sample_rate = 8000;
                    status_msg = "RATE: " + String(current_sample_rate) + "Hz";
                    status_timer = millis() + 1500;
                }
                // Mode Toggle (F)
                if (M5Cardputer.Keyboard.isKeyPressed('f') || M5Cardputer.Keyboard.isKeyPressed('F')) {
                    current_play_mode = (current_play_mode == MODE_BYTEBEAT) ? MODE_FLOATBEAT : MODE_BYTEBEAT;
                    status_msg = (current_play_mode == MODE_BYTEBEAT) ? "MODE: BYTEBEAT" : "MODE: FLOATBEAT";
                    status_timer = millis() + 1500;
                }
                // Visualizers (0-4)
                if (M5Cardputer.Keyboard.isKeyPressed('1')) { current_vis = VIS_WAV_WIRE; bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('2')) { current_vis = VIS_DIA_AMP;  bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('3')) { current_vis = VIS_DIA_BIT;  bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('4')) { current_vis = VIS_WAV_ORIG; bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('0')) { current_vis = VIS_HISTORY; }
                // Math Scale & Volume Controls
                if (M5Cardputer.Keyboard.isKeyPressed('=')) { drawScale = std::min(10, drawScale + 1); bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed('-')) { drawScale = std::max(0, drawScale - 1); bg_sprite.fillScreen(theme.bg); }
                if (M5Cardputer.Keyboard.isKeyPressed(';')) { volume_perc = std::min(1.0f, volume_perc + VOL_STEP); M5.Speaker.setVolume(getLogVolume()); }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) { volume_perc = std::max(0.0f, volume_perc - VOL_STEP); M5.Speaker.setVolume(getLogVolume()); }
                // Cursor Navigation
                if (M5Cardputer.Keyboard.isKeyPressed(',')) { if (cursor_pos > 0) cursor_pos--; }
                if (M5Cardputer.Keyboard.isKeyPressed('/')) { if (cursor_pos < (int)input_buffer.length()) cursor_pos++; }
                // Panic Reset (`)
                if (M5Cardputer.Keyboard.isKeyPressed('`')) { is_playing = false; t_raw = 0; input_buffer = ""; cursor_pos = 0; bg_sprite.fillScreen(theme.bg); }
            } 
            // --- STANDARD TYPING ---
            else {
                if (st.word.size() > 0) {
                    for (auto i : st.word) { input_buffer = input_buffer.substring(0, cursor_pos) + i + input_buffer.substring(cursor_pos); cursor_pos++; }
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
                    rpn_mode = !rpn_mode; input_buffer = decompile(rpn_mode); cursor_pos = input_buffer.length();
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                    saveUndo(); 
                    bg_sprite.fillScreen(theme.bg); 
                    bool valid = rpn_mode ? compileRPN(input_buffer) : compileInfix(input_buffer, false); 
                    applyCompilationResult(valid);
                } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                    if (cursor_pos > 0) { input_buffer.remove(cursor_pos - 1, 1); cursor_pos--; }
                }
            }
        }
        
        // Refresh UI at constant framerate
        if (millis() - last_draw > UI_REFRESH_MS) { draw(); last_draw = millis(); }
        
        // Essential yield for Core 0 to handle WiFi and System background processes
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    auto cfg = M5.config(); 
    M5Cardputer.begin(cfg, true); 
    M5.update();

    input_buffer.reserve(2048);

    canvas.setColorDepth(8);
    canvas.createSprite(240, 135); 
    bg_sprite.setColorDepth(8);
    bg_sprite.createSprite(240, 135); 
    bg_sprite.fillScreen(theme.bg);

    // Initialize DSP Accelerators (Sine LUT)
    init_fast_math(); 

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 
    
    // Configured massive 12KB DMA Acoustic Runway
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.dma_buf_len = 1024; 
    spk_cfg.dma_buf_count = 12;
    M5.Speaker.config(spk_cfg);
    
    M5.Speaker.begin(); 
    M5.Speaker.setVolume(getLogVolume());
    
    // Load Presets from NVS
    prefs.begin("bytebeat", false);
    for (int i = 0; i < 10; i++) {
        if (TEST_PRESETS) { slots[i] = testPresets[i]; }
        else if (REWRITE_PRESETS) { slots[i] = classicPresets[i]; prefs.putString(("s" + String(i)).c_str(), slots[i]); }
        else {
            slots[i] = prefs.getString(("s" + String(i)).c_str(), "");
            if (slots[i] == "") { 
                slots[i] = classicPresets[i]; 
                prefs.putString(("s" + String(i)).c_str(), slots[i]); 
            }
        }
    }
    
    input_buffer = slots[0]; 
    undo_stack[0] = input_buffer;
    compileInfix(input_buffer, true);
    active_eval_formula = input_buffer; 
    
    // Launch Core 1 Audio Task (Priority 24 - Bare Metal DSP)
    xTaskCreatePinnedToCore(playBytebeat, "audio", 4096, NULL, 24, NULL, 1);
    
    // Launch Core 0 UI Task (Priority 5 - Responsive Graphics & WiFi)
    xTaskCreatePinnedToCore(uiTask, "ui_task", 8192, NULL, 5, NULL, 0);
}

void loop() {
    // Arduino loop task is silenced to prevent context switching on Core 1
    vTaskDelay(portMAX_DELAY);
}
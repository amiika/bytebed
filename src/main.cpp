#include <WiFi.h>
#include <ESPmDNS.h>
#include "state.h"
#include "vm.h"
#include "ui.h"
#include "captive.h"
#include "../test/tests.h" 

void playBytebeat(void *pvParameters) {
    int16_t local_mono_buf[AUDIO_BUF_SIZE];

    while(1) {
        if (is_playing) {
            for(int i = 0; i < AUDIO_BUF_SIZE; i++) {
                uint8_t sample = execute_vm(t_raw);
                
                if (current_vis == VIS_WAV_WIRE) { 
                    int mod = std::max(1, 1 << drawScale); 
                    if (t_raw % mod == 0) wave_buf[(t_raw/mod)%240] = sample; 
                } else {
                    updatePersistentVis(sample, t_raw);
                }
                
                t_raw++;
                local_mono_buf[i] = (int16_t)((sample - 128) << 8); 
            }
            
            M5.Speaker.playRaw(local_mono_buf, AUDIO_BUF_SIZE, SAMPLE_RATE_MATH, false, 1, 0); 

        } else {
            vTaskDelay(10);
        }
    }
}

void setup() {
    auto cfg = M5.config(); M5Cardputer.begin(cfg, true); M5.begin(cfg);
    canvas.createSprite(240, 135); bg_sprite.createSprite(240, 135); bg_sprite.fillScreen(theme.bg);
    
    runTests(canvas);

    WiFi.mode(WIFI_AP);
    WiFi.softAP("BYTEBED"); 
    
    xTaskCreatePinnedToCore(startBytebeatServer, "HTTP Server", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(startDnsHijack, "DNS Server", 2048, NULL, 1, NULL, 1); 
    
    M5.Speaker.begin(); M5.Speaker.setVolume(getLogVolume());
    prefs.begin("bytebeat", false);
    for (int i = 0; i < 10; i++) {
        slots[i] = prefs.getString(("s" + String(i)).c_str(), "");
        if (slots[i] == "") { slots[i] = classicPresets[i]; prefs.putString(("s" + String(i)).c_str(), slots[i]); }
    }
    undo_stack[0] = input_buffer;
    compileInfix(input_buffer, true);
    active_eval_formula = input_buffer; 
    
    xTaskCreatePinnedToCore(playBytebeat, "audio", 4096, NULL, 20, NULL, 0);
}

void loop() {
    M5.update(); M5Cardputer.update();
    
    auto st = M5Cardputer.Keyboard.keysState();
    
    if (st.opt) {
        strncpy(current_top_text, "LOAD: 0-9", 63);
    } else if (st.alt) {
        strncpy(current_top_text, "SAVE: 0-9", 63);
    } else if (st.ctrl) {
        strncpy(current_top_text, "CTRL: Z / Y", 63);
    } else if (st.fn) {
        strncpy(current_top_text, "FN: 0-4 / Arrows / W / T / +-", 63);
    } else {
        strncpy(current_top_text, "BYTEBED", 63);
    }
    current_top_text[63] = '\0'; 

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        if (st.opt) {
            for (int i = 0; i < 10; i++) if (M5Cardputer.Keyboard.isKeyPressed('0' + i) && slots[i] != "") {
                input_buffer = slots[i]; cursor_pos = input_buffer.length(); rpn_mode = false; 
                saveUndo(); 
                
                bool valid = compileInfix(input_buffer, false); 
                bg_sprite.fillScreen(theme.bg);
                
                if (valid) {
                    active_eval_formula = input_buffer; 
                    status_msg = "LOAD " + String(i) + " OK"; 
                } else {
                    status_msg = "LOAD " + String(i) + " ERR"; 
                }
                status_timer = millis() + 1500;
            }
        } else if (st.alt) {
            for (int i = 0; i < 10; i++) if (M5Cardputer.Keyboard.isKeyPressed('0' + i)) {
                slots[i] = rpn_mode ? decompile(false) : input_buffer; prefs.putString(("s" + String(i)).c_str(), slots[i]);
                status_msg = "SAVED " + String(i); status_timer = millis() + 1500;
            }
        } else if (st.ctrl) {
            if (M5Cardputer.Keyboard.isKeyPressed('Z')) { int prev = (undo_ptr - 1 + UNDO_DEPTH) % UNDO_DEPTH; if (undo_stack[prev] != "") { undo_ptr = prev; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); } }
            if (M5Cardputer.Keyboard.isKeyPressed('Y')) { if (undo_ptr != undo_max) { undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; input_buffer = undo_stack[undo_ptr]; cursor_pos = input_buffer.length(); } }
            

        } else if (st.fn) {
            if (M5Cardputer.Keyboard.isKeyPressed('W') || M5Cardputer.Keyboard.isKeyPressed('w')) {
                is_streaming = !is_streaming;
                status_msg = is_streaming ? "WIFI ACTIVE" : "WIFI STOPPED";
                status_timer = millis() + 1500;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('T') || M5Cardputer.Keyboard.isKeyPressed('t')) { 
                current_theme_idx = (current_theme_idx + 1) % 3; 
                bg_sprite.fillScreen(theme.bg); 
            }
            if (M5Cardputer.Keyboard.isKeyPressed('1')) { current_vis = VIS_WAV_WIRE; bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed('2')) { current_vis = VIS_DIA_AMP;  bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed('3')) { current_vis = VIS_DIA_BIT;  bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed('4')) { current_vis = VIS_WAV_ORIG; bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed('0')) { current_vis = VIS_HISTORY; }
            if (M5Cardputer.Keyboard.isKeyPressed('=')) { drawScale = std::min(10, drawScale + 1); bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed('-')) { drawScale = std::max(0, drawScale - 1); bg_sprite.fillScreen(theme.bg); }
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { volume_perc = std::min(1.0f, volume_perc + VOL_STEP); M5.Speaker.setVolume(getLogVolume()); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { volume_perc = std::max(0.0f, volume_perc - VOL_STEP); M5.Speaker.setVolume(getLogVolume()); }
            if (M5Cardputer.Keyboard.isKeyPressed(',')) { if (cursor_pos > 0) cursor_pos--; }
            if (M5Cardputer.Keyboard.isKeyPressed('/')) { if (cursor_pos < (int)input_buffer.length()) cursor_pos++; }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { is_playing = false; t_raw = 0; input_buffer = ""; cursor_pos = 0; bg_sprite.fillScreen(theme.bg); }
        } else {
            if (st.word.size() > 0) {
                for (auto i : st.word) { input_buffer = input_buffer.substring(0, cursor_pos) + i + input_buffer.substring(cursor_pos); cursor_pos++; }
            } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_TAB)) {
                rpn_mode = !rpn_mode; input_buffer = decompile(rpn_mode); cursor_pos = input_buffer.length();
            } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                saveUndo(); 
                bg_sprite.fillScreen(theme.bg); 
                
                bool valid = false;
                if (rpn_mode) valid = compileRPN(input_buffer); 
                else valid = compileInfix(input_buffer, false); 
                
                if (valid) {
                    is_playing = true;
                    active_eval_formula = rpn_mode ? decompile(false) : input_buffer; 
                    status_msg = "OK";
                } else {
                    status_msg = "ERR";
                }
                status_timer = millis() + 1500;
                
            } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
                if (cursor_pos > 0) { input_buffer.remove(cursor_pos - 1, 1); cursor_pos--; }
            }
        }
    }
    if (millis() - last_draw > UI_REFRESH_MS) { draw(); last_draw = millis(); }
}
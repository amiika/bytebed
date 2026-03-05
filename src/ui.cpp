#include "ui.h"
#include <WiFi.h>

void updatePersistentVis(uint8_t sample, int32_t t) {
    if (current_vis == VIS_HISTORY) return; 
    
    Layout L = getLayout();
    
    uint16_t dyn_color = canvas.color565(
        (sample * theme.r_base) / 255, 
        (sample * theme.g_base) / 255, 
        (sample * theme.b_base) / 255
    );

    if (current_vis == VIS_DIA_AMP) {
        int s_val = drawScale;
        int x = (t >> s_val) % 240; 
        int y_c = (L.input_y - 1) - (sample * L.vis_h / 255); 
        int y_center = (L.input_y - 1) - (L.vis_h / 2); 
        
        if (t % (1 << s_val) == 0) {
            bg_sprite.drawFastVLine((x + 1) % 240, L.vis_y, L.vis_h, theme.bg);
        }
        bg_sprite.drawFastVLine(x, std::min(y_c, y_center), std::abs(y_c - y_center) + 1, dyn_color);
    } 
    else if (current_vis == VIS_DIA_BIT) {
        int s_val = std::max(1, drawScale);
        int dSize = std::max(1, 256 >> s_val); 
        int col = (t >> s_val) % 240; 
        int rS = dSize * (t % (1 << s_val));
        if (rS < L.vis_h) bg_sprite.fillRect(col, L.vis_y + rS, 1, dSize, dyn_color);
    }
    else if (current_vis == VIS_WAV_ORIG) {
        int s_val = drawScale; 
        int x = (t >> s_val) % 240; 
        int y_c = (L.input_y-1)-(sample*L.vis_h/255); 
        int y_p = (L.input_y-1)-(last_sample_val*L.vis_h/255);
        
        if (t % (1 << s_val) == 0) {
            bg_sprite.drawFastVLine((x+1)%240, L.vis_y, L.vis_h, theme.bg);
        }
        
        uint8_t shade_val = std::max((uint8_t)48, sample); 
        uint16_t wav_dyn_color = canvas.color565(
            (shade_val * theme.r_base) / 255, 
            (shade_val * theme.g_base) / 255, 
            (shade_val * theme.b_base) / 255
        );

        bg_sprite.drawFastVLine(x, std::min(y_p, y_c), std::abs(y_p - y_c) + 1, wav_dyn_color); 
    }
    
    last_sample_val = sample;
}

void draw() {
    Layout L = getLayout(); canvas.fillScreen(theme.bg);
    
    if (current_vis != VIS_WAV_WIRE && current_vis != VIS_HISTORY) {
        bg_sprite.pushSprite(&canvas, 0, 0);
    }
    else if (current_vis == VIS_WAV_WIRE) {
        for (int i = 0; i < 239; i++) {
            int y1 = (L.input_y - 1) - (wave_buf[i] * L.vis_h / 255);
            int y2 = (L.input_y - 1) - (wave_buf[i+1] * L.vis_h / 255);
            canvas.drawLine(i, y1, i+1, y2, theme.primaryColor);
        }
    }
    else if (current_vis == VIS_HISTORY) {
        int max_lines = L.vis_h / 10;
        std::vector<String> hist_lines;
        for (int i = 0; i < UNDO_DEPTH; i++) {
            int idx = (undo_ptr - i + UNDO_DEPTH) % UNDO_DEPTH;
            if (undo_stack[idx] != "") {
                hist_lines.push_back(undo_stack[idx]);
                if (hist_lines.size() >= max_lines) break;
            }
        }
        
        for (int i = 0; i < hist_lines.size(); i++) {
            int y_pos = L.vis_y + L.vis_h - 12 - (i * 10);
            canvas.setCursor(5, y_pos);
            if (i == 0) canvas.setTextColor(theme.textColor); 
            else canvas.setTextColor(theme.dimColor);         
            
            String s = hist_lines[i];
            if (s.length() > 38) s = s.substring(0, 35) + "..."; 
            canvas.print(s);
        }
    }
    
    canvas.fillRect(0, 0, 240, 21, theme.bg); 
    canvas.setTextColor(theme.textColor); 
    canvas.setCursor(5, 5);
    canvas.print(current_top_text); 
    
    if (millis() < status_timer && status_msg != "") {
        int text_width = canvas.textWidth(status_msg); 
        canvas.setCursor(240 - 5 - text_width, 5); 
        canvas.setTextColor(theme.primaryColor); 
        canvas.print(status_msg);
    }
    
    static float last_vol = volume_perc;
    static uint32_t vol_timer = 0;
    
    if (last_vol != volume_perc) {
        last_vol = volume_perc;
        vol_timer = millis() + 1500; 
    }

    if (millis() < vol_timer && (millis() >= status_timer || status_msg == "")) {
        canvas.drawRect(155, 6, 65, 8, theme.dimColor); 
        canvas.fillRect(155, 6, (int)(volume_perc * 65), 8, theme.primaryColor);
    }
    
    canvas.drawFastHLine(0, 21, 240, theme.dimColor);
    canvas.fillRect(0, L.input_y, 240, 135 - L.input_y, theme.bg); 
    canvas.drawFastHLine(0, L.input_y, 240, theme.dimColor);
    canvas.setCursor(5, L.input_y + 4); 
    canvas.print(input_buffer);
    
    int c_x = 5 + (cursor_pos % 40) * 6, c_y = (L.input_y + 4) + (cursor_pos / 40) * 8;
    canvas.drawFastVLine(c_x, c_y, 10, is_playing ? theme.primaryColor : TFT_RED);
    canvas.pushSprite(0, 0);
}
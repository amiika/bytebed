#include "state.h"

Theme themes[3] = {
    { 0x07E0, 0x0320, 0x0000, 0x07E0, 0, 255, 0 },    
    { 0x07FF, 0x01AA, 0x0000, 0x07FF, 0, 200, 255 },  
    { 0xF81F, 0x780F, 0x0000, 0xF81F, 255, 0, 255 }   
};
int current_theme_idx = 0;

httpd_handle_t stream_server = NULL;
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool is_streaming = false;

Preferences prefs;
VisMode current_vis = VIS_WAV_WIRE; 
LGFX_Sprite canvas(&M5.Display);
LGFX_Sprite bg_sprite(&M5.Display);

int drawScale = 5; 
float volume_perc = 0.4f; 
int32_t t_raw = 0;
uint8_t wave_buf[240] = {0};
uint8_t last_sample_val = 128;
uint32_t last_draw = 0;

const char* classicPresets[10] = {
    "t*((t>>12|t>>8)&63&t>>4)",
    "(t*5&t>>7)|(t*3&t>>10)",
    "(t>>6|t<<1)+(t>>5|t<<3|t>>10)",
    "t*(42&t>>10)",
    "t&t>>8",
    "t*(t>>5|t>>8)>>8&63",
    "(t*t/(1+t>>10))&t>>8",
    "(t>>7|t|t>>6)*10+4*(t&t>>13)",
    "t*(t>>11&t>>8&123&t>>3)",
    "t>>4|t>>5|t%256"
};

String slots[10];
String input_buffer = classicPresets[0];
String active_eval_formula = classicPresets[0];
String status_msg = "";
uint32_t status_timer = 0;
String undo_stack[UNDO_DEPTH];
int cursor_pos = input_buffer.length();
int undo_ptr = 0;
int undo_max = 0;
bool rpn_mode = false;
bool is_playing = true;

char current_top_text[64] = "BYTENATOR";

Layout getLayout() {
    int input_h = (input_buffer.length() > 80) ? 60 : 25;
    return { 135 - input_h, 21, (135 - input_h) - 21 };
}

uint8_t getLogVolume() { 
    return (uint8_t)(volume_perc * volume_perc * volume_perc * 255.0f); 
}
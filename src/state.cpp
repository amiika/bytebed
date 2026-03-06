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

const char* testPresets[10] = {
    "g(n){n<1?1:n%2<1?g(n/2):2*g(n/2)}; g(t>>7)*(t&128|t>>4)",    // 0: Gould's Fractal Synth
    "tm(n){n<1?0:n%2<1?tm(n/2):1-tm(n/2)}; t*tm(t>>11)*(t>>5&7)", // 1: Thue-Morse Rhythm Bass
    "fib(n){n<2?1:fib(n-1)+fib(n-2)}; t*fib((t>>11)%7+3)",        // 2: Fibonacci Arpeggiator
    "env=(t>>12)%4; env<1?t*3:env<2?t*5:env<3?t*6:t*9",           // 3: Ternary Acid Bassline
    "osc(x,dt){x*(t>>dt)}; osc(t,5)+osc(t,7)",                    // 4: Multi-Oscillator Chords
    "lfo=t>>11&15; t*lfo",                                        // 5: Simple LFO Envelope
    "t*(42&t>>10)",                                               // 6: Classic (Control)
    "t&t>>8",                                                     // 7: Sierpinski (Control)
    "(t>>6|t<<1)+(t>>5|t<<3|t>>10)",                              // 8: Classic (Control)
    "t*(t>>11&t>>8&123&t>>3)"                                     // 9: Classic (Control)
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

void saveUndo() { 
    if (input_buffer == undo_stack[undo_ptr]) return; 
    undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; 
    undo_stack[undo_ptr] = input_buffer; 
    undo_max = undo_ptr; 
}
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

PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = DEFAULT_SAMPLE_RATE;

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
    "g(n){n<1?1:n%2<1?g(n/2):2*g(n/2)}; g(t>>7)*(t&128|t>>4)", 
    "tm(n){n<1?0:n%2<1?tm(n/2):1-tm(n/2)}; t*tm(t>>11)*(t>>5&7)",
    "fib(n){n<2?1:fib(n-1)+fib(n-2)}; t*fib((t>>11)%7+3)",
    "a=[2,3.26,2,2.5,2.5];b=[134,356,345,384,234,345,345];sin(t/33000*a[t>>12&3]*b[t>>16&3])",
    "osc(x,dt){x*(t>>dt)}; osc(t,5)+osc(t,7)",
    "lfo=t>>11&15; t*lfo",
    "a=t*2**([1,2,3][t>>22&3]/4),b=t*2**([1,2,3][t>>11&63]/12+2),(3*a^t>>6&256/'1112'[t>>14&3]-1|a)%256*2/3+(b^b*2)%256/3",                                               
    "a=[1,2,3][(t>>10)%4]*[2,4,5,6][(t>>16)%4]*t,b=a%32+t>>a,c=b%t+a,[(t>>a)+b,c]",                                  
    "t*(4|t>>13&3)>>(~t>>11&1)&128|t*(t>>11&t>>13)*(~t>>9&3)&127", 
    "t/=5.51;a=t*((t&4096?59392>t%65536?7:t&7:16)+(1&t>>14))>>(3&-t>>(t&2048?2:10))|t>>(t&16384?t&4096?10:3:2);b=a/3%(85+1/3)+(61440>t%65536?44583.53:0)/(t&4095)/2%128;c=(256>t%1024?4:0)*t*r()|t>>0;d=(256>t%512?4:0)*t*r()|t>>0;e=14336>t%16384?c:d;f=e/8%32+b%190;126976>t%131072?f:f+44583/(-t&4095)/2%128"
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
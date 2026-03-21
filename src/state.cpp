#include "state.h"

Theme themes[3] = {
    { 0x07E0, 0x0320, 0x0000, 0x07E0, 0, 255, 0 },    
    { 0x07FF, 0x01AA, 0x0000, 0x07FF, 0, 200, 255 },  
    { 0xF81F, 0x780F, 0x0000, 0xF81F, 255, 0, 255 }   
};
int current_theme_idx = 0;

#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
httpd_handle_t stream_server = NULL;
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool is_streaming = false;

Preferences prefs;
VisMode current_vis = VIS_WAV_WIRE; 
LGFX_Sprite canvas(&M5.Display);
LGFX_Sprite bg_sprite(&M5.Display);
#else
VisMode current_vis = VIS_WAV_WIRE; 
#endif

int drawScale = 5; 
float volume_perc = 0.4f; 
int32_t t_raw = 0;
uint8_t wave_buf[240] = {0};
uint8_t last_sample_val = 128;
uint32_t last_draw = 0;

PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = DEFAULT_SAMPLE_RATE;

#define EMPTY_PRESET {"t", 8000, MODE_BYTEBEAT}

// --- MASSIVE 100-SLOT PRESET MATRIX ---
const PresetConfig defaultBanks[10][10] = {
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    // BANK 0: CLASSIC (8kHz Bytebeat)
    {
        {"t*((t>>12|t>>8)&63&t>>4)", 8000, MODE_BYTEBEAT},
        {"(t*5&t>>7)|(t*3&t>>10)", 8000, MODE_BYTEBEAT},
        {"(t>>6|t<<1)+(t>>5|t<<3|t>>10)", 8000, MODE_BYTEBEAT},
        {"t*(42&t>>10)", 8000, MODE_BYTEBEAT},
        {"t&t>>8", 8000, MODE_BYTEBEAT},
        {"t*(t>>5|t>>8)>>8&63", 8000, MODE_BYTEBEAT},
        {"(t*t/(1+t>>10))&t>>8", 8000, MODE_BYTEBEAT},
        {"(t>>7|t|t>>6)*10+4*(t&t>>13)", 8000, MODE_BYTEBEAT},
        {"t*(t>>11&t>>8&123&t>>3)", 8000, MODE_BYTEBEAT},
        {"t>>4|t>>5|t%256", 8000, MODE_BYTEBEAT}
    },
    // BANK 1: TESTS (8kHz Bytebeat)
    {
        {"g=(n)=>{n<1?1:n%2<1?g(n/2):2*g(n/2)}; g(t>>7)*(t&128|t>>4)", 8000, MODE_BYTEBEAT}, 
        {"tm=(n)=>{n<1?0:n%2<1?tm(n/2):1-tm(n/2)}; t*tm(t>>11)*(t>>5&7)", 8000, MODE_BYTEBEAT},
        {"fib=(n)=>{n<2?1:fib(n-1)+fib(n-2)}; t*fib((t>>11)%7+3)", 8000, MODE_BYTEBEAT},
        {"env=(t>>12)%4; env<1?t*3:env<2?t*5:env<3?t*6:t*9", 8000, MODE_BYTEBEAT},
        {"osc=(x,dt)=>{x*(t>>dt)}; osc(t,5)+osc(t,7)", 8000, MODE_BYTEBEAT},
        {"lfo=t>>11&15; t*lfo", 8000, MODE_BYTEBEAT},
        {"a=t*2**([1,2,3][t>>22&3]/4),b=t*2**([1,2,3][t>>11&63]/12+2),(3*a^t>>6&256/'1112'[t>>14&3]-1|a)%256*2/3+(b^b*2)%256/3", 8000, MODE_BYTEBEAT},                                               
        {"a=[1,2,3][(t>>10)%4]*[2,4,5,6][(t>>16)%4]*t,b=a%32+t>>a,c=b%t+a,[(t>>a)+b,c]", 8000, MODE_BYTEBEAT},                                  
        {"t*(4|t>>13&3)>>(~t>>11&1)&128|t*(t>>11&t>>13)*(~t>>9&3)&127", 8000, MODE_BYTEBEAT}, 
        {"t", 8000, MODE_BYTEBEAT}
    },
    // BANK 2: FLOATBEAT (48kHz Floatbeat)
    {
        {"gc=t/(t/[12,13,14,15,16,17,18,19,20,21,22,23,24][t>>12&13]),(sin(t/gc)+sin(t/20)+sin(t/20)+tan(sin(t/t-t/(gc+.01))))/4", 48000, MODE_FLOATBEAT},                                                                 
        {"sin(t/20)*exp(-(t%8000)/1000)", 48000, MODE_FLOATBEAT},                                             
        {"w=t/50, sin(w + sin(w*2)*2)", 48000, MODE_FLOATBEAT},                                               
        {"p=t*2**([0,4,7,12][(t>>13)%4]/12)/2.43, sin(p*PI/16)*exp(-(t%8192)/2000)", 48000, MODE_FLOATBEAT},  
        {"p=(t>>15&t>>13)*[3,2,1.5,0][3&t>>11]/2,b=.5*[1,2][1&t>>13]*[2,2.5,3,3.5][3&t>>15]/2,sin(sin(p*t/4.79)/40*(32-t/256%32)+p*t/40.743665)*(32-t/256%32)/64+sin(sin(b*t/81.48732)/6*(32-t/256%32)+b*t/40.743665)*(32-t/256%32)/48", 48000, MODE_FLOATBEAT},                                  
        {"w=t/20, sin(w)*abs(sin(t/4000))", 48000, MODE_FLOATBEAT},                                           
        {"f=t/(50+sin(t/8000)*20), sin(f)", 48000, MODE_FLOATBEAT},                                           
        {"e=256,r=40,d=32,p=t*2**([0,12,12,0,12,12,5,7][7&t>>13]/12)/2.43,env=r-t/e%d,sin(sin(sin(p*PI/16)/76*env+p*PI/256)/8*env+p*PI/256)*env/64", 48000, MODE_FLOATBEAT}, 
        {"f=(t%4000)/4000, sin(t/(20+f*50))", 48000, MODE_FLOATBEAT},                                         
        {"sin(t/(t&16384?1.5:1)/[1,2,3,4,1,2,3,4,6,4,5,4,8,8,8,8][15&t>>10])/((t&[1023,1023,1023,2047][3&t>>12])*.01)", 48000, MODE_FLOATBEAT}                                                  
    },
    // BANKS 3-9: EMPTY PRESETS
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET },
    { EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET, EMPTY_PRESET }
};

SlotState slots[10][10];
int current_bank = 0;

String input_buffer = defaultBanks[0][0].formula;
String active_eval_formula = defaultBanks[0][0].formula;
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
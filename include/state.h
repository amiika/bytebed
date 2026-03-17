#pragma once

// --- Bulletproof Native & Web Gate (Includes) ---
#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_http_server.h>
#include <DNSServer.h>
#else
// --- Web & Mac Native Fallback ---
#include "Arduino.h" 
#endif

#include <vector>
#include <map>

#define DEFAULT_SAMPLE_RATE 8000   
#define AUDIO_BUF_SIZE      512    
#define VOL_STEP            0.05f  
#define UI_REFRESH_MS       33
#define UNDO_DEPTH          20

enum VisMode { 
    VIS_WAV_WIRE, VIS_DIA_AMP, VIS_DIA_BIT, VIS_WAV_ORIG, VIS_HISTORY 
};

enum PlayMode { 
    MODE_BYTEBEAT, 
    MODE_FLOATBEAT 
};

enum OpCode : uint8_t { 
    OP_VAL, OP_T, OP_LOAD, OP_STORE, OP_STORE_KEEP, OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, 
    OP_AND, OP_OR,  OP_XOR, OP_SHL, OP_SHR, 
    OP_LT,  OP_GT,  OP_EQ,  OP_NEQ, OP_LTE, OP_GTE,
    OP_COND, OP_NEG, OP_NOT, OP_BNOT,               
    OP_SIN, OP_COS, OP_TAN, OP_SQRT, OP_LOG, OP_EXP,
    OP_ABS, OP_FLOOR, OP_CEIL, OP_ROUND,
    OP_MIN, OP_MAX, OP_POW, 
    OP_JMP, OP_PUSH_FUNC, OP_DYN_CALL, OP_DYN_CALL_IF_FUNC, OP_RET,
    OP_BIND, OP_UNBIND, OP_ASSIGN_VAR, 
    OP_ALLOC, OP_VEC, OP_AT, OP_STORE_AT, 
    
    OP_SC_AND, OP_SC_OR, OP_NONE,
    
    OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN, OP_MOD_ASSIGN,
    OP_AND_ASSIGN, OP_OR_ASSIGN, OP_XOR_ASSIGN, OP_POW_ASSIGN, OP_SHL_ASSIGN, OP_SHR_ASSIGN
};

struct Theme {
    uint16_t primaryColor; uint16_t dimColor; uint16_t bg; uint16_t textColor;
    uint8_t r_base; uint8_t g_base; uint8_t b_base;
};

extern Theme themes[3];
extern int current_theme_idx;
#define theme themes[current_theme_idx]

struct Layout { int input_y; int vis_y; int vis_h; };

// --- Bulletproof Native & Web Gate (Variables) ---
#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
extern httpd_handle_t stream_server;
extern DNSServer dnsServer;
extern const byte DNS_PORT;
extern bool is_streaming;

extern Preferences prefs;
extern LGFX_Sprite canvas;
extern LGFX_Sprite bg_sprite; 
#endif

extern VisMode current_vis;
extern int drawScale; 
extern float volume_perc; 
extern int32_t t_raw;
extern uint8_t wave_buf[240];
extern uint8_t last_sample_val;
extern uint32_t last_draw;

extern PlayMode current_play_mode;
extern int current_sample_rate;

extern const char* classicPresets[10];
extern const char* testPresets[10];
extern String slots[10];
extern String input_buffer;         
extern String active_eval_formula;  
extern String status_msg;
extern uint32_t status_timer;
extern String undo_stack[UNDO_DEPTH];
extern int cursor_pos;
extern int undo_ptr;
extern int undo_max;
extern bool rpn_mode;
extern bool is_playing;

extern char current_top_text[64];

Layout getLayout();
uint8_t getLogVolume();
void saveUndo();
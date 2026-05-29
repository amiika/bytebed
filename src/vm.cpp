#include "vm.h"
#include "fast_math.h"
#include <algorithm>

/**
 * @struct MathFunc
 * Describes a built-in mathematical function or shorthand operator mapping.
 */
const MathFunc mathLibrary[] = {
    {"sin",   OP_SIN,   true},  {"cos",   OP_COS,   true},  {"tan",   OP_TAN,   true},
    {"sqrt",  OP_SQRT,  true},  {"log",   OP_LOG,   true},  {"exp",   OP_EXP,   true},
    {"abs",   OP_ABS,   true},  {"floor", OP_FLOOR, true},  {"ceil",  OP_CEIL,  true},
    {"round", OP_ROUND, true},  {"cbrt",  OP_CBRT,  true},  {"asin",  OP_ASIN,  true},
    {"acos",  OP_ACOS,  true},  {"atan",  OP_ATAN,  true},
    {"min",   OP_MIN,   false}, {"max",   OP_MAX,   false}, {"pow",   OP_POW,   false},
    {"random", OP_RAND, true},  {"int",   OP_INT,   true},  {"in",    OP_PHASE, false}, 
    {"dup",   OP_DUP,   false}, {"swap",  OP_SWAP,  false}, {"rot",   OP_ROT,   false},
    {"over",  OP_OVER,  false},
    {"env",   OP_ENV,   false}, {"osc",   OP_LFO,   false},
    {"pc",    OP_PC,    false}, {"euclid",OP_EUCLID,false}, {"on",    OP_ON,    false},
    {"as",    OP_DUR,   false}, {"to",    OP_TO,    false}, {"at",    OP_AT_MASK, false}
};
const int mathLibrarySize = 32;

const MathFunc shorthands[] = {
    {"s", OP_SIN, true}, {"c", OP_COS, true}, {"r", OP_RAND, true}, 
    {"ec", OP_EUCLID, true}
};
const int shorthandsSize = 4;

const OpInfo opList[] = {
    {"+",  OP_ADD, 7}, {"-",  OP_SUB, 7}, {"*",  OP_MUL, 8},
    {"/",  OP_DIV, 8}, {"%",  OP_MOD, 8}, {"&",  OP_AND, 3},
    {"|",  OP_OR,  1}, {"^",  OP_XOR, 2}, {"<",  OP_LT,  5},
    {">",  OP_GT,  5}, {"!",  OP_NOT, 9}, {"~",  OP_BNOT, 9}, 
    {"==", OP_EQ,  4}, {"!=", OP_NEQ, 4}, {"<=", OP_LTE, 5}, 
    {">=", OP_GTE, 5}, {"<<", OP_SHL, 6}, {">>", OP_SHR, 6},
    {"&&", OP_SC_AND, 3}, {"||", OP_SC_OR, 1}, {"**", OP_POW, 9}, 
    {"_",  OP_VEC, 10}, {"@", OP_AT, 10}, {"#", OP_STORE_AT, -1},
    {"$",  OP_ALLOC, 10}, {":=", OP_STORE_KEEP, -1},
    {"+=", OP_ADD_ASSIGN, -1}, {"-=", OP_SUB_ASSIGN, -1},
    {"*=", OP_MUL_ASSIGN, -1}, {"/=", OP_DIV_ASSIGN, -1},
    {"%=", OP_MOD_ASSIGN, -1}, {"&=", OP_AND_ASSIGN, -1},
    {"|=", OP_OR_ASSIGN, -1},  {"^=", OP_XOR_ASSIGN, -1},
    {"<<=", OP_SHL_ASSIGN, -1},{">>=", OP_SHR_ASSIGN, -1},
    {"**=", OP_POW_ASSIGN, -1},
    {"<>",  OP_SWAP, 10}, 
    {"++",  OP_DUP,  10}, 
    {"^^",  OP_OVER, 10}, 
    {"@@",  OP_ROT,  10},  
    {"--",  OP_POP,  10},
    {":", OP_ARITY, 10} 
};
const int opListSize = 42;

Instruction program_bank[2][512];
int prog_len_bank[2] = {0, 0};
volatile uint8_t active_bank = 0;

Val vars[64];
String symNames[64];
int var_count = 0;

String string_table[64];
int string_table_count = 0;

float* global_array_mem = nullptr;
int32_t global_array_capacity = 0;

float anchor_bpm = 120.0f;
float anchor_beat = 0.0f;
int32_t anchor_t = 0;
int anchor_sample_rate = 8000;

constexpr int MAX_LOCAL_ARRAY = 1024;
float local_array_mem[MAX_LOCAL_ARRAY];
int local_array_ptr = 0;

volatile int32_t alloc_requested_size = 0;
volatile bool alloc_request_pending = false;

static int cached_t_var_id = -1;
static int cached_i_sr     = -1;
static int cached_i_bpm    = -1;
static int cached_i_step   = -1;
static int cached_i_beat   = -1;
static int cached_i_bar    = -1;
static int cached_i_steps  = -1;
static int cached_i_beats  = -1;
static int cached_i_bars   = -1;
static int cached_i_sign   = -1;
static bool sys_indices_initialized = false;
static int cache_epoch_var_count = -1;

#if defined(ESP32)
DRAM_ATTR Val vm_stack[512]; 
DRAM_ATTR int32_t vm_call_stack[512]; 
DRAM_ATTR int32_t vm_call_args[512];
DRAM_ATTR Val vm_shadow_val[512];
#else
Val vm_stack[512]; 
int32_t vm_call_stack[512]; 
int32_t vm_call_args[512];
Val vm_shadow_val[512];
#endif

struct LoopState { 
    int type;       
    float acc;      
    float limit;    
    int f_pc;       
    float i;        
    int saved_sp;   
    Val iterable;   
    Val current_val; 
};
static LoopState loop_stack[32];
int loop_sp = -1;

inline float encode_vec(int32_t v) { 
    union { float f; uint32_t i; } u;
    u.i = 0x78000000 | (v & 0x07FFFFFF); 
    return u.f; 
}

inline bool is_vec_tag(float f) { 
    union { float f; uint32_t i; } u;
    u.f = f;
    return (u.i & 0xF8000000) == 0x78000000; 
}

inline int32_t decode_vec(float f) { 
    union { float f; uint32_t i; } u;
    u.f = f;
    return (int32_t)(u.i & 0x07FFFFFF); 
}

inline float encode_str(int32_t v) { 
    union { float f; uint32_t i; } u;
    u.i = 0x70000000 | (v & 0x07FFFFFF); 
    return u.f; 
}

inline bool is_str_tag(float f) { 
    union { float f; uint32_t i; } u;
    u.f = f;
    return (u.i & 0xF8000000) == 0x70000000; 
}

inline int32_t decode_str(float f) { 
    union { float f; uint32_t i; } u;
    u.f = f;
    return (int32_t)(u.i & 0x07FFFFFF); 
}

inline float sanitize(float f) {
    union { float f; uint32_t i; } u;
    u.f = f;
    if ((u.i & 0x7F800000) == 0x7F800000) return 0.0f;
    return f;
}

/**
 * Gets or creates an ID for a variable name.
 * @param name The name of the variable
 * @return The ID of the variable
 */
int getVarId(const String& name) {
    int len = name.length();
    for (int i = 0; i < var_count; i++) {
        if (symNames[i].length() == len && symNames[i].equals(name)) return i;
    }
    if (var_count >= 63) return 63; 
    symNames[var_count] = name;
    return var_count++;
}

/**
 * Ensures system variable indices are cached efficiently, auto-invalidating 
 * if external processes clear the global variable table.
 */
static inline void init_system_indices() {
    if (__builtin_expect(!sys_indices_initialized || var_count < cache_epoch_var_count, 0)) {
        cached_t_var_id = getVarId("t");
        cached_i_sr     = getVarId("sr");
        cached_i_bpm    = getVarId("bpm");
        cached_i_step   = getVarId("step");
        cached_i_beat   = getVarId("beat");
        cached_i_bar    = getVarId("bar");
        cached_i_steps  = getVarId("steps");
        cached_i_beats  = getVarId("beats");
        cached_i_bars   = getVarId("bars");
        cached_i_sign   = getVarId("sign");
        sys_indices_initialized = true;
    }
    cache_epoch_var_count = var_count;
}

/**
 * Gets the name of a variable by its ID.
 * @param id The ID of the variable
 * @return The name of the variable
 */
String getVarName(int id) {
    if (id >= 0 && id < var_count) return symNames[id];
    return "v" + String(id);
}

/**
 * Checks if a variable is defined.
 * @param name The name to check
 * @return true if defined, false otherwise
 */
bool isVarDefined(const String& name) {
    int len = name.length();
    for (int i = 0; i < var_count; i++) {
        if (symNames[i].length() == len && symNames[i].equals(name)) return true;
    }
    return false;
}

/**
 * Gets the precedence of a given OpCode.
 * @param op The OpCode
 * @return The precedence level
 */
int getPrecedence(OpCode op) {
    for (int i = 0; i < opListSize; i++) if (opList[i].code == op) return opList[i].precedence;
    if (op == OP_COND || op == OP_COLON) return 0;
    if (op == OP_ASSIGN_VAR || op == OP_STORE_AT || (op >= OP_ADD_ASSIGN && op <= OP_SHR_ASSIGN)) return -1;
    if (op == OP_STORE || op == OP_STORE_KEEP) return -2;
    if (op == OP_NONE || op == OP_DYN_CALL || op == OP_DYN_CALL_IF_FUNC) return -3;
    return 10; 
}

static float calculatePitch(float deg, float mask_f, float root_deg, float edo, float base_hz, float oct) {
    uint32_t mask = (uint32_t)mask_f;
    int max_edo = (int)edo; 
    if (max_edo > 32) max_edo = 32; else if (max_edo < 1) max_edo = 1;
    
    int scale_bits[32]; 
    int scale_len = 0;
    for (int i = 0; i < max_edo; i++) {
        if (mask & (1U << i)) scale_bits[scale_len++] = i;
    }
    if (scale_len == 0) { scale_bits[0] = 0; scale_len = 1; }
    
    float shifted_deg = deg + floorf(root_deg);
    float deg_floor = floorf(shifted_deg);
    float fraction = shifted_deg - deg_floor;
    
    int deg_i1 = (int)deg_floor;
    int octaves1 = (int)floorf((float)deg_i1 / scale_len);
    int local_deg1 = deg_i1 - (octaves1 * scale_len);
    if (local_deg1 < 0) { local_deg1 += scale_len; } 
    float semitone1 = (float)scale_bits[local_deg1] + (octaves1 * edo) + (oct * edo);
    
    int deg_i2 = deg_i1 + 1;
    int octaves2 = (int)floorf((float)deg_i2 / scale_len);
    int local_deg2 = deg_i2 - (octaves2 * scale_len);
    if (local_deg2 < 0) { local_deg2 += scale_len; }
    float semitone2 = (float)scale_bits[local_deg2] + (octaves2 * edo) + (oct * edo);
    
    float interpolated_semitone = semitone1 + (fraction * (semitone2 - semitone1));
    constexpr float BASELINE_MIDI_NOTE = 60.0f;
    return base_hz * powf(2.0f, (interpolated_semitone + (BASELINE_MIDI_NOTE - 69.0f)) / edo);
}

String getOpSym(OpCode op) {
    if (op == OP_COND) return "?"; 
    if (op == OP_BNOT) return "~";
    for (int i = 0; i < shorthandsSize; i++) if (shorthands[i].code == op) return shorthands[i].name; 
    for (int i = 0; i < mathLibrarySize; i++) if (mathLibrary[i].code == op) return mathLibrary[i].name; 
    for (int i = 0; i < opListSize; i++) if (opList[i].code == op) return opList[i].sym; 
    return "";
}

/**
 * Gets the OpCode for a given symbol.
 * @param sym The symbol string
 * @param outCode Reference to store the OpCode
 * @return true if found, false otherwise
 */
bool getOpCode(const String& sym, OpCode& outCode) {
    for (int i = 0; i < opListSize; i++) if (sym == opList[i].sym) { outCode = opList[i].code; return true; }
    return false;
}

/**
 * Dynamically computes vectorized matrix arithmetic and structural combinations.
 * @param lhs Left-hand operand stack parameter
 * @param rhs Right-hand operand stack parameter
 * @param op The mathematical operation identifier mapping to execute
 * @return An updated Val object containing the vectorized matrix product structure
 */
static Val eval_vector_bin_op(Val lhs, Val rhs, OpCode op) {
    bool lhs_is_vec = (lhs.type == 2);
    bool rhs_is_vec = (rhs.type == 2);
    
    int szA = lhs_is_vec ? (lhs.v & 0xFFFF) : 1;
    int szB = rhs_is_vec ? (rhs.v & 0xFFFF) : 1;
    int offA = lhs_is_vec ? (lhs.v >> 16) : 0;
    int offB = rhs_is_vec ? (rhs.v >> 16) : 0;
    
    int new_size = szA * szB;
    if (local_array_ptr + new_size > MAX_LOCAL_ARRAY) {
        Val err; err.type = 0; err.f = 0.0f;
        return err;
    }
    
    int new_offset = local_array_ptr;
    for (int j = 0; j < szB; j++) {
        float valB = rhs_is_vec ? local_array_mem[offB + j] : rhs.f;
        if (is_vec_tag(valB) || is_str_tag(valB)) valB = 0.0f; 
        
        for (int i = 0; i < szA; i++) {
            float valA = lhs_is_vec ? local_array_mem[offA + i] : lhs.f;
            if (is_vec_tag(valA) || is_str_tag(valA)) valA = 0.0f;
            
            float res = 0.0f;
            switch(op) {
                case OP_ADD: res = valA + valB; break;
                case OP_SUB: res = valA - valB; break;
                case OP_MUL: res = valA * valB; break;
                case OP_DIV: res = (valB != 0.0f) ? valA / valB : 0.0f; break;
                case OP_MOD: res = (valB != 0.0f) ? valA - (int32_t)(valA / valB) * valB : 0.0f; break;
                case OP_AND: res = (float)((int32_t)valA & (int32_t)valB); break;
                case OP_OR:  res = (float)((int32_t)valA | (int32_t)valB); break;
                case OP_XOR: res = (float)((int32_t)valA ^ (int32_t)valB); break;
                case OP_SHL: res = (float)((int32_t)valA << (int32_t)valB); break;
                case OP_SHR: res = (float)((int32_t)valA >> (int32_t)valB); break;
                case OP_LT:  res = (valA < valB) ? 1.0f : 0.0f; break;
                case OP_GT:  res = (valA > valB) ? 1.0f : 0.0f; break;
                case OP_EQ:  res = (valA == valB) ? 1.0f : 0.0f; break;
                case OP_NEQ: res = (valA != valB) ? 1.0f : 0.0f; break;
                case OP_LTE: res = (valA <= valB) ? 1.0f : 0.0f; break;
                case OP_GTE: res = (valA >= valB) ? 1.0f : 0.0f; break;
                case OP_POW: res = fast_pow(valA, valB); break;
                case OP_MIN: res = (valA < valB) ? valA : valB; break;
                case OP_MAX: res = (valA > valB) ? valA : valB; break;
                default: res = 0.0f; break;
            }
            local_array_mem[local_array_ptr++] = sanitize(res);
        }
    }
    
    Val ret;
    ret.type = 2;
    ret.v = (new_offset << 16) | (new_size & 0xFFFF);
    return ret;
}

/**
 * @struct MaskData
 * A unified container holding extraction data for macro operators.
 */
struct MaskData {
    uint32_t mask;
    int n_steps;
    float extra[2]; 
    int auto_len;
};


inline MaskData parse_rhythm_mask(int args, Val& tos, Val* st, int& sp, int sys_steps) {
    MaskData out = {0, sys_steps, {1.0f, 1.0f}, 0};
    Val base_mask = tos;
    
    if (args > 1) {
        int to_pop = args - 1;
        int base_idx = sp - to_pop + 1; 
        if (base_idx >= 0) base_mask = st[base_idx]; else base_mask = Val{0,0};
        
        if (args >= 2) out.n_steps = (int)sanitize((args == 2) ? tos.f : st[base_idx + 1].f);
        if (args >= 3) out.extra[0] = sanitize((args == 3) ? tos.f : st[base_idx + 2].f);
        if (args >= 4) out.extra[1] = sanitize((args >= 4) ? tos.f : st[base_idx + 3].f); 
        
        sp -= to_pop;
        if (sp < -1) sp = -1;
    }
    
    if (base_mask.type == 1) {
        out.mask = (uint32_t)base_mask.v;
        if (base_mask.len > 0) out.auto_len = base_mask.len;
    } else if (base_mask.type == 2) {
        int off = base_mask.v >> 16, sz = base_mask.v & 0xFFFF;
        if (sz >= 2 && off + 1 < MAX_LOCAL_ARRAY) {
            out.mask = (uint32_t)local_array_mem[off];
            out.auto_len = (int32_t)local_array_mem[off + 1];
        }
    } else {
        if (base_mask.f >= 0.0f && base_mask.f <= 4294967295.0f) out.mask = (uint32_t)base_mask.f;
        else { union { float f; uint32_t u; } cast_u; cast_u.f = base_mask.f; out.mask = cast_u.u; }
    }
    
    if (args < 2 && out.auto_len > 0) out.n_steps = out.auto_len;
    if (out.n_steps < 1) out.n_steps = 1;
    if (out.n_steps > 32) out.n_steps = 32;
    
    return out;
}


void IRAM_ATTR executeVmBlock(float start_t, float t_step, int length, uint32_t* out_buf) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    bool is_bb = (current_play_mode == MODE_BYTEBEAT);
    if (len == 0) { memset(out_buf, 128, length * sizeof(uint32_t)); return; }

    static const void* dispatch_table[] = {
        &&L_OP_VAL, &&L_OP_T, &&L_OP_LOAD, &&L_OP_STORE, &&L_OP_STORE_KEEP, &&L_OP_POP,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD, 
        &&L_OP_AND, &&L_OP_OR,  &&L_OP_XOR, &&L_OP_SHL, &&L_OP_SHR, 
        &&L_OP_LT,  &&L_OP_GT,  &&L_OP_EQ,  &&L_OP_NEQ, &&L_OP_LTE, &&L_OP_GTE,
        &&L_OP_COND, &&L_OP_NEG, &&L_OP_NOT, &&L_OP_BNOT,                
        &&L_OP_SIN, &&L_OP_COS, &&L_OP_TAN, &&L_OP_SQRT, &&L_OP_LOG, &&L_OP_EXP,
        &&L_OP_ABS, &&L_OP_FLOOR, &&L_OP_CEIL, &&L_OP_ROUND, &&L_OP_CBRT, &&L_OP_ASIN, &&L_OP_ACOS, &&L_OP_ATAN,
        &&L_OP_MIN, &&L_OP_MAX, &&L_OP_POW, 
        &&L_OP_JMP, &&L_OP_PUSH_FUNC, &&L_OP_DYN_CALL, &&L_OP_DYN_CALL_IF_FUNC, &&L_OP_RET,
        &&L_OP_BIND, &&L_OP_UNBIND, &&L_OP_ASSIGN_VAR, 
        &&L_OP_ALLOC, &&L_OP_VEC, &&L_OP_AT, &&L_OP_STORE_AT, 
        &&L_OP_SC_AND, &&L_OP_SC_OR, &&L_DEFAULT,
        &&L_OP_ADD_ASSIGN, &&L_OP_SUB_ASSIGN, &&L_OP_MUL_ASSIGN, &&L_OP_DIV_ASSIGN, &&L_OP_MOD_ASSIGN,
        &&L_OP_AND_ASSIGN, &&L_OP_OR_ASSIGN, &&L_OP_XOR_ASSIGN, &&L_OP_POW_ASSIGN, &&L_OP_SHL_ASSIGN, &&L_OP_SHR_ASSIGN,
        &&L_OP_RAND, &&L_OP_INT, &&L_OP_PHASE,
        &&L_OP_DUP, &&L_OP_SWAP, &&L_OP_ROT, &&L_OP_OVER,
        &&L_OP_LOOP_PREP, &&L_OP_LOOP_EVAL, &&L_OP_LOOP_DONE,
        &&L_OP_LOAD_STR,
        &&L_OP_ENV, &&L_OP_LFO,
        &&L_DEFAULT,
        &&L_OP_PC, &&L_OP_EUCLID, &&L_OP_ON,
        &&L_OP_DUR, &&L_OP_TO, &&L_OP_AT_MASK,
        &&L_OP_DEFAULT_CHECK, &&L_OP_DEFAULT_INJECT,
        &&L_OP_ARITY
    };

    Instruction* prog = program_bank[bank];
    init_system_indices();

    int t_var_id = cached_t_var_id;
    int i_sr     = cached_i_sr;
    int i_bpm    = cached_i_bpm;
    int i_step   = cached_i_step;
    int i_beat   = cached_i_beat;
    int i_bar    = cached_i_bar;
    int i_steps  = cached_i_steps; 
    int i_beats  = cached_i_beats;
    int i_bars   = cached_i_bars;
    int i_sign   = cached_i_sign;

    if (vars[i_steps].f == 0.0f) {
        vars[i_steps].type = 0;
        vars[i_steps].f = 16.0f; 
    }

    for (int sample_idx = 0; sample_idx < length; sample_idx++) {
        float t = start_t + (sample_idx * t_step); 

        vars[i_sr].type = 0; 
        vars[i_sr].f = (float)current_sample_rate;

        float current_bpm = vars[i_bpm].f;
        if (current_bpm <= 0.01f) current_bpm = 0.01f;

        if (t < anchor_t) {
            anchor_t = (int32_t)t;
            anchor_beat = 0.0f;
        }

        if (current_bpm != anchor_bpm || current_sample_rate != anchor_sample_rate) {
            anchor_beat = anchor_beat + (t - anchor_t) * (anchor_bpm / (60.0f * anchor_sample_rate));
            anchor_t = (int32_t)t;
            anchor_bpm = current_bpm;
            anchor_sample_rate = current_sample_rate;
        }

        float current_beat = anchor_beat + (t - anchor_t) * (vars[i_bpm].f / (60.0f * current_sample_rate));
        
        float global_beats = 4.0f;
        if (vars[i_sign].f > 0.0f) global_beats = vars[i_sign].f * 4.0f;
        else if (vars[i_beats].f > 0.0f) global_beats = vars[i_beats].f;
        
        float steps_per_bar = vars[i_steps].f;
        if (steps_per_bar <= 0.01f) steps_per_bar = 16.0f; 

        float current_bar = current_beat / global_beats;

        float local_beat = current_beat;
        float local_bar  = current_bar;
        
        float global_bars = vars[i_bars].f;
        if (global_bars > 0.0f) {
            float total_loop_beats = global_bars * global_beats;
            local_beat = fmodf(current_beat, total_loop_beats);
            if (local_beat < 0.0f) local_beat += total_loop_beats;
            local_bar = local_beat / global_beats;
        }

        float steps_per_beat = steps_per_bar / global_beats;
        if (steps_per_beat <= 0.0f) steps_per_beat = 4.0f;

        vars[i_beat].type = 0; vars[i_beat].f = local_beat;
        vars[i_bar].type  = 0; vars[i_bar].f  = local_bar;
        
        vars[i_step].type = 0; vars[i_step].f = local_beat * steps_per_beat;
        
        vars[t_var_id].type = 0; 
        vars[t_var_id].f = t;

        local_array_ptr = 0; int sp = -1; int csp = -1; int ssp = -1; loop_sp = -1; 
        int dynamic_arity = -1; 
        Val tos = {0, 0}; 

        int pc = 0; Instruction inst = prog[pc]; goto *dispatch_table[(uint8_t)inst.op];
        #define BOING() do { if (++pc >= len) goto L_END; inst = prog[pc]; goto *dispatch_table[(uint8_t)inst.op]; } while(0)

        L_OP_VAL: if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.v = inst.val; BOING();
        L_OP_T:   if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.f = t; BOING();
        L_OP_LOAD: if (sp < 511) vm_stack[++sp] = tos; tos = vars[inst.val]; BOING();
        L_OP_LOAD_STR: if (sp < 511) vm_stack[++sp] = tos; tos.type = 4; tos.v = inst.val; BOING(); 

        L_OP_STORE: {
            vars[inst.val] = tos; 
            if (sp >= 0) { tos = vm_stack[sp--]; }
            BOING();
        }

        L_OP_POP: { 
            if (sp >= 0) { tos = vm_stack[sp--]; } 
            BOING(); 
        }
        L_OP_STORE_KEEP: vars[inst.val] = tos; BOING();
        L_OP_JMP: pc += inst.val - 1; BOING(); 
        L_OP_PUSH_FUNC: if (sp < 511) vm_stack[++sp] = tos; tos.type = 1; tos.v = pc + 1; pc += inst.val - 1; BOING(); 
        L_OP_ASSIGN_VAR: { BOING(); }
        
        L_OP_DYN_CALL:
        L_OP_DYN_CALL_IF_FUNC:
            if (tos.type == 1) { 
                int provided = (dynamic_arity != -1) ? dynamic_arity : ((inst.op == OP_DYN_CALL) ? inst.val : 0); 
                dynamic_arity = -1;
                
                int expected = 0;
                for (int temp_pc = tos.v; temp_pc < len; temp_pc++) {
                    if (prog[temp_pc].op == OP_BIND) expected++;
                    else if (prog[temp_pc].op != OP_DEFAULT_CHECK && prog[temp_pc].op != OP_DEFAULT_INJECT) break;
                }
                
                if (provided > expected) {
                    int excess = provided - expected;
                    sp -= excess;
                    if (sp < -1) sp = -1;
                    provided = expected; 
                }

                if (csp < 511) { 
                    vm_call_stack[++csp] = pc; 
                    vm_call_args[csp] = provided; 
                    pc = tos.v - 1; 
                } 
                tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; 
            }
            BOING();
            
        L_OP_RET: if (csp >= 0) pc = vm_call_stack[csp--]; else pc = len; BOING();
        L_OP_BIND: if (ssp < 511) vm_shadow_val[++ssp] = vars[inst.val]; vars[inst.val] = tos; tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; BOING();
        L_OP_UNBIND: if (ssp >= 0) vars[inst.val] = vm_shadow_val[ssp--]; BOING();
        
        L_OP_VEC: {
            int32_t size = (int32_t)tos.f;
            if (size >= 1) {
                int offset = local_array_ptr; int start_idx = sp - size + 1;
                for (int i = 0; i < size; i++) {
                    if (local_array_ptr < MAX_LOCAL_ARRAY) {
                        Val& item = vm_stack[start_idx + i];
                        if (item.type == 2) local_array_mem[local_array_ptr++] = encode_vec(item.v);
                        else if (item.type == 4) local_array_mem[local_array_ptr++] = encode_str(item.v);
                        else local_array_mem[local_array_ptr++] = item.f;
                    }
                }
                sp -= size; tos.type = 2; tos.v = (offset << 16) | (size & 0xFFFF); 
            } BOING();
        }
        
        L_OP_ALLOC: {
            int32_t size = (int32_t)tos.f; 
            if (size > 0) {
            #if !defined(ESP32)
                ensureGlobalArray(size);
            #else
                if (__builtin_expect(size > global_array_capacity, 0)) {
                    if (!alloc_request_pending) {
                        alloc_requested_size = size;
                        alloc_request_pending = true; 
                    }
                }
            #endif
            }
            tos.type = 3; 
            tos.f = (float)(global_array_capacity > 0 ? global_array_capacity : size); 
            BOING();
        }
        L_OP_AT: {
            int32_t idx = (int32_t)tos.f;
            Val base = (sp >= 0) ? vm_stack[sp--] : Val{0, 0};
            if (base.type == 2) { 
                int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
                if (sz > 0) { if (idx < 0 || idx >= sz) idx = ((idx % sz) + sz) % sz; }
                float raw = (off + idx < MAX_LOCAL_ARRAY) ? local_array_mem[off + idx] : 0.0f;
                if (is_vec_tag(raw)) { tos.type = 2; tos.v = decode_vec(raw); }
                else if (is_str_tag(raw)) { tos.type = 4; tos.v = decode_str(raw); }
                else { tos.type = 0; tos.f = sanitize(raw); }
            } else if (base.type == 3 && global_array_capacity > 0) { 
                if (idx < 0 || idx >= global_array_capacity) idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity; 
                float raw = global_array_mem[idx];
                if (is_vec_tag(raw)) { tos.type = 2; tos.v = decode_vec(raw); }
                else if (is_str_tag(raw)) { tos.type = 4; tos.v = decode_str(raw); }
                else { tos.type = 0; tos.f = sanitize(raw); }
            } else if (base.type == 4) { 
                if (base.v >= 0 && base.v < string_table_count) {
                    String str = string_table[base.v];
                    int sz = str.length();
                    if (sz > 0) {
                        if (idx < 0 || idx >= sz) idx = ((idx % sz) + sz) % sz;
                        char c = str[idx];
                        float val = 0.0f;
                        if (c >= '0' && c <= '9') val = c - '0';
                        else if (c >= 'a' && c <= 'z') val = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'Z') val = c - 'A' + 36;
                        tos.type = 0; tos.f = val;
                    } else { tos = {0, 0}; }
                } else { tos = {0, 0}; }
            } else { tos = {0, 0}; } 
            BOING();
        }
        L_OP_STORE_AT: {
            Val val_to_s = tos; 
            int32_t idx = (sp >= 0) ? (int32_t)vm_stack[sp--].f : 0; 
            Val base = (sp >= 0) ? vm_stack[sp--] : Val{0,0};
            if (base.type == 2) { 
                int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
                if (sz > 0) { if (idx < 0 || idx >= sz) idx = ((idx % sz) + sz) % sz; }
                if (off + idx < MAX_LOCAL_ARRAY) {
                    if (val_to_s.type == 2) local_array_mem[off + idx] = encode_vec(val_to_s.v);
                    else if (val_to_s.type == 4) local_array_mem[off + idx] = encode_str(val_to_s.v);
                    else local_array_mem[off + idx] = sanitize(val_to_s.f);
                }
            } else if (base.type == 3 && global_array_capacity > 0) { 
                if (idx < 0 || idx >= global_array_capacity) idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity;
                if (val_to_s.type == 2) global_array_mem[idx] = encode_vec(val_to_s.v);
                else if (val_to_s.type == 4) global_array_mem[idx] = encode_str(val_to_s.v);
                else global_array_mem[idx] = sanitize(val_to_s.f); 
            }
            tos = val_to_s; 
            BOING();
        }
        L_OP_SC_AND: { if (inst.val != 0) { if (tos.f == 0.0f) pc += inst.val - 1; else tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; } else { if (sp >= 0 && vm_stack[sp].f == 0.0f) tos = vm_stack[sp--]; else sp--; } BOING(); }
        L_OP_SC_OR: { if (inst.val != 0) { if (tos.f != 0.0f) pc += inst.val - 1; else tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; } else { if (sp >= 0 && vm_stack[sp].f != 0.0f) tos = vm_stack[sp--]; else sp--; } BOING(); }
        L_OP_NEG: tos.f = -tos.f; BOING();
        L_OP_NOT: tos.f = (tos.f == 0.0f) ? 1.0f : 0.0f; BOING();
        L_OP_BNOT: tos.f = (float)(~(int32_t)tos.f); BOING();
        
        L_OP_SIN: { tos.f = is_bb ? 128.0f + fast_sin(tos.f * 0.02454369f) * 127.0f : fast_sin(tos.f); BOING(); }
        L_OP_COS: { tos.f = is_bb ? 128.0f + fast_sin(tos.f * 0.02454369f + 1.570796f) * 127.0f : fast_sin(tos.f + 1.570796f); BOING(); }
        L_OP_TAN: { tos.f = is_bb ? 128.0f + sanitize(tanf(tos.f * 0.02454369f)) * 127.0f : sanitize(tanf(tos.f)); BOING(); }
        
        L_OP_SQRT:  tos.f = sanitize(tos.f >= 0.0f ? sqrtf(tos.f) : 0.0f); BOING();
        L_OP_LOG:   tos.f = sanitize(tos.f > 0.0f ? logf(tos.f) : 0.0f); BOING();
        L_OP_EXP:   tos.f = sanitize(expf(tos.f)); BOING();
        L_OP_ABS:   tos.f = fabsf(tos.f); BOING();
        L_OP_FLOOR: tos.f = floorf(tos.f); BOING();
        L_OP_CEIL:  tos.f = ceilf(tos.f); BOING();
        L_OP_ROUND: tos.f = roundf(tos.f); BOING();
        
        L_OP_CBRT: { 
            tos.f = sanitize(fast_cbrt(tos.f)); 
            BOING(); 
        }
        L_OP_ASIN:  tos.f = asinf(tos.f); BOING();
        L_OP_ACOS:  tos.f = acosf(tos.f); BOING();
        
        L_OP_ATAN: { 
            tos.f = sanitize(fast_atan(tos.f)); 
            BOING(); 
        }
        L_OP_COND: { Val f = tos, tv = (sp >= 0) ? vm_stack[sp--] : Val{0,0}, c = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; Val tgt = (c.f != 0.0f) ? tv : f; if (tgt.type == 1) { if (csp < 511) { vm_call_stack[++csp] = pc; pc = tgt.v - 1; } tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; } else { tos = tgt; } BOING(); }
        
        L_OP_ADD: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_ADD); else tos.f = lhs.f + tos.f; BOING(); }
        L_OP_SUB: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_SUB); else tos.f = lhs.f - tos.f; BOING(); }
        L_OP_MUL: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_MUL); else tos.f = lhs.f * tos.f; BOING(); }
        L_OP_DIV: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_DIV); else tos.f = (tos.f != 0.0f ? sanitize(lhs.f / tos.f) : 0.0f); BOING(); }
        L_OP_MOD: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_MOD); else tos.f = (tos.f != 0.0f ? sanitize(lhs.f - (int32_t)(lhs.f / tos.f) * tos.f) : 0.0f); BOING(); }
        
        L_OP_AND: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_AND); else tos.f = (float)((int32_t)lhs.f & (int32_t)tos.f); BOING(); }
        L_OP_OR:  { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_OR);  else tos.f = (float)((int32_t)lhs.f | (int32_t)tos.f); BOING(); }
        L_OP_XOR: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_XOR); else tos.f = (float)((int32_t)lhs.f ^ (int32_t)tos.f); BOING(); }
        L_OP_SHL: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_SHL); else tos.f = (float)((int32_t)lhs.f << (int32_t)tos.f); BOING(); }
        L_OP_SHR: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_SHR); else tos.f = (float)((int32_t)lhs.f >> (int32_t)tos.f); BOING(); }
        
        L_OP_LT:  { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_LT);  else tos.f = (lhs.f < tos.f) ? 1.0f : 0.0f; BOING(); }
        L_OP_GT:  { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_GT);  else tos.f = (lhs.f > tos.f) ? 1.0f : 0.0f; BOING(); }
        L_OP_EQ:  { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_EQ);  else tos.f = (lhs.f == tos.f) ? 1.0f : 0.0f; BOING(); }
        L_OP_NEQ: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_NEQ); else tos.f = (lhs.f != tos.f) ? 1.0f : 0.0f; BOING(); }
        L_OP_LTE: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_LTE); else tos.f = (lhs.f <= tos.f) ? 1.0f : 0.0f; BOING(); }
        L_OP_GTE: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_GTE); else tos.f = (lhs.f >= tos.f) ? 1.0f : 0.0f; BOING(); }
        
        L_OP_MIN: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_MIN); else tos.f = (lhs.f < tos.f ? lhs.f : tos.f); BOING(); }
        L_OP_MAX: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_MAX); else tos.f = (lhs.f > tos.f ? lhs.f : tos.f); BOING(); }
        L_OP_POW: { 
            Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); 
            if (lhs.type == 2 || tos.type == 2) {
                tos = eval_vector_bin_op(lhs, tos, OP_POW); 
            } else {
                if (lhs.f == 2.0f) {
#if defined(ESP32)
                    tos.f = sanitize(exp2f(tos.f));
#else
                    tos.f = powf(2.0f, tos.f);
#endif
                } else if (lhs.f <= 0.0f) {
                    tos.f = 0.0f;
                } else {
                    tos.f = fast_pow(lhs.f, tos.f);
                }
                tos.type = 0;
            } 
            BOING(); 
        }

        L_OP_ADD_ASSIGN: { float r = vars[inst.val].f + tos.f; tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_SUB_ASSIGN: { float r = vars[inst.val].f - tos.f; tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_MUL_ASSIGN: { float r = vars[inst.val].f * tos.f; tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_DIV_ASSIGN: { float n = vars[inst.val].f, d = tos.f; float r = (d != 0.0f ? sanitize(n / d) : 0.0f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_MOD_ASSIGN: { float n = vars[inst.val].f, d = tos.f; float r = (d != 0.0f ? sanitize(n - (int32_t)(n / d) * d) : 0.0f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_AND_ASSIGN: { float r = (float)((int32_t)vars[inst.val].f & (int32_t)tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_OR_ASSIGN:  { float r = (float)((int32_t)vars[inst.val].f | (int32_t)tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_XOR_ASSIGN: { float r = (float)((int32_t)vars[inst.val].f ^ (int32_t)tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_SHL_ASSIGN: { float r = (float)((int32_t)vars[inst.val].f << (int32_t)tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_SHR_ASSIGN: { float r = (float)((int32_t)vars[inst.val].f >> (int32_t)tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        L_OP_POW_ASSIGN: { float r = fast_pow(vars[inst.val].f, tos.f); tos.f = r; vars[inst.val] = tos; BOING(); }
        
        L_OP_RAND: {
            static uint32_t x_rng = 123456789; x_rng ^= x_rng << 13; x_rng ^= x_rng >> 17; x_rng ^= x_rng << 5;
            if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.f = (float)(x_rng & 0xFFFFFF) / 16777216.0f; BOING();
        }

        L_OP_INT: { tos.f = (float)((int32_t)tos.f); BOING(); }
        
        L_OP_ARITY: {
            dynamic_arity = (int)tos.f;
            tos = (sp >= 0) ? vm_stack[sp--] : Val{0, 0};
            BOING();
        }

        L_OP_PHASE: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 1;
            dynamic_arity = -1;
            if (args == 0) { 
                if (sp < 511) vm_stack[++sp] = tos;
                tos.type = 0; tos.f = 0.0f; 
                BOING(); 
            }

            float absolute_beat = vars[i_beat].f;
            float global_steps = vars[i_steps].f > 0.0f ? vars[i_steps].f : 16.0f;
            float global_beats = 4.0f;
            if (vars[i_sign].f > 0.0f) global_beats = vars[i_sign].f * 4.0f;
            else if (vars[i_beats].f > 0.0f) global_beats = vars[i_beats].f;
            float steps_per_beat = global_steps / global_beats;
            if (steps_per_beat <= 0.0f) steps_per_beat = 4.0f;

            float total_dur = 0.0f;
            int sz = 0; bool is_global = false; int off = 0;
            bool is_array_arg = (args == 1 && (tos.type == 2 || tos.type == 3));
            float temp_args[32];

            if (is_array_arg) {
                if (tos.type == 2) { off = tos.v >> 16; sz = tos.v & 0xFFFF; }
                else if (tos.type == 3) { sz = global_array_capacity; is_global = true; }

                for (int i = 0; i < sz; i++) {
                    float val = is_global ? global_array_mem[i] : (off + i < MAX_LOCAL_ARRAY ? local_array_mem[off + i] : 0.0f);
                    temp_args[i] = sanitize(val) / steps_per_beat;
                    total_dur += temp_args[i];
                }
            } else {
                sz = args > 32 ? 32 : args;
                temp_args[sz - 1] = sanitize(tos.f) / steps_per_beat;
                for (int i = sz - 2; i >= 0; i--) temp_args[i] = sanitize((sp >= 0) ? vm_stack[sp--].f : 0.0f) / steps_per_beat;
                for (int i = 0; i < sz; i++) total_dur += temp_args[i];
                int extra_args = args - 1 - (sz - 1);
                if (extra_args > 0) { sp -= extra_args; if (sp < -1) sp = -1; }
            }

            if (total_dur > 0.0f && sz > 0) {
                float loop_count = floorf(absolute_beat / total_dur);
                float local_beat = absolute_beat - loop_count * total_dur;
                float accum = 0.0f; int current_step = sz - 1;
                float step_dur = temp_args[sz - 1]; float step_start = total_dur - step_dur; 
                
                for (int i = 0; i < sz; i++) {
                    float v = temp_args[i];
                    if (local_beat >= accum && local_beat < accum + v) {
                        current_step = i; step_dur = v; step_start = accum; break;
                    }
                    accum += v;
                }
                
                if (step_dur <= 0.0f) step_dur = 0.001f;
                float frac = (local_beat - step_start) / step_dur;
                
                tos.type = 0; 
                tos.f = (loop_count * sz) + current_step + frac;
            } else {
                tos.type = 0; tos.f = 0.0f;
            }
            BOING();
        }

        L_OP_DUP: { 
            if (sp < 511) {
                sp++;
                vm_stack[sp] = tos; 
            }
            BOING(); 
        }

        L_OP_SWAP: { 
            if (sp >= 0) {
                Val tmp = tos;
                tos = vm_stack[sp];
                vm_stack[sp] = tmp; 
            }
            BOING(); 
        }

        L_OP_ROT: {
            if (sp >= 1) { 
                Val a = vm_stack[sp - 1]; 
                Val b = vm_stack[sp];     
                Val c = tos;           
                
                vm_stack[sp - 1] = b;     
                vm_stack[sp] = c;         
                tos = a;               
            }
            BOING();
        }

        L_OP_OVER: {
            if (sp >= 0 && sp < 511) {
                Val a = vm_stack[sp];
                sp++;
                vm_stack[sp] = tos;
                tos = a;
            }
            BOING();
        }

        L_OP_LOOP_PREP: {
            int ltype = inst.val; Val func = tos; Val iter = (sp >= 0) ? vm_stack[sp--] : Val{0,0};
            float limit = 0.0f; Val iterable = {0, 0};
            if (iter.type == 2) { iterable = iter; limit = (float)(iter.v & 0xFFFF); } 
            else if (iter.type == 3) { iterable = iter; limit = iter.f; } 
            else if (iter.type == 4) { iterable = iter; limit = (float)string_table[iter.v].length(); } 
            else { limit = iter.f; }
            
            Val empty_val; 
            empty_val.type = 0; 
            empty_val.f = 0.0f;
            if (loop_sp < 31) { loop_stack[++loop_sp] = {ltype, 0.0f, limit, (int)func.v, 0.0f, sp, iterable, empty_val}; }
            tos.type = 0; tos.f = 0.0f; BOING();
        }
        
        L_OP_LOOP_EVAL: {
            if (loop_sp >= 0) {
                LoopState& s = loop_stack[loop_sp];
                if (s.i >= s.limit) {
                    if (s.type == 0 || s.type == 3) { tos.type = 0; tos.f = s.acc; sp = s.saved_sp; loop_sp--; pc += inst.val - 1; BOING(); } 
                    else { tos.type = 0; tos.f = (s.type == 4) ? s.acc : s.limit; loop_sp--; pc += inst.val - 1; goto L_OP_VEC; }
                } else {
                    s.saved_sp = sp; if (csp < 511) { vm_call_stack[++csp] = pc; pc = s.f_pc - 1; }
                    
                    Val item_val;
                    item_val.type = 0;
                    item_val.f = s.i;
                    
                    if (s.iterable.type != 0) { 
                        int32_t idx = (int32_t)s.i;
                        if (s.iterable.type == 2) {
                            int off = s.iterable.v >> 16, sz = s.iterable.v & 0xFFFF;
                            if (idx >= 0 && idx < sz && off + idx < MAX_LOCAL_ARRAY) {
                                float raw = local_array_mem[off + idx];
                                if (is_vec_tag(raw)) { item_val.type = 2; item_val.v = decode_vec(raw); }
                                else if (is_str_tag(raw)) { item_val.type = 4; item_val.v = decode_str(raw); }
                                else { item_val.type = 0; item_val.f = sanitize(raw); }
                            }
                        } else if (s.iterable.type == 3 && global_array_capacity > 0) {
                            if (idx >= 0 && idx < global_array_capacity) {
                                float raw = global_array_mem[idx];
                                if (is_vec_tag(raw)) { item_val.type = 2; item_val.v = decode_vec(raw); }
                                else if (is_str_tag(raw)) { item_val.type = 4; item_val.v = decode_str(raw); }
                                else { item_val.type = 0; item_val.f = sanitize(raw); }
                            }
                        } else if (s.iterable.type == 4) { 
                            String str = string_table[s.iterable.v];
                            int sz = str.length();
                            if (idx >= 0 && idx < sz) {
                                char c = str[idx];
                                float val = 0.0f;
                                if (c >= '0' && c <= '9') val = c - '0';
                                else if (c >= 'a' && c <= 'z') val = c - 'a' + 10;
                                else if (c >= 'A' && c <= 'Z') val = c - 'A' + 36;
                                item_val.type = 0; item_val.f = val;
                            }
                        }
                    }
                    s.current_val = item_val; 
                    if (sp < 511) { vm_stack[++sp].type = 0; vm_stack[sp].f = (s.type == 3) ? s.acc : 0.0f; } 
                    tos = item_val; 
                }
            } else { tos.type = 0; tos.f = 0.0f; } BOING();
        }
        
        L_OP_LOOP_DONE: {
            if (loop_sp >= 0) {
                LoopState& s = loop_stack[loop_sp];
                if (s.type == 0) { s.acc += tos.f; sp = s.saved_sp; } 
                else if (s.type == 3) { s.acc = tos.f; sp = s.saved_sp; } 
                else if (s.type == 4) { 
                    sp = s.saved_sp; 
                    if (tos.f != 0.0f) { if (sp < 511) { vm_stack[++sp] = s.current_val; } s.acc += 1.0f; }
                }
                else { sp = s.saved_sp; if (sp < 511) vm_stack[++sp] = tos; } 
                s.i += 1.0f; pc -= inst.val - 1;     
            }
            tos.type = 0; tos.f = 0.0f; BOING();
        }

      L_OP_ENV: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 3;
            dynamic_arity = -1;
            
            float p       = 0.0f; 
            float attack  = 10.0f; 
            float d_power = 4.0f;  
            float sustain = 0.5f;  
            float release = 4.0f;  
            
            if (args >= 5) {
                release = sanitize(tos.f);
                sustain = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.5f);
                d_power = sanitize(sp >= 0 ? vm_stack[sp--].f : 4.0f);
                attack  = sanitize(sp >= 0 ? vm_stack[sp--].f : 10.0f);
                p       = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
                int extra = args - 5;
                if (extra > 0) { sp -= extra; if (sp < -1) sp = -1; }
            } else if (args == 4) {
                sustain = sanitize(tos.f);
                d_power = sanitize(sp >= 0 ? vm_stack[sp--].f : 4.0f);
                attack  = sanitize(sp >= 0 ? vm_stack[sp--].f : 10.0f);
                p       = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
            } else if (args == 3) {
                d_power = sanitize(tos.f);
                attack  = sanitize(sp >= 0 ? vm_stack[sp--].f : 10.0f);
                p       = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
            } else if (args == 2) {
                attack  = sanitize(tos.f);
                p       = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
            } else if (args == 1) {
                p = sanitize(tos.f);
            }

            float p_mod = p - floorf(p); 
            if (p_mod < 0.0f) p_mod += 1.0f; 
            
            float out_val = 0.0f;

            if (p_mod < 0.5f) {
                float p_local = p_mod * 2.0f;
                float attack_ramp = p_local * attack; 
                if (attack_ramp > 1.0f) attack_ramp = 1.0f;
                float decay_ramp = fast_pow(1.0f - p_local, d_power);
                out_val = attack_ramp * (sustain + (1.0f - sustain) * decay_ramp);
            } else {
                float p_local = (p_mod - 0.5f) * 2.0f;
                out_val = sustain * fast_pow(1.0f - p_local, release);
            }
            
            if (out_val < 0.0f) out_val = 0.0f;
            if (out_val > 1.0f) out_val = 1.0f;

            tos.type = 0; 
            if (is_bb) {
                tos.f = out_val * 256.0f;
            } else {
                tos.f = out_val;
            }
            
            BOING();
        }

        L_OP_LFO: {
            static float phase_mem[8] = {0.0f};
            static bool initialized[8] = {false};
            
            static float track_t = -1.0f;
            static int auto_id = 0;
            
            int args = (dynamic_arity != -1) ? dynamic_arity : 2;
            dynamic_arity = -1;
            
            if (args == 0) {
                if (sp < 511) vm_stack[++sp] = tos;
                tos.type = 0; tos.f = 0.0f;
                BOING();
            }

            float input = 0.0f;
            float type = 0.0f;       
            int voice_id = 0;        

            if (t != track_t) {
                track_t = t;
                auto_id = 0;
            }

            if (args >= 3) {
                voice_id = (int)sanitize(tos.f);
                type = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
                input = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
                int extra = args - 3;
                if (extra > 0) { sp -= extra; if (sp < -1) sp = -1; }
            } else if (args == 2) {
                type = sanitize(tos.f);
                input = sanitize(sp >= 0 ? vm_stack[sp--].f : 0.0f);
                voice_id = auto_id++;
            } else if (args == 1) {
                input = sanitize(tos.f);
                voice_id = auto_id++;
            }

            voice_id = (voice_id % 8 + 8) % 8;
            
            float dt = 0.0f;

            if (input > 1.0f || input < -1.0f) {
                if (!initialized[voice_id]) {
                    initialized[voice_id] = true;
                }
                
                dt = fabsf(input) / (float)current_sample_rate;
                if (dt > 0.5f) dt = 0.5f; 
                
                if (input < 0.0f) {
                    phase_mem[voice_id] -= dt;
                } else {
                    phase_mem[voice_id] += dt;
                }
                
                phase_mem[voice_id] -= floorf(phase_mem[voice_id]);
                if (phase_mem[voice_id] < 0.0f) phase_mem[voice_id] += 1.0f;
            } else {
                float wrapped = input - floorf(input);
                if (wrapped < 0.0f) wrapped += 1.0f;
                phase_mem[voice_id] = wrapped;
                initialized[voice_id] = true;
            }

            float p = phase_mem[voice_id];
            float out = 0.0f; 
            int t_int = (int)type;

            if (t_int == 0) {
                out = fast_sin(p * 6.2831853f);
            } 
            else if (t_int == 1) {
                out = 1.0f - (p * 2.0f); 
                
                if (dt > 0.0f) {
                    if (p < dt) {
                        float t_pb = p / dt;
                        out -= (1.0f - t_pb) * (1.0f - t_pb);
                    } else if (p > 1.0f - dt) {
                        float t_pb = (1.0f - p) / dt;
                        out += (1.0f - t_pb) * (1.0f - t_pb);
                    }
                }
            } 
            else if (t_int == 2) {
                out = (p < 0.5f) ? 1.0f : -1.0f;
                
                if (dt > 0.0f) {
                    if (p < dt) {
                        float t_pb = p / dt;
                        out -= (1.0f - t_pb) * (1.0f - t_pb);
                    } else if (p > 1.0f - dt) {
                        float t_pb = (1.0f - p) / dt;
                        out += (1.0f - t_pb) * (1.0f - t_pb);
                    }
                    
                    float p2 = p + 0.5f;
                    if (p2 >= 1.0f) p2 -= 1.0f;
                    
                    if (p2 < dt) {
                        float t_pb = p2 / dt;
                        out += (1.0f - t_pb) * (1.0f - t_pb);
                    } else if (p2 > 1.0f - dt) {
                        float t_pb = (1.0f - p2) / dt;
                        out -= (1.0f - t_pb) * (1.0f - t_pb);
                    }
                }
            } 
            else if (t_int == 3) {
                out = fast_sin(p * 6.2831853f + 1.570796f);
            }

            tos.type = 0; 
            if (is_bb) {
                tos.f = (out + 1.0f) * 127.5f;
            } else {
                tos.f = out;
            }
            
            BOING();
        }

        L_OP_PC: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 6;
            dynamic_arity = -1;
            
            float p[6] = {0.0f, 2741.0f, 0.0f, 12.0f, 440.0f, 0.0f}; 
            
            int to_pop = (args > 6) ? 6 : args;
            for (int i = to_pop - 1; i >= 0; i--) {
                if (i == to_pop - 1) p[i] = sanitize(tos.f);
                else if (sp >= 0) p[i] = sanitize(vm_stack[sp--].f);
            }
            if (args > 6) sp -= (args - 6); 
            
            tos.type = 0;
            tos.f = calculatePitch(p[0], p[1], p[2], p[3], p[4], p[5]);
            BOING();
        }

        L_OP_EUCLID: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 2;
            dynamic_arity = -1;
            
            float dynamic_steps = vars[i_steps].f > 0.01f ? vars[i_steps].f : 16.0f;
            float p[3] = {0.0f, dynamic_steps, 0.0f}; 
            
            int to_pop = (args > 3) ? 3 : args;
            for (int i = to_pop - 1; i >= 0; i--) {
                if (i == to_pop - 1) {
                    p[i] = (tos.type == 1) ? (float)tos.v : sanitize(tos.f);
                } else if (sp >= 0) {
                    Val v = vm_stack[sp--];
                    p[i] = (v.type == 1) ? (float)v.v : sanitize(v.f);
                }
            }
            if (args > 3) {
                sp -= (args - 3);
                if (sp < -1) sp = -1;
            }
            
            float raw_k = p[0];
            float raw_n = p[1];
            bool invert_pattern   = (raw_k < 0.0f);
            bool reverse_pattern  = (raw_n < 0.0f);

            int k = (int)fabsf(raw_k);
            int n = (int)fabsf(raw_n);
            int r = (int)p[2];

            if (n < 1 || n > 32) n = 8;
            if (k < 0) k = 0; 
            if (k > n) k = n;

            uint32_t mask = 0;
            if (k >= n) {
                mask = (n == 32) ? 0xFFFFFFFF : ((1U << n) - 1);
            } else if (k > 0) {
                for (int i = 0; i < n; i++) {
                    if ((i * k) % n < k) {
                        int logical_idx = reverse_pattern ? (n - 1 - i) : i;
                        
                        int pos = (logical_idx + r) % n;
                        if (pos < 0) pos += n;
                        
                        mask |= (1U << (n - 1 - pos)); 
                    }
                }
            }
            
            if (invert_pattern) {
                uint32_t window_mask = (n == 32) ? 0xFFFFFFFF : ((1U << n) - 1);
                mask = (~mask) & window_mask;
            }
            
            tos.type = 1; 
            tos.v = (int32_t)mask; 
            tos.len = n;

            BOING();
        }

        L_OP_ON: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 2;
            dynamic_arity = -1;
            
            bool manual_override = false;
            int manual_step = 0;
            
            if (args >= 3) {
                manual_step = (int)sanitize(tos.f);
                manual_override = true;
                tos = (sp >= 0) ? vm_stack[sp--] : Val{0, 0};
                args = 2; 
            }
            
            MaskData md = parse_rhythm_mask(args, tos, vm_stack, sp, (int)(vars[i_steps].f > 0 ? vars[i_steps].f : 16));
            
            int target_slot = 0;
            if (manual_override) {
                target_slot = manual_step % md.n_steps;
            } else {
                float absolute_beat = vars[i_beat].f;
                float global_steps = vars[i_steps].f > 0.0f ? vars[i_steps].f : 16.0f;
                float global_beats = 4.0f;
                if (vars[i_sign].f > 0.0f) global_beats = vars[i_sign].f * 4.0f;
                else if (vars[i_beats].f > 0.0f) global_beats = vars[i_beats].f;

                float steps_per_beat = global_steps / global_beats;
                if (steps_per_beat <= 0.0f) steps_per_beat = 4.0f;

                float continuous_step = absolute_beat * steps_per_beat;
                target_slot = ((int)floorf(continuous_step)) % md.n_steps;
            }
            
            if (target_slot < 0) target_slot += md.n_steps;
            
            int bit_idx = md.n_steps - 1 - target_slot;
            
            tos.type = 0; 
            tos.f = (md.mask & (1U << bit_idx)) ? 1.0f : 0.0f; 
            tos.len = 0;
            BOING();
        }

   L_OP_DUR: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 4;
            dynamic_arity = -1;
            
            MaskData md = parse_rhythm_mask(args, tos, vm_stack, sp, (int)(vars[i_steps].f > 0 ? vars[i_steps].f : 16));

            int hit_positions[32]; int hit_count = 0;
            for (int i = 0; i < md.n_steps; i++) {
                int bit_pos = md.n_steps - 1 - i;
                if (md.mask & (1U << bit_pos)) hit_positions[hit_count++] = i;
            }

            int offset = local_array_ptr; int actual_elements = 0;

            if (hit_count > 0) {
                actual_elements = (hit_count > MAX_LOCAL_ARRAY - offset) ? (MAX_LOCAL_ARRAY - offset) : hit_count;
                for (int i = 0; i < actual_elements; i++) {
                    int step_distance = ((i + 1 < hit_count) ? hit_positions[i + 1] : (hit_positions[0] + md.n_steps)) - hit_positions[i];
                    local_array_mem[local_array_ptr++] = (1.0f * md.extra[0]) + ((step_distance - 1) * md.extra[1]);
                }
            } else {
                actual_elements = 1; local_array_mem[local_array_ptr++] = 1.0f * md.extra[0];
            }
            
            tos.type = 2; tos.v = (offset << 16) | (actual_elements & 0xFFFF); tos.len = 0;
            BOING();
        }

      L_OP_TO: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 6;
            dynamic_arity = -1;
            
            float p[6] = {0.0f, 2741.0f, 0.0f, 12.0f, 440.0f, 0.0f}; 
            int to_pop = (args > 6) ? 6 : args;
            for (int i = to_pop - 1; i >= 0; i--) {
                if (i == to_pop - 1) p[i] = sanitize(tos.f);
                else if (sp >= 0) p[i] = sanitize(vm_stack[sp--].f);
            }
            if (args > 6) sp -= (args - 6); 
            
            float freq = calculatePitch(p[0], p[1], p[2], p[3], p[4], p[5]);
            tos.type = 0;

            float stride_scaler = (current_sample_rate >= 44100) ? 0.5f : 1.0f;

            if (is_bb) {
                float phase_step = (freq * 256.0f) / (float)current_sample_rate;
                tos.f = (float)((int32_t)(t * phase_step * stride_scaler) & 255);
            } else {
                float phase_step = (freq * 6.2831853f) / (float)current_sample_rate;
                float total_angle = t * phase_step * stride_scaler;
                float wrapped_angle = total_angle - (floorf(total_angle / 6.2831853f) * 6.2831853f);
                if (wrapped_angle < 0.0f) wrapped_angle += 6.2831853f;
                tos.f = wrapped_angle;
            }
            BOING();
        }

        L_OP_AT_MASK: {
            int args = (dynamic_arity != -1) ? dynamic_arity : 3;
            dynamic_arity = -1;
            
            MaskData md = parse_rhythm_mask(args, tos, vm_stack, sp, (int)(vars[i_steps].f > 0 ? vars[i_steps].f : 16));

            int hits_per_pattern = 0;
            for (int i = 0; i < md.n_steps; i++) {
                if (md.mask & (1U << (md.n_steps - 1 - i))) hits_per_pattern++;
            }

            if (hits_per_pattern == 0) {
                tos.type = 0; tos.f = sanitize(vars[i_beat].f); tos.len = 0;
                BOING();
            }

            float absolute_beat = (args == 3) ? md.extra[0] : vars[i_beat].f;
            float master_steps = vars[i_steps].f > 0.0f ? vars[i_steps].f : 16.0f;
            
            float global_beats = 4.0f;
            if (vars[i_sign].f > 0.0f) global_beats = vars[i_sign].f * 4.0f;
            else if (vars[i_beats].f > 0.0f) global_beats = vars[i_beats].f;

            float steps_per_beat = master_steps / global_beats;
            if (steps_per_beat <= 0.0f) steps_per_beat = 4.0f;

            float continuous_step = absolute_beat * steps_per_beat;

            int completed_patterns = (int)floorf(continuous_step / md.n_steps);
            float current_pattern_slot = fmodf(continuous_step, (float)md.n_steps);
            if (current_pattern_slot < 0.0f) { 
                current_pattern_slot += md.n_steps; 
                completed_patterns--;
            }

            int local_hits = 0;
            int target_slot_int = (int)floorf(current_pattern_slot);
            for (int i = 0; i <= target_slot_int; i++) {
                if (i < md.n_steps) {
                    if (md.mask & (1U << (md.n_steps - 1 - i))) local_hits++;
                }
            }

            long total_absolute_hits = ((long)completed_patterns * hits_per_pattern) + local_hits;
            
            float active_index = (total_absolute_hits > 0) ? (float)(total_absolute_hits - 1) : 0.0f;
            if (active_index < 0.0f) active_index = 0.0f;

            tos.type = 0; tos.f = active_index; tos.len = 0;                 
            BOING();
        }

        L_OP_DEFAULT_CHECK: { int required = inst.val; int provided = (csp >= 0) ? vm_call_args[csp] : 0; if (provided >= required) pc++; BOING(); }
        L_OP_DEFAULT_INJECT: { if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.f = getF(inst.val); BOING(); }

        L_DEFAULT: BOING();

        L_END:

        if (tos.type == 1) { 
            out_buf[sample_idx] = (uint32_t)tos.v; 
            continue; 
        }

        float out = 0.0f;
        if (tos.type == 2 || tos.type == 3) {
            int32_t meta = tos.v; 
            int off = meta >> 16;
            int sz = meta & 0xFFFF;
            if (tos.type == 2 && sz >= 2) {
                float L = off < MAX_LOCAL_ARRAY ? local_array_mem[off] : 0.0f;
                float R = off + 1 < MAX_LOCAL_ARRAY ? local_array_mem[off + 1] : 0.0f;
                if (is_bb) { 
                    out_buf[sample_idx] = (uint32_t)((((int32_t)L & 255) + ((int32_t)R & 255)) >> 1); 
                    continue; 
                } else {
                    out = (L + R) * 0.5f;
                }
            } else if (tos.type == 2 && sz == 1) { 
                out = off < MAX_LOCAL_ARRAY ? local_array_mem[off] : 0.0f; 
            } else { 
                out = (float)sz; 
            }
        } else { 
            out = tos.f; 
        }

        if (!is_bb) {
            out = sanitize(out);

            if (out > 8.0f || out < -8.0f) {
                float fraction = out - (float)((int32_t)out);
                out = (fraction * 2.0f) - 1.0f; 
            } 

            if (out > 1.0f) out = 1.0f;
            if (out < -1.0f) out = -1.0f;
            
            int32_t signed_pcm = (int32_t)(out * (float)INT32_MAX);
            out_buf[sample_idx] = (uint32_t)signed_pcm ^ 0x80000000;
        } else if (tos.type != 2 && tos.type != 3) {
            out_buf[sample_idx] = (uint32_t)((int32_t)sanitize(out) & 255); 
        }
    }
#undef BOING
}

/**
 * Executes the VM for a single discrete time step natively.
 * @param t The absolute time step
 * @return The generated audio sample buffer as uint32_t
 */
uint32_t IRAM_ATTR executeVm(int32_t t) {
    uint32_t out; 
    executeVmBlock((float)t, 1.0f, 1, &out); 
    return out;
}

/**
 * Updates IMU variables in the VM state.
 */
void updateIMUVars(float ax, float ay, float az, float gx, float gy, float gz) {
    int i_ax = getVarId("ax"); vars[i_ax].type = 0; vars[i_ax].f = sanitize(ax);
    int i_ay = getVarId("ay"); vars[i_ay].type = 0; vars[i_ay].f = sanitize(ay);
    int i_az = getVarId("az"); vars[i_az].type = 0; vars[i_az].f = sanitize(az);
    int i_gx = getVarId("gx"); vars[i_gx].type = 0; vars[i_gx].f = sanitize(gx);
    int i_gy = getVarId("gy"); vars[i_gy].type = 0; vars[i_gy].f = sanitize(gy);
    int i_gz = getVarId("gz"); vars[i_gz].type = 0; vars[i_gz].f = sanitize(gz);
}

/**
 * Updates mouse variables in the VM state.
 */
void updateMouseVars(float mx, float my, float mv) {
    int i_mx = getVarId("mx"); vars[i_mx].type = 0; vars[i_mx].f = sanitize(mx);
    int i_my = getVarId("my"); vars[i_my].type = 0; vars[i_my].f = sanitize(my);
    int i_mv = getVarId("mv"); vars[i_mv].type = 0; vars[i_mv].f = sanitize(mv);
}

/**
 * Updates the virtual machine memory with active MIDI parameters.
 * @param freq The frequency of the active MIDI note in Hertz
 * @param gate The velocity/gate state (0.0 for off, >0.0 for active)
 * @param note The raw MIDI note number (0-127)
 */
void updateMIDIVars(float freq, float gate, float note) {
    int i_mf = getVarId("mf"); 
    vars[i_mf].type = 0; 
    vars[i_mf].f = sanitize(freq);

    int i_mg = getVarId("mg"); 
    vars[i_mg].type = 0; 
    vars[i_mg].f = sanitize(gate);

    int i_mn = getVarId("mn"); 
    vars[i_mn].type = 0; 
    vars[i_mn].f = sanitize(note);
}

/**
 * Ensures the global array has the requested capacity.
 * @param req_size The required size in float elements
 */
void ensureGlobalArray(int32_t req_size) {
    if (req_size <= 0 || req_size > 65536) return; 
    if (req_size > global_array_capacity) {
        float* new_mem = nullptr;
        #if defined(ESP32)
        new_mem = (float*)heap_caps_malloc(req_size * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
        #else
        new_mem = (float*)malloc(req_size * sizeof(float));
        #endif
        if (new_mem) {
            memset(new_mem, 0, req_size * sizeof(float));
            if (global_array_mem) {
                if (global_array_capacity > 0) memcpy(new_mem, global_array_mem, global_array_capacity * sizeof(float));
                #if defined(ESP32)
                heap_caps_free(global_array_mem);
                #else
                free(global_array_mem);
                #endif
            }
            global_array_mem = new_mem; global_array_capacity = req_size;
        }
    }
}

/**
 * Clears the global array memory.
 */
void clearGlobalArray() {
    if (global_array_mem && global_array_capacity > 0) {
        memset(global_array_mem, 0, global_array_capacity * sizeof(float));
    }
}
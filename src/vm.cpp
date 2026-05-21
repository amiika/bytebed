#include "vm.h"
#include "fast_math.h"
#include <algorithm>

const MathFunc mathLibrary[] = {
    {"sin",   OP_SIN,   true},  {"cos",   OP_COS,   true},  {"tan",   OP_TAN,   true},
    {"sqrt",  OP_SQRT,  true},  {"log",   OP_LOG,   true},  {"exp",   OP_EXP,   true},
    {"abs",   OP_ABS,   true},  {"floor", OP_FLOOR, true},  {"ceil",  OP_CEIL,  true},
    {"round", OP_ROUND, true},  {"cbrt",  OP_CBRT,  true},  {"asin",  OP_ASIN,  true},
    {"acos",  OP_ACOS,  true},  {"atan",  OP_ATAN,  true},
    {"min",   OP_MIN,   false}, {"max",   OP_MAX,   false}, {"pow",   OP_POW,   false},
    {"random", OP_RAND, true},  {"int",   OP_INT,   true},  {"phase", OP_PHASE, false}, 
    {"dup",   OP_DUP,   false}, {"swap",  OP_SWAP,  false}, {"rot",   OP_ROT,   false},
    {"over",  OP_OVER,  false},
    {"env",   OP_ENV,   false}, {"lfo",   OP_LFO,   false},
    {"pc",    OP_PC,    false}, {"euclid",OP_EUCLID,false}, {"on",    OP_ON,    false}
};
const int mathLibrarySize = 29;

const MathFunc shorthands[] = {
    {"s", OP_SIN, true}, {"c", OP_COS, true}, {"r", OP_RAND, true}, 
    {"euc", OP_EUCLID, true}
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
    {"--",  OP_POP,  10}
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

/**
 * Gets the symbol string for a given OpCode.
 * @param op The OpCode
 * @return The symbol string
 */
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
 * Executes a block of bytecode for audio generation.
 * @param start_t The starting time step
 * @param length The number of samples to generate
 * @param out_buf The output buffer to write to
 */
void IRAM_ATTR execute_vm_block(int32_t start_t, int length, uint8_t* out_buf) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    bool is_bb = (current_play_mode == MODE_BYTEBEAT);
    if (len == 0) { memset(out_buf, 128, length); return; }

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
        &&L_OP_DEFAULT_CHECK, &&L_OP_DEFAULT_INJECT
    };

    Instruction* prog = program_bank[bank];
    int t_var_id = getVarId("t");
    int i_sr = getVarId("sr");
    int i_bpm = getVarId("bpm");
    int i_step = getVarId("step");
    int i_beat = getVarId("beat");
    int i_bar = getVarId("bar");
    int i_steps = getVarId("steps"); // <-- Look for "steps" (Steps per bar)

    // Initialize a standard 16-step grid fallback if undefined
    if (vars[i_steps].f == 0.0f) {
        vars[i_steps].type = 0;
        vars[i_steps].f = 16.0f; // Default to 16 steps per bar
    }

    for (int sample_idx = 0; sample_idx < length; sample_idx++) {
        int32_t t = start_t + sample_idx; 

        vars[i_sr].type = 0; 
        vars[i_sr].f = (float)current_sample_rate;

        float current_bpm = vars[i_bpm].f;
        if (current_bpm <= 0.01f) current_bpm = 0.01f;

        // --- PREVENT PHANTOM NEGATIVE TIME HISS ---
        if (t < anchor_t) {
            anchor_t = t;
            anchor_beat = 0.0f;
        }

        if (current_bpm != anchor_bpm || current_sample_rate != anchor_sample_rate) {
            anchor_beat = anchor_beat + (t - anchor_t) * (anchor_bpm / (60.0f * anchor_sample_rate));
            anchor_t = t;
            anchor_bpm = current_bpm;
            anchor_sample_rate = current_sample_rate;
        }

        float current_beat = anchor_beat + (t - anchor_t) * (vars[i_bpm].f / (60.0f * current_sample_rate));
        float current_bar = current_beat / 4.0f;

        // Fetch the grid layout from the script space
        float steps_per_bar = vars[i_steps].f;
        if (steps_per_bar <= 0.01f) steps_per_bar = 16.0f; // Safety guard clamp

        vars[i_beat].type = 0; vars[i_beat].f = current_beat;
        vars[i_bar].type  = 0; vars[i_bar].f  = current_bar;
        
        // Calculate your sequencer index relative to the bar structure!
        vars[i_step].type = 0; vars[i_step].f = current_bar * steps_per_bar;
        
        vars[t_var_id].type = 0; vars[t_var_id].f = (float)t;

        local_array_ptr = 0; int sp = -1; int csp = -1; int ssp = -1; loop_sp = -1; 
        Val tos = {0, 0}; 

        int pc = 0; Instruction inst = prog[pc]; goto *dispatch_table[(uint8_t)inst.op];
        #define BOING() do { if (++pc >= len) goto L_END; inst = prog[pc]; goto *dispatch_table[(uint8_t)inst.op]; } while(0)

        L_OP_VAL: if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.v = inst.val; BOING();
        L_OP_T:   if (sp < 511) vm_stack[++sp] = tos; tos.type = 0; tos.f = (float)t; BOING();
        L_OP_LOAD: if (sp < 511) vm_stack[++sp] = tos; tos = vars[inst.val]; BOING();
        L_OP_LOAD_STR: if (sp < 511) vm_stack[++sp] = tos; tos.type = 4; tos.v = inst.val; BOING(); 
        L_OP_STORE: vars[inst.val] = tos; tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; BOING();
        L_OP_STORE_KEEP: vars[inst.val] = tos; BOING();
        L_OP_POP: tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; BOING();
        L_OP_JMP: pc += inst.val - 1; BOING(); 
        L_OP_PUSH_FUNC: if (sp < 511) vm_stack[++sp] = tos; tos.type = 1; tos.v = pc + 1; pc += inst.val - 1; BOING(); 
        L_OP_ASSIGN_VAR: BOING();
        L_OP_DYN_CALL:
        L_OP_DYN_CALL_IF_FUNC:
            if (tos.type == 1) { 
                if (csp < 511) { 
                    vm_call_stack[++csp] = pc; 
                    vm_call_args[csp] = (inst.op == OP_DYN_CALL) ? inst.val : 0; 
                    pc = tos.v - 1; 
                } 
                tos = (sp >= 0) ? vm_stack[sp--] : Val{0,0}; 
            } 
            else if (inst.op == OP_DYN_CALL) { 
                int args = inst.val; 
                if (args > 0) sp -= args; 
                tos = {0, 0}; 
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
            int32_t size = (int32_t)tos.f; if (size > 0) ensure_global_array(size); 
            tos.type = 3; tos.f = (float)(global_array_capacity > 0 ? global_array_capacity : size); BOING();
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
        L_OP_CBRT:  tos.f = cbrtf(tos.f); BOING();
        L_OP_ASIN:  tos.f = asinf(tos.f); BOING();
        L_OP_ACOS:  tos.f = acosf(tos.f); BOING();
        L_OP_ATAN:  tos.f = atanf(tos.f); BOING();
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
        L_OP_POW: { Val lhs = (sp >= 0 ? vm_stack[sp--] : Val{0,0}); if (lhs.type == 2 || tos.type == 2) tos = eval_vector_bin_op(lhs, tos, OP_POW); else tos.f = fast_pow(lhs.f, tos.f); BOING(); }

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

        L_OP_PHASE: {
            int args = inst.val;
            if (args == 0) { 
                if (sp < 511) vm_stack[++sp] = tos;
                tos.type = 0; tos.f = 0.0f; 
                BOING(); 
            }

            float total_dur = 0.0f;
            int sz = 0;
            bool is_global = false;
            int off = 0;
            bool is_array_arg = (args == 1 && (tos.type == 2 || tos.type == 3));
            
            float temp_args[32];

            if (is_array_arg) {
                if (tos.type == 2) { off = tos.v >> 16; sz = tos.v & 0xFFFF; }
                else if (tos.type == 3) { sz = global_array_capacity; is_global = true; }

                for (int i = 0; i < sz; i++) {
                    temp_args[i] = is_global ? global_array_mem[i] : (off + i < MAX_LOCAL_ARRAY ? local_array_mem[off + i] : 0.0f);
                    temp_args[i] = sanitize(temp_args[i]);
                    total_dur += temp_args[i];
                }
            } else {
                sz = args > 32 ? 32 : args;
                temp_args[sz - 1] = sanitize(tos.f);
                for (int i = sz - 2; i >= 0; i--) {
                    temp_args[i] = sanitize(vm_stack[sp - (sz - 2 - i)].f);
                }
                for (int i = 0; i < sz; i++) total_dur += temp_args[i];
                sp -= (args - 1);
            }

            if (total_dur > 0.0f && sz > 0) {
                float b = vars[i_beat].f;
                float loop_count = floorf(b / total_dur);
                float local_beat = b - loop_count * total_dur;
                float accum = 0.0f;
                int current_step = sz - 1;
                float step_dur = temp_args[sz - 1]; 
                float step_start = total_dur - step_dur; 
                
                for (int i = 0; i < sz; i++) {
                    float v = temp_args[i];
                    if (local_beat >= accum && local_beat < accum + v) {
                        current_step = i;
                        step_dur = v;
                        step_start = accum;
                        break;
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
            if (sp >= 1) { // 3 args total (tos + 2 in stack)
                float d = sanitize(tos.f);
                float a = sanitize(vm_stack[sp--].f);
                float p = sanitize(vm_stack[sp--].f);

                float p_mod = p - floorf(p); 
                if (p_mod < 0.0f) p_mod += 1.0f; 
                
                float attack = p_mod * a;
                if (attack > 1.0f) attack = 1.0f;
                float decay = 1.0f - p_mod;
                
                tos.type = 0; 
                tos.f = attack * fast_pow(decay, d);
                
                int args_to_pop = inst.val - 3;
                if (args_to_pop > 0) sp -= args_to_pop;
            } else { tos.type = 0; tos.f = 0.0f; }
            BOING();
        }

        L_OP_LFO: {
            if (sp >= 0) { // 2 args total (tos + 1 in stack)
                float type = sanitize(tos.f);
                float p = sanitize(vm_stack[sp--].f);

                float p_mod = p - floorf(p); 
                if (p_mod < 0.0f) p_mod += 1.0f; 
                
                float out = 0.0f;
                int t_int = (int)type;

                if (t_int == 0) { 
                    out = fast_sin(p_mod * 6.2831853f);
                } else if (t_int == 1) { 
                    out = 1.0f - (p_mod * 2.0f);
                } else if (t_int == 2) { 
                    out = (p_mod < 0.5f) ? 1.0f : -1.0f;
                } else if (t_int == 3) { 
                    out = (p_mod < 0.5f) ? (p_mod * 4.0f - 1.0f) : (3.0f - p_mod * 4.0f);
                }

                tos.type = 0; 
                tos.f = out;
                
                int args_to_pop = inst.val - 2;
                if (args_to_pop > 0) sp -= args_to_pop;
            } else { tos.type = 0; tos.f = 0.0f; }
            BOING();
        }

        L_OP_PC: {
            int args = inst.val;
            float p[6] = {0.0f, 0.0f, 0.0f, 2741.0f, 12.0f, 440.0f}; 
            int fill = (args > 6) ? 6 : args;
            if (fill > 0) p[fill - 1] = sanitize(tos.f);
            for (int i = fill - 2; i >= 0; i--) {
                p[i] = sanitize(vm_stack[sp--].f);
            }
            
            float deg = p[0], oct = p[1], root = p[2], mask = p[3], edo = p[4], base_hz = p[5];
            uint32_t m = (uint32_t)mask;
            int max_edo = (int)edo; if (max_edo > 32) max_edo = 32; else if (max_edo < 1) max_edo = 1;
            int bits[32]; int scale_len = 0;
            
            for (int i = 0; i < max_edo; i++) {
                if (m & (1 << i)) bits[scale_len++] = i;
            }
            if (scale_len == 0) { bits[0] = 0; scale_len = 1; }
            
            int octaves = 0;
            if (deg >= 0) octaves = (int)(deg / scale_len);
            else octaves = (int)((deg - scale_len + 1.0f) / scale_len);
            
            int local_deg = (int)deg - (octaves * scale_len);
            if (local_deg < 0) local_deg += scale_len;
            
            float interval = (float)bits[local_deg] + (octaves * edo) + (oct * edo);
            tos.type = 0;
            tos.f = base_hz * powf(2.0f, (root + interval - 69.0f) / edo);
            BOING();
        }

      L_OP_EUCLID: {
            int args = inst.val;
            float p[3] = {0.0f, 0.0f, 0.0f};
            int fill = (args > 3) ? 3 : args;
            if (fill > 0) p[fill - 1] = sanitize(tos.f);
            for (int i = fill - 2; i >= 0; i--) p[i] = sanitize(vm_stack[sp--].f);
            
            int pulses = (int)p[0]; 
            int length = (int)p[1]; 
            int rotate = (int)p[2];

            if (length < 1) length = 1;
            if (pulses < 0) pulses = 0;
            if (pulses > length) pulses = length;

            uint32_t mask = 0;
            if (pulses >= length) {
                mask = 0xFFFFFFFF >> (32 - length);
            } else if (pulses > 0) {
                for (int t = 0; t < length; t++) {
                    int current = (pulses * t) % length;
                    int next = (pulses * (t + 1)) % length;
                    
                    if (current > next) {
                        // Apply rotation and set bit
                        int pos = (t + rotate) % length;
                        if (pos < 0) pos += length;
                        mask |= (1 << pos);
                    }
                }
            }
            
            tos.type = 0; 
            tos.f = (float)mask; 
            BOING();
        }

        L_OP_ON: {
            int args = inst.val;
            float p[2] = {0.0f, 0.0f};
            int fill = (args > 2) ? 2 : args;
            if (fill > 0) p[fill - 1] = sanitize(tos.f);
            for (int i = fill - 2; i >= 0; i--) p[i] = sanitize(vm_stack[sp--].f);
            
            uint32_t mask = (uint32_t)p[0];
            int slot = (int)floorf(p[1]) % 32;
            if (slot < 0) slot += 32;
            
            tos.type = 0;
            tos.f = (mask & (1 << slot)) ? 1.0f : 0.0f;
            BOING();
        }

        L_OP_DEFAULT_CHECK: {
            int required = inst.val;
            int provided = (csp >= 0) ? vm_call_args[csp] : 0;
            if (provided >= required) {
                pc++; 
            }
            BOING();
        }

        L_OP_DEFAULT_INJECT: {
            if (sp < 511) vm_stack[++sp] = tos;
            tos.type = 0; tos.f = getF(inst.val);
            BOING();
        }

        L_DEFAULT: BOING();
        L_END:
        float out = 0.0f;
        if (tos.type == 2 || tos.type == 3) {
            int32_t meta = tos.v; int off = meta >> 16, sz = meta & 0xFFFF;
            if (tos.type == 2 && sz >= 2) {
                float L = off < MAX_LOCAL_ARRAY ? local_array_mem[off] : 0.0f, R = off + 1 < MAX_LOCAL_ARRAY ? local_array_mem[off + 1] : 0.0f;
                if (is_bb) { out_buf[sample_idx] = (uint8_t)((((int32_t)L & 255) + ((int32_t)R & 255)) >> 1); continue; }
                else out = (L + R) * 0.5f;
            } else if (tos.type == 2 && sz == 1) { out = off < MAX_LOCAL_ARRAY ? local_array_mem[off] : 0.0f; }
            else { out = (float)sz; }
        } else { out = tos.f; }
        if (!is_bb) {
            out = sanitize(out); if (out < -1.0f) out = -1.0f; if (out > 1.0f) out = 1.0f;
            out_buf[sample_idx] = (uint8_t)((out + 1.0f) * 127.5f);
        } else if (tos.type != 2 && tos.type != 3) {
            out_buf[sample_idx] = (uint8_t)((int32_t)sanitize(out) & 255); 
        }
    }
    #undef BOING
}

/**
 * Executes the VM for a single discrete time step.
 * @param t The absolute time step
 * @return The generated audio sample byte
 */
uint8_t IRAM_ATTR execute_vm(int32_t t) {
    uint8_t out; execute_vm_block(t, 1, &out); return out;
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
void ensure_global_array(int32_t req_size) {
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
                // If old memory exists, copy it over before freeing
                if (global_array_capacity > 0) {
                    memcpy(new_mem, global_array_mem, global_array_capacity * sizeof(float));
                }
                #if defined(ESP32)
                heap_caps_free(global_array_mem);
                #else
                free(global_array_mem);
                #endif
            }
            global_array_mem = new_mem;
            global_array_capacity = req_size;
        }
    }
}

/**
 * Clears the global array memory.
 */
void clear_global_array() {
    if (global_array_mem && global_array_capacity > 0) {
        memset(global_array_mem, 0, global_array_capacity * sizeof(float));
    }
}
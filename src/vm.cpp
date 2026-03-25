#include "vm.h"
#include "fast_math.h"
#include <algorithm>

Instruction program_bank[2][512];
int prog_len_bank[2] = {0, 0};
volatile uint8_t active_bank = 0;

Val vars[64];
String symNames[64];
int var_count = 0;

float* global_array_mem = nullptr;
int32_t global_array_capacity = 0;

float local_array_mem[1024];
int local_array_ptr = 0;

struct LoopState { 
    int type;       // 0=SUM, 1=GEN, 2=MAP, 3=REDUCE, 4=FILTER
    float acc;      
    float limit;    
    int f_pc;       
    float i;        
    int saved_sp;   
    Val iterable;   
    float current_val; // Retained for filter repackaging
};
static LoopState loop_stack[32];
int loop_sp = -1;

void ensure_global_array(int32_t req_size) {
    if (req_size <= 0 || req_size > 65536) return; 
    if (req_size > global_array_capacity) {
        if (global_array_mem) free(global_array_mem);
        #if defined(ESP32)
        global_array_mem = (float*)heap_caps_malloc(req_size * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        #else
        global_array_mem = (float*)malloc(req_size * sizeof(float));
        #endif
        if (global_array_mem) {
            memset(global_array_mem, 0, req_size * sizeof(float));
            global_array_capacity = req_size;
        } else {
            global_array_capacity = 0;
        }
    }
}

void clear_global_array() {
    if (global_array_mem && global_array_capacity > 0) {
        memset(global_array_mem, 0, global_array_capacity * sizeof(float));
    }
}

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

inline float sanitize(float f) {
    union { float f; uint32_t i; } u;
    u.f = f;
    if ((u.i & 0x7F800000) == 0x7F800000) return 0.0f;
    return f;
}

const MathFunc mathLibrary[] = {
    {"sin",   OP_SIN,   true},  {"cos",   OP_COS,   true},  {"tan",   OP_TAN,   true},
    {"sqrt",  OP_SQRT,  true},  {"log",   OP_LOG,   true},  {"exp",   OP_EXP,   true},
    {"abs",   OP_ABS,   true},  {"floor", OP_FLOOR, true},  {"ceil",  OP_CEIL,  true},
    {"round", OP_ROUND, true},  {"cbrt",  OP_CBRT,  true},  {"asin",  OP_ASIN,  true},
    {"acos",  OP_ACOS,  true},  {"atan",  OP_ATAN,  true},
    {"min",   OP_MIN,   false}, {"max",   OP_MAX,   false}, {"pow",   OP_POW,   false},
    {"random", OP_RAND, true},  {"int",   OP_INT,   true}
};
const int mathLibrarySize = 19;

const MathFunc shorthands[] = {
    {"s", OP_SIN, true}, {"c", OP_COS, true}, {"f", OP_FLOOR, true}, {"i", OP_INT, true}, {"r", OP_RAND, true}
};
const int shorthandsSize = 5;

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
    {"**=", OP_POW_ASSIGN, -1}
};
const int opListSize = 37;

int getVarId(String name) {
    for (int i = 0; i < var_count; i++) if (symNames[i] == name) return i;
    if (var_count >= 63) return 63; 
    symNames[var_count] = name;
    return var_count++;
}

String getVarName(int id) {
    if (id >= 0 && id < var_count) return symNames[id];
    return "v" + String(id);
}

bool isVarDefined(const String& name) {
    for (int i = 0; i < var_count; i++) if (symNames[i] == name) return true;
    return false;
}

int getPrecedence(OpCode op) {
    for (int i = 0; i < opListSize; i++) if (opList[i].code == op) return opList[i].precedence;
    if (op == OP_COND || op == OP_COLON) return 0;
    if (op == OP_ASSIGN_VAR || op == OP_STORE_AT || (op >= OP_ADD_ASSIGN && op <= OP_SHR_ASSIGN)) return -1;
    if (op == OP_STORE || op == OP_STORE_KEEP) return -2;
    if (op == OP_NONE || op == OP_DYN_CALL || op == OP_DYN_CALL_IF_FUNC) return -3;
    return 10; 
}

String getOpSym(OpCode op) {
    if (op == OP_COND) return "?"; 
    if (op == OP_BNOT) return "~";
    for (int i = 0; i < shorthandsSize; i++) if (shorthands[i].code == op) return shorthands[i].name; 
    for (int i = 0; i < mathLibrarySize; i++) if (mathLibrary[i].code == op) return mathLibrary[i].name; 
    for (int i = 0; i < opListSize; i++) if (opList[i].code == op) return opList[i].sym; 
    return "";
}

bool getOpCode(const String& sym, OpCode& outCode) {
    for (int i = 0; i < opListSize; i++) if (sym == opList[i].sym) { outCode = opList[i].code; return true; }
    return false;
}

void IRAM_ATTR execute_vm_block(int32_t start_t, int length, uint8_t* out_buf) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    bool is_bb = (current_play_mode == MODE_BYTEBEAT);
    if (len == 0) { memset(out_buf, 128, length); return; }

    static Val stack[512]; static int32_t call_stack[512]; static Val shadow_val[512]; 

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
        &&L_OP_RAND, &&L_OP_INT,
        &&L_OP_LOOP_PREP, &&L_OP_LOOP_EVAL, &&L_OP_LOOP_DONE,
        &&L_DEFAULT
    };

    Instruction* prog = program_bank[bank];
    int t_var_id = getVarId("t");

    for (int sample_idx = 0; sample_idx < length; sample_idx++) {
        vars[t_var_id].type = 0; vars[t_var_id].f = (float)(start_t + sample_idx); 
        int32_t t = start_t + sample_idx; 
        local_array_ptr = 0; int sp = -1; int csp = -1; int ssp = -1; loop_sp = -1; 
        Val tos = {0, 0}; 

        int pc = 0; Instruction inst = prog[pc]; goto *dispatch_table[inst.op];
        #define BOING() do { if (++pc >= len) goto L_END; inst = prog[pc]; goto *dispatch_table[inst.op]; } while(0)

        L_OP_VAL: stack[++sp] = tos; tos.type = 0; tos.v = inst.val; BOING();
        L_OP_T:   stack[++sp] = tos; tos.type = 0; tos.f = (float)t; BOING();
        L_OP_LOAD: stack[++sp] = tos; tos = vars[inst.val]; BOING();
        L_OP_STORE: vars[inst.val] = tos; tos = stack[sp--]; BOING();
        L_OP_STORE_KEEP: vars[inst.val] = tos; BOING();
        L_OP_POP: tos = stack[sp--]; BOING();
        L_OP_JMP: pc += inst.val - 1; BOING(); 
        L_OP_PUSH_FUNC: stack[++sp] = tos; tos.type = 1; tos.v = pc + 1; pc += inst.val - 1; BOING(); 
        L_OP_ASSIGN_VAR: BOING();
        L_OP_DYN_CALL:
        L_OP_DYN_CALL_IF_FUNC:
            if (tos.type == 1) { if (csp < 511) { call_stack[++csp] = pc; pc = tos.v - 1; } tos = stack[sp--]; } 
            else if (inst.op == OP_DYN_CALL) { int args = inst.val; sp -= args; tos = {0, 0}; } BOING();
        L_OP_RET: if (csp >= 0) pc = call_stack[csp--]; else pc = len; BOING();
        L_OP_BIND: if (ssp < 511) shadow_val[++ssp] = vars[inst.val]; vars[inst.val] = tos; tos = stack[sp--]; BOING();
        L_OP_UNBIND: if (ssp >= 0) vars[inst.val] = shadow_val[ssp--]; BOING();
        
        L_OP_VEC: {
            int32_t size = (int32_t)tos.f;
            if (size >= 1) {
                int offset = local_array_ptr; int start_idx = sp - size + 1;
                for (int i = 0; i < size; i++) {
                    if (local_array_ptr < 1024) {
                        Val& item = stack[start_idx + i];
                        local_array_mem[local_array_ptr++] = (item.type == 2) ? encode_vec(item.v) : item.f;
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
            Val base = stack[sp--];
            if (base.type == 2) { 
                int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
                if (sz > 0) { if (idx < 0 || idx >= sz) idx = ((idx % sz) + sz) % sz; }
                float raw = (off + idx < 1024) ? local_array_mem[off + idx] : 0.0f;
                if (is_vec_tag(raw)) { tos.type = 2; tos.v = decode_vec(raw); }
                else { tos.type = 0; tos.f = sanitize(raw); }
            } else if (base.type == 3 && global_array_capacity > 0) { 
                if (idx < 0 || idx >= global_array_capacity) idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity; 
                tos.type = 0; tos.f = sanitize(global_array_mem[idx]);
            } else { tos = {0, 0}; } 
            BOING();
        }
        L_OP_STORE_AT: {
            Val val_to_s = tos; 
            int32_t idx = (int32_t)stack[sp--].f; 
            Val base = stack[sp--];
            if (base.type == 2) { 
                int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
                if (sz > 0) { if (idx < 0 || idx >= sz) idx = ((idx % sz) + sz) % sz; }
                if (off + idx < 1024) local_array_mem[off + idx] = (val_to_s.type == 2) ? encode_vec(val_to_s.v) : sanitize(val_to_s.f);
            } else if (base.type == 3 && global_array_capacity > 0) { 
                if (idx < 0 || idx >= global_array_capacity) idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity;
                global_array_mem[idx] = sanitize(val_to_s.f); 
            }
            tos = val_to_s; 
            BOING();
        }
        L_OP_SC_AND: { if (inst.val != 0) { if (tos.f == 0.0f) pc += inst.val - 1; else tos = stack[sp--]; } else { if (stack[sp].f == 0.0f) tos = stack[sp--]; else sp--; } BOING(); }
        L_OP_SC_OR: { if (inst.val != 0) { if (tos.f != 0.0f) pc += inst.val - 1; else tos = stack[sp--]; } else { if (stack[sp].f != 0.0f) tos = stack[sp--]; else sp--; } BOING(); }
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
        L_OP_CBRT:  tos.f = sanitize(cbrtf(tos.f)); BOING();
        L_OP_ASIN:  tos.f = sanitize(asinf(tos.f)); BOING();
        L_OP_ACOS:  tos.f = sanitize(acosf(tos.f)); BOING();
        L_OP_ATAN:  tos.f = sanitize(atanf(tos.f)); BOING();
        L_OP_COND: { Val f = tos, tv = stack[sp--], c = stack[sp--]; Val tgt = (c.f != 0.0f) ? tv : f; if (tgt.type == 1) { if (csp < 511) { call_stack[++csp] = pc; pc = tgt.v - 1; } tos = stack[sp--]; } else { tos = tgt; } BOING(); }
        
        L_OP_ADD: tos.f = stack[sp].f + tos.f; sp--; BOING();
        L_OP_SUB: tos.f = stack[sp].f - tos.f; sp--; BOING();
        L_OP_MUL: tos.f = stack[sp].f * tos.f; sp--; BOING();
        L_OP_DIV: { float d = tos.f; tos.f = (d != 0.0f ? sanitize(stack[sp].f / d) : 0.0f); sp--; BOING(); }
        L_OP_MOD: { float n = stack[sp].f, d = tos.f; tos.f = (d != 0.0f ? sanitize(n - (int32_t)(n / d) * d) : 0.0f); sp--; BOING(); }
        
        L_OP_AND: tos.f = (float)((int32_t)stack[sp].f & (int32_t)tos.f); sp--; BOING();
        L_OP_OR:  tos.f = (float)((int32_t)stack[sp].f | (int32_t)tos.f); sp--; BOING();
        L_OP_XOR: tos.f = (float)((int32_t)stack[sp].f ^ (int32_t)tos.f); sp--; BOING();
        L_OP_SHL: tos.f = (float)((int32_t)stack[sp].f << (int32_t)tos.f); sp--; BOING();
        L_OP_SHR: tos.f = (float)((int32_t)stack[sp].f >> (int32_t)tos.f); sp--; BOING();
        
        L_OP_LT:  tos.f = (stack[sp].f < tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_GT:  tos.f = (stack[sp].f > tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_EQ:  tos.f = (stack[sp].f == tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_NEQ: tos.f = (stack[sp].f != tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_LTE: tos.f = (stack[sp].f <= tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_GTE: tos.f = (stack[sp].f >= tos.f) ? 1.0f : 0.0f; sp--; BOING();
        L_OP_MIN: { float a = stack[sp].f, b = tos.f; tos.f = (a < b ? a : b); sp--; BOING(); }
        L_OP_MAX: { float a = stack[sp].f, b = tos.f; tos.f = (a > b ? a : b); sp--; BOING(); }
        L_OP_POW: tos.f = fast_pow(stack[sp].f, tos.f); sp--; BOING();

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
            stack[++sp] = tos; tos.type = 0; tos.f = (float)(x_rng & 0xFFFFFF) / 16777216.0f; BOING();
        }

        L_OP_INT: { tos.f = (float)((int32_t)tos.f); BOING(); }

        L_OP_LOOP_PREP: {
            int ltype = inst.val; Val func = tos; Val iter = stack[sp--];
            float limit = 0.0f; Val iterable = {0, 0};
            if (iter.type == 2) {
                iterable = iter; limit = (float)(iter.v & 0xFFFF);
            } else if (iter.type == 3) {
                iterable = iter; limit = iter.f; 
            } else { limit = iter.f; }
            if (loop_sp < 31) { loop_stack[++loop_sp] = {ltype, 0.0f, limit, (int)func.v, 0.0f, sp, iterable, 0.0f}; }
            tos.type = 0; tos.f = 0.0f; BOING();
        }
        
        L_OP_LOOP_EVAL: {
            if (loop_sp >= 0) {
                LoopState& s = loop_stack[loop_sp];
                if (s.i >= s.limit) {
                    if (s.type == 0 || s.type == 3) { 
                        tos.type = 0; tos.f = s.acc; sp = s.saved_sp; loop_sp--; pc += inst.val - 1; BOING(); 
                    } else { 
                        tos.type = 0; tos.f = (s.type == 4) ? s.acc : s.limit; // Filter uses acc for array size
                        loop_sp--; pc += inst.val - 1; goto L_OP_VEC; 
                    }
                } else {
                    s.saved_sp = sp; if (csp < 511) { call_stack[++csp] = pc; pc = s.f_pc - 1; }
                    float val = s.i; 
                    if (s.iterable.type != 0) { 
                        int32_t idx = (int32_t)s.i;
                        if (s.iterable.type == 2) {
                            int off = s.iterable.v >> 16, sz = s.iterable.v & 0xFFFF;
                            if (idx >= 0 && idx < sz && off + idx < 1024) val = local_array_mem[off + idx];
                        } else if (s.iterable.type == 3) {
                            if (idx >= 0 && idx < global_array_capacity) val = global_array_mem[idx];
                        }
                    }
                    s.current_val = val; 
                    stack[++sp].type = 0; stack[sp].f = (s.type == 3) ? s.acc : 0.0f; // Push dummy OR accumulator
                    tos.type = 0; tos.f = val; 
                }
            } else { tos.type = 0; tos.f = 0.0f; } BOING();
        }
        
        L_OP_LOOP_DONE: {
            if (loop_sp >= 0) {
                LoopState& s = loop_stack[loop_sp];
                if (s.type == 0) { s.acc += tos.f; sp = s.saved_sp; } // SUM (C++ Optimised addition)
                else if (s.type == 3) { s.acc = tos.f; sp = s.saved_sp; } // REDUCE (Lambda-driven accumulator update)
                else if (s.type == 4) { // FILTER (Dynamic dropping)
                    sp = s.saved_sp; 
                    if (tos.f != 0.0f) { stack[++sp].type = 0; stack[sp].f = s.current_val; s.acc += 1.0f; }
                }
                else { sp = s.saved_sp; stack[++sp] = tos; } // GEN & MAP (1:1 Repacking)
                
                s.i += 1.0f; pc -= inst.val - 1;     
            }
            tos.type = 0; tos.f = 0.0f; BOING();
        }

        L_DEFAULT: BOING();
        L_END:
        float out = 0.0f;
        if (tos.type == 2 || tos.type == 3) {
            int32_t meta = tos.v; int off = meta >> 16, sz = meta & 0xFFFF;
            if (tos.type == 2 && sz >= 2) {
                float L = off < 1024 ? local_array_mem[off] : 0.0f, R = off + 1 < 1024 ? local_array_mem[off + 1] : 0.0f;
                if (is_bb) { out_buf[sample_idx] = (uint8_t)((((int32_t)L & 255) + ((int32_t)R & 255)) >> 1); continue; }
                else out = (L + R) * 0.5f;
            } else if (tos.type == 2 && sz == 1) { out = off < 1024 ? local_array_mem[off] : 0.0f; }
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

uint8_t IRAM_ATTR execute_vm(int32_t t) {
    uint8_t out; execute_vm_block(t, 1, &out); return out;
}

// --- IMU Data Injector Implementation ---
void updateIMUVars(float ax, float ay, float az, float gx, float gy, float gz) {
    int i_ax = getVarId("ax"); vars[i_ax].type = 0; vars[i_ax].f = sanitize(ax);
    int i_ay = getVarId("ay"); vars[i_ay].type = 0; vars[i_ay].f = sanitize(ay);
    int i_az = getVarId("az"); vars[i_az].type = 0; vars[i_az].f = sanitize(az);
    int i_gx = getVarId("gx"); vars[i_gx].type = 0; vars[i_gx].f = sanitize(gx);
    int i_gy = getVarId("gy"); vars[i_gy].type = 0; vars[i_gy].f = sanitize(gy);
    int i_gz = getVarId("gz"); vars[i_gz].type = 0; vars[i_gz].f = sanitize(gz);
}

// --- Mouse Data Injector Implementation ---
void updateMouseVars(float mx, float my, float mv) {
    int i_mx = getVarId("mx"); vars[i_mx].type = 0; vars[i_mx].f = sanitize(mx);
    int i_my = getVarId("my"); vars[i_my].type = 0; vars[i_my].f = sanitize(my);
    int i_mv = getVarId("mv"); vars[i_mv].type = 0; vars[i_mv].f = sanitize(mv);
}
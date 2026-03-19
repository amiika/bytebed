#include "vm.h"
#include <math.h>
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

inline float encode_vec(int32_t v) {
    uint32_t u = (uint32_t)v | 0x80000000;
    return *(float*)&u;
}
inline bool is_vec_tag(float f) {
    return (*(uint32_t*)&f & 0x80000000) != 0;
}
inline int32_t decode_vec(float f) {
    return (int32_t)(*(uint32_t*)&f & 0x7FFFFFFF);
}
inline float sanitize(float f) {
    if (isnan(f) || isinf(f)) return 0.0f;
    return f;
}

void clear_global_array() {
    if (global_array_mem && global_array_capacity > 0) {
        memset(global_array_mem, 0, global_array_capacity * sizeof(float));
    }
}

void ensure_global_array(int32_t req_size) {
    if (req_size <= 0 || req_size > 262144) return; 
    if (req_size > global_array_capacity) {
        if (global_array_mem) free(global_array_mem);
        #if defined(ESP32)
        global_array_mem = (float*)ps_malloc(req_size * sizeof(float));
        if (!global_array_mem) global_array_mem = (float*)malloc(req_size * sizeof(float));
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

const MathFunc mathLibrary[] = {
    {"sin",   OP_SIN,   true},  {"cos",   OP_COS,   true},  {"tan",   OP_TAN,   true},
    {"sqrt",  OP_SQRT,  true},  {"log",   OP_LOG,   true},  {"exp",   OP_EXP,   true},
    {"abs",   OP_ABS,   true},  {"floor", OP_FLOOR, true},  {"ceil",  OP_CEIL,  true},
    {"round", OP_ROUND, true},  {"cbrt",  OP_CBRT,  true},  {"asin",  OP_ASIN,  true},
    {"acos",  OP_ACOS,  true},  {"atan",  OP_ATAN,  true},
    {"min",   OP_MIN,   false}, {"max",   OP_MAX,   false}, {"pow",   OP_POW,   false}
};
const int mathLibrarySize = 17;

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
    for (int i = 0; i < var_count; i++) {
        if (symNames[i] == name) return i;
    }
    if (var_count >= 63) return 63; 
    symNames[var_count] = name;
    return var_count++;
}

String getVarName(int id) {
    if (id >= 0 && id < var_count) return symNames[id];
    return "v" + String(id);
}

int getPrecedence(OpCode op) {
    for (int i = 0; i < opListSize; i++) {
        if (opList[i].code == op) return opList[i].precedence;
    }
    if (op == OP_COND) return 0;
    if (op == OP_ASSIGN_VAR || op == OP_STORE_AT || (op >= OP_ADD_ASSIGN && op <= OP_SHR_ASSIGN)) return -1;
    if (op == OP_STORE || op == OP_STORE_KEEP) return -2;
    if (op == OP_NONE || op == OP_DYN_CALL || op == OP_DYN_CALL_IF_FUNC) return -3;
    return 10; 
}

String getOpSym(OpCode op) {
    if (op == OP_COND) return "?"; 
    if (op == OP_BNOT) return "~";
    for (int i = 0; i < mathLibrarySize; i++) if (mathLibrary[i].code == op) return mathLibrary[i].name; 
    for (int i = 0; i < opListSize; i++) if (opList[i].code == op) return opList[i].sym; 
    return "";
}

bool getOpCode(const String& sym, OpCode& outCode) {
    for (int i = 0; i < opListSize; i++) {
        if (sym == opList[i].sym) {
            outCode = opList[i].code;
            return true;
        }
    }
    return false;
}

uint8_t IRAM_ATTR execute_vm(int32_t t) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    if (len == 0) return 128;
    
    local_array_ptr = 0; 
    
    static Val stack[512]; int sp = -1;
    static int32_t call_stack[512]; int csp = -1;
    static Val shadow_val[512]; int ssp = -1;

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
        &&L_OP_AND_ASSIGN, &&L_OP_OR_ASSIGN, &&L_OP_XOR_ASSIGN, &&L_OP_POW_ASSIGN, &&L_OP_SHL_ASSIGN, &&L_OP_SHR_ASSIGN
    };

    Instruction* prog = program_bank[bank];
    int pc = 0;
    Instruction inst = prog[pc];
    goto *dispatch_table[inst.op];

    #define BEEP() do { if (++pc >= len) goto L_END; inst = prog[pc]; goto *dispatch_table[inst.op]; } while(0)

    L_OP_VAL: stack[++sp] = {0, inst.val}; BEEP();
    L_OP_T:   stack[++sp] = {0, setF((float)t)}; BEEP();
    L_OP_LOAD: stack[++sp] = vars[inst.val]; BEEP();
    L_OP_STORE: if(sp>=0) vars[inst.val] = stack[sp--]; BEEP();
    L_OP_STORE_KEEP: if(sp>=0) vars[inst.val] = stack[sp]; BEEP();
    L_OP_POP: if(sp>=0) sp--; BEEP();
    L_OP_JMP: pc += inst.val - 1; BEEP(); 
    L_OP_PUSH_FUNC: stack[++sp] = {1, pc + 1}; pc += inst.val - 1; BEEP(); 
    L_OP_ASSIGN_VAR: BEEP();
    
    L_OP_DYN_CALL:
    L_OP_DYN_CALL_IF_FUNC:
        if (sp >= 0 && stack[sp].type == 1) { 
            if (csp < 511) { call_stack[++csp] = pc; pc = stack[sp--].v - 1; }
            else sp--;
        } else if (inst.op == OP_DYN_CALL) {
            int args = inst.val; if (sp >= args) sp -= (args + 1); else sp = -1;
            stack[++sp] = {0, 0}; 
        } BEEP();
        
    L_OP_RET: if (csp >= 0) pc = call_stack[csp--]; else pc = len; BEEP();
    L_OP_BIND: if (sp >= 0) { if (ssp < 511) shadow_val[++ssp] = vars[inst.val]; vars[inst.val] = stack[sp--]; } BEEP();
    L_OP_UNBIND: if (ssp >= 0) vars[inst.val] = shadow_val[ssp--]; BEEP();
    
    L_OP_VEC: {
        if (sp < 0) return 128;
        int32_t size = (int32_t)getF(stack[sp--].v);
        if (size >= 1 && sp >= (size - 1)) {
            int offset = local_array_ptr;
            int start_idx = sp - size + 1;
            for (int i = 0; i < size; i++) {
                if (local_array_ptr < 1024) {
                    Val& item = stack[start_idx + i];
                    local_array_mem[local_array_ptr++] = (item.type == 2) ? encode_vec(item.v) : getF(item.v);
                }
            }
            sp -= size; 
            stack[++sp].type = 2; 
            stack[sp].v = (offset << 16) | (size & 0xFFFF); 
        } BEEP();
    }
    
    L_OP_ALLOC: {
        if (sp < 0) return 128;
        int32_t size = (int32_t)getF(stack[sp].v);
        if (size > 0) ensure_global_array(size); 
        stack[sp].type = 3; 
        stack[sp].v = setF((float)(global_array_capacity > 0 ? global_array_capacity : size));
        BEEP();
    }
        
    L_OP_AT: {
        if (sp < 1) return 128; 
        Val base = stack[sp--]; 
        int32_t idx = (int32_t)getF(stack[sp--].v);
        if (base.type == 2) { 
            int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
            if (sz > 0) idx = ((idx % sz) + sz) % sz;
            float raw = (off + idx < 1024) ? local_array_mem[off + idx] : 0.0f;
            if (is_vec_tag(raw)) { stack[++sp] = {2, decode_vec(raw)}; }
            else { stack[++sp] = {0, setF(sanitize(raw))}; }
        } else if (base.type == 3 && global_array_capacity > 0) { 
            idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity; 
            stack[++sp] = {0, setF(sanitize(global_array_mem[idx]))};
        } else { stack[++sp] = {0, 0}; }
        BEEP();
    }
    
    L_OP_STORE_AT: {
        if (sp < 2) return 128;
        Val base = stack[sp--]; int32_t idx = (int32_t)getF(stack[sp--].v); Val val_to_s = stack[sp--];
        if (base.type == 2) { 
            int32_t m = base.v; int off = m >> 16, sz = m & 0xFFFF;
            if (sz > 0) idx = ((idx % sz) + sz) % sz;
            if (off + idx < 1024) local_array_mem[off + idx] = (val_to_s.type == 2) ? encode_vec(val_to_s.v) : sanitize(getF(val_to_s.v));
        } else if (base.type == 3 && global_array_capacity > 0) { 
            idx = ((idx % global_array_capacity) + global_array_capacity) % global_array_capacity;
            global_array_mem[idx] = sanitize(getF(val_to_s.v)); 
        }
        stack[++sp] = val_to_s; BEEP();
    }
    
    L_OP_SC_AND: {
        if (inst.val != 0) { if (sp >= 0) { if (getF(stack[sp].v) == 0.0f) pc += inst.val - 1; else sp--; } }
        else { if (sp >= 1) { if (getF(stack[sp-1].v) == 0.0f) sp--; else { stack[sp-1] = stack[sp]; sp--; } } }
        BEEP();
    }
    L_OP_SC_OR: {
        if (inst.val != 0) { if (sp >= 0) { if (getF(stack[sp].v) != 0.0f) pc += inst.val - 1; else sp--; } }
        else { if (sp >= 1) { if (getF(stack[sp-1].v) != 0.0f) sp--; else { stack[sp-1] = stack[sp]; sp--; } } }
        BEEP();
    }
    
    L_OP_NEG: stack[sp].v = setF(-getF(stack[sp].v)); BEEP();
    L_OP_NOT: stack[sp].v = setF(getF(stack[sp].v) == 0.0f ? 1.0f : 0.0f); BEEP();
    L_OP_BNOT: stack[sp].v = setF((float)(~(int32_t)getF(stack[sp].v))); BEEP();
    
    L_OP_SIN: { float v = getF(stack[sp].v); stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + sanitize(sinf(v/128.0f*M_PI))*127.0f : sanitize(sinf(v))); BEEP(); }
    L_OP_COS: { float v = getF(stack[sp].v); stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + sanitize(cosf(v/128.0f*M_PI))*127.0f : sanitize(cosf(v))); BEEP(); }
    L_OP_TAN: { float v = getF(stack[sp].v); stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + sanitize(tanf(v/128.0f*M_PI))*127.0f : sanitize(tanf(v))); BEEP(); }
    
    L_OP_SQRT:  stack[sp].v = setF(sanitize(getF(stack[sp].v) >= 0.0f ? sqrtf(getF(stack[sp].v)) : 0.0f)); BEEP();
    L_OP_LOG:   stack[sp].v = setF(sanitize(getF(stack[sp].v) > 0.0f ? logf(getF(stack[sp].v)) : 0.0f)); BEEP();
    L_OP_EXP:   stack[sp].v = setF(sanitize(expf(getF(stack[sp].v)))); BEEP();
    L_OP_ABS:   stack[sp].v = setF(fabsf(getF(stack[sp].v))); BEEP();
    L_OP_FLOOR: stack[sp].v = setF(floorf(getF(stack[sp].v))); BEEP();
    L_OP_CEIL:  stack[sp].v = setF(ceilf(getF(stack[sp].v))); BEEP();
    L_OP_ROUND: stack[sp].v = setF(roundf(getF(stack[sp].v))); BEEP();
    L_OP_CBRT:  stack[sp].v = setF(sanitize(cbrtf(getF(stack[sp].v)))); BEEP();
    L_OP_ASIN:  stack[sp].v = setF(sanitize(asinf(getF(stack[sp].v)))); BEEP();
    L_OP_ACOS:  stack[sp].v = setF(sanitize(acosf(getF(stack[sp].v)))); BEEP();
    L_OP_ATAN:  stack[sp].v = setF(sanitize(atanf(getF(stack[sp].v)))); BEEP();
    
    L_OP_COND: {
        if(sp < 2) return 128;
        Val f = stack[sp--], tv = stack[sp--], c = stack[sp--]; 
        Val tgt = (getF(c.v) != 0.0f) ? tv : f;
        if (tgt.type == 1) { if (csp < 511) { call_stack[++csp] = pc; pc = tgt.v - 1; } } 
        else { stack[++sp] = tgt; } BEEP(); 
    }
    
    L_OP_ADD: stack[sp-1].v = setF(sanitize(getF(stack[sp-1].v) + getF(stack[sp].v))); sp--; BEEP();
    L_OP_SUB: stack[sp-1].v = setF(sanitize(getF(stack[sp-1].v) - getF(stack[sp].v))); sp--; BEEP();
    L_OP_MUL: stack[sp-1].v = setF(sanitize(getF(stack[sp-1].v) * getF(stack[sp].v))); sp--; BEEP();
    L_OP_DIV: { float n = getF(stack[sp-1].v), d = getF(stack[sp].v); stack[sp-1].v = setF(d != 0.0f ? sanitize(n / d) : 0.0f); sp--; BEEP(); }
    L_OP_MOD: { float n = getF(stack[sp-1].v), d = getF(stack[sp].v); stack[sp-1].v = setF(d != 0.0f ? sanitize(fmodf(n, d)) : 0.0f); sp--; BEEP(); }
    
    L_OP_AND: stack[sp-1].v = setF((float)((int32_t)getF(stack[sp-1].v) & (int32_t)getF(stack[sp].v))); sp--; BEEP();
    L_OP_OR:  stack[sp-1].v = setF((float)((int32_t)getF(stack[sp-1].v) | (int32_t)getF(stack[sp].v))); sp--; BEEP();
    L_OP_XOR: stack[sp-1].v = setF((float)((int32_t)getF(stack[sp-1].v) ^ (int32_t)getF(stack[sp].v))); sp--; BEEP();
    L_OP_SHL: stack[sp-1].v = setF((float)((int32_t)getF(stack[sp-1].v) << (int32_t)getF(stack[sp].v))); sp--; BEEP();
    L_OP_SHR: stack[sp-1].v = setF((float)((int32_t)getF(stack[sp-1].v) >> (int32_t)getF(stack[sp].v))); sp--; BEEP();
    L_OP_LT:  stack[sp-1].v = setF(getF(stack[sp-1].v) < getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_GT:  stack[sp-1].v = setF(getF(stack[sp-1].v) > getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_EQ:  stack[sp-1].v = setF(getF(stack[sp-1].v) == getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_NEQ: stack[sp-1].v = setF(getF(stack[sp-1].v) != getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_LTE: stack[sp-1].v = setF(getF(stack[sp-1].v) <= getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_GTE: stack[sp-1].v = setF(getF(stack[sp-1].v) >= getF(stack[sp].v) ? 1.0f : 0.0f); sp--; BEEP();
    L_OP_MIN: stack[sp-1].v = setF(std::min(getF(stack[sp-1].v), getF(stack[sp].v))); sp--; BEEP();
    L_OP_MAX: stack[sp-1].v = setF(std::max(getF(stack[sp-1].v), getF(stack[sp].v))); sp--; BEEP();
    L_OP_POW: stack[sp-1].v = setF(sanitize(powf(getF(stack[sp-1].v), getF(stack[sp].v)))); sp--; BEEP();

    L_OP_ADD_ASSIGN: { float r = sanitize(getF(vars[inst.val].v) + getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SUB_ASSIGN: { float r = sanitize(getF(vars[inst.val].v) - getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_MUL_ASSIGN: { float r = sanitize(getF(vars[inst.val].v) * getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_DIV_ASSIGN: { float n = getF(vars[inst.val].v), d = getF(stack[sp].v); float r = (d != 0.0f ? sanitize(n / d) : 0.0f); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_MOD_ASSIGN: { float n = getF(vars[inst.val].v), d = getF(stack[sp].v); float r = (d != 0.0f ? sanitize(fmodf(n, d)) : 0.0f); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_AND_ASSIGN: { float r = (float)((int32_t)getF(vars[inst.val].v) & (int32_t)getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_OR_ASSIGN:  { float r = (float)((int32_t)getF(vars[inst.val].v) | (int32_t)getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_XOR_ASSIGN: { float r = (float)((int32_t)getF(vars[inst.val].v) ^ (int32_t)getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SHL_ASSIGN: { float r = (float)((int32_t)getF(vars[inst.val].v) << (int32_t)getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SHR_ASSIGN: { float r = (float)((int32_t)getF(vars[inst.val].v) >> (int32_t)getF(stack[sp].v)); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_POW_ASSIGN: { float r = sanitize(powf(getF(vars[inst.val].v), getF(stack[sp].v))); stack[sp].v = setF(r); vars[inst.val] = stack[sp]; BEEP(); }
    
    L_DEFAULT: BEEP();

    L_END:
    if (sp >= 0) {
        float out = 0.0f; bool is_bb = (current_play_mode == MODE_BYTEBEAT);
        if (stack[sp].type == 2 || stack[sp].type == 3) {
            int32_t meta = stack[sp].v; int off = meta >> 16, sz = meta & 0xFFFF;
            if (stack[sp].type == 2 && sz >= 2) {
                float L = off < 1024 ? local_array_mem[off] : 0.0f, R = off + 1 < 1024 ? local_array_mem[off + 1] : 0.0f;
                if (is_bb) return (uint8_t)((((int32_t)L & 255) + ((int32_t)R & 255)) / 2);
                else out = (L + R) / 2.0f;
            } else if (stack[sp].type == 2 && sz == 1) { out = off < 1024 ? local_array_mem[off] : 0.0f; }
            else { out = sz; }
        } else { out = getF(stack[sp].v); }
        out = sanitize(out);
        if (!is_bb) {
            if (out < -1.0f) out = -1.0f; if (out > 1.0f) out = 1.0f;
            return (uint8_t)((out + 1.0f) * 127.5f);
        } else { return (uint8_t)((int32_t)out & 255); }
    }
    return 128;
}
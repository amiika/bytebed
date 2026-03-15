#include "vm.h"
#include <math.h>
#include <algorithm>

Instruction program_bank[2][256];
int prog_len_bank[2] = {0, 0};
volatile uint8_t active_bank = 0;

Val vars[64];
String symNames[64];
int var_count = 0;

const MathFunc mathLibrary[] = {
    {"sin",  OP_SIN,  true},  {"cos",  OP_COS,  true},  {"tan",  OP_TAN,  true},
    {"sqrt", OP_SQRT, true},  {"log",  OP_LOG,  true},  {"exp",  OP_EXP,  true},
    {"min",  OP_MIN,  false}, {"max",  OP_MAX,  false}, {"pow",  OP_POW,  false}
};
const int mathLibrarySize = 9;

const OpInfo opList[] = {
    {"+",  OP_ADD, 7}, {"-",  OP_SUB, 7}, {"*",  OP_MUL, 8},
    {"/",  OP_DIV, 8}, {"%",  OP_MOD, 8}, {"&",  OP_AND, 3},
    {"|",  OP_OR,  1}, {"^",  OP_XOR, 2}, {"<",  OP_LT,  5},
    {">",  OP_GT,  5}, {"!",  OP_NOT, 9}, {"~",  OP_BNOT, 9}, 
    {"==", OP_EQ,  4}, {"!=", OP_NEQ, 4}, {"<=", OP_LTE, 5}, 
    {">=", OP_GTE, 5}, {"<<", OP_SHL, 6}, {">>", OP_SHR, 6},
    {"&&", OP_SC_AND, 3}, {"||", OP_SC_OR, 1}, {"**", OP_POW, 9}, 
    {"_",  OP_VEC, 10}, {"@", OP_AT, 10},
    {"+=", OP_ADD_ASSIGN, -1}, {"-=", OP_SUB_ASSIGN, -1},
    {"*=", OP_MUL_ASSIGN, -1}, {"/=", OP_DIV_ASSIGN, -1},
    {"%=", OP_MOD_ASSIGN, -1}, {"&=", OP_AND_ASSIGN, -1},
    {"|=", OP_OR_ASSIGN, -1},  {"^=", OP_XOR_ASSIGN, -1},
    {"<<=", OP_SHL_ASSIGN, -1},{">>=", OP_SHR_ASSIGN, -1},
    {"**=", OP_POW_ASSIGN, -1}
};
const int opListSize = 34;

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
    if (op == OP_ASSIGN_VAR || (op >= OP_ADD_ASSIGN && op <= OP_SHR_ASSIGN)) return -1;
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
    
    static Val stack[256]; int sp = -1;
    static int32_t call_stack[256]; int csp = -1;
    
    static Val shadow_val[256]; int ssp = -1;

    static const void* dispatch_table[] = {
        &&L_OP_VAL, &&L_OP_T, &&L_OP_LOAD, &&L_OP_STORE, &&L_OP_STORE_KEEP, &&L_OP_POP,
        &&L_OP_ADD, &&L_OP_SUB, &&L_OP_MUL, &&L_OP_DIV, &&L_OP_MOD, 
        &&L_OP_AND, &&L_OP_OR,  &&L_OP_XOR, &&L_OP_SHL, &&L_OP_SHR, 
        &&L_OP_LT,  &&L_OP_GT,  &&L_OP_EQ,  &&L_OP_NEQ, &&L_OP_LTE, &&L_OP_GTE,
        &&L_OP_COND, &&L_OP_NEG, &&L_OP_NOT, &&L_OP_BNOT,               
        &&L_OP_SIN, &&L_OP_COS, &&L_OP_TAN, &&L_OP_SQRT, &&L_OP_LOG, &&L_OP_EXP,
        &&L_OP_MIN, &&L_OP_MAX, &&L_OP_POW, 
        &&L_OP_JMP, &&L_OP_PUSH_FUNC, &&L_OP_DYN_CALL, &&L_OP_DYN_CALL_IF_FUNC, &&L_OP_RET,
        &&L_OP_BIND, &&L_OP_UNBIND, &&L_OP_ASSIGN_VAR, 
        &&L_OP_VEC, &&L_OP_AT, &&L_OP_SC_AND, &&L_OP_SC_OR, &&L_DEFAULT,
        &&L_OP_ADD_ASSIGN, &&L_OP_SUB_ASSIGN, &&L_OP_MUL_ASSIGN, &&L_OP_DIV_ASSIGN, &&L_OP_MOD_ASSIGN,
        &&L_OP_AND_ASSIGN, &&L_OP_OR_ASSIGN, &&L_OP_XOR_ASSIGN, &&L_OP_POW_ASSIGN, &&L_OP_SHL_ASSIGN, &&L_OP_SHR_ASSIGN
    };

    Instruction* prog = program_bank[bank];
    int pc = 0;
    Instruction inst = prog[pc];

    goto *dispatch_table[inst.op];

    #define BEEP() do { \
        if (++pc >= len) goto L_END; \
        inst = prog[pc]; \
        goto *dispatch_table[inst.op]; \
    } while(0)

    L_OP_VAL: stack[++sp] = {0, inst.val}; BEEP();
    L_OP_T:   stack[++sp] = {0, setF(t)}; BEEP();
    L_OP_LOAD: stack[++sp] = vars[inst.val]; BEEP();
    L_OP_STORE: if(sp>=0) vars[inst.val] = stack[sp--]; BEEP();
    L_OP_STORE_KEEP: if(sp>=0) vars[inst.val] = stack[sp]; BEEP();
    L_OP_POP: if(sp>=0) sp--; BEEP();
    L_OP_JMP: pc = inst.val - 1; BEEP();
    L_OP_PUSH_FUNC: stack[++sp] = {1, pc + 1}; pc = inst.val - 1; BEEP();
    L_OP_ASSIGN_VAR: BEEP();
    
    L_OP_DYN_CALL:
        if (stack[sp].type == 1) { 
            if (csp < 255) { call_stack[++csp] = pc; pc = stack[sp--].v - 1; }
            else sp--; 
        } else {
            int args = inst.val;
            if (sp >= args) sp -= (args + 1); else sp = -1;
            stack[++sp] = {0, 0}; 
        }
        BEEP();
        
    L_OP_DYN_CALL_IF_FUNC:
        if (stack[sp].type == 1) { 
            if (csp < 255) { call_stack[++csp] = pc; pc = stack[sp--].v - 1; }
            else sp--;
        }
        BEEP();
        
    L_OP_RET: if (csp >= 0) pc = call_stack[csp--]; else pc = len; BEEP();
    
    L_OP_BIND: 
        if (sp >= 0) {
            if (ssp < 255) shadow_val[++ssp] = vars[inst.val];
            vars[inst.val] = stack[sp--];
        }
        BEEP();
        
    L_OP_UNBIND: 
        if (ssp >= 0) vars[inst.val] = shadow_val[ssp--]; 
        BEEP();
    
    L_OP_VEC: 
        if (sp >= 0) stack[sp].type = 2; 
        BEEP();
        
    L_OP_AT: {
        if (sp < 1) return 128; 
        int32_t idx = (int32_t)getF(stack[sp--].v);
        int32_t size = (int32_t)getF(stack[sp--].v);
        if (size <= 0 || sp < size - 1) return 128; 
        idx = ((idx % size) + size) % size;
        int32_t res = stack[sp - size + 1 + idx].v; 
        sp -= size; 
        stack[++sp] = {0, res}; 
        BEEP();
    }
    
    // NEW: Dual-Mode Execution Logic for &&
    L_OP_SC_AND: {
        if (inst.val != 0) { // Infix Mode: Evaluate left and conditionally jump
            if (sp >= 0) {
                if (getF(stack[sp].v) == 0.0f) pc = inst.val - 1; 
                else sp--; 
            }
        } else { // RPN Mode: Both sides are evaluated, logically pop
            if (sp >= 1) {
                if (getF(stack[sp-1].v) == 0.0f) sp--; 
                else { stack[sp-1] = stack[sp]; sp--; }
            }
        }
        BEEP();
    }
    
    // NEW: Dual-Mode Execution Logic for ||
    L_OP_SC_OR: {
        if (inst.val != 0) { // Infix Mode
            if (sp >= 0) {
                if (getF(stack[sp].v) != 0.0f) pc = inst.val - 1; 
                else sp--; 
            }
        } else { // RPN Mode
            if (sp >= 1) {
                if (getF(stack[sp-1].v) != 0.0f) sp--; 
                else { stack[sp-1] = stack[sp]; sp--; }
            }
        }
        BEEP();
    }
    
    L_OP_NEG: stack[sp].v = setF(-getF(stack[sp].v)); BEEP();
    L_OP_NOT: stack[sp].v = setF(getF(stack[sp].v) == 0.0f ? 1.0f : 0.0f); BEEP();
    L_OP_BNOT: stack[sp].v = setF((float)(~(int32_t)getF(stack[sp].v))); BEEP();
    
    L_OP_SIN: {
        float val = getF(stack[sp].v);
        stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + sinf(val/128.0f*M_PI)*127.0f : sinf(val));
        BEEP();
    }
    L_OP_COS: {
        float val = getF(stack[sp].v);
        stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + cosf(val/128.0f*M_PI)*127.0f : cosf(val));
        BEEP();
    }
    L_OP_TAN: {
        float val = getF(stack[sp].v);
        stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + tanf(val/128.0f*M_PI)*127.0f : tanf(val));
        BEEP();
    }
    
    L_OP_SQRT: stack[sp].v = setF(getF(stack[sp].v) >= 0.0f ? sqrtf(getF(stack[sp].v)) : 0.0f); BEEP();
    L_OP_LOG:  stack[sp].v = setF(getF(stack[sp].v) > 0.0f ? logf(getF(stack[sp].v)) : 0.0f); BEEP();
    L_OP_EXP:  stack[sp].v = setF(expf(getF(stack[sp].v))); BEEP();
    
    L_OP_COND: { 
        Val f = stack[sp--]; Val tv = stack[sp--]; Val c = stack[sp--]; 
        Val target = (getF(c.v) != 0.0f) ? tv : f;
        if (target.type == 1) { if (csp < 255) { call_stack[++csp] = pc; pc = target.v - 1; } } 
        else { stack[++sp] = target; }
        BEEP(); 
    }
    
    L_OP_ADD: stack[sp-1].v = setF(getF(stack[sp-1].v) + getF(stack[sp].v)); sp--; BEEP();
    L_OP_SUB: stack[sp-1].v = setF(getF(stack[sp-1].v) - getF(stack[sp].v)); sp--; BEEP();
    L_OP_MUL: stack[sp-1].v = setF(getF(stack[sp-1].v) * getF(stack[sp].v)); sp--; BEEP();
    
    L_OP_DIV: {
        float n = getF(stack[sp-1].v), d = getF(stack[sp].v);
        if (d != 0.0f) {
            if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) stack[sp-1].v = setF((float)((int32_t)n / (int32_t)d));
            else stack[sp-1].v = setF(n / d);
        } else stack[sp-1].v = setF(0.0f);
        sp--; BEEP();
    }
    L_OP_MOD: {
        float n = getF(stack[sp-1].v), d = getF(stack[sp].v);
        if (d != 0.0f) {
            if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) stack[sp-1].v = setF((float)((int32_t)n % (int32_t)d));
            else stack[sp-1].v = setF(fmodf(n, d));
        } else stack[sp-1].v = setF(0.0f);
        sp--; BEEP();
    }
    
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
    L_OP_POW: stack[sp-1].v = setF(powf(getF(stack[sp-1].v), getF(stack[sp].v))); sp--; BEEP();

    L_OP_ADD_ASSIGN: { stack[sp].v = setF(getF(vars[inst.val].v) + getF(stack[sp].v)); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SUB_ASSIGN: { stack[sp].v = setF(getF(vars[inst.val].v) - getF(stack[sp].v)); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_MUL_ASSIGN: { stack[sp].v = setF(getF(vars[inst.val].v) * getF(stack[sp].v)); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_DIV_ASSIGN: {
        float n = getF(vars[inst.val].v), d = getF(stack[sp].v);
        if (d != 0.0f) {
            if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) stack[sp].v = setF((float)((int32_t)n / (int32_t)d));
            else stack[sp].v = setF(n / d);
        } else stack[sp].v = setF(0.0f);
        vars[inst.val] = stack[sp]; BEEP();
    }
    L_OP_MOD_ASSIGN: {
        float n = getF(vars[inst.val].v), d = getF(stack[sp].v);
        if (d != 0.0f) {
            if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) stack[sp].v = setF((float)((int32_t)n % (int32_t)d));
            else stack[sp].v = setF(fmodf(n, d));
        } else stack[sp].v = setF(0.0f);
        vars[inst.val] = stack[sp]; BEEP();
    }
    L_OP_AND_ASSIGN: { stack[sp].v = setF((float)((int32_t)getF(vars[inst.val].v) & (int32_t)getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_OR_ASSIGN:  { stack[sp].v = setF((float)((int32_t)getF(vars[inst.val].v) | (int32_t)getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_XOR_ASSIGN: { stack[sp].v = setF((float)((int32_t)getF(vars[inst.val].v) ^ (int32_t)getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SHL_ASSIGN: { stack[sp].v = setF((float)((int32_t)getF(vars[inst.val].v) << (int32_t)getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_SHR_ASSIGN: { stack[sp].v = setF((float)((int32_t)getF(vars[inst.val].v) >> (int32_t)getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    L_OP_POW_ASSIGN: { stack[sp].v = setF(powf(getF(vars[inst.val].v), getF(stack[sp].v))); vars[inst.val] = stack[sp]; BEEP(); }
    
    L_DEFAULT: 
        BEEP();

    L_END:
    if (sp >= 0) {
        float out_val = 0.0f;
        bool is_bytebeat = (current_play_mode == MODE_BYTEBEAT);
        
        if (stack[sp].type == 2) {
            int32_t size = (int32_t)getF(stack[sp].v);
            if (size > 0 && sp >= size) {
                if (size >= 2) {
                    float L = getF(stack[sp - size].v);
                    float R = getF(stack[sp - size + 1].v);
                    if (is_bytebeat) {
                        return (uint8_t)((((int32_t)L & 255) + ((int32_t)R & 255)) / 2);
                    } else {
                        out_val = (L + R) / 2.0f;
                    }
                } else {
                    out_val = getF(stack[sp - size].v);
                }
            }
        } else {
            out_val = getF(stack[sp].v);
        }

        if (!is_bytebeat) {
            if (out_val < -1.0f) out_val = -1.0f;
            if (out_val > 1.0f) out_val = 1.0f;
            return (uint8_t)((out_val + 1.0f) * 127.5f);
        } else {
            return (uint8_t)((int32_t)out_val & 255);
        }
    }
    return 128;
}
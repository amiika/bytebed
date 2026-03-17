#include "vm.h"
#include <math.h>
#include <algorithm>

bool validateProgram(uint8_t bank, int len) {
    if (len == 0 || len > 512) return false;
    for(int i=0; i<len; i++) {
        OpCode op = program_bank[bank][i].op;
        if (op == OP_JMP || op == OP_PUSH_FUNC || op == OP_SC_AND || op == OP_SC_OR) {
            int target_pc = i + program_bank[bank][i].val;
            if (program_bank[bank][i].val != 0 && (target_pc < 0 || target_pc > len)) return false;
        }
    }

    static Val v_stack[512]; int sp = -1;
    static int32_t c_stack[512]; int csp = -1;
    
    static Val l_vars[64];
    static Val l_shadow_val[512]; int l_ssp = -1;

    memset(l_vars, 0, sizeof(l_vars));

    int steps = 0;
    for (int pc = 0; pc < len && pc >= 0; pc++) {
        if (++steps > 8192) return false; 
        Instruction& inst = program_bank[bank][pc];
        switch (inst.op) {
            case OP_VAL: if (sp >= 510) return false; v_stack[++sp] = {0, inst.val}; break;
            case OP_T:   if (sp >= 510) return false; v_stack[++sp] = {0, setF(0.0f)}; break; 
            case OP_LOAD: if (sp >= 510) return false; v_stack[++sp] = l_vars[inst.val]; break;
            
            case OP_STORE: if(sp<0) return false; l_vars[inst.val] = v_stack[sp--]; break;
            case OP_STORE_KEEP: if(sp<0) return false; l_vars[inst.val] = v_stack[sp]; break;
                
            case OP_POP: if(sp<0) return false; sp--; break;
            case OP_JMP: pc += inst.val - 1; break; 
            case OP_PUSH_FUNC: if (sp >= 510) return false; v_stack[++sp] = {1, pc + 1}; pc += inst.val - 1; break; 
            case OP_DYN_CALL:
                if (sp<0) return false;
                if (v_stack[sp].type == 1) { 
                    if (csp < 511) { c_stack[++csp] = pc; pc = v_stack[sp--].v - 1; }
                    else sp--;
                } else {
                    int args = inst.val;
                    if (sp >= args) sp -= (args + 1); else sp = -1;
                    if (sp >= 510) return false;
                    v_stack[++sp] = {0, 0};
                }
                break;
            case OP_DYN_CALL_IF_FUNC:
                if (sp<0) return false;
                if (v_stack[sp].type == 1) { 
                    if (csp < 511) { c_stack[++csp] = pc; pc = v_stack[sp--].v - 1; }
                    else sp--;
                }
                break;
            case OP_RET:
                if (csp >= 0) pc = c_stack[csp--];
                else pc = len; 
                break;
                
            case OP_BIND:
                if (sp<0) return false;
                if (l_ssp < 511) l_shadow_val[++l_ssp] = l_vars[inst.val];
                l_vars[inst.val] = v_stack[sp--];
                break;
            case OP_UNBIND:
                if (l_ssp >= 0) l_vars[inst.val] = l_shadow_val[l_ssp--];
                break;
            
            case OP_VEC: {
                if (sp < 0) return false;
                int32_t size = (int32_t)getF(v_stack[sp].v);
                if (size >= 1 && sp >= size) {
                    sp -= size; 
                    v_stack[sp].type = 2; 
                }
                break; 
            }
            
            case OP_ALLOC: {
                if (sp < 0) return false;
                v_stack[sp].type = 3; 
                break;
            }
            
            case OP_AT: {
                if (sp < 1) return false;
                sp -= 2; 
                v_stack[++sp] = {0, 0}; 
                break;
            }
            
            case OP_STORE_AT: {
                if (sp < 2) return false;
                Val val = v_stack[sp - 2]; 
                sp -= 3; 
                v_stack[++sp] = val; 
                break;
            }
            
            case OP_SC_AND: {
                if (inst.val != 0) {
                    if (sp < 0) return false;
                    if (getF(v_stack[sp].v) == 0.0f) pc += inst.val - 1; 
                    else sp--;
                } else {
                    if (sp < 1) return false;
                    if (getF(v_stack[sp-1].v) == 0.0f) sp--;
                    else { v_stack[sp-1] = v_stack[sp]; sp--; }
                }
                break;
            }
            case OP_SC_OR: {
                if (inst.val != 0) {
                    if (sp < 0) return false;
                    if (getF(v_stack[sp].v) != 0.0f) pc += inst.val - 1; 
                    else sp--;
                } else {
                    if (sp < 1) return false;
                    if (getF(v_stack[sp-1].v) != 0.0f) sp--;
                    else { v_stack[sp-1] = v_stack[sp]; sp--; }
                }
                break;
            }

            case OP_COND: {
                if (sp<2) return false;
                Val f = v_stack[sp--]; Val tv = v_stack[sp--]; Val c = v_stack[sp--]; 
                Val target = (getF(c.v) != 0.0f) ? tv : f;
                if (target.type == 1) { if (csp < 511) { c_stack[++csp] = pc; pc = target.v - 1; } } 
                else { if (sp >= 510) return false; v_stack[++sp] = target; }
                break; 
            }
            case OP_NEG: if(sp<0) return false; v_stack[sp].v = setF(-getF(v_stack[sp].v)); break;
            case OP_NOT: if(sp<0) return false; v_stack[sp].v = setF(getF(v_stack[sp].v) == 0.0f ? 1.0f : 0.0f); break;
            case OP_BNOT: if(sp<0) return false; v_stack[sp].v = setF((float)(~(int32_t)getF(v_stack[sp].v))); break; 
            
            case OP_SIN: {
                if(sp<0) return false;
                float val = getF(v_stack[sp].v);
                v_stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + sinf(val/128.0f*M_PI)*127.0f : sinf(val));
                break;
            }
            case OP_COS: {
                if(sp<0) return false;
                float val = getF(v_stack[sp].v);
                v_stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + cosf(val/128.0f*M_PI)*127.0f : cosf(val));
                break;
            }
            case OP_TAN: {
                if(sp<0) return false;
                float val = getF(v_stack[sp].v);
                v_stack[sp].v = setF(current_play_mode == MODE_BYTEBEAT ? 128.0f + tanf(val/128.0f*M_PI)*127.0f : tanf(val));
                break;
            }
            
            case OP_ADD: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) + getF(v_stack[sp].v)); sp--; break;
            case OP_SUB: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) - getF(v_stack[sp].v)); sp--; break;
            case OP_MUL: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) * getF(v_stack[sp].v)); sp--; break;
            
            case OP_DIV: {
                if(sp<1) return false;
                float n = getF(v_stack[sp-1].v);
                float d = getF(v_stack[sp].v);
                if (d != 0.0f) {
                    if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) 
                        v_stack[sp-1].v = setF((float)((int32_t)n / (int32_t)d));
                    else 
                        v_stack[sp-1].v = setF(n / d);
                } else v_stack[sp-1].v = setF(0.0f);
                sp--; break;
            }
            case OP_MOD: {
                if(sp<1) return false;
                float n = getF(v_stack[sp-1].v);
                float d = getF(v_stack[sp].v);
                if (d != 0.0f) {
                    if (current_play_mode == MODE_BYTEBEAT && n == (int32_t)n && d == (int32_t)d) 
                        v_stack[sp-1].v = setF((float)((int32_t)n % (int32_t)d));
                    else 
                        v_stack[sp-1].v = setF(fmodf(n, d));
                } else v_stack[sp-1].v = setF(0.0f);
                sp--; break;
            }
            
            case OP_AND: if(sp<1) return false; v_stack[sp-1].v = setF((float)((int32_t)getF(v_stack[sp-1].v) & (int32_t)getF(v_stack[sp].v))); sp--; break;
            case OP_OR:  if(sp<1) return false; v_stack[sp-1].v = setF((float)((int32_t)getF(v_stack[sp-1].v) | (int32_t)getF(v_stack[sp].v))); sp--; break;
            case OP_XOR: if(sp<1) return false; v_stack[sp-1].v = setF((float)((int32_t)getF(v_stack[sp-1].v) ^ (int32_t)getF(v_stack[sp].v))); sp--; break;
            case OP_SHL: if(sp<1) return false; v_stack[sp-1].v = setF((float)((int32_t)getF(v_stack[sp-1].v) << (int32_t)getF(v_stack[sp].v))); sp--; break;
            case OP_SHR: if(sp<1) return false; v_stack[sp-1].v = setF((float)((int32_t)getF(v_stack[sp-1].v) >> (int32_t)getF(v_stack[sp].v))); sp--; break;
            case OP_LT:  if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) < getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_GT:  if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) > getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_EQ:  if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) == getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_NEQ: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) != getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_LTE: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) <= getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_GTE: if(sp<1) return false; v_stack[sp-1].v = setF(getF(v_stack[sp-1].v) >= getF(v_stack[sp].v) ? 1.0f : 0.0f); sp--; break;
            case OP_MIN: if(sp<1) return false; v_stack[sp-1].v = setF(std::min(getF(v_stack[sp-1].v), getF(v_stack[sp].v))); sp--; break;
            case OP_MAX: if(sp<1) return false; v_stack[sp-1].v = setF(std::max(getF(v_stack[sp-1].v), getF(v_stack[sp].v))); sp--; break;
            case OP_POW: if(sp<1) return false; v_stack[sp-1].v = setF(powf(getF(v_stack[sp-1].v), getF(v_stack[sp].v))); sp--; break;
            case OP_SQRT: if(sp<0) return false; v_stack[sp].v = setF(getF(v_stack[sp].v) >= 0.0f ? sqrtf(getF(v_stack[sp].v)) : 0.0f); break;
            case OP_LOG:  if(sp<0) return false; v_stack[sp].v = setF(getF(v_stack[sp].v) > 0.0f ? logf(getF(v_stack[sp].v)) : 0.0f); break;
            case OP_EXP:  if(sp<0) return false; v_stack[sp].v = setF(expf(getF(v_stack[sp].v))); break;
            case OP_ABS:   if(sp<0) return false; v_stack[sp].v = setF(fabsf(getF(v_stack[sp].v))); break;
            case OP_FLOOR: if(sp<0) return false; v_stack[sp].v = setF(floorf(getF(v_stack[sp].v))); break;
            case OP_CEIL:  if(sp<0) return false; v_stack[sp].v = setF(ceilf(getF(v_stack[sp].v))); break;
            case OP_ROUND: if(sp<0) return false; v_stack[sp].v = setF(roundf(getF(v_stack[sp].v))); break;
            
            case OP_ADD_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF(getF(l_vars[inst.val].v) + getF(v_stack[sp].v)); l_vars[inst.val] = v_stack[sp]; break;
            case OP_SUB_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF(getF(l_vars[inst.val].v) - getF(v_stack[sp].v)); l_vars[inst.val] = v_stack[sp]; break;
            case OP_MUL_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF(getF(l_vars[inst.val].v) * getF(v_stack[sp].v)); l_vars[inst.val] = v_stack[sp]; break;
            case OP_DIV_ASSIGN: {
                if (sp < 0) return false;
                float n = getF(l_vars[inst.val].v), d = getF(v_stack[sp].v);
                if (d != 0.0f) v_stack[sp].v = setF(n / d); else v_stack[sp].v = setF(0.0f);
                l_vars[inst.val] = v_stack[sp]; break;
            }
            case OP_MOD_ASSIGN: {
                if (sp < 0) return false;
                float n = getF(l_vars[inst.val].v), d = getF(v_stack[sp].v);
                if (d != 0.0f) v_stack[sp].v = setF(fmodf(n, d)); else v_stack[sp].v = setF(0.0f);
                l_vars[inst.val] = v_stack[sp]; break;
            }
            case OP_AND_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF((float)((int32_t)getF(l_vars[inst.val].v) & (int32_t)getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;
            case OP_OR_ASSIGN:  if (sp < 0) return false; v_stack[sp].v = setF((float)((int32_t)getF(l_vars[inst.val].v) | (int32_t)getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;
            case OP_XOR_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF((float)((int32_t)getF(l_vars[inst.val].v) ^ (int32_t)getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;
            case OP_SHL_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF((float)((int32_t)getF(l_vars[inst.val].v) << (int32_t)getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;
            case OP_SHR_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF((float)((int32_t)getF(l_vars[inst.val].v) >> (int32_t)getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;
            case OP_POW_ASSIGN: if (sp < 0) return false; v_stack[sp].v = setF(powf(getF(l_vars[inst.val].v), getF(v_stack[sp].v))); l_vars[inst.val] = v_stack[sp]; break;

            default: break;
        }
    }
    return sp >= 0;
}
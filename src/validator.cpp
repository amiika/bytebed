#include "vm.h"
#include <math.h>

bool validateProgram(uint8_t bank, int len) {
    if (len == 0 || len > 256) return false;
    for(int i=0; i<len; i++) {
        OpCode op = program_bank[bank][i].op;
        if (op == OP_JMP || op == OP_PUSH_FUNC) {
            if (program_bank[bank][i].val < 0 || program_bank[bank][i].val > len) return false;
        }
    }

    static Val v_stack[256]; int sp = -1;
    static int32_t c_stack[256]; int csp = -1;
    static Val l_vars[64][32];
    static int l_vsp[64];

    memset(l_vars, 0, sizeof(l_vars));
    memset(l_vsp, 0, sizeof(l_vsp));

    int steps = 0;
    for (int pc = 0; pc < len && pc >= 0; pc++) {
        if (++steps > 8192) return false; 
        Instruction& inst = program_bank[bank][pc];
        switch (inst.op) {
            case OP_VAL: if (sp >= 254) return false; v_stack[++sp] = {0, inst.val}; break;
            case OP_T:   if (sp >= 254) return false; v_stack[++sp] = {0, 0}; break; 
            case OP_LOAD: if (sp >= 254) return false; v_stack[++sp] = l_vars[inst.val][l_vsp[inst.val]]; break;
            case OP_STORE: if(sp<0) return false; l_vars[inst.val][l_vsp[inst.val]] = v_stack[sp--]; break;
            case OP_STORE_KEEP: if(sp<0) return false; l_vars[inst.val][l_vsp[inst.val]] = v_stack[sp]; break;
            case OP_POP: if(sp<0) return false; sp--; break;
            case OP_JMP: pc = inst.val - 1; break;
            case OP_PUSH_FUNC: if (sp >= 254) return false; v_stack[++sp] = {1, pc + 1}; pc = inst.val - 1; break;
            case OP_DYN_CALL:
                if (sp<0) return false;
                if (v_stack[sp].type == 1) { 
                    if (csp < 255) { c_stack[++csp] = pc; pc = v_stack[sp--].v - 1; }
                    else sp--;
                } else {
                    int args = inst.val;
                    if (sp >= args) sp -= (args + 1); else sp = -1;
                    if (sp >= 254) return false;
                    v_stack[++sp] = {0, 0};
                }
                break;
            case OP_DYN_CALL_IF_FUNC:
                if (sp<0) return false;
                if (v_stack[sp].type == 1) { 
                    if (csp < 255) { c_stack[++csp] = pc; pc = v_stack[sp--].v - 1; }
                    else sp--;
                }
                break;
            case OP_RET:
                if (csp >= 0) pc = c_stack[csp--];
                else pc = len; 
                break;
            case OP_BIND:
                if (sp<0) return false;
                if (l_vsp[inst.val] < 31) l_vsp[inst.val]++;
                l_vars[inst.val][l_vsp[inst.val]] = v_stack[sp--];
                break;
            case OP_UNBIND:
                if (l_vsp[inst.val] > 0) l_vsp[inst.val]--;
                break;
            
            case OP_VEC: break; 
            case OP_AT: {
                if (sp < 1) return false;
                sp--; // pop idx
                int32_t size = v_stack[sp--].v;
                if (size <= 0 || sp < size - 1) return false;
                sp -= size; // safely drop array
                v_stack[++sp] = {0, 0}; // push result
                break;
            }

            case OP_COND: {
                if (sp<2) return false;
                Val f = v_stack[sp--]; Val tv = v_stack[sp--]; Val c = v_stack[sp--]; 
                Val target = c.v ? tv : f;
                if (target.type == 1) { if (csp < 255) { c_stack[++csp] = pc; pc = target.v - 1; } } 
                else { if (sp >= 254) return false; v_stack[++sp] = target; }
                break; 
            }
            case OP_NEG: if(sp<0) return false; v_stack[sp].v = -v_stack[sp].v; break;
            case OP_NOT: if(sp<0) return false; v_stack[sp].v = !v_stack[sp].v; break;
            case OP_BNOT: if(sp<0) return false; v_stack[sp].v = ~v_stack[sp].v; break; 
            case OP_SIN: if(sp<0) return false; v_stack[sp].v = (int32_t)(128.0f + sinf(v_stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_COS: if(sp<0) return false; v_stack[sp].v = (int32_t)(128.0f + cosf(v_stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_TAN: if(sp<0) return false; v_stack[sp].v = (int32_t)(128.0f + tanf(v_stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_ADD: if(sp<1) return false; v_stack[sp-1].v += v_stack[sp].v; sp--; break;
            case OP_SUB: if(sp<1) return false; v_stack[sp-1].v -= v_stack[sp].v; sp--; break;
            case OP_MUL: if(sp<1) return false; v_stack[sp-1].v *= v_stack[sp].v; sp--; break;
            case OP_DIV: if(sp<1) return false; v_stack[sp-1].v = v_stack[sp].v ? v_stack[sp-1].v / v_stack[sp].v : 0; sp--; break;
            case OP_MOD: if(sp<1) return false; v_stack[sp-1].v = v_stack[sp].v ? v_stack[sp-1].v % v_stack[sp].v : 0; sp--; break;
            case OP_AND: if(sp<1) return false; v_stack[sp-1].v &= v_stack[sp].v; sp--; break;
            case OP_OR:  if(sp<1) return false; v_stack[sp-1].v |= v_stack[sp].v; sp--; break;
            case OP_XOR: if(sp<1) return false; v_stack[sp-1].v ^= v_stack[sp].v; sp--; break;
            case OP_SHL: if(sp<1) return false; v_stack[sp-1].v <<= v_stack[sp].v; sp--; break;
            case OP_SHR: if(sp<1) return false; v_stack[sp-1].v >>= v_stack[sp].v; sp--; break;
            case OP_LT:  if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v < v_stack[sp].v); sp--; break;
            case OP_GT:  if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v > v_stack[sp].v); sp--; break;
            case OP_EQ:  if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v == v_stack[sp].v); sp--; break;
            case OP_NEQ: if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v != v_stack[sp].v); sp--; break;
            case OP_LTE: if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v <= v_stack[sp].v); sp--; break;
            case OP_GTE: if(sp<1) return false; v_stack[sp-1].v = (v_stack[sp-1].v >= v_stack[sp].v); sp--; break;
            case OP_MIN: if(sp<1) return false; v_stack[sp-1].v = std::min(v_stack[sp-1].v, v_stack[sp].v); sp--; break;
            case OP_MAX: if(sp<1) return false; v_stack[sp-1].v = std::max(v_stack[sp-1].v, v_stack[sp].v); sp--; break;
            case OP_POW: if(sp<1) return false; v_stack[sp-1].v = (int32_t)powf((float)v_stack[sp-1].v, (float)v_stack[sp].v); sp--; break;
            case OP_SQRT: if(sp<0) return false; v_stack[sp].v = v_stack[sp].v >= 0 ? (int32_t)sqrtf((float)v_stack[sp].v) : 0; break;
            case OP_LOG:  if(sp<0) return false; v_stack[sp].v = v_stack[sp].v > 0 ? (int32_t)logf((float)v_stack[sp].v) : 0; break;
            case OP_EXP:  if(sp<0) return false; v_stack[sp].v = (int32_t)expf((float)v_stack[sp].v); break;
            default: break;
        }
    }
    return sp >= 0;
}
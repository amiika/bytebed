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
        if (++steps > 16384) return true;
        Instruction& inst = program_bank[bank][pc];
        
        switch (inst.op) {
            case OP_VAL: case OP_T: 
                if (sp >= 511) return false;
                v_stack[++sp] = {0, 0}; break;
            
            case OP_LOAD: 
                if (sp >= 511) return false;
                v_stack[++sp] = l_vars[inst.val]; break;
            
            case OP_STORE: 
                if (sp < 0) return false; 
                l_vars[inst.val] = v_stack[sp--]; break;
            
            case OP_STORE_KEEP: case OP_ASSIGN_VAR:
                if (sp < 0) return false; 
                l_vars[inst.val] = v_stack[sp]; break;
                
            case OP_POP: if (sp >= 0) sp--; break;
            case OP_JMP: pc += inst.val - 1; break; 
            
            case OP_PUSH_FUNC: 
                if (sp >= 511) return false;
                v_stack[++sp] = {1, pc + 1}; 
                pc += inst.val - 1; break; 
            
            case OP_DYN_CALL:
                if (sp < 0) return false;
                if (v_stack[sp].type == 1 && csp < 5) { 
                    if (csp < 511) { 
                        c_stack[++csp] = pc; 
                        pc = v_stack[sp--].v - 1; 
                    } else return false;
                } else {
                    int args = inst.val;
                    if (sp >= args) sp -= args; 
                    v_stack[sp] = {0, 0}; 
                }
                break;

            case OP_DYN_CALL_IF_FUNC:
                if (sp >= 0 && v_stack[sp].type == 1 && csp < 5) { 
                    c_stack[++csp] = pc; 
                    pc = v_stack[sp--].v - 1; 
                }
                break;

            case OP_RET:
                if (csp >= 0) pc = c_stack[csp--];
                else pc = len; break;
                
            case OP_BIND:
                if (sp < 0) return false;
                if (l_ssp < 511) l_shadow_val[++l_ssp] = l_vars[inst.val];
                l_vars[inst.val] = v_stack[sp--]; break;

            case OP_UNBIND:
                if (l_ssp >= 0) l_vars[inst.val] = l_shadow_val[l_ssp--]; break;
            
            case OP_VEC: 
                sp = std::max(-1, sp - 1);
                if (sp >= 0) v_stack[sp].type = 2; break; 
            
            case OP_ALLOC: if (sp >= 0) v_stack[sp].type = 3; break;
            case OP_AT: if (sp >= 1) { sp--; v_stack[sp].type = 0; } break;
            case OP_STORE_AT: if (sp >= 2) { sp -= 2; v_stack[sp].type = 0; } break;
            
            case OP_SC_AND: case OP_SC_OR:
                if (inst.val != 0) { if (sp >= 0) sp--; } break;

            case OP_COND: if (sp >= 2) sp -= 2; break;

            case OP_NEG: case OP_NOT: case OP_BNOT:
            case OP_SIN: case OP_COS: case OP_TAN: 
            case OP_SQRT: case OP_LOG: case OP_EXP:
            case OP_ABS: case OP_FLOOR: case OP_CEIL: case OP_ROUND:
            case OP_CBRT: case OP_ASIN: case OP_ACOS: case OP_ATAN:
                if (sp < 0) { /* pass */ } break;

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
            case OP_LT: case OP_GT: case OP_EQ: case OP_NEQ: case OP_LTE: case OP_GTE:
            case OP_MIN: case OP_MAX: case OP_POW:
                if (sp >= 1) sp--; break;

            default: break;
        }
    }
    return true; 
}
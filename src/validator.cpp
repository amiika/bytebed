#include "vm.h"
#include <math.h>
#include <algorithm>
#include <memory>

String last_vm_error = "";

#if defined(__EMSCRIPTEN__) || defined(NATIVE_BUILD)
    #define V_ERR(shortMsg, longMsg) do { last_vm_error = String(longMsg); return false; } while(0)
#else
    #define V_ERR(shortMsg, longMsg) do { last_vm_error = String(shortMsg); return false; } while(0)
#endif

/**
 * Validates a compiled virtual machine program to ensure memory and stack safety.
 * @param bank The program bank index to validate
 * @param len The length of the compiled program
 * @return true if the program is valid and safe to execute, false otherwise
 */
bool validateProgram(uint8_t bank, int len) {
    last_vm_error = "";
    
    if (len == 0) return true; 
    
    if (len > 512) V_ERR("ERR: PROG>512", "Compilation failed: Program exceeds 512 instructions.");
    
    for(int i=0; i<len; i++) {
        OpCode op = program_bank[bank][i].op;
        if (op == OP_JMP || op == OP_PUSH_FUNC || op == OP_SC_AND || op == OP_SC_OR || op == OP_LOOP_EVAL || op == OP_LOOP_DONE) {
            int target_pc = i + program_bank[bank][i].val;
            if (op == OP_LOOP_DONE) target_pc = i - program_bank[bank][i].val;
            if (program_bank[bank][i].val != 0 && (target_pc < 0 || target_pc > len)) {
                V_ERR("ERR: JMP @" + String(i), "Fatal: Jump target out of bounds at PC " + String(i));
            }
        }
    }

    std::unique_ptr<Val[]> v_stack(new Val[512]); int sp = -1;
    std::unique_ptr<int32_t[]> c_stack(new int32_t[512]); int csp = -1;
    std::unique_ptr<Val[]> l_vars(new Val[64]);
    std::unique_ptr<Val[]> l_shadow_val(new Val[512]); int l_ssp = -1;
    
    int v_loop_pcs[16];
    int v_loop_types[16];
    int v_loop_ptr = -1;

    memset(l_vars.get(), 0, 64 * sizeof(Val));

    int steps = 0;
    for (int pc = 0; pc < len && pc >= 0; pc++) {
        if (++steps > 16384) return true;
        Instruction& inst = program_bank[bank][pc];
        
        switch (inst.op) {
            case OP_VAL: case OP_T: 
                if (sp >= 511) V_ERR("ERR: OVF @" + String(pc), "Stack Overflow pushing value at PC " + String(pc));
                sp++; v_stack[sp].type = 0; v_stack[sp].f = 0.0f; 
                break;
            
            case OP_LOAD: 
                if (inst.val < 0 || inst.val >= 64) V_ERR("ERR: VAR @" + String(pc), "Invalid variable ID loaded at PC " + String(pc));
                if (sp >= 511) V_ERR("ERR: OVF @" + String(pc), "Stack Overflow loading variable at PC " + String(pc));
                v_stack[++sp] = l_vars[inst.val]; break;
            
            case OP_STORE: 
                if (inst.val < 0 || inst.val >= 64) V_ERR("ERR: VAR @" + String(pc), "Invalid variable ID stored at PC " + String(pc));
                if (sp < 0) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow storing variable at PC " + String(pc));
                l_vars[inst.val] = v_stack[sp--]; break;
            
            case OP_STORE_KEEP: case OP_ASSIGN_VAR:
            case OP_ADD_ASSIGN: case OP_SUB_ASSIGN: case OP_MUL_ASSIGN: 
            case OP_DIV_ASSIGN: case OP_MOD_ASSIGN: case OP_AND_ASSIGN: 
            case OP_OR_ASSIGN:  case OP_XOR_ASSIGN: case OP_SHL_ASSIGN: 
            case OP_SHR_ASSIGN: case OP_POW_ASSIGN:
                if (inst.val < 0 || inst.val >= 64) V_ERR("ERR: VAR @" + String(pc), "Invalid variable ID assigned at PC " + String(pc));
                if (sp < 0) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow assigning variable at PC " + String(pc));
                l_vars[inst.val] = v_stack[sp]; break;
                
            case OP_POP: if (sp >= 0) sp--; break;
            case OP_JMP: pc += inst.val - 1; break; 
            
            case OP_PUSH_FUNC: 
                if (sp >= 511) V_ERR("ERR: OVF @" + String(pc), "Stack Overflow pushing function at PC " + String(pc));
                sp++; v_stack[sp].type = 1; v_stack[sp].v = (uint32_t)(pc + 1); 
                pc += inst.val - 1; break; 
            
            case OP_DYN_CALL:
                if (sp < 0) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow on dynamic call at PC " + String(pc));
                if (v_stack[sp].type == 1 && csp < 5) { 
                    if (csp < 511) { 
                        c_stack[++csp] = pc; 
                        pc = v_stack[sp].v - 1; 
                        sp--;
                    } else V_ERR("ERR: CALL_OVF @" + String(pc), "Call Stack Overflow at PC " + String(pc));
                } else {
                    int args = inst.val;
                    if (sp >= args) sp -= args; 
                    v_stack[sp].type = 0; v_stack[sp].f = 0.0f;
                }
                break;

            case OP_DYN_CALL_IF_FUNC:
                if (sp >= 0 && v_stack[sp].type == 1 && csp < 5) { 
                    c_stack[++csp] = pc; 
                    pc = v_stack[sp].v - 1; 
                    sp--;
                }
                break;

            case OP_RET:
                if (csp >= 0) pc = c_stack[csp--];
                else pc = len; break;
                
            case OP_BIND:
                if (inst.val < 0 || inst.val >= 64) V_ERR("ERR: VAR @" + String(pc), "Invalid bind ID at PC " + String(pc));
                if (sp < 0) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow binding variable at PC " + String(pc));
                if (l_ssp < 511) l_shadow_val[++l_ssp] = l_vars[inst.val];
                l_vars[inst.val] = v_stack[sp--]; break;

            case OP_UNBIND:
                if (inst.val < 0 || inst.val >= 64) V_ERR("ERR: VAR @" + String(pc), "Invalid unbind ID at PC " + String(pc));
                if (l_ssp >= 0) l_vars[inst.val] = l_shadow_val[l_ssp--]; break;
            
            case OP_VEC: 
                sp = std::max(-1, sp - 1);
                if (sp >= 0) v_stack[sp].type = 2; break; 
            
            case OP_ALLOC: if (sp >= 0) v_stack[sp].type = 3; break;
            case OP_AT: if (sp >= 1) { sp--; v_stack[sp].type = 0; } break;
            case OP_STORE_AT: if (sp >= 2) { sp -= 2; v_stack[sp].type = 0; } break;
            
            case OP_LOOP_PREP: {
                if (sp < 1) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow on loop prep at PC " + String(pc));
                if (v_stack[sp].type == 1) {
                    if (v_loop_ptr < 15) {
                        v_loop_ptr++;
                        v_loop_pcs[v_loop_ptr] = v_stack[sp].v;
                        v_loop_types[v_loop_ptr] = inst.val;
                    }
                } else {
                    if (v_loop_ptr < 15) {
                        v_loop_ptr++;
                        v_loop_pcs[v_loop_ptr] = -1;
                        v_loop_types[v_loop_ptr] = inst.val;
                    }
                }
                sp -= 2; 
                break;
            }
            
            case OP_LOOP_EVAL: {
                sp++; 
                if (v_loop_ptr >= 0 && v_loop_pcs[v_loop_ptr] != -1) {
                    v_stack[sp].type = 0; 
                    if (csp < 511) {
                        c_stack[++csp] = pc; 
                        pc = v_loop_pcs[v_loop_ptr] - 1; 
                        v_loop_pcs[v_loop_ptr] = -1; 
                    } else V_ERR("ERR: CALL_OVF @" + String(pc), "Call Stack Overflow on loop eval at PC " + String(pc));
                } else {
                    int ltype = (v_loop_ptr >= 0) ? v_loop_types[v_loop_ptr] : 0;
                    v_stack[sp].type = (ltype == 0 || ltype == 3) ? 0 : 2; 
                }
                break;
            }
                
            case OP_LOOP_DONE: {
                if (sp < 0) V_ERR("ERR: UDF @" + String(pc), "Stack Underflow on loop done at PC " + String(pc));
                sp--; 
                int ltype = (v_loop_ptr >= 0) ? v_loop_types[v_loop_ptr] : 0;
                sp++; 
                if (ltype == 0 || ltype == 3) { v_stack[sp].type = 0; }
                else { v_stack[sp].type = 2; } 
                if (v_loop_ptr >= 0) v_loop_ptr--; 
                break;
            }

            case OP_SC_AND: case OP_SC_OR:
                if (inst.val != 0) { if (sp >= 0) sp--; } break;

            case OP_COND: if (sp >= 2) sp -= 2; break;

            case OP_NEG: case OP_NOT: case OP_BNOT:
            case OP_SIN: case OP_COS: case OP_TAN: 
            case OP_SQRT: case OP_LOG: case OP_EXP:
            case OP_ABS: case OP_FLOOR: case OP_CEIL: case OP_ROUND:
            case OP_CBRT: case OP_ASIN: case OP_ACOS: case OP_ATAN:
            case OP_INT: 
                if (sp < 0) { /* pass */ } break;

            case OP_RAND:
                if (sp >= 511) V_ERR("ERR: OVF @" + String(pc), "Stack Overflow on RAND at PC " + String(pc));
                sp++; v_stack[sp].type = 0; v_stack[sp].f = 0.0f; break;

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
#include "vm.h"
#include <string.h>
#include <vector>

String decompileRPN() {
    String out = "";
    int len = prog_len_bank[active_bank];
    if (len == 0) return "";
    Instruction* prog = program_bank[active_bank];
    
    int func_ends[256];
    int fe_ptr = -1;

    int sc_targets[256];
    OpCode sc_ops[256];
    int sc_ptr = -1;
    
    for (int pc = 0; pc < len; pc++) {
        Instruction inst = prog[pc];
        
        while (fe_ptr >= 0 && pc == func_ends[fe_ptr]) {
            out += "} ";
            fe_ptr--;
        }
        
        while (sc_ptr >= 0 && pc == sc_targets[sc_ptr]) {
            out += getOpSym(sc_ops[sc_ptr]) + " ";
            sc_ptr--;
        }

        switch (inst.op) {
            case OP_VAL: {
                float v = getF(inst.val);
                if (pc + 1 < len && prog[pc+1].op == OP_NEG) { v = -v; pc++; }
                out += String(v) + " "; 
                break;
            }
            case OP_T: {
                if (pc + 1 < len && prog[pc+1].op == OP_NEG) { out += "-t "; pc++; }
                else { out += "t "; }
                break;
            }
            case OP_LOAD: {
                int id = inst.val;
                bool is_func = false;
                
                for (int k = 0; k < len; k++) {
                    if ((prog[k].op == OP_STORE || prog[k].op == OP_STORE_KEEP || prog[k].op == OP_ASSIGN_VAR) && prog[k].val == id) {
                        if (k >= 1 && prog[k-1].op == OP_RET) is_func = true;
                        else is_func = false; 
                    }
                }
                
                bool is_call = (pc + 1 < len && (prog[pc+1].op == OP_DYN_CALL || prog[pc+1].op == OP_DYN_CALL_IF_FUNC));
                int neg_offset = is_call ? 2 : 1;
                bool has_neg = (pc + neg_offset < len && prog[pc+neg_offset].op == OP_NEG);
                
                String prefix = has_neg ? "-" : "";
                
                if (is_call) {
                    out += prefix + getVarName(id) + " ";
                    pc++; 
                    if (has_neg) pc++;
                } 
                else {
                    if (is_func) out += prefix + "&" + getVarName(id) + " ";
                    else {
                        out += prefix + getVarName(id) + " ";
                        if (has_neg) pc++;
                    }
                }
                break;
            }
            case OP_STORE: 
                out += getVarName(inst.val) + " = "; 
                break;
            case OP_STORE_KEEP:
            case OP_ASSIGN_VAR:
                if (pc + 1 < len && prog[pc+1].op == OP_POP) {
                    out += getVarName(inst.val) + " = ";
                    pc++; 
                } else {
                    out += getVarName(inst.val) + " := ";
                }
                break;
            case OP_ADD_ASSIGN: out += getVarName(inst.val) + " += "; break;
            case OP_SUB_ASSIGN: out += getVarName(inst.val) + " -= "; break;
            case OP_MUL_ASSIGN: out += getVarName(inst.val) + " *= "; break;
            case OP_DIV_ASSIGN: out += getVarName(inst.val) + " /= "; break;
            case OP_MOD_ASSIGN: out += getVarName(inst.val) + " %= "; break;
            case OP_AND_ASSIGN: out += getVarName(inst.val) + " &= "; break;
            case OP_OR_ASSIGN:  out += getVarName(inst.val) + " |= "; break;
            case OP_XOR_ASSIGN: out += getVarName(inst.val) + " ^= "; break;
            case OP_SHL_ASSIGN: out += getVarName(inst.val) + " <<= "; break;
            case OP_SHR_ASSIGN: out += getVarName(inst.val) + " >>= "; break;
            case OP_POW_ASSIGN: out += getVarName(inst.val) + " **= "; break;
            case OP_POP: 
                out += "; ";
                break;
            case OP_PUSH_FUNC: {
                int end_pc = pc + inst.val; 
                if (fe_ptr < 255) func_ends[++fe_ptr] = end_pc;
                
                String params[8]; int param_cnt = 0;
                int bind_pc = pc + 1;
                while (bind_pc < end_pc && prog[bind_pc].op == OP_BIND) {
                    if (param_cnt < 8) params[param_cnt++] = getVarName(prog[bind_pc].val);
                    bind_pc++;
                }
                
                if (param_cnt > 0) {
                    out += "(";
                    for (int i = param_cnt - 1; i >= 0; i--) {
                        out += params[i];
                        if (i > 0) out += " ";
                    }
                    out += ") { "; 
                } else {
                    out += "() { ";
                }
                pc = bind_pc - 1; 
                break;
            }
            case OP_DYN_CALL: 
                out += "call" + String((int)inst.val) + " ";
                break;
            case OP_DYN_CALL_IF_FUNC: 
                break;
            case OP_RET: 
            case OP_BIND: 
            case OP_UNBIND: 
                break;
            case OP_VEC:
                out += "_ ";
                break;
            case OP_ALLOC: 
                out += "$ ";
                break;
            case OP_AT:
                out += "@ "; 
                break;
            case OP_STORE_AT:
                out += "# ";
                break;
            case OP_LOOP_PREP:
                if (inst.val == 1) out += "gen ";
                else if (inst.val == 2) out += "map ";
                else if (inst.val == 3) out += "reduce ";
                else if (inst.val == 4) out += "filter ";
                else out += "sum ";
                break;
            case OP_LOOP_EVAL:
            case OP_LOOP_DONE:
                break;
            case OP_SC_AND:
            case OP_SC_OR:
                if (inst.val != 0) {
                    if (sc_ptr < 255) {
                        sc_targets[++sc_ptr] = pc + inst.val; 
                        sc_ops[sc_ptr] = inst.op;
                    }
                } else {
                    out += getOpSym(inst.op) + " ";
                }
                break;
            case OP_NEG:
                out += "-1 * ";
                break;
            case OP_NEQ:
                out += "!= ";
                break;
            default:
                String sym = getOpSym(inst.op);
                if (sym != "") out += sym + " ";
                break;
        }
    }
    
    while (fe_ptr >= 0) {
        out += "} ";
        fe_ptr--;
    }
    while (sc_ptr >= 0) {
        out += getOpSym(sc_ops[sc_ptr]) + " ";
        sc_ptr--;
    }

    out.trim();
    return out;
}

String decompileInfixRange(Instruction* prog, int start_pc, int end_pc) {
    String out = "";
    
    std::vector<String> stack(256);
    int prec_stack[256];
    int sp = -1;
    
    for (int pc = start_pc; pc < end_pc; pc++) {
        Instruction inst = prog[pc];
        
        if (inst.op == OP_VAL) {
            if (sp < 255) { stack[++sp] = String(getF(inst.val)); prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_T) {
            if (sp < 255) { stack[++sp] = "t"; prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_LOAD) {
            if (sp < 255) { stack[++sp] = getVarName(inst.val); prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_RAND) {
            if (sp < 255) { stack[++sp] = getOpSym(inst.op) + "()"; prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_STORE && sp >= 0) {
            String val = stack[sp]; sp--;
            if (out != "") out += ";";
            out += getVarName(inst.val) + "=" + val;
        }
        else if ((inst.op == OP_STORE_KEEP || inst.op == OP_ASSIGN_VAR || (inst.op >= OP_ADD_ASSIGN && inst.op <= OP_SHR_ASSIGN)) && sp >= 0) {
            String val = stack[sp]; sp--;
            
            String op_str = "=";
            if (inst.op == OP_ADD_ASSIGN) op_str = "+=";
            else if (inst.op == OP_SUB_ASSIGN) op_str = "-=";
            else if (inst.op == OP_MUL_ASSIGN) op_str = "*=";
            else if (inst.op == OP_DIV_ASSIGN) op_str = "/=";
            else if (inst.op == OP_MOD_ASSIGN) op_str = "%=";
            else if (inst.op == OP_AND_ASSIGN) op_str = "&=";
            else if (inst.op == OP_OR_ASSIGN) op_str = "|=";
            else if (inst.op == OP_XOR_ASSIGN) op_str = "^=";
            else if (inst.op == OP_POW_ASSIGN) op_str = "**=";
            else if (inst.op == OP_SHL_ASSIGN) op_str = "<<=";
            else if (inst.op == OP_SHR_ASSIGN) op_str = ">>=";

            if (sp < 255) { stack[++sp] = getVarName(inst.val) + op_str + val; prec_stack[sp] = -1; }
        }
        else if (inst.op == OP_STORE_AT && sp >= 2) {
            String val = stack[sp--]; 
            String idx = stack[sp--];  
            String base = stack[sp--];  
            if (sp < 255) { stack[++sp] = base + "[" + idx + "]=" + val; prec_stack[sp] = -1; }
        }
        else if (inst.op == OP_POP && sp >= 0) {
            String val = stack[sp]; sp--;
            if (out != "") out += ";";
            out += val;
        }
        else if (inst.op == OP_PUSH_FUNC) {
            int target_end = pc + inst.val; 
            String params[8]; int param_cnt = 0;
            int bind_pc = pc + 1;
            while (bind_pc < target_end && prog[bind_pc].op == OP_BIND) {
                if (param_cnt < 8) params[param_cnt++] = getVarName(prog[bind_pc].val);
                bind_pc++;
            }
            
            String func_str = "(";
            for (int i = param_cnt - 1; i >= 0; i--) {
                func_str += params[i];
                if (i > 0) func_str += ",";
            }
            func_str += ")=>{";
            
            int inner_start = bind_pc;
            int inner_end = target_end;
            
            if (inner_end > inner_start && prog[inner_end-1].op == OP_RET) inner_end--;
            
            func_str += decompileInfixRange(prog, inner_start, inner_end) + "}";
            
            if (sp < 255) { stack[++sp] = func_str; prec_stack[sp] = 10; }
            
            pc = target_end - 1;
        }
        else if (inst.op == OP_DYN_CALL && sp >= inst.val) {
            int args = inst.val;
            String func_name = stack[sp];
            
            String func_call = func_name + "(";
            for (int i = 0; i < args; i++) {
                func_call += stack[sp - args + i];
                if (i < args - 1) func_call += ",";
            }
            func_call += ")";
            
            sp -= (args + 1);
            if (sp < 255) { stack[++sp] = func_call; prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_DYN_CALL_IF_FUNC) {
        }
        else if (inst.op == OP_RET || inst.op == OP_BIND || inst.op == OP_UNBIND) {
        }
        else if (inst.op == OP_VEC && sp >= 0) {
            int size = stack[sp].toInt(); sp--;
            if (sp >= size - 1) {
                String vec_str = "[";
                for (int i = 0; i < size; i++) {
                    vec_str += stack[sp - size + 1 + i];
                    if (i < size - 1) vec_str += ",";
                }
                vec_str += "]";
                sp -= size;
                if (sp < 255) { stack[++sp] = vec_str; prec_stack[sp] = 10; }
            }
        }
        else if (inst.op == OP_ALLOC && sp >= 0) {
            String size = stack[sp]; sp--;
            if (sp < 255) { stack[++sp] = "$[" + size + "]"; prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_AT && sp >= 1) {
            String idx = stack[sp--];  
            String base = stack[sp--]; 
            if (sp < 255) { stack[++sp] = base + "[" + idx + "]"; prec_stack[sp] = 10; }
        }
        else if (inst.op == OP_LOOP_PREP && sp >= 1) {
            String f = stack[sp]; sp--;
            String count = stack[sp]; sp--;
            String fnName = "sum";
            if (inst.val == 1) fnName = "gen";
            else if (inst.val == 2) fnName = "map";
            else if (inst.val == 3) fnName = "reduce";
            else if (inst.val == 4) fnName = "filter";
            
            bool use_dot = true;
            if (isdigit(count[0]) || count[0] == '-' || count[0] == '(') use_dot = false; 
            if (inst.val == 1) use_dot = false; 
            
            if (use_dot) {
                if (sp < 255) { stack[++sp] = count + "." + fnName + "(" + f + ")"; prec_stack[sp] = 10; }
            } else {
                if (sp < 255) { stack[++sp] = fnName + "(" + count + "," + f + ")"; prec_stack[sp] = 10; }
            }
        }
        else if (inst.op == OP_LOOP_EVAL || inst.op == OP_LOOP_DONE) {
        }
        else if (inst.op == OP_COND && sp >= 2) {
            String f = stack[sp--]; 
            String tv = stack[sp--]; 
            String c = stack[sp--]; 
            
            if (tv.startsWith("()=>{") && tv.endsWith("}")) tv = tv.substring(5, tv.length() - 1);
            if (f.startsWith("()=>{") && f.endsWith("}")) f = f.substring(5, f.length() - 1);
            
            if (sp < 255) { stack[++sp] = c + "?" + tv + ":" + f; prec_stack[sp] = 0; }
        }
        else if (inst.op == OP_SC_AND || inst.op == OP_SC_OR) {
            if (inst.val != 0) {
                String left = "0"; int lp = 10;
                if (sp >= 0) { left = stack[sp]; lp = prec_stack[sp]; sp--; }
                
                int target_end = pc + inst.val; 
                String right = decompileInfixRange(prog, pc + 1, target_end);
                
                int op_p = getPrecedence(inst.op);
                String sym = (inst.op == OP_SC_AND) ? "&&" : "||";
                
                if (lp < op_p) left = "(" + left + ")";
                
                if (strstr(right.c_str(), sym.c_str()) != nullptr || strchr(right.c_str(), '?') != nullptr) {
                    right = "(" + right + ")"; 
                }
                
                if (sp < 255) { stack[++sp] = left + sym + right; prec_stack[sp] = op_p; }
                
                pc = target_end - 1;
            } else {
                if (sp >= 1) {
                    String right = stack[sp]; int rp = prec_stack[sp]; sp--;
                    String left = stack[sp]; int lp = prec_stack[sp]; sp--;
                    
                    int op_p = getPrecedence(inst.op);
                    String sym = getOpSym(inst.op);
                    
                    if (lp < op_p) left = "(" + left + ")";
                    if (rp <= op_p) right = "(" + right + ")";
                    if (sp < 255) { stack[++sp] = left + sym + right; prec_stack[sp] = op_p; }
                }
            }
        }
        else if ((inst.op >= OP_SIN && inst.op <= OP_ATAN || inst.op == OP_INT) && sp >= 0) { 
             String val = stack[sp--]; 
             String sym = getOpSym(inst.op);
             if (sp < 255) { stack[++sp] = sym + "(" + val + ")"; prec_stack[sp] = 10; }
        }
        else if (inst.op >= OP_MIN && inst.op <= OP_POW && sp >= 1) { 
             String right = stack[sp--]; 
             String left = stack[sp--]; 
             String sym = getOpSym(inst.op);
             if (sp < 255) { stack[++sp] = sym + "(" + left + "," + right + ")"; prec_stack[sp] = 10; }
        }
        else if ((inst.op == OP_NEG || inst.op == OP_NOT || inst.op == OP_BNOT) && sp >= 0) {
            String val = stack[sp]; 
            int p = prec_stack[sp];
            sp--;
            
            String sym = getOpSym(inst.op);
            if (inst.op == OP_NEG) sym = "-";
            
            if (p < 9 || val.startsWith("-")) val = "(" + val + ")";
            if (sp < 255) { stack[++sp] = sym + val; prec_stack[sp] = 9; }
        }
        else if (sp >= 1) {
            String right = stack[sp]; int rp = prec_stack[sp]; sp--;
            String left = stack[sp]; int lp = prec_stack[sp]; sp--;
            
            int op_p = getPrecedence(inst.op);
            String sym = getOpSym(inst.op);
            
            if (inst.op == OP_MUL && right == "-1") {
                if (lp < 9 || left.startsWith("-")) left = "(" + left + ")";
                if (sp < 255) { stack[++sp] = "-" + left; prec_stack[sp] = 9; }
            }
            else if (inst.op == OP_MUL && left == "-1") {
                if (rp < 9 || right.startsWith("-")) right = "(" + right + ")";
                if (sp < 255) { stack[++sp] = "-" + right; prec_stack[sp] = 9; }
            }
            else {
                if (lp < op_p) left = "(" + left + ")";
                if (rp <= op_p) right = "(" + right + ")";
                if (sp < 255) { stack[++sp] = left + sym + right; prec_stack[sp] = op_p; }
            }
        }
    }
    
    String assignments = "";
    String orphaned_args[64]; int oa_cnt = 0;
    
    for (int i = 0; i <= sp; i++) {
        if (prec_stack[i] == -1) {
            if (assignments != "") assignments += ";";
            assignments += stack[i];
        } else {
            if (oa_cnt < 64) orphaned_args[oa_cnt++] = stack[i];
        }
    }
    
    String final_out = out;
    if (assignments != "") {
        if (final_out != "") final_out += ";";
        final_out += assignments;
    }
    
    if (oa_cnt >= 1) {
        String call = orphaned_args[oa_cnt - 1];
        if (oa_cnt > 1) {
            call += "(";
            for (int i = 0; i < oa_cnt - 1; i++) {
                call += orphaned_args[i];
                if (i < oa_cnt - 2) call += ",";
            }
            call += ")";
        }
        if (final_out != "") final_out += ";";
        final_out += call;
    }
    
    return final_out;
}

String decompileInfix() {
    return decompileInfixRange(program_bank[active_bank], 0, prog_len_bank[active_bank]);
}

String decompile(bool to_rpn) {
    if (prog_len_bank[active_bank] == 0) return "";
    return to_rpn ? decompileRPN() : decompileInfix();
}
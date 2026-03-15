#include "vm.h"
#include <vector>

String decompileRPN() {
    String out = "";
    Instruction* prog = program_bank[active_bank];
    int len = prog_len_bank[active_bank];
    
    std::vector<int> func_ends;
    
    for (int pc = 0; pc < len; pc++) {
        Instruction inst = prog[pc];
        
        while (!func_ends.empty() && pc == func_ends.back()) {
            out += "} ";
            func_ends.pop_back();
        }

        switch (inst.op) {
            case OP_VAL: {
                float v = getF(inst.val);
                // Lookahead: Collapse OP_NEG into a unary prefix
                if (pc + 1 < len && prog[pc+1].op == OP_NEG) {
                    v = -v;
                    pc++;
                }
                out += String(v) + " "; 
                break;
            }
            case OP_T: {
                // Lookahead: Collapse OP_NEG into `-t`
                if (pc + 1 < len && prog[pc+1].op == OP_NEG) {
                    out += "-t "; 
                    pc++;
                } else {
                    out += "t "; 
                }
                break;
            }
            case OP_LOAD: {
                int id = inst.val;
                bool is_func = false;
                
                for (int k = 0; k < len; k++) {
                    if ((prog[k].op == OP_STORE || prog[k].op == OP_STORE_KEEP || prog[k].op == OP_ASSIGN_VAR) && prog[k].val == id) {
                        if (k >= 1 && prog[k-1].op == OP_RET) is_func = true;
                    }
                }
                
                bool is_call = (pc + 1 < len && (prog[pc+1].op == OP_DYN_CALL || prog[pc+1].op == OP_DYN_CALL_IF_FUNC));
                int neg_offset = is_call ? 2 : 1;
                bool has_neg = (pc + neg_offset < len && prog[pc+neg_offset].op == OP_NEG);
                
                String prefix = has_neg ? "-" : "";
                
                if (is_call) {
                    out += prefix + getVarName(id) + " ";
                    pc++; // skip DYN_CALL
                    if (has_neg) pc++; // skip OP_NEG
                } 
                else {
                    if (is_func) {
                        out += "{" + getVarName(id) + "} "; 
                    } else {
                        out += prefix + getVarName(id) + " ";
                        if (has_neg) pc++; // skip OP_NEG
                    }
                }
                break;
            }
            case OP_STORE: 
            case OP_STORE_KEEP:
            case OP_ASSIGN_VAR:
                out += getVarName(inst.val) + " = "; 
                break;
            case OP_POP: 
                // Silent in RPN
                break; 
            case OP_PUSH_FUNC: {
                int end_pc = inst.val;
                func_ends.push_back(end_pc);
                
                std::vector<String> params;
                int bind_pc = pc + 1;
                while (bind_pc < end_pc && prog[bind_pc].op == OP_BIND) {
                    params.push_back(getVarName(prog[bind_pc].val));
                    bind_pc++;
                }
                
                if (params.size() > 0) {
                    out += "(";
                    for (int i = params.size() - 1; i >= 0; i--) {
                        out += params[i];
                        if (i > 0) out += " ";
                    }
                    out += ") {"; 
                } else {
                    out += "() {";
                }
                pc = bind_pc - 1; 
                break;
            }
            case OP_DYN_CALL: 
            case OP_DYN_CALL_IF_FUNC: 
                break;
            case OP_RET: 
            case OP_BIND: 
            case OP_UNBIND: 
                break;
            case OP_VEC:
                out += "_ ";
                break;
            case OP_AT:
                out += "@ "; 
                break;
            case OP_NEG:
                // FALLBACK: Complex expressions mathematically negate by multiplying by -1
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
    
    while (!func_ends.empty()) {
        out += "} "; 
        func_ends.pop_back();
    }

    out.trim();
    return out;
}

String decompileInfixRange(Instruction* prog, int start_pc, int end_pc) {
    String out = "";
    std::vector<String> stack;
    std::vector<int> prec_stack;
    
    for (int pc = start_pc; pc < end_pc; pc++) {
        Instruction inst = prog[pc];
        
        if (inst.op == OP_VAL) {
            stack.push_back(String(getF(inst.val)));
            prec_stack.push_back(10);
        }
        else if (inst.op == OP_T) {
            stack.push_back("t");
            prec_stack.push_back(10);
        }
        else if (inst.op == OP_LOAD) {
            stack.push_back(getVarName(inst.val));
            prec_stack.push_back(10);
        }
        else if ((inst.op == OP_STORE || inst.op == OP_STORE_KEEP || inst.op == OP_ASSIGN_VAR) && stack.size() >= 1) {
            String val = stack.back(); stack.pop_back(); prec_stack.pop_back();
            stack.push_back(getVarName(inst.val) + " = " + val);
            prec_stack.push_back(-1);
        }
        else if (inst.op == OP_POP && stack.size() >= 1) {
            String val = stack.back(); stack.pop_back(); prec_stack.pop_back();
            if (out != "") out += "; ";
            out += val;
        }
        else if (inst.op == OP_PUSH_FUNC) {
            int target_end = inst.val;
            std::vector<String> params;
            int bind_pc = pc + 1;
            while (bind_pc < target_end && prog[bind_pc].op == OP_BIND) {
                params.push_back(getVarName(prog[bind_pc].val));
                bind_pc++;
            }
            
            String func_str = "(";
            for (int i = params.size() - 1; i >= 0; i--) {
                func_str += params[i];
                if (i > 0) func_str += ", ";
            }
            func_str += ") => { ";
            
            int inner_start = bind_pc;
            int inner_end = target_end;
            
            if (inner_end > inner_start && prog[inner_end-1].op == OP_RET) inner_end--;
            
            func_str += decompileInfixRange(prog, inner_start, inner_end) + " }";
            
            stack.push_back(func_str);
            prec_stack.push_back(10);
            
            pc = target_end - 1;
        }
        else if (inst.op == OP_DYN_CALL && stack.size() >= inst.val + 1) {
            int args = inst.val;
            
            // Explicitly pops the function pointer safely from the top of the stack
            String func_name = stack.back();
            stack.pop_back(); 
            prec_stack.pop_back();
            
            String func_call = func_name + "(";
            for (int i = 0; i < args; i++) {
                func_call += stack[stack.size() - args + i];
                if (i < args - 1) func_call += ", ";
            }
            func_call += ")";
            
            for (int i = 0; i < args; i++) { 
                stack.pop_back(); 
                prec_stack.pop_back(); 
            }
            
            stack.push_back(func_call);
            prec_stack.push_back(10);
        }
        else if (inst.op == OP_DYN_CALL_IF_FUNC) {
        }
        else if (inst.op == OP_RET || inst.op == OP_BIND || inst.op == OP_UNBIND) {
        }
        else if (inst.op == OP_VEC && stack.size() >= 1) {
            int size = stack.back().toInt();
            stack.pop_back(); prec_stack.pop_back();
            if (stack.size() >= size) {
                String vec_str = "[";
                for (int i = 0; i < size; i++) {
                    vec_str += stack[stack.size() - size + i];
                    if (i < size - 1) vec_str += ", ";
                }
                vec_str += "]";
                for (int i = 0; i < size; i++) { stack.pop_back(); prec_stack.pop_back(); }
                stack.push_back(vec_str);
                prec_stack.push_back(10);
            }
        }
        else if (inst.op == OP_AT && stack.size() >= 2) {
            String idx = stack.back(); stack.pop_back(); prec_stack.pop_back();
            String vec = stack.back(); stack.pop_back(); prec_stack.pop_back();
            stack.push_back(vec + "[" + idx + "]");
            prec_stack.push_back(10);
        }
        else if (inst.op == OP_COND && stack.size() >= 3) {
            String f = stack.back(); stack.pop_back(); prec_stack.pop_back();
            String tv = stack.back(); stack.pop_back(); prec_stack.pop_back();
            String c = stack.back(); stack.pop_back(); prec_stack.pop_back();
            
            if (tv.startsWith("() => { ") && tv.endsWith(" }")) tv = tv.substring(8, tv.length() - 2);
            if (f.startsWith("() => { ") && f.endsWith(" }")) f = f.substring(8, f.length() - 2);
            
            stack.push_back(c + " ? " + tv + " : " + f);
            prec_stack.push_back(0);
        }
        else if (inst.op >= OP_SIN && inst.op <= OP_EXP && stack.size() >= 1) { 
             String val = stack.back(); stack.pop_back(); prec_stack.pop_back();
             String sym = getOpSym(inst.op);
             stack.push_back(sym + "(" + val + ")");
             prec_stack.push_back(10);
        }
        else if (inst.op >= OP_MIN && inst.op <= OP_POW && stack.size() >= 2) { 
             String right = stack.back(); stack.pop_back(); prec_stack.pop_back();
             String left = stack.back(); stack.pop_back(); prec_stack.pop_back();
             String sym = getOpSym(inst.op);
             stack.push_back(sym + "(" + left + ", " + right + ")");
             prec_stack.push_back(10);
        }
        else if ((inst.op == OP_NEG || inst.op == OP_NOT || inst.op == OP_BNOT) && stack.size() >= 1) {
            String val = stack.back(); 
            int p = prec_stack.back();
            stack.pop_back(); prec_stack.pop_back();
            
            String sym = getOpSym(inst.op);
            if (inst.op == OP_NEG) sym = "-";
            
            if (p < 9 || val.startsWith("-")) val = "(" + val + ")";
            stack.push_back(sym + val);
            prec_stack.push_back(9);
        }
        else if (stack.size() >= 2) {
            String right = stack.back(); int rp = prec_stack.back(); stack.pop_back(); prec_stack.pop_back();
            String left = stack.back(); int lp = prec_stack.back(); stack.pop_back(); prec_stack.pop_back();
            
            int op_p = getPrecedence(inst.op);
            String sym = getOpSym(inst.op);
            
            // PEEPHOLE OPTIMIZATION: Translates `-1 *` back into standard `-(...)`
            if (inst.op == OP_MUL && right == "-1") {
                if (lp < 9 || left.startsWith("-")) left = "(" + left + ")";
                stack.push_back("-" + left);
                prec_stack.push_back(9);
            }
            else if (inst.op == OP_MUL && left == "-1") {
                if (rp < 9 || right.startsWith("-")) right = "(" + right + ")";
                stack.push_back("-" + right);
                prec_stack.push_back(9);
            }
            else {
                if (lp < op_p) left = "(" + left + ")";
                if (rp <= op_p) right = "(" + right + ")";
                
                stack.push_back(left + " " + sym + " " + right);
                prec_stack.push_back(op_p);
            }
        }
    }
    
    String assignments = "";
    std::vector<String> orphaned_args;
    
    for (size_t i = 0; i < stack.size(); i++) {
        if (prec_stack[i] == -1) {
            if (assignments != "") assignments += "; ";
            assignments += stack[i];
        } else {
            orphaned_args.push_back(stack[i]);
        }
    }
    
    String final_out = out;
    if (assignments != "") {
        if (final_out != "") final_out += "; ";
        final_out += assignments;
    }
    
    if (orphaned_args.size() >= 1) {
        String call = orphaned_args.back();
        if (orphaned_args.size() > 1) {
            call += "(";
            for (size_t i = 0; i < orphaned_args.size() - 1; i++) {
                call += orphaned_args[i];
                if (i < orphaned_args.size() - 2) call += ", ";
            }
            call += ")";
        }
        if (final_out != "") final_out += "; ";
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
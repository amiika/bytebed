#include "vm.h"

struct Node { String s; int p; bool is_block; bool is_sig; };

bool isMathOperator(OpCode op) {
    if (op >= OP_ADD && op <= OP_POW) return true;
    if (op == OP_SHL || op == OP_SHR || op == OP_COND) return true;
    return false;
}

String decompile(bool to_rpn) {
    uint8_t bank = active_bank;
    int len = prog_len_bank[bank];
    if (len == 0) return "";
    
    // Prepass to determine function arities
    std::map<int, int> is_func_arity;
    for (int i = 0; i < len; i++) {
        if (program_bank[bank][i].op == OP_PUSH_FUNC) {
            int arity = 0, j = i + 1;
            while (j < len && program_bank[bank][j].op == OP_BIND) { arity++; j++; }
            int depth = 1;
            while (j < len && depth > 0) {
                if (program_bank[bank][j].op == OP_PUSH_FUNC) depth++;
                else if (program_bank[bank][j].op == OP_RET) depth--;
                j++;
            }
            if (j < len && (program_bank[bank][j].op == OP_STORE || program_bank[bank][j].op == OP_STORE_KEEP)) {
                is_func_arity[program_bank[bank][j].val] = arity;
            }
        }
    }

    // Propagate arity 
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < len - 1; i++) {
            if (program_bank[bank][i].op == OP_LOAD) {
                int src = program_bank[bank][i].val, dst = -1;
                if (program_bank[bank][i+1].op == OP_STORE || program_bank[bank][i+1].op == OP_STORE_KEEP) dst = program_bank[bank][i+1].val;
                else if (i + 2 < len && program_bank[bank][i+1].op == OP_DYN_CALL_IF_FUNC && 
                        (program_bank[bank][i+2].op == OP_STORE || program_bank[bank][i+2].op == OP_STORE_KEEP)) dst = program_bank[bank][i+2].val;
                if (dst != -1 && is_func_arity.count(src) && !is_func_arity.count(dst)) {
                    is_func_arity[dst] = is_func_arity[src]; changed = true; 
                }
            }
        }
    }

    // RPN decompilation
    if (to_rpn) {
        String res = ""; 
        for (int i = 0; i < len; i++) {
            OpCode op = program_bank[bank][i].op;
            if (op == OP_VAL) res += String(program_bank[bank][i].val) + " ";
            else if (op == OP_T) res += "t "; 
            else if (op == OP_LOAD) {
                int id = program_bank[bank][i].val;
                if (i + 1 < len && (program_bank[bank][i+1].op == OP_DYN_CALL_IF_FUNC || program_bank[bank][i+1].op == OP_DYN_CALL)) {
                    res += getVarName(id) + " "; i++; 
                } else {
                    if (is_func_arity.count(id)) res += "{" + getVarName(id) + "} ";
                    else res += getVarName(id) + " ";
                }
            }
            else if (op == OP_STORE || op == OP_STORE_KEEP) { res += getVarName(program_bank[bank][i].val) + " = "; }
            else if (op == OP_PUSH_FUNC) {
                int j = i + 1; std::vector<String> pnames;
                while (j < len && program_bank[bank][j].op == OP_BIND) { pnames.insert(pnames.begin(), getVarName(program_bank[bank][j].val)); j++; }
                if (pnames.empty()) res += "() { "; 
                else {
                    String params = "( ";
                    for (size_t k = 0; k < pnames.size(); k++) params += pnames[k] + (k == pnames.size() - 1 ? "" : " ");
                    params += ") { "; res += params;
                }
                i = j - 1; 
            }
            else if (op == OP_RET) res += "} ";
            else if (op >= OP_ADD && op <= OP_POW) res += getOpSym(op) + " ";
            else if (op == OP_COND) res += "? ";
        }
        res.trim(); 
        return res;
    }
    
    // Infix decompilation
    std::vector<Node> st;
    for (int i = 0; i < len; i++) {
        OpCode op = program_bank[bank][i].op; 
        int cur_p = getPrecedence(op);
        
        if (op == OP_VAL) st.push_back({String(program_bank[bank][i].val), 10, false, false});
        else if (op == OP_T) st.push_back({"t", 10, false, false});
        else if (op == OP_LOAD) st.push_back({getVarName(program_bank[bank][i].val), 10, false, false});
        else if (op == OP_PUSH_FUNC) {
            int j = i + 1; std::vector<String> pnames;
            while (j < len && program_bank[bank][j].op == OP_BIND) { pnames.insert(pnames.begin(), getVarName(program_bank[bank][j].val)); j++; }
            String params = "(";
            for (size_t k = 0; k < pnames.size(); k++) params += pnames[k] + (k == pnames.size()-1 ? "" : ", ");
            params += ")"; 
            st.push_back({params, 100, true, true}); i = j - 1; 
        }
        else if (op == OP_RET) {
            String body_str = "";
            while (!st.empty() && !st.back().is_sig) {
                Node stmt = st.back(); st.pop_back();
                body_str = stmt.s + (body_str.length() > 0 ? "; " : "") + body_str;
            }
            if (!st.empty() && st.back().is_sig) {
                Node params = st.back(); st.pop_back();
                st.push_back({params.s + " { " + body_str + " }", 100, true, false});
            }
        }
        else if (op == OP_STORE || op == OP_STORE_KEEP) {
            if (st.empty()) continue;
            Node val = st.back(); st.pop_back();
            if (val.is_block) st.push_back({getVarName(program_bank[bank][i].val) + val.s, 0, false, false});
            else st.push_back({getVarName(program_bank[bank][i].val) + " = " + val.s, 0, false, false});
        }
        else if (op == OP_DYN_CALL || op == OP_DYN_CALL_IF_FUNC) {
            if (st.empty()) continue;
            Node func = st.back(); st.pop_back();
            
            bool is_known_func = false;
            int args_to_fold = 0;
            
            if (op == OP_DYN_CALL) {
                is_known_func = true; args_to_fold = program_bank[bank][i].val;
            } else {
                int fid = getVarId(func.s);
                if (is_func_arity.count(fid)) {
                    is_known_func = true; args_to_fold = is_func_arity[fid];
                } else if (func.s.startsWith("(") || func.s.startsWith("{") || func.s == "f" || func.s == "g") {
                    is_known_func = true; args_to_fold = 1; 
                }
            }
            
            if (is_known_func) {
                String arg_str = "";
                for (int a = 0; a < args_to_fold; a++) {
                    if (st.empty()) break;
                    Node arg = st.back(); st.pop_back();
                    arg_str = arg.s + (arg_str.length() > 0 ? ", " : "") + arg_str;
                }
                st.push_back({func.s + "(" + arg_str + ")", 10, false, false});
            } else {
                st.push_back({func.s, 10, false, false}); 
            }
        }
        else if (op == OP_COND) {
            if (st.size() < 3) continue;
            Node f = st.back(); st.pop_back(); Node tv = st.back(); st.pop_back(); Node c = st.back(); st.pop_back();
            auto unwrap = [](Node n) {
                String s = n.s; s.trim();
                if (n.is_block) {
                    if (s.startsWith("()")) { s = s.substring(2); s.trim(); }
                    if (s.startsWith("{") && s.endsWith("}")) { s = s.substring(1, s.length()-1); s.trim(); }
                }
                return s;
            };
            st.push_back({c.s + " ? " + unwrap(tv) + " : " + unwrap(f), 0, false, false});
        } 
        else if (op >= OP_ADD && op <= OP_POW) {
            String sym = getOpSym(op); bool u = (op == OP_NEG || op == OP_NOT), is_bin = false; 
            for (auto const& f : mathLibrary) { if (f.code == op) { if (f.unary) u = true; else is_bin = true; break; } }
            if (st.empty()) continue;
            if (u) { Node a = st.back(); st.pop_back(); st.push_back({sym + "(" + a.s + ")", 10, false, false}); }
            else {
                if (st.size() < 2) continue; 
                Node b = st.back(); st.pop_back(); Node a = st.back(); st.pop_back();
                if (is_bin) st.push_back({sym + "(" + a.s + ", " + b.s + ")", 10, false, false}); 
                else {
                    String l = (a.p < cur_p) ? "(" + a.s + ")" : a.s; 
                    String r = (b.p <= cur_p) ? "(" + b.s + ")" : b.s;
                    st.push_back({l + " " + sym + " " + r, cur_p, false, false}); 
                }
            }
        }
    }
    String res = "";
    for (size_t k = 0; k < st.size(); k++) res += st[k].s + (k == st.size() - 1 ? "" : "; ");
    return res;
}
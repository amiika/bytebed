#include "vm.h"

bool compileRPN(String input) {
    var_count = 0; 
    memset(vars, 0, sizeof(vars)); 
    memset(vsp, 0, sizeof(vsp));
    
    uint8_t target = 1 - active_bank;
    memset(program_bank[target], 0, sizeof(Instruction) * 256);
    int len = 0; 
    
    std::map<int, int> rpn_func_arity;
    std::vector<int> block_arity_stack;
    int last_completed_arity = 0;
    
    std::vector<String> tokens;
    const char* p = input.c_str();
    while (*p) {
        if (isspace(*p)) p++;
        else if (*p == '-' && *(p+1) && !isspace(*(p+1))) {
            String w = "-"; p++;
            if (strchr("(){}=,;<>!+-*/%&|^~@_", *p)) {
                if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                    String test = String(*p) + *(p+1);
                    OpCode dummy;
                    if (getOpCode(test, dummy)) {
                        w += test; p += 2;
                    } else { w += *p++; }
                } else { w += *p++; }
            } else {
                while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_", *p)) w += *p++;
            }
            tokens.push_back(w);
        }
        else if (*p == '\'') {
            String w = "'"; p++;
            while (*p && *p != '\'') w += *p++;
            if (*p == '\'') { w += "'"; p++; }
            tokens.push_back(w);
        }
        else if (strchr("(){}=,;<>!+-*/%&|^~@_", *p)) {
            if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                String test = String(*p) + *(p+1);
                OpCode dummy;
                if (getOpCode(test, dummy)) {
                    tokens.push_back(test); p += 2; continue;
                }
            }
            if (*p != ',' && *p != ';') tokens.push_back(String(*p)); 
            p++;
        } else {
            String w = "";
            while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_", *p) && *p != '\'') w += *p++;
            if (w.length() > 0) tokens.push_back(w);
        }
    }

    std::vector<int> block_starts;
    std::vector<std::vector<int>> block_params;
    std::vector<int> current_params;
    bool parsing_params = false;

    for (size_t i = 0; i < tokens.size(); i++) {
        if (len >= 256) return false;
        String s = tokens[i];

        bool negate = false;
        if (s.startsWith("-") && s.length() > 1 && !isdigit(s[1])) {
            negate = true; s = s.substring(1);
        }

        if (s == "(") {
            bool is_params = false;
            for (size_t j = i + 1; j < tokens.size(); j++) {
                if (tokens[j] == ")") { if (j + 1 < tokens.size() && tokens[j+1] == "{") is_params = true; break; }
            }
            if (is_params) { current_params.clear(); parsing_params = true; } 
            else { block_starts.push_back(len); program_bank[target][len++] = {OP_PUSH_FUNC, 0}; block_params.push_back({}); }
        }
        else if (s == ")") {
            if (parsing_params) parsing_params = false;
            else {
                if (block_starts.empty()) return false;
                int start = block_starts.back(); block_starts.pop_back(); block_params.pop_back(); 
                program_bank[target][len++] = {OP_RET, 0}; program_bank[target][start].val = len;
            }
        }
        else if (s == "{") {
            if (i + 2 < tokens.size() && tokens[i+2] == "}") {
                int id = getVarId(tokens[i+1]);
                if (rpn_func_arity.count(id)) { program_bank[target][len++] = {OP_LOAD, id}; i += 2; continue; }
            }
            block_starts.push_back(len); program_bank[target][len++] = {OP_PUSH_FUNC, 0};
            for (int k = current_params.size() - 1; k >= 0; k--) program_bank[target][len++] = {OP_BIND, current_params[k]};
            block_params.push_back(current_params); block_arity_stack.push_back(current_params.size()); current_params.clear();
        }
        else if (s == "}") {
            if (block_starts.empty()) return false;
            int start = block_starts.back(); block_starts.pop_back(); auto params = block_params.back(); block_params.pop_back();
            for (int param : params) program_bank[target][len++] = {OP_UNBIND, param};
            program_bank[target][len++] = {OP_RET, 0}; program_bank[target][start].val = len;
            if (!block_arity_stack.empty()) { last_completed_arity = block_arity_stack.back(); block_arity_stack.pop_back(); }
        }
        else if (s == "=") {
            if (len >= 2 && program_bank[target][len-1].op == OP_DYN_CALL_IF_FUNC && program_bank[target][len-2].op == OP_LOAD) {
                int var_id = program_bank[target][len-2].val; program_bank[target][len-2].op = OP_STORE; len--;
                if (len >= 2 && program_bank[target][len-2].op == OP_RET) rpn_func_arity[var_id] = last_completed_arity;
            } else return false;
        }
        else if (s.startsWith("'") && s.endsWith("'")) {
            int count = 0;
            for (size_t k = 1; k < s.length() - 1; k++) {
                int32_t val = (s[k] >= '0' && s[k] <= '9') ? (s[k] - '0') : s[k];
                program_bank[target][len++] = {OP_VAL, val};
                count++;
            }
            program_bank[target][len++] = {OP_VAL, count};
            program_bank[target][len++] = {OP_VEC, 0};
        }
        else if (parsing_params) { current_params.push_back(getVarId(s)); }
        else {
            if (isdigit(s[0]) || (s[0] == '-' && isdigit(s[1]))) program_bank[target][len++] = {OP_VAL, (int32_t)s.toInt()};
            else if (s == "t") program_bank[target][len++] = {OP_T, 0};
            else if (s == "?") program_bank[target][len++] = {OP_COND, 0};
            else {
                bool is_math = false;
                for (int _m = 0; _m < mathLibrarySize; _m++) {
                    if (s == mathLibrary[_m].name) {
                        program_bank[target][len++] = {mathLibrary[_m].code, 0};
                        is_math = true; break; 
                    }
                }
                
                OpCode opc;
                if (!is_math) { 
                    if (getOpCode(s, opc)) { program_bank[target][len++] = {opc, 0}; is_math = true; } 
                }
                if (!is_math) {
                    int id = getVarId(s); program_bank[target][len++] = {OP_LOAD, id};
                    if (rpn_func_arity.count(id)) program_bank[target][len++] = {OP_DYN_CALL, rpn_func_arity[id]};
                    else program_bank[target][len++] = {OP_DYN_CALL_IF_FUNC, 0};
                }
            }
        }
        if (negate) program_bank[target][len++] = {OP_NEG, 0};
    }
    if (!validateProgram(target, len)) return false; 
    prog_len_bank[target] = len; active_bank = target; 
    return true;
}

static void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, std::vector<int>* cond_starts, OpCode stopAt = OP_NONE, int minPrec = -1, bool stopAtMarker = false) {
    while (ot >= 0 && os[ot] != stopAt) {
        if (stopAtMarker && os[ot] == OP_STORE) break;
        if (os[ot] == OP_DYN_CALL) break; 
        if (minPrec != -1 && getPrecedence(os[ot]) < minPrec) break;
        if (len < 256) {
            if (os[ot] == OP_ASSIGN_VAR) { program_bank[target][len++] = {OP_STORE_KEEP, os_id[ot--]}; }
            else if (os[ot] == OP_STORE) { program_bank[target][len++] = {OP_STORE, os_id[ot--]}; }
            else if (os[ot] == OP_COND) {
                if (cond_starts && !cond_starts->empty()) {
                    program_bank[target][len++] = {OP_RET, 0};
                    program_bank[target][cond_starts->back()].val = len; cond_starts->pop_back();
                }
                program_bank[target][len++] = {OP_COND, 0}; ot--;
            }
            else { program_bank[target][len++] = {os[ot--], 0}; }
        } else ot--;
    }
}

static bool isFuncDef(const char* p, std::vector<String>& params) {
    if (*p != '(') return false;
    const char* q = p + 1; int depth = 1; String curr = "";
    while (*q && depth > 0) {
        if (*q == '(') depth++;
        else if (*q == ')') {
            depth--;
            if (depth == 0) {
                if (curr.length() > 0) { curr.trim(); if(curr.length()>0) params.push_back(curr); }
                q++; while (isspace(*q)) q++; return *q == '{';
            }
        }
        else if (*q == ',' && depth == 1) { curr.trim(); if(curr.length()>0) params.push_back(curr); curr = ""; }
        else curr += *q; q++;
    }
    return false;
}

bool compileInfix(String input, bool reset_t) {
    var_count = 0; memset(vars, 0, sizeof(vars)); memset(vsp, 0, sizeof(vsp));
    uint8_t target = 1 - active_bank; memset(program_bank[target], 0, sizeof(Instruction) * 256);
    int len = 0; OpCode os[32]; int os_id[32]; int ot = -1; const char* p = input.c_str();
    int paren_depth = 0; int cond_depth = 0;
    std::vector<int> block_starts, call_arg_counts, cond_starts;
    std::vector<std::vector<int>> block_params;

    std::vector<int> bracket_types; 
    std::vector<int> array_counts;

    bool expect_op = false; 

    while (*p) {
        if (len >= 256 || ot >= 31) return false; 
        if (isspace(*p)) { p++; continue; }
        
        if (isdigit(*p)) { 
            if (expect_op) return false; 
            expect_op = true;
            program_bank[target][len++] = {OP_VAL, (int32_t)strtol(p, (char**)&p, 10)}; 
        }
        else if (isalpha(*p)) { 
            String word = ""; while (isalpha(*p) || isdigit(*p) || *p == '_') word += *p++; 
            if (word == "t") { 
                if (expect_op) return false; expect_op = true;
                program_bank[target][len++] = {OP_T, 0}; continue; 
            }
            if (word == "return") { expect_op = false; continue; } 
            
            bool is_math = false;
            for (int _m = 0; _m < mathLibrarySize; _m++) { 
                if (word == mathLibrary[_m].name) { 
                    if (expect_op) return false; 
                    expect_op = false; os[++ot] = mathLibrary[_m].code; os_id[ot] = 0; is_math = true; break; 
                } 
            }
            if (is_math) continue; 
            
            int id = getVarId(word); while (isspace(*p)) p++;

            if (expect_op) return false; 

            if (*p == '(') {
                std::vector<String> params;
                if (isFuncDef(p, params)) {
                    block_starts.push_back(len); program_bank[target][len++] = {OP_PUSH_FUNC, 0};
                    std::vector<int> p_ids;
                    for (int i = params.size() - 1; i >= 0; i--) { int pid = getVarId(params[i]); p_ids.push_back(pid); program_bank[target][len++] = {OP_BIND, pid}; }
                    block_params.push_back(p_ids);
                    int depth = 1; p++;
                    while (*p && depth > 0) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
                    while (isspace(*p)) p++; if (*p == '{') p++; 
                    os[++ot] = OP_STORE; os_id[ot] = id;
                    expect_op = false; 
                } else { 
                    os[++ot] = OP_DYN_CALL; os_id[ot] = id; expect_op = false; 
                }
            } else if (*p == '=') { 
                if (*(p+1) == '=') { program_bank[target][len++] = {OP_LOAD, id}; expect_op = true; } 
                else { os[++ot] = OP_ASSIGN_VAR; os_id[ot] = id; p++; expect_op = false; }
            } else { program_bank[target][len++] = {OP_LOAD, id}; expect_op = true; }
        }
        else if (*p == '\'') {
            if (expect_op) return false;
            p++; int count = 0;
            while (*p && *p != '\'') {
                int32_t val = (*p >= '0' && *p <= '9') ? (*p - '0') : *p;
                program_bank[target][len++] = {OP_VAL, val};
                count++; p++;
            }
            if (*p == '\'') p++;
            program_bank[target][len++] = {OP_VAL, count};
            program_bank[target][len++] = {OP_VEC, 0};
            expect_op = true;
        }
        else if (*p == '(') { 
            if (expect_op) return false; 
            std::vector<String> params;
            if (isFuncDef(p, params)) {
                block_starts.push_back(len); program_bank[target][len++] = {OP_PUSH_FUNC, 0};
                std::vector<int> p_ids;
                for (int i = params.size() - 1; i >= 0; i--) {
                    int pid = getVarId(params[i]); p_ids.push_back(pid); program_bank[target][len++] = {OP_BIND, pid};
                }
                block_params.push_back(p_ids);
                int depth = 1; p++;
                while (*p && depth > 0) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
                while (isspace(*p)) p++; if (*p == '{') p++; 
                expect_op = false; 
            } else {
                os[++ot] = OP_NONE; paren_depth++; bracket_types.push_back(0); 
                // HIGH IMPACT FIX: Empty parenthesis check
                const char* temp = p + 1;
                while (isspace(*temp)) temp++;
                if (*temp == ')') call_arg_counts.push_back(0);
                else call_arg_counts.push_back(1);
                
                p++; expect_op = false; 
            }
        }
        else if (*p == ')') { 
            if (paren_depth <= 0 || bracket_types.back() != 0) return false; 
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, -1, true); 
            int args = call_arg_counts.back(); call_arg_counts.pop_back(); bracket_types.pop_back();
            if (ot >= 0) { 
                ot--; 
                if (ot >= 0) {
                    if (os[ot] == OP_DYN_CALL) { program_bank[target][len++] = {OP_LOAD, os_id[ot]}; program_bank[target][len++] = {OP_DYN_CALL, args}; ot--; } 
                    else if (os[ot] >= OP_SIN && os[ot] <= OP_POW) { program_bank[target][len++] = {os[ot--], 0}; }
                }
            }
            paren_depth--; p++; expect_op = true; 
        }
        else if (*p == '[') {
            if (expect_op) {
                os[++ot] = OP_NONE; paren_depth++; bracket_types.push_back(2);
                expect_op = false;
            } else {
                os[++ot] = OP_NONE; paren_depth++; bracket_types.push_back(1);
                const char* temp = p + 1;
                while (isspace(*temp)) temp++;
                if (*temp == ']') array_counts.push_back(0); 
                else array_counts.push_back(1);
                expect_op = false;
            }
            p++;
        }
        else if (*p == ']') {
            if (paren_depth <= 0 || (bracket_types.back() != 1 && bracket_types.back() != 2)) return false;
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, -1, true);
            if (ot >= 0) ot--; 
            int btype = bracket_types.back(); bracket_types.pop_back();
            if (btype == 1) { 
                int size = array_counts.back(); array_counts.pop_back(); 
                program_bank[target][len++] = {OP_VAL, size};
                program_bank[target][len++] = {OP_VEC, 0};
            } else { 
                program_bank[target][len++] = {OP_AT, 0}; 
            }
            paren_depth--; p++; expect_op = true;
        }
        else if (*p == '}') {
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, -1, true); 
            if (block_starts.empty()) return false;
            int start = block_starts.back(); block_starts.pop_back(); auto p_ids = block_params.back(); block_params.pop_back();
            for (int i = p_ids.size() - 1; i >= 0; i--) { program_bank[target][len++] = {OP_UNBIND, p_ids[i]}; }
            program_bank[target][len++] = {OP_RET, 0}; program_bank[target][start].val = len;
            if (ot >= 0 && os[ot] == OP_STORE) { program_bank[target][len++] = {OP_STORE_KEEP, os_id[ot]}; ot--; }
            p++; expect_op = true; 
        }
        else if (*p == '?') { 
            if (!expect_op) return false; expect_op = false;
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, getPrecedence(OP_COND) + 1, true); 
            os[++ot] = OP_COND; cond_depth++; p++; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; cond_starts.push_back(len - 1);
        }
        else if (*p == ':') { 
            if (!expect_op) return false; expect_op = false;
            if (cond_depth <= 0) return false; 
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_COND, -1, true); 
            program_bank[target][len++] = {OP_RET, 0}; if (cond_starts.empty()) return false;
            program_bank[target][cond_starts.back()].val = len; cond_starts.pop_back();
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; cond_starts.push_back(len - 1); cond_depth--; p++; 
        }
        else if (*p == ';') { 
            expect_op = false; flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, -1, true); 
            program_bank[target][len++] = {OP_POP, 0}; p++; 
        }
        else if (*p == ',') { 
            if (!expect_op) return false; expect_op = false;
            flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, -1, true); 
            if (paren_depth > 0 && !bracket_types.empty()) {
                if (bracket_types.back() == 0) call_arg_counts.back()++;
                else if (bracket_types.back() == 1) array_counts.back()++;
            }
            p++; 
        }
        else {
            String opStr = ""; opStr += *p;
            if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                String test = opStr + *(p+1);
                OpCode dummy;
                if (getOpCode(test, dummy)) opStr = test;
            }
            OpCode cur = OP_NONE;
            getOpCode(opStr, cur);
            
            if (cur == OP_SUB && !expect_op) cur = OP_NEG;

            if (cur != OP_NONE) { 
                bool is_unary = (cur == OP_NOT || cur == OP_NEG || cur == OP_BNOT);
                if (!expect_op && !is_unary) return false; 
                expect_op = false;
                flushOps(target, len, os, os_id, ot, &cond_starts, OP_NONE, getPrecedence(cur), true); 
                os[++ot] = cur; os_id[ot] = 0; p += opStr.length(); 
            } 
            else return false; 
        }
    }
    if (paren_depth != 0 || cond_depth != 0 || !block_starts.empty()) return false; 
    flushOps(target, len, os, os_id, ot, &cond_starts);
    if (!validateProgram(target, len)) return false;
    if (reset_t) t_raw = 0; prog_len_bank[target] = len; active_bank = target; 
    return true;
}
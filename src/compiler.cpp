#include "vm.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

bool compileRPN(String input) {
    var_count = 0; 
    memset(vars, 0, sizeof(vars)); 
    
    vars[getVarId("PI")] = {0, setF(M_PI)};
    vars[getVarId("pi")] = {0, setF(M_PI)};
    vars[getVarId("E")]  = {0, setF(M_E)};
    vars[getVarId("e")]  = {0, setF(M_E)};
    
    uint8_t target = 1 - active_bank;
    memset(program_bank[target], 0, sizeof(Instruction) * 256);
    int len = 0; 
    
    int rpn_func_arity[64];
    for (int i = 0; i < 64; i++) rpn_func_arity[i] = -1;
    
    int block_arity_stack[16]; int bas_ptr = -1;
    int last_completed_arity = 0;
    
    String tokens[128];
    int tok_cnt = 0;
    
    const char* p = input.c_str();
    while (*p) {
        if (tok_cnt >= 128) break; 
        
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
            tokens[tok_cnt++] = w;
        }
        else if (*p == '\'') {
            String w = "'"; p++;
            while (*p && *p != '\'') w += *p++;
            if (*p == '\'') { w += "'"; p++; }
            tokens[tok_cnt++] = w;
        }
        else if (strchr("(){}=,;<>!+-*/%&|^~@_", *p)) {
            // FIXED: 3-character lookahead added to the RPN lexer so <<=, >>=, and **= are kept intact
            if (*(p+1) && *(p+2) && strchr("=<>!&|*", *(p+1)) && *(p+2) == '=') {
                String test3 = String(*p) + *(p+1) + *(p+2);
                OpCode dummy;
                if (getOpCode(test3, dummy)) {
                    tokens[tok_cnt++] = test3; p += 3; continue;
                }
            }
            if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                String test2 = String(*p) + *(p+1);
                OpCode dummy;
                if (getOpCode(test2, dummy)) {
                    tokens[tok_cnt++] = test2; p += 2; continue;
                }
            }
            if (*p != ',' && *p != ';') tokens[tok_cnt++] = String(*p); 
            p++;
        } else {
            String w = "";
            while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_", *p) && *p != '\'') {
                w += *p++;
                if ((w.endsWith("e") || w.endsWith("E")) && (*p == '+' || *p == '-')) {
                    w += *p++; 
                }
            }
            if (w.length() > 0) tokens[tok_cnt++] = w;
        }
    }

    int block_starts[16]; int bs_ptr = -1;
    int block_params[16][8]; int bp_counts[16]; int bp_ptr = -1;
    int current_params[8]; int cp_cnt = 0;
    bool parsing_params = false;

    for (int i = 0; i < tok_cnt; i++) {
        if (len >= 256) return false;
        String s = tokens[i];

        bool negate = false;
        if (s.startsWith("-") && s.length() > 1 && !isdigit(s[1]) && s[1] != '.' && s != "-=") {
            negate = true; s = s.substring(1);
        }

        if (s == "(") {
            bool is_params = false;
            for (int j = i + 1; j < tok_cnt; j++) {
                if (tokens[j] == ")") { if (j + 1 < tok_cnt && tokens[j+1] == "{") is_params = true; break; }
            }
            if (is_params) { cp_cnt = 0; parsing_params = true; } 
            else { 
                if (bs_ptr < 15) block_starts[++bs_ptr] = len; 
                program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
                if (bp_ptr < 15) { bp_ptr++; bp_counts[bp_ptr] = 0; }
            }
        }
        else if (s == ")") {
            if (parsing_params) parsing_params = false;
            else {
                if (bs_ptr < 0) return false;
                int start = block_starts[bs_ptr--]; bp_ptr--; 
                program_bank[target][len++] = {OP_RET, 0}; program_bank[target][start].val = len;
            }
        }
        else if (s == "{") {
            if (i + 2 < tok_cnt && tokens[i+2] == "}") {
                int id = getVarId(tokens[i+1]);
                if (id < 64 && rpn_func_arity[id] != -1) { program_bank[target][len++] = {OP_LOAD, id}; i += 2; continue; }
            }
            if (bs_ptr < 15) block_starts[++bs_ptr] = len; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0};
            
            for (int k = cp_cnt - 1; k >= 0; k--) program_bank[target][len++] = {OP_BIND, current_params[k]};
            
            if (bp_ptr < 15) {
                bp_ptr++;
                bp_counts[bp_ptr] = cp_cnt;
                for (int k = 0; k < cp_cnt; k++) block_params[bp_ptr][k] = current_params[k];
            }
            if (bas_ptr < 15) block_arity_stack[++bas_ptr] = cp_cnt;
            cp_cnt = 0;
        }
        else if (s == "}") {
            if (bs_ptr < 0) return false;
            int start = block_starts[bs_ptr--]; 
            
            int p_cnt = bp_counts[bp_ptr];
            for (int k = 0; k < p_cnt; k++) {
                program_bank[target][len++] = {OP_UNBIND, block_params[bp_ptr][k]};
            }
            bp_ptr--;
            
            program_bank[target][len++] = {OP_RET, 0}; program_bank[target][start].val = len;
            if (bas_ptr >= 0) { last_completed_arity = block_arity_stack[bas_ptr--]; }
        }
        else if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" || s == "&=" || s == "|=" || s == "^=" || s == "<<=" || s == ">>=" || s == "**=") {
            if (len >= 2 && program_bank[target][len-1].op == OP_DYN_CALL_IF_FUNC && program_bank[target][len-2].op == OP_LOAD) {
                int var_id = program_bank[target][len-2].val; 
                
                if (s == "=") program_bank[target][len-2].op = OP_STORE; 
                else if (s == "+=") program_bank[target][len-2].op = OP_ADD_ASSIGN;
                else if (s == "-=") program_bank[target][len-2].op = OP_SUB_ASSIGN;
                else if (s == "*=") program_bank[target][len-2].op = OP_MUL_ASSIGN;
                else if (s == "/=") program_bank[target][len-2].op = OP_DIV_ASSIGN;
                else if (s == "%=") program_bank[target][len-2].op = OP_MOD_ASSIGN;
                else if (s == "&=") program_bank[target][len-2].op = OP_AND_ASSIGN;
                else if (s == "|=") program_bank[target][len-2].op = OP_OR_ASSIGN;
                else if (s == "^=") program_bank[target][len-2].op = OP_XOR_ASSIGN;
                else if (s == "<<=") program_bank[target][len-2].op = OP_SHL_ASSIGN;
                else if (s == ">>=") program_bank[target][len-2].op = OP_SHR_ASSIGN;
                else if (s == "**=") program_bank[target][len-2].op = OP_POW_ASSIGN;

                len--;
                
                if (s == "=" && len >= 2 && program_bank[target][len-2].op == OP_RET) {
                    if (var_id < 64) rpn_func_arity[var_id] = last_completed_arity;
                    for (int k = 0; k < len - 1; k++) {
                        if (program_bank[target][k].op == OP_LOAD && program_bank[target][k].val == var_id) {
                            if (program_bank[target][k+1].op == OP_DYN_CALL_IF_FUNC) {
                                program_bank[target][k+1].op = OP_DYN_CALL;
                                program_bank[target][k+1].val = last_completed_arity;
                            }
                        }
                    }
                } 
                else if (s == "=" && len >= 2 && program_bank[target][len-2].op == OP_LOAD) {
                    int src_id = program_bank[target][len-2].val;
                    if (src_id < 64 && var_id < 64 && rpn_func_arity[src_id] != -1) rpn_func_arity[var_id] = rpn_func_arity[src_id];
                }
            } else return false;
        }
        else if (s.startsWith("'") && s.endsWith("'")) {
            int count = 0;
            for (size_t k = 1; k < s.length() - 1; k++) {
                float val = (s[k] >= '0' && s[k] <= '9') ? (s[k] - '0') : s[k];
                program_bank[target][len++] = {OP_VAL, setF(val)};
                count++;
            }
            program_bank[target][len++] = {OP_VAL, setF(count)};
            program_bank[target][len++] = {OP_VEC, 0};
        }
        else if (parsing_params) { if (cp_cnt < 8) current_params[cp_cnt++] = getVarId(s); }
        else {
            if (isdigit(s[0]) || (s[0] == '-' && isdigit(s[1])) || (s[0] == '.' && isdigit(s[1])) || (s.startsWith("-.") && s.length() > 2 && isdigit(s[2]))) {
                program_bank[target][len++] = {OP_VAL, setF(strtof(s.c_str(), NULL))};
            }
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
                    if (id < 64 && rpn_func_arity[id] != -1) program_bank[target][len++] = {OP_DYN_CALL, rpn_func_arity[id]};
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

static void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, int* cond_starts, int& cs_ptr, OpCode stopAt = OP_NONE, int minPrec = -1, bool stopAtMarker = false) {
    while (ot >= 0 && os[ot] != stopAt) {
        if (stopAtMarker && os[ot] == OP_STORE) break;
        if (os[ot] == OP_DYN_CALL) break; 
        if (minPrec != -1 && getPrecedence(os[ot]) < minPrec) break;
        if (len < 256) {
            if (os[ot] == OP_ASSIGN_VAR) { program_bank[target][len++] = {OP_STORE_KEEP, os_id[ot--]}; }
            else if (os[ot] == OP_STORE) { program_bank[target][len++] = {OP_STORE, os_id[ot--]}; }
            else if (os[ot] == OP_COND) {
                if (cs_ptr >= 0) {
                    program_bank[target][len++] = {OP_RET, 0};
                    program_bank[target][cond_starts[cs_ptr--]].val = len;
                }
                program_bank[target][len++] = {OP_COND, 0}; ot--;
            }
            else if (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN) { program_bank[target][len++] = {os[ot], os_id[ot]}; ot--; }
            else { program_bank[target][len++] = {os[ot--], 0}; }
        } else ot--;
    }
}

static bool isLambdaDef(const char* p, String* params, int& param_cnt, int& consume_len) {
    if (*p != '(') return false;
    const char* q = p + 1;
    String curr = "";
    while (*q && *q != ')') {
        if (*q == '(') return false; 
        if (*q == ',') { curr.trim(); if(curr.length()>0 && param_cnt < 8) params[param_cnt++] = curr; curr = ""; }
        else curr += *q;
        q++;
    }
    if (*q != ')') return false;
    curr.trim(); if(curr.length()>0 && param_cnt < 8) params[param_cnt++] = curr;
    q++;
    while (isspace(*q)) q++;
    if (*q == '=' && *(q+1) == '>') {
        consume_len = (q + 2) - p;
        return true;
    }
    return false;
}

bool compileInfix(String input, bool reset_t) {
    var_count = 0; memset(vars, 0, sizeof(vars)); 
    
    vars[getVarId("PI")] = {0, setF(M_PI)};
    vars[getVarId("pi")] = {0, setF(M_PI)};
    vars[getVarId("E")]  = {0, setF(M_E)};
    vars[getVarId("e")]  = {0, setF(M_E)};
    
    uint8_t target = 1 - active_bank; memset(program_bank[target], 0, sizeof(Instruction) * 256);
    int len = 0; OpCode os[32]; int os_id[32]; int ot = -1; const char* p = input.c_str();
    int paren_depth = 0; int cond_depth = 0;
    
    int call_arg_counts[16]; int cac_ptr = -1;
    int cond_starts[16]; int cs_ptr = -1;
    
    struct LambdaCtx {
        int depth;
        int p_ids[8];
        int p_cnt;
        int start_pc;
        bool uses_braces;
    };
    LambdaCtx open_lambdas[8]; int ol_ptr = -1;

    int bracket_types[16]; int bt_ptr = -1;
    int array_counts[16]; int ac_ptr = -1;

    bool expect_op = false; 

    while (*p) {
        if (len >= 256 || ot >= 31) return false; 
        
        if (*p == ')' || *p == ',' || *p == ';' || *p == ']') {
            while (ol_ptr >= 0) {
                auto& lam = open_lambdas[ol_ptr];
                if (!lam.uses_braces && paren_depth <= lam.depth) {
                    flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
                    if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                    
                    int assigns_to_keep[16]; int atk_cnt = 0;
                    while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                        if (atk_cnt < 16) assigns_to_keep[atk_cnt++] = os_id[ot];
                        ot--;
                    }
                    
                    for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
                    program_bank[target][len++] = {OP_RET, 0};
                    program_bank[target][lam.start_pc].val = len;
                    
                    for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, assigns_to_keep[i]};
                    
                    ol_ptr--;
                    expect_op = true;
                } else break;
            }
        }

        if (isspace(*p)) { p++; continue; }
        
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1)))) { 
            if (expect_op) return false; 
            expect_op = true;
            program_bank[target][len++] = {OP_VAL, setF(strtof(p, (char**)&p))}; 
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
            
            const char* temp = p;
            while (isspace(*temp)) temp++;
            if (*temp == '=' && *(temp+1) == '>') {
                if (expect_op) return false;
                int pid = getVarId(word);
                int start_pc = len;
                program_bank[target][len++] = {OP_PUSH_FUNC, 0};
                program_bank[target][len++] = {OP_BIND, pid};
                
                temp += 2; 
                while (isspace(*temp)) temp++;
                bool braces = (*temp == '{');
                if (braces) temp++;
                
                if (ol_ptr < 7) {
                    ol_ptr++;
                    open_lambdas[ol_ptr].depth = paren_depth;
                    open_lambdas[ol_ptr].p_cnt = 1;
                    open_lambdas[ol_ptr].p_ids[0] = pid;
                    open_lambdas[ol_ptr].start_pc = start_pc;
                    open_lambdas[ol_ptr].uses_braces = braces;
                }
                os[++ot] = OP_NONE; 
                p = temp;
                expect_op = false;
                continue;
            }

            int id = getVarId(word); while (isspace(*p)) p++;

            if (expect_op) return false; 

            if (*p == '(') {
                os[++ot] = OP_DYN_CALL; os_id[ot] = id; expect_op = false; 
            } else if (*p == '=' && *(p+1) != '=') { 
                os[++ot] = OP_ASSIGN_VAR; os_id[ot] = id; p++; expect_op = false; 
            } else if (*p == '+' && *(p+1) == '=') {
                os[++ot] = OP_ADD_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '-' && *(p+1) == '=') {
                os[++ot] = OP_SUB_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '*' && *(p+1) == '*' && *(p+2) == '=') {
                os[++ot] = OP_POW_ASSIGN; os_id[ot] = id; p += 3; expect_op = false;
            } else if (*p == '*' && *(p+1) == '=' && *(p+2) != '*') {
                os[++ot] = OP_MUL_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '/' && *(p+1) == '=') {
                os[++ot] = OP_DIV_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '%' && *(p+1) == '=') {
                os[++ot] = OP_MOD_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '&' && *(p+1) == '=' && *(p+2) != '&') {
                os[++ot] = OP_AND_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '|' && *(p+1) == '=' && *(p+2) != '|') {
                os[++ot] = OP_OR_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '^' && *(p+1) == '=') {
                os[++ot] = OP_XOR_ASSIGN; os_id[ot] = id; p += 2; expect_op = false;
            } else if (*p == '<' && *(p+1) == '<' && *(p+2) == '=') {
                os[++ot] = OP_SHL_ASSIGN; os_id[ot] = id; p += 3; expect_op = false;
            } else if (*p == '>' && *(p+1) == '>' && *(p+2) == '=') {
                os[++ot] = OP_SHR_ASSIGN; os_id[ot] = id; p += 3; expect_op = false;
            } else { 
                program_bank[target][len++] = {OP_LOAD, id}; expect_op = true; 
            }
        }
        else if (*p == '\'') {
            if (expect_op) return false;
            p++; int count = 0;
            while (*p && *p != '\'') {
                float val = (*p >= '0' && *p <= '9') ? (*p - '0') : *p;
                program_bank[target][len++] = {OP_VAL, setF(val)};
                count++; p++;
            }
            if (*p == '\'') p++;
            program_bank[target][len++] = {OP_VAL, setF(count)};
            program_bank[target][len++] = {OP_VEC, 0};
            expect_op = true;
        }
        else if (*p == '(') { 
            if (expect_op) return false; 
            
            String params[8]; int param_cnt = 0;
            int consume_len = 0;
            if (isLambdaDef(p, params, param_cnt, consume_len)) {
                int start_pc = len;
                program_bank[target][len++] = {OP_PUSH_FUNC, 0};
                
                LambdaCtx lam;
                lam.depth = paren_depth;
                lam.p_cnt = param_cnt;
                lam.start_pc = start_pc;
                
                for (int i = param_cnt - 1; i >= 0; i--) {
                    int pid = getVarId(params[i]);
                    lam.p_ids[i] = pid;
                    program_bank[target][len++] = {OP_BIND, pid};
                }
                
                p += consume_len;
                while (isspace(*p)) p++;
                bool braces = (*p == '{');
                if (braces) p++;
                lam.uses_braces = braces;
                
                if (ol_ptr < 7) open_lambdas[++ol_ptr] = lam;
                os[++ot] = OP_NONE; 
                expect_op = false; 
                continue;
            }

            os[++ot] = OP_NONE; paren_depth++; 
            if (bt_ptr < 15) bracket_types[++bt_ptr] = 0;
            const char* temp = p + 1;
            while (isspace(*temp)) temp++;
            if (cac_ptr < 15) {
                if (*temp == ')') call_arg_counts[++cac_ptr] = 0;
                else call_arg_counts[++cac_ptr] = 1;
            }
            p++; expect_op = false; 
        }
        else if (*p == ')') { 
            if (paren_depth <= 0 || (bt_ptr >= 0 && bracket_types[bt_ptr] != 0)) return false; 
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); 
            int args = 0; if (cac_ptr >= 0) args = call_arg_counts[cac_ptr--]; 
            if (bt_ptr >= 0) bt_ptr--;
            
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
                os[++ot] = OP_NONE; paren_depth++; 
                if (bt_ptr < 15) bracket_types[++bt_ptr] = 2;
                expect_op = false;
            } else {
                os[++ot] = OP_NONE; paren_depth++; 
                if (bt_ptr < 15) bracket_types[++bt_ptr] = 1;
                const char* temp = p + 1;
                while (isspace(*temp)) temp++;
                if (ac_ptr < 15) {
                    if (*temp == ']') array_counts[++ac_ptr] = 0; 
                    else array_counts[++ac_ptr] = 1;
                }
                expect_op = false;
            }
            p++;
        }
        else if (*p == ']') {
            if (paren_depth <= 0 || bt_ptr < 0 || (bracket_types[bt_ptr] != 1 && bracket_types[bt_ptr] != 2)) return false;
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
            if (ot >= 0) ot--; 
            int btype = bracket_types[bt_ptr--];
            if (btype == 1) { 
                int size = 0; if (ac_ptr >= 0) size = array_counts[ac_ptr--]; 
                program_bank[target][len++] = {OP_VAL, setF(size)};
                program_bank[target][len++] = {OP_VEC, 0};
            } else { 
                program_bank[target][len++] = {OP_AT, 0}; 
            }
            paren_depth--; p++; expect_op = true;
        }
        else if (*p == '{') { return false; } 
        else if (*p == '}') {
            if (ol_ptr >= 0 && open_lambdas[ol_ptr].uses_braces) {
                auto& lam = open_lambdas[ol_ptr];
                flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
                if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                
                int assigns_to_keep[16]; int atk_cnt = 0;
                while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                    if (atk_cnt < 16) assigns_to_keep[atk_cnt++] = os_id[ot];
                    ot--;
                }
                
                for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
                program_bank[target][len++] = {OP_RET, 0};
                program_bank[target][lam.start_pc].val = len;
                
                for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, assigns_to_keep[i]};
                
                ol_ptr--;
                p++; expect_op = true; 
            } else return false;
        }
        else if (*p == '?') { 
            if (!expect_op) return false; expect_op = false;
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, getPrecedence(OP_COND) + 1, true); 
            os[++ot] = OP_COND; cond_depth++; p++; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
            if (cs_ptr < 15) cond_starts[++cs_ptr] = len - 1;
        }
        else if (*p == ':') { 
            if (!expect_op) return false; expect_op = false;
            if (cond_depth <= 0) return false; 
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_COND, -1, true); 
            program_bank[target][len++] = {OP_RET, 0}; if (cs_ptr < 0) return false;
            program_bank[target][cond_starts[cs_ptr--]].val = len;
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
            if (cs_ptr < 15) cond_starts[++cs_ptr] = len - 1; 
            cond_depth--; p++; 
        }
        else if (*p == ';') { 
            expect_op = false; flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); 
            program_bank[target][len++] = {OP_POP, 0}; p++; 
        }
        else if (*p == ',') { 
            if (!expect_op) return false; expect_op = false;
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); 
            if (paren_depth > 0 && bt_ptr >= 0) {
                if (bracket_types[bt_ptr] == 0) call_arg_counts[cac_ptr]++;
                else if (bracket_types[bt_ptr] == 1) array_counts[ac_ptr]++;
            } else {
                program_bank[target][len++] = {OP_POP, 0};
            }
            p++; 
        }
        else {
            String opStr = ""; opStr += *p;
            
            if (*(p+1) && *(p+2) && strchr("=<>!&|*", *(p+1)) && *(p+2) == '=') {
                String test3 = opStr + *(p+1) + *(p+2);
                OpCode dummy;
                if (getOpCode(test3, dummy)) opStr = test3;
            }
            if (opStr.length() == 1 && *(p+1) && strchr("=<>!&|*", *(p+1))) {
                String test2 = opStr + *(p+1);
                OpCode dummy;
                if (getOpCode(test2, dummy)) opStr = test2;
            }
            
            OpCode cur = OP_NONE;
            getOpCode(opStr, cur);
            
            if (cur == OP_SUB && !expect_op) cur = OP_NEG;

            if (cur != OP_NONE) { 
                bool is_unary = (cur == OP_NOT || cur == OP_NEG || cur == OP_BNOT);
                if (!expect_op && !is_unary) return false; 
                expect_op = false;
                
                int prec = getPrecedence(cur);
                if (cur == OP_POW) prec++; 
                
                flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, prec, true); 
                os[++ot] = cur; os_id[ot] = 0; p += opStr.length(); 
            } 
            else return false; 
        }
    }
    
    while (ol_ptr >= 0) {
        auto& lam = open_lambdas[ol_ptr];
        if (!lam.uses_braces) {
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
            if (ot >= 0 && os[ot] == OP_NONE) ot--; 
            
            int assigns_to_keep[16]; int atk_cnt = 0;
            while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                if (atk_cnt < 16) assigns_to_keep[atk_cnt++] = os_id[ot];
                ot--;
            }
            
            for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
            program_bank[target][len++] = {OP_RET, 0};
            program_bank[target][lam.start_pc].val = len;
            
            for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, assigns_to_keep[i]};
            
            ol_ptr--;
        } else return false; 
    }

    if (paren_depth != 0 || cond_depth != 0) return false; 
    flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr);
    if (!validateProgram(target, len)) return false;
    if (reset_t) t_raw = 0; prog_len_bank[target] = len; active_bank = target; 
    return true;
}
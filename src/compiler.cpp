#include "vm.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.785398163397448309616
#endif
#ifndef M_1_PI
#define M_1_PI 0.318309886183790671538
#endif
#ifndef M_2_PI
#define M_2_PI 0.636619772367581343076
#endif
#ifndef M_2_SQRTPI
#define M_2_SQRTPI 1.12837916709551257390
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif
#ifndef M_LOG2E
#define M_LOG2E 1.44269504088896340736
#endif
#ifndef M_LOG10E
#define M_LOG10E 0.434294481903251827651
#endif
#ifndef M_LN2
#define M_LN2 0.693147180559945309417
#endif
#ifndef M_LN10
#define M_LN10 2.30258509299404568402
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2 0.707106781186547524401
#endif

struct LambdaCtx {
    int depth;
    int p_ids[8];
    int p_cnt;
    int start_pc;
    bool uses_braces;
};

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
    if (*q == '=' && *(q+1) == '>') { consume_len = (q + 2) - p; return true; }
    return false;
}

static int tokenize(const String& input, String* tokens, int max_tokens) {
    int tok_cnt = 0;
    const char* p = input.c_str();
    while (*p && tok_cnt < max_tokens) {
        if (isspace(*p)) p++;
        else if (*p == '&' && isalpha(*(p+1))) {
            String w = "&"; p++;
            while (isalpha(*p) || isdigit(*p) || *p == '_') w += *p++;
            tokens[tok_cnt++] = w;
        }
        else if (*p == '-' && *(p+1) && !isspace(*(p+1))) {
            String w = "-"; p++;
            if (strchr("(){}=,;<>!+-*/%&|^~@_#$:", *p)) { 
                if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                    String test = String(*p) + *(p+1);
                    OpCode dummy;
                    if (getOpCode(test, dummy)) { w += test; p += 2; } 
                    else { w += *p++; }
                } else { w += *p++; }
            } else {
                while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_#$:", *p)) w += *p++; 
            }
            tokens[tok_cnt++] = w;
        }
        else if (*p == '\'') {
            String w = "'"; p++;
            while (*p && *p != '\'') w += *p++;
            if (*p == '\'') { w += "'"; p++; }
            tokens[tok_cnt++] = w;
        }
        else if (strchr("(){}=,;<>!+-*/%&|^~@_#$:", *p)) { 
            if (*(p+1) && *(p+2) && strchr("=<>!&|*", *(p+1)) && *(p+2) == '=') {
                String test3 = String(*p) + *(p+1) + *(p+2);
                OpCode dummy;
                if (getOpCode(test3, dummy)) { tokens[tok_cnt++] = test3; p += 3; continue; }
            }
            if (*(p+1) && strchr("=<>!&|*", *(p+1))) {
                String test2 = String(*p) + *(p+1);
                OpCode dummy;
                if (getOpCode(test2, dummy)) { tokens[tok_cnt++] = test2; p += 2; continue; }
            }
            if (*p != ',') tokens[tok_cnt++] = String(*p); 
            p++;
        } else {
            String w = "";
            while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_#$:", *p) && *p != '\'') { 
                w += *p++;
                if ((w.endsWith("e") || w.endsWith("E")) && (*p == '+' || *p == '-')) { w += *p++; }
            }
            if (w.length() > 0) tokens[tok_cnt++] = w;
        }
    }
    return tok_cnt;
}

static int get_expr_start(uint8_t target, int end_pc) {
    int stack_need = 1;
    int pc = end_pc;
    int func_depth = 0; 
    
    while (pc >= 0 && stack_need > 0) {
        OpCode op = program_bank[target][pc].op;
        
        if (op == OP_RET) func_depth++;
        if (op == OP_PUSH_FUNC && func_depth > 0) {
            func_depth--;
            if (func_depth == 0) { stack_need -= 1; pc--; continue; }
        }

        if (func_depth == 0) {
            int produces = 1;
            if (op == OP_POP || op == OP_STORE || op == OP_JMP || op == OP_BIND || op == OP_UNBIND || op == OP_RET) produces = 0;
            
            int consumes = 0;
            if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD || op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_SHL || op == OP_SHR || op == OP_LT || op == OP_GT || op == OP_EQ || op == OP_NEQ || op == OP_LTE || op == OP_GTE || op == OP_MIN || op == OP_MAX || op == OP_POW || op == OP_SC_AND || op == OP_SC_OR || op == OP_AT) consumes = 2;
            else if (op == OP_STORE_AT || op == OP_COND) consumes = 3;
            else if (op >= OP_ADD_ASSIGN && op <= OP_POW_ASSIGN) consumes = 1;
            else if (op == OP_NEG || op == OP_NOT || op == OP_BNOT || op == OP_SIN || op == OP_COS || op == OP_TAN || op == OP_SQRT || op == OP_LOG || op == OP_EXP || op == OP_ABS || op == OP_FLOOR || op == OP_CEIL || op == OP_ROUND || op == OP_CBRT || op == OP_ASIN || op == OP_ACOS || op == OP_ATAN || op == OP_STORE || op == OP_STORE_KEEP || op == OP_POP || op == OP_ASSIGN_VAR || op == OP_BIND || op == OP_ALLOC) consumes = 1;
            else if (op == OP_DYN_CALL) consumes = program_bank[target][pc].val + 1;
            else if (op == OP_DYN_CALL_IF_FUNC) consumes = 1;
            else if (op == OP_VEC) consumes = (int32_t)getF(program_bank[target][pc-1].val) + 1; 
            
            stack_need = stack_need - produces + consumes;
        }
        pc--;
    }
    return pc + 1;
}

static void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, int* cond_starts, int& cs_ptr, OpCode stopAt = OP_NONE, int minPrec = -1, bool stopAtMarker = false) {
    while (ot >= 0 && os[ot] != stopAt) {
        if (stopAtMarker && os[ot] == OP_STORE) break;
        if (os[ot] == OP_DYN_CALL) break; 
        if (minPrec != -1 && getPrecedence(os[ot]) < minPrec) break;
        if (len < 512) {
            if (os[ot] == OP_ASSIGN_VAR) { program_bank[target][len++] = {OP_STORE_KEEP, os_id[ot--]}; }
            else if (os[ot] == OP_STORE) { program_bank[target][len++] = {OP_STORE, os_id[ot--]}; }
            else if (os[ot] == OP_STORE_AT) {
                int val_start = get_expr_start(target, len - 1);
                int base_start = get_expr_start(target, val_start - 1);
                int idx_start = get_expr_start(target, base_start - 1);
                
                int val_len = len - val_start;
                int base_len = val_start - base_start;
                int idx_len = base_start - idx_start;
                
                Instruction temp[512];
                memcpy(temp, &program_bank[target][idx_start], (idx_len + base_len + val_len) * sizeof(Instruction));
                
                int ptr = idx_start;
                memcpy(&program_bank[target][ptr], &temp[idx_len + base_len], val_len * sizeof(Instruction)); ptr += val_len;
                memcpy(&program_bank[target][ptr], &temp[0], idx_len * sizeof(Instruction)); ptr += idx_len;
                memcpy(&program_bank[target][ptr], &temp[idx_len], base_len * sizeof(Instruction)); ptr += base_len;
                
                program_bank[target][len++] = {OP_STORE_AT, 0}; 
                ot--;
            }
            else if (os[ot] == OP_COND) {
                if (cs_ptr >= 0) {
                    program_bank[target][len++] = {OP_RET, 0};
                    int start = cond_starts[cs_ptr--];
                    program_bank[target][start].val = len - start; 
                }
                program_bank[target][len++] = {OP_COND, 0}; ot--;
            }
            else if (os[ot] == OP_SC_AND || os[ot] == OP_SC_OR) {
                int start = os_id[ot];
                program_bank[target][start].val = len - start; 
                ot--;
            }
            else if (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN) { program_bank[target][len++] = {os[ot], os_id[ot]}; ot--; }
            else { program_bank[target][len++] = {os[ot--], 0}; }
        } else ot--;
    }
}

bool compileRPN(String input) {
    clear_global_array(); 
    var_count = 0; memset(vars, 0, sizeof(vars)); 
    
    // PERFECTLY SYNCED CONSTANT DICTIONARY
    vars[getVarId("PI")] = {0, setF(M_PI)};
    vars[getVarId("pi")] = {0, setF(M_PI)};
    vars[getVarId("TAU")] = {0, setF(M_PI * 2.0)};
    vars[getVarId("tau")] = {0, setF(M_PI * 2.0)};
    vars[getVarId("E")]  = {0, setF(M_E)};
    vars[getVarId("e")]  = {0, setF(M_E)};
    vars[getVarId("LOG2E")] = {0, setF(M_LOG2E)};
    vars[getVarId("LOG10E")] = {0, setF(M_LOG10E)};
    vars[getVarId("LN2")] = {0, setF(M_LN2)};
    vars[getVarId("LN10")] = {0, setF(M_LN10)};
    vars[getVarId("PI2")] = {0, setF(M_PI_2)};
    vars[getVarId("PI4")] = {0, setF(M_PI_4)};
    vars[getVarId("INVPI")] = {0, setF(M_1_PI)};
    vars[getVarId("INVPI2")] = {0, setF(M_2_PI)};
    vars[getVarId("INVSQRTPI")] = {0, setF(M_2_SQRTPI)};
    vars[getVarId("SQRT2")] = {0, setF(M_SQRT2)};
    vars[getVarId("SQRT12")] = {0, setF(M_SQRT1_2)};
    
    uint8_t target = 1 - active_bank; 
    memset(program_bank[target], 0, sizeof(Instruction) * 512);
    int len = 0; 
    int rpn_func_arity[64]; for (int i = 0; i < 64; i++) rpn_func_arity[i] = -1;
    int block_arity_stack[32]; int bas_ptr = -1; int last_completed_arity = 0;
    
    String tokens[512]; int tok_cnt = tokenize(input, tokens, 512);
    int block_starts[32]; int bs_ptr = -1;
    int block_params[32][8]; int bp_counts[32]; int bp_ptr = -1;
    int current_params[8]; int cp_cnt = 0;
    bool parsing_params = false;

    for (int i = 0; i < tok_cnt; i++) {
        if (len >= 512) return false;
        String s = tokens[i];
        bool negate = false;
        if (s.startsWith("-") && s.length() > 1 && !isdigit(s[1]) && s[1] != '.' && s != "-=") { negate = true; s = s.substring(1); }

        if (s.startsWith("&") && s.length() > 1 && isalpha(s[1])) {
            int id = getVarId(s.substring(1));
            program_bank[target][len++] = {OP_LOAD, id};
        }
        else if (s == "(") {
            bool is_params = false;
            for (int j = i + 1; j < tok_cnt; j++) { if (tokens[j] == ")") { if (j + 1 < tok_cnt && tokens[j+1] == "{") is_params = true; break; } }
            if (is_params) { cp_cnt = 0; parsing_params = true; } 
            else { 
                if (bs_ptr < 31) block_starts[++bs_ptr] = len; 
                program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
                if (bp_ptr < 31) { bp_ptr++; bp_counts[bp_ptr] = 0; }
            }
        }
        else if (s == ")") {
            if (parsing_params) parsing_params = false;
            else {
                if (bs_ptr < 0) return false;
                int start = block_starts[bs_ptr--]; bp_ptr--; 
                program_bank[target][len++] = {OP_RET, 0}; 
                program_bank[target][start].val = len - start; 
            }
        }
        else if (s == "{") {
            if (bs_ptr < 31) block_starts[++bs_ptr] = len; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0};
            for (int k = cp_cnt - 1; k >= 0; k--) program_bank[target][len++] = {OP_BIND, current_params[k]};
            if (bp_ptr < 31) {
                bp_ptr++; bp_counts[bp_ptr] = cp_cnt;
                for (int k = 0; k < cp_cnt; k++) block_params[bp_ptr][k] = current_params[k];
            }
            if (bas_ptr < 31) block_arity_stack[++bas_ptr] = cp_cnt;
            cp_cnt = 0;
        }
        else if (s == "}") {
            if (bs_ptr < 0) return false;
            int start = block_starts[bs_ptr--]; 
            int p_cnt = bp_counts[bp_ptr];
            for (int k = 0; k < p_cnt; k++) program_bank[target][len++] = {OP_UNBIND, block_params[bp_ptr][k]};
            bp_ptr--;
            program_bank[target][len++] = {OP_RET, 0}; 
            program_bank[target][start].val = len - start; 
            if (bas_ptr >= 0) last_completed_arity = block_arity_stack[bas_ptr--];
        }
        else if (s == "=" || s == ":=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=" || s == "&=" || s == "|=" || s == "^=" || s == "<<=" || s == ">>=" || s == "**=") {
            if (len >= 2 && (program_bank[target][len-1].op == OP_DYN_CALL_IF_FUNC || program_bank[target][len-1].op == OP_DYN_CALL) && program_bank[target][len-2].op == OP_LOAD) {
                int var_id = program_bank[target][len-2].val; 
                
                if (s == "=") program_bank[target][len-2].op = OP_STORE; 
                else if (s == ":=") program_bank[target][len-2].op = OP_STORE_KEEP;
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
                
                if ((s == "=" || s == ":=") && len >= 2 && program_bank[target][len-2].op == OP_RET) {
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
                else if ((s == "=" || s == ":=") && len >= 2 && program_bank[target][len-2].op == OP_LOAD) {
                    int src_id = program_bank[target][len-2].val;
                    if (src_id < 64 && var_id < 64 && rpn_func_arity[src_id] != -1) rpn_func_arity[var_id] = rpn_func_arity[src_id];
                }
            } else return false;
        }
        else if (s.startsWith("'") && s.endsWith("'")) {
            int count = 0;
            for (size_t k = 1; k < s.length() - 1; k++) {
                float val = (s[k] >= '0' && s[k] <= '9') ? (s[k] - '0') : s[k];
                program_bank[target][len++] = {OP_VAL, setF(val)}; count++;
            }
            program_bank[target][len++] = {OP_VAL, setF(count)}; program_bank[target][len++] = {OP_VEC, 0}; 
        }
        else if (parsing_params) { if (cp_cnt < 8) current_params[cp_cnt++] = getVarId(s); }
        else {
            if (isdigit(s[0]) || (s[0] == '-' && isdigit(s[1])) || (s[0] == '.' && isdigit(s[1])) || (s.startsWith("-.") && s.length() > 2 && isdigit(s[2]))) {
                program_bank[target][len++] = {OP_VAL, setF(strtof(s.c_str(), NULL))};
            }
            else if (s == "t") program_bank[target][len++] = {OP_T, 0};
            else if (s == ";") program_bank[target][len++] = {OP_POP, 0}; 
            else if (s == "?") program_bank[target][len++] = {OP_COND, 0};
            else if (s.startsWith("call")) {
                int args = 0;
                if (s.length() > 4) args = s.substring(4).toInt();
                program_bank[target][len++] = {OP_DYN_CALL, args};
            }
            else {
                bool is_math = false;
                for (int _m = 0; _m < mathLibrarySize; _m++) {
                    if (s == mathLibrary[_m].name) { program_bank[target][len++] = {mathLibrary[_m].code, 0}; is_math = true; break; }
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

bool compileInfix(String input, bool reset_t) {
    clear_global_array();
    var_count = 0; memset(vars, 0, sizeof(vars)); 
    
    vars[getVarId("PI")] = {0, setF(M_PI)};
    vars[getVarId("pi")] = {0, setF(M_PI)};
    vars[getVarId("TAU")] = {0, setF(M_PI * 2.0)};
    vars[getVarId("tau")] = {0, setF(M_PI * 2.0)};
    vars[getVarId("E")]  = {0, setF(M_E)};
    vars[getVarId("e")]  = {0, setF(M_E)};
    vars[getVarId("LOG2E")] = {0, setF(M_LOG2E)};
    vars[getVarId("LOG10E")] = {0, setF(M_LOG10E)};
    vars[getVarId("LN2")] = {0, setF(M_LN2)};
    vars[getVarId("LN10")] = {0, setF(M_LN10)};
    vars[getVarId("PI2")] = {0, setF(M_PI_2)};
    vars[getVarId("PI4")] = {0, setF(M_PI_4)};
    vars[getVarId("INVPI")] = {0, setF(M_1_PI)};
    vars[getVarId("INVPI2")] = {0, setF(M_2_PI)};
    vars[getVarId("INVSQRTPI")] = {0, setF(M_2_SQRTPI)};
    vars[getVarId("SQRT2")] = {0, setF(M_SQRT2)};
    vars[getVarId("SQRT12")] = {0, setF(M_SQRT1_2)};
    
    uint8_t target = 1 - active_bank; memset(program_bank[target], 0, sizeof(Instruction) * 512);
    OpCode os[256]; int os_id[256]; int ot = -1; int len = 0; const char* p = input.c_str();
    int paren_depth = 0; int cond_depth = 0;
    int call_arg_counts[128]; int cac_ptr = -1;
    int cond_starts[128]; int cs_ptr = -1;
    LambdaCtx open_lambdas[64]; int ol_ptr = -1;
    int bracket_types[128]; int bt_ptr = -1;
    int array_counts[128]; int ac_ptr = -1;
    bool expect_op = false; 

    while (*p) {
        if (len >= 512 || ot >= 255) return false; 
        
        if (*p == ')' || *p == ',' || *p == ';' || *p == ']') {
            while (ol_ptr >= 0) {
                auto& lam = open_lambdas[ol_ptr];
                if (!lam.uses_braces && paren_depth <= lam.depth) {
                    flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
                    if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                    
                    int assigns_to_keep[64]; int atk_cnt = 0;
                    while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                        if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
                    }
                    
                    for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
                    program_bank[target][len++] = {OP_RET, 0}; 
                    program_bank[target][lam.start_pc].val = len - lam.start_pc; 
                    for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, assigns_to_keep[i]};
                    ol_ptr--; expect_op = true;
                } else break;
            }
        }

        if (isspace(*p)) { p++; continue; }
        
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1)))) { 
            if (expect_op) return false; expect_op = true;
            program_bank[target][len++] = {OP_VAL, setF(strtof(p, (char**)&p))}; 
        }
        else if (isalpha(*p)) { 
            String word = ""; while (isalpha(*p) || isdigit(*p) || *p == '_') word += *p++; 
            if (word == "t") { if (expect_op) return false; expect_op = true; program_bank[target][len++] = {OP_T, 0}; continue; }
            if (word == "return") { expect_op = false; continue; } 
            
            bool is_math = false;
            for (int _m = 0; _m < mathLibrarySize; _m++) { 
                if (word == mathLibrary[_m].name) { if (expect_op) return false; expect_op = false; os[++ot] = mathLibrary[_m].code; os_id[ot] = 0; is_math = true; break; } 
            }
            if (is_math) continue; 
            
            const char* temp = p; while (isspace(*temp)) temp++;
            if (*temp == '=' && *(temp+1) == '>') {
                if (expect_op) return false;
                int pid = getVarId(word); int start_pc = len;
                program_bank[target][len++] = {OP_PUSH_FUNC, 0}; program_bank[target][len++] = {OP_BIND, pid};
                
                temp += 2; while (isspace(*temp)) temp++;
                bool braces = (*temp == '{'); if (braces) temp++;
                
                if (ol_ptr < 63) {
                    ol_ptr++; open_lambdas[ol_ptr].depth = paren_depth; open_lambdas[ol_ptr].p_cnt = 1;
                    open_lambdas[ol_ptr].p_ids[0] = pid; open_lambdas[ol_ptr].start_pc = start_pc; open_lambdas[ol_ptr].uses_braces = braces;
                }
                os[++ot] = OP_NONE; p = temp; expect_op = false; continue;
            }

            int id = getVarId(word); while (isspace(*p)) p++;
            if (expect_op) return false; 

            if (*p == '(') { os[++ot] = OP_DYN_CALL; os_id[ot] = id; expect_op = false; } 
            else { program_bank[target][len++] = {OP_LOAD, id}; expect_op = true; }
        }
        else if (*p == '\'') {
            if (expect_op) return false; p++; int count = 0;
            while (*p && *p != '\'') { float val = (*p >= '0' && *p <= '9') ? (*p - '0') : *p; program_bank[target][len++] = {OP_VAL, setF(val)}; count++; p++; }
            if (*p == '\'') p++;
            program_bank[target][len++] = {OP_VAL, setF(count)}; program_bank[target][len++] = {OP_VEC, 1}; 
            expect_op = true;
        }
        else if (*p == '$' && *(p+1) == '[') {
            if (expect_op) return false;
            os[++ot] = OP_NONE; paren_depth++; if (bt_ptr < 127) bracket_types[++bt_ptr] = 3; 
            p += 2; expect_op = false;
        }
        else if (*p == '(') { 
            if (expect_op) {
                os[++ot] = OP_DYN_CALL; os_id[ot] = -1;
                expect_op = false;
            }
            
            String params[8]; int param_cnt = 0, consume_len = 0;
            if (isLambdaDef(p, params, param_cnt, consume_len)) {
                int start_pc = len; program_bank[target][len++] = {OP_PUSH_FUNC, 0};
                LambdaCtx lam; lam.depth = paren_depth; lam.p_cnt = param_cnt; lam.start_pc = start_pc;
                for (int i = param_cnt - 1; i >= 0; i--) {
                    int pid = getVarId(params[i]); lam.p_ids[i] = pid; program_bank[target][len++] = {OP_BIND, pid};
                }
                p += consume_len; while (isspace(*p)) p++;
                bool braces = (*p == '{'); if (braces) p++;
                lam.uses_braces = braces; if (ol_ptr < 63) open_lambdas[++ol_ptr] = lam;
                os[++ot] = OP_NONE; expect_op = false; continue;
            }
            os[++ot] = OP_NONE; paren_depth++; if (bt_ptr < 127) bracket_types[++bt_ptr] = 0;
            const char* temp = p + 1; while (isspace(*temp)) temp++;
            if (cac_ptr < 127) call_arg_counts[++cac_ptr] = (*temp == ')') ? 0 : 1;
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
                    if (os[ot] == OP_DYN_CALL) { 
                        if (os_id[ot] != -1) {
                            program_bank[target][len++] = {OP_LOAD, os_id[ot]}; 
                            program_bank[target][len++] = {OP_DYN_CALL, args}; 
                        } else {
                            if (args > 0) {
                                int args_start = len;
                                for (int i = 0; i < args; i++) {
                                    args_start = get_expr_start(target, args_start - 1);
                                }
                                int expr_start = get_expr_start(target, args_start - 1);
                                
                                int expr_len = args_start - expr_start;
                                int args_len = len - args_start;
                                
                                Instruction temp[512];
                                memcpy(temp, &program_bank[target][expr_start], (expr_len + args_len) * sizeof(Instruction));
                                
                                int ptr = expr_start;
                                memcpy(&program_bank[target][ptr], &temp[expr_len], args_len * sizeof(Instruction)); ptr += args_len;
                                memcpy(&program_bank[target][ptr], &temp[0], expr_len * sizeof(Instruction));
                            }
                            program_bank[target][len++] = {OP_DYN_CALL, args}; 
                        }
                        ot--; 
                    } 
                    else if (os[ot] >= OP_SIN && os[ot] <= OP_POW) { program_bank[target][len++] = {os[ot--], 0}; }
                }
            }
            paren_depth--; p++; expect_op = true; 
        }
        else if (*p == '[') {
            if (expect_op) { os[++ot] = OP_NONE; paren_depth++; if (bt_ptr < 127) bracket_types[++bt_ptr] = 2; expect_op = false; } 
            else {
                os[++ot] = OP_NONE; paren_depth++; if (bt_ptr < 127) bracket_types[++bt_ptr] = 1;
                const char* temp = p + 1; while (isspace(*temp)) temp++;
                if (ac_ptr < 127) array_counts[++ac_ptr] = (*temp == ']') ? 0 : 1;
                expect_op = false;
            }
            p++;
        }
        else if (*p == ']') {
            if (paren_depth <= 0 || bt_ptr < 0) return false;
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
            if (ot >= 0) ot--; 
            int btype = bracket_types[bt_ptr--];
            
            if (btype == 1) { 
                int size = 0; if (ac_ptr >= 0) size = array_counts[ac_ptr--]; 
                program_bank[target][len++] = {OP_VAL, setF(size)}; program_bank[target][len++] = {OP_VEC, 0}; expect_op = true;
            } 
            else if (btype == 3) {
                program_bank[target][len++] = {OP_ALLOC, 0}; expect_op = true;
            }
            else { 
                int idx_start = get_expr_start(target, len - 1);
                int base_start = get_expr_start(target, idx_start - 1);
                int idx_len = len - idx_start;
                int base_len = idx_start - base_start;
                
                Instruction temp[512];
                memcpy(temp, &program_bank[target][base_start], (base_len + idx_len) * sizeof(Instruction));
                
                int ptr = base_start;
                memcpy(&program_bank[target][ptr], &temp[base_len], idx_len * sizeof(Instruction)); ptr += idx_len;
                memcpy(&program_bank[target][ptr], &temp[0], base_len * sizeof(Instruction));
                
                program_bank[target][len++] = {OP_AT, 0}; expect_op = true;
            }
            paren_depth--; p++;
        }
        else if (*p == '{') { return false; } 
        else if (*p == '}') {
            if (ol_ptr >= 0 && open_lambdas[ol_ptr].uses_braces) {
                auto& lam = open_lambdas[ol_ptr];
                flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true);
                if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                int assigns_to_keep[64]; int atk_cnt = 0;
                while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                    if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
                }
                for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
                program_bank[target][len++] = {OP_RET, 0}; 
                program_bank[target][lam.start_pc].val = len - lam.start_pc; 
                for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, assigns_to_keep[i]};
                ol_ptr--; p++; expect_op = true; 
            } else return false;
        }
        else if (*p == '?') { 
            if (!expect_op) return false; expect_op = false;
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, getPrecedence(OP_COND) + 1, true); 
            os[++ot] = OP_COND; cond_depth++; p++; program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
            if (cs_ptr < 127) cond_starts[++cs_ptr] = len - 1;
        }
        else if (*p == ':') { 
            if (!expect_op) return false; expect_op = false; if (cond_depth <= 0) return false; 
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_COND, -1, true); 
            program_bank[target][len++] = {OP_RET, 0}; if (cs_ptr < 0) return false;
            int start = cond_starts[cs_ptr--];
            program_bank[target][start].val = len - start; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; if (cs_ptr < 127) cond_starts[++cs_ptr] = len - 1; 
            cond_depth--; p++; 
        }
        else if (*p == ';') { expect_op = false; flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); program_bank[target][len++] = {OP_POP, 0}; p++; }
        else if (*p == ',') { 
            if (!expect_op) return false; expect_op = false; 
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); 
            
            bool is_func_call = (ot > 0 && (os[ot-1] == OP_DYN_CALL || os[ot-1] == OP_DYN_CALL_IF_FUNC || (os[ot-1] >= OP_SIN && os[ot-1] <= OP_POW)));
            bool is_array = (bt_ptr >= 0 && bracket_types[bt_ptr] == 1);
            if (is_func_call) call_arg_counts[cac_ptr]++; 
            else if (is_array) array_counts[ac_ptr]++; 
            else program_bank[target][len++] = {OP_POP, 0}; 
            
            p++; 
        }
        else if (*p == '=' && *(p+1) != '=' && *(p+1) != '>') {
            if (len > 0 && program_bank[target][len - 1].op == OP_AT) {
                len--; 
                os[++ot] = OP_STORE_AT; os_id[ot] = 0; p++; expect_op = false;
            } else {
                int load_idx = -1;
                for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
                if (load_idx != -1) {
                    int var_id = program_bank[target][load_idx].val;
                    for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                    len--; os[++ot] = OP_ASSIGN_VAR; os_id[ot] = var_id; p++; expect_op = false;
                } else return false;
            }
        }
        else if (*p == '+' && *(p+1) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_ADD_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '-' && *(p+1) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_SUB_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '*' && *(p+1) == '*' && *(p+2) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_POW_ASSIGN; os_id[ot] = var_id; p += 3; expect_op = false;
            } else return false;
        }
        else if (*p == '*' && *(p+1) == '=' && *(p+2) != '*') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_MUL_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '/' && *(p+1) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_DIV_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '%' && *(p+1) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_MOD_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '&' && *(p+1) == '=' && *(p+2) != '&') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_AND_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '|' && *(p+1) == '=' && *(p+2) != '|') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_OR_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '^' && *(p+1) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_XOR_ASSIGN; os_id[ot] = var_id; p += 2; expect_op = false;
            } else return false;
        }
        else if (*p == '<' && *(p+1) == '<' && *(p+2) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_SHL_ASSIGN; os_id[ot] = var_id; p += 3; expect_op = false;
            } else return false;
        }
        else if (*p == '>' && *(p+1) == '>' && *(p+2) == '=') {
            int load_idx = -1;
            for (int k = len - 1; k >= 0 && k >= len - 10; k--) if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
            if (load_idx != -1) {
                int var_id = program_bank[target][load_idx].val;
                for (int k = load_idx; k < len - 1; k++) program_bank[target][k] = program_bank[target][k+1];
                len--; os[++ot] = OP_SHR_ASSIGN; os_id[ot] = var_id; p += 3; expect_op = false;
            } else return false;
        }
        else {
            String opStr = ""; opStr += *p;
            if (*(p+1) && *(p+2) && strchr("=<>!&|*", *(p+1)) && *(p+2) == '=') { String test3 = opStr + *(p+1) + *(p+2); OpCode dummy; if (getOpCode(test3, dummy)) opStr = test3; }
            if (opStr.length() == 1 && *(p+1) && strchr("=<>!&|*", *(p+1))) { String test2 = opStr + *(p+1); OpCode dummy; if (getOpCode(test2, dummy)) opStr = test2; }
            OpCode cur = OP_NONE; getOpCode(opStr, cur); if (cur == OP_SUB && !expect_op) cur = OP_NEG;
            if (cur != OP_NONE) { 
                bool is_unary = (cur == OP_NOT || cur == OP_NEG || cur == OP_BNOT);
                if (!expect_op && !is_unary) return false; expect_op = false;
                int prec = getPrecedence(cur); if (cur == OP_POW) prec++; 
                flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, prec, true); 
                if (cur == OP_SC_AND || cur == OP_SC_OR) { program_bank[target][len++] = {cur, 0}; os[++ot] = cur; os_id[ot] = len - 1; } 
                else { os[++ot] = cur; os_id[ot] = 0; }
                p += opStr.length(); 
            } else return false; 
        }
    }
    
    while (ol_ptr >= 0) {
        auto& lam = open_lambdas[ol_ptr];
        if (!lam.uses_braces) {
            flushOps(target, len, os, os_id, ot, cond_starts, cs_ptr, OP_NONE, -1, true); if (ot >= 0 && os[ot] == OP_NONE) ot--; 
            int assigns_to_keep[64]; int atk_cnt = 0;
            while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
            }
            for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, lam.p_ids[i]};
            program_bank[target][len++] = {OP_RET, 0}; 
            program_bank[target][lam.start_pc].val = len - lam.start_pc; 
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
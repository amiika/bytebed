#include "compiler.h"
#include <memory>

extern String last_vm_error;

#if defined(__EMSCRIPTEN__) || defined(NATIVE_BUILD)
    #define P_ERR(shortMsg, longMsg) do { last_vm_error = String(longMsg); return false; } while(0)
#else
    #define P_ERR(shortMsg, longMsg) do { last_vm_error = String(shortMsg); return false; } while(0)
#endif

/**
 * Compiles an infix formula into VM bytecode.
 * @param input The infix expression to compile
 * @param reset_t Determines if the playback time should be reset
 * @return true if compilation succeeds, false otherwise
 */
bool compileInfix(String input, bool reset_t) {
    initCompilerState();
    last_vm_error = "";
    
    uint8_t target = 1 - active_bank; memset(program_bank[target], 0, sizeof(Instruction) * 512);
    
    std::unique_ptr<OpCode[]> os(new OpCode[256]);
    std::unique_ptr<int[]> os_id(new int[256]);
    std::unique_ptr<int[]> call_arg_counts(new int[128]);
    std::unique_ptr<int[]> cond_starts(new int[128]);
    std::unique_ptr<LambdaCtx[]> open_lambdas(new LambdaCtx[64]);
    std::unique_ptr<int[]> bracket_types(new int[128]);
    std::unique_ptr<int[]> array_counts(new int[128]);
    
    int ot = -1; int len = 0; const char* p = input.c_str();
    int paren_depth = 0; int cond_depth = 0;
    int cac_ptr = -1; int cs_ptr = -1; int ol_ptr = -1;
    int bt_ptr = -1; int ac_ptr = -1;
    bool expect_op = false; 

    String wordBuffer;

    #if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
        wordBuffer.reserve(16); // Cardputer optimization
    #endif

    while (*p) {
        if (len >= 512) P_ERR("ERR: PROG>512", "Compile Error: Program exceeds 512 instructions");
        if (ot >= 255) P_ERR("ERR: OT>255", "Compile Error: Operator stack overflow (formula too complex)");
        
        if (*p == ')' || *p == ',' || *p == ';' || *p == ']') {
            while (ol_ptr >= 0) {
                auto& lam = open_lambdas[ol_ptr];
                if (!lam.uses_braces && paren_depth <= lam.depth) {
                    flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true);
                    if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                    
                    int assigns_to_keep[64]; int atk_cnt = 0;
                    while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                        if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
                    }
                    
                    for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, (int32_t)lam.p_ids[i]};
                    program_bank[target][len++] = {OP_RET, 0}; 
                    program_bank[target][lam.start_pc].val = (int32_t)(len - lam.start_pc); 
                    for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, (int32_t)assigns_to_keep[i]};
                    ol_ptr--; expect_op = true;
                } else break;
            }
        }

        if (isspace(*p)) { p++; continue; }
        
        if (isdigit(*p) || (*p == '.' && isdigit(*(p+1)))) { 
            if (expect_op) P_ERR("ERR: .?", "Compile Error: Expected operator before number");
            expect_op = true;
            program_bank[target][len++] = {OP_VAL, setF(strtof(p, (char**)&p))}; 
        }
        else if (isalpha(*p)) { 
            wordBuffer = ""; 
            while (isalpha(*p) || isdigit(*p) || *p == '_') wordBuffer += *p++; 
            if (wordBuffer == "return") { expect_op = false; continue; } 
            
            bool is_math = false;
            for (int _m = 0; _m < mathLibrarySize; _m++) { 
                if (wordBuffer == mathLibrary[_m].name) { 
                    if (expect_op) P_ERR("ERR: "+wordBuffer, "Compile Error: Expected operator before function '" + wordBuffer + "'");
                    expect_op = false; os[++ot] = mathLibrary[_m].code; os_id[ot] = 0; is_math = true; break; 
                } 
            }
            if (!is_math && (wordBuffer == "sum" || wordBuffer == "gen" || wordBuffer == "map" || wordBuffer == "reduce" || wordBuffer == "filter")) {
                if (expect_op) P_ERR("ERR: "+wordBuffer, "Compile Error: Expected operator before iterator '" + wordBuffer + "'");
                expect_op = false; 
                os[++ot] = OP_LOOP_PREP; 
                int ltype = 0; if (wordBuffer == "gen") ltype = 1; else if (wordBuffer == "map") ltype = 2; else if (wordBuffer == "reduce") ltype = 3; else if (wordBuffer == "filter") ltype = 4;
                os_id[ot] = ltype; 
                is_math = true; 
            }
            if (!is_math && !isVarDefined(wordBuffer)) {
                const char* temp2 = p;
                while (isspace(*temp2)) temp2++;
                if (*temp2 == '(') {
                    for (int _s = 0; _s < shorthandsSize; _s++) {
                        if (wordBuffer == shorthands[_s].name) {
                            if (expect_op) P_ERR("ERR: "+wordBuffer, "Compile Error: Expected operator before shorthand '" + wordBuffer + "'");
                            expect_op = false; os[++ot] = shorthands[_s].code; os_id[ot] = 0; is_math = true; break;
                        }
                    }
                }
            }
            if (is_math) continue; 
            
            const char* temp = p; while (isspace(*temp)) temp++;
            if (*temp == '=' && *(temp+1) == '>') {
                if (expect_op) P_ERR("ERR: =>", "Compile Error: Unexpected '=>' after variable");
                int pid = getVarId(wordBuffer); int start_pc = len;
                program_bank[target][len++] = {OP_PUSH_FUNC, 0}; program_bank[target][len++] = {OP_BIND, (int32_t)pid};
                
                temp += 2; while (isspace(*temp)) temp++;
                bool braces = (*temp == '{'); if (braces) temp++;
                
                if (ol_ptr < 63) {
                    ol_ptr++; open_lambdas[ol_ptr].depth = paren_depth; open_lambdas[ol_ptr].p_cnt = 1;
                    open_lambdas[ol_ptr].p_ids[0] = pid; open_lambdas[ol_ptr].start_pc = start_pc; open_lambdas[ol_ptr].uses_braces = braces;
                }
                os[++ot] = OP_NONE; p = temp; expect_op = false; continue;
            }

            int id = getVarId(wordBuffer); while (isspace(*p)) p++;
            if (expect_op) P_ERR("ERR: "+wordBuffer, "Compile Error: Expected operator before variable '" + wordBuffer + "'");

            if (*p == '(') { os[++ot] = OP_DYN_CALL; os_id[ot] = id; expect_op = false; } 
            else { program_bank[target][len++] = {OP_LOAD, (int32_t)id}; expect_op = true; }
        }
        else if (*p == '\'') {
            if (expect_op) P_ERR("ERR: \'", "Compile Error: Expected operator before string literal");
            p++; int count = 0;
            while (*p && *p != '\'') { 
                char c = *p; float val;
                if (c >= '0' && c <= '9') val = c - '0';
                else if (c >= 'a' && c <= 'z') val = c - 'a' + 10;
                else if (c >= 'A' && c <= 'Z') val = c - 'A' + 10;
                else val = c;
                program_bank[target][len++] = {OP_VAL, setF(val)}; count++; p++; 
            }
            if (*p == '\'') p++;
            program_bank[target][len++] = {OP_VAL, setF((float)count)}; program_bank[target][len++] = {OP_VEC, 1}; 
            expect_op = true;
        }
        else if (*p == '$' && *(p+1) == '[') {
            if (expect_op) P_ERR("ERR: ?=$", "Compile Error: Expected operator before allocation '$['");
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
                    int pid = getVarId(params[i]); lam.p_ids[i] = pid; program_bank[target][len++] = {OP_BIND, (int32_t)pid};
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
            if (paren_depth <= 0 || (bt_ptr >= 0 && bracket_types[bt_ptr] != 0)) P_ERR("ERR: (", "Compile Error: Unmatched closing parenthesis ')'"); 
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true); 
            int args = 0; if (cac_ptr >= 0) args = call_arg_counts[cac_ptr--]; 
            if (bt_ptr >= 0) bt_ptr--;
            if (ot >= 0) { 
                ot--; 
                if (ot >= 0) {
                    if (os[ot] == OP_DYN_CALL) { 
                        if (os_id[ot] != -1) {
                            program_bank[target][len++] = {OP_LOAD, (int32_t)os_id[ot]}; 
                            program_bank[target][len++] = {OP_DYN_CALL, (int32_t)args}; 
                        } else {
                            if (args > 0) {
                                int args_start = len;
                                for (int i = 0; i < args; i++) {
                                    args_start = get_expr_start(target, args_start - 1);
                                }
                                int expr_start = get_expr_start(target, args_start - 1);
                                
                                int expr_len = args_start - expr_start;
                                int args_len = len - args_start;
                                
                                std::unique_ptr<Instruction[]> temp(new Instruction[512]);
                                memcpy(temp.get(), &program_bank[target][expr_start], (expr_len + args_len) * sizeof(Instruction));
                                
                                int ptr = expr_start;
                                memcpy(&program_bank[target][ptr], &temp[expr_len], args_len * sizeof(Instruction)); ptr += args_len;
                                memcpy(&program_bank[target][ptr], &temp[0], expr_len * sizeof(Instruction));
                            }
                            program_bank[target][len++] = {OP_DYN_CALL, (int32_t)args}; 
                        }
                        ot--; 
                    } 
                    else if ((os[ot] >= OP_SIN && os[ot] <= OP_POW) || os[ot] == OP_INT || os[ot] == OP_RAND || os[ot] == OP_LOOP_PREP) {
                        if (os[ot] == OP_LOOP_PREP) {
                            program_bank[target][len++] = {os[ot], (int32_t)os_id[ot]}; 
                            int start_pc = len;
                            program_bank[target][len++] = {OP_LOOP_EVAL, 0};
                            int eval_pc = len - 1;
                            program_bank[target][len++] = {OP_LOOP_DONE, 0};
                            
                            program_bank[target][eval_pc].val = (int32_t)(len - eval_pc);
                            program_bank[target][len - 1].val = (int32_t)(len - start_pc + 1);
                            ot--;
                        } else {
                            program_bank[target][len++] = {os[ot--], 0}; 
                        }
                    }
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
            if (paren_depth <= 0 || bt_ptr < 0) P_ERR("ERR: [", "Compile Error: Unmatched closing bracket ']'");
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true);
            if (ot >= 0) ot--; 
            int btype = bracket_types[bt_ptr--];
            
            if (btype == 1) { 
                int size = 0; if (ac_ptr >= 0) size = array_counts[ac_ptr--]; 
                program_bank[target][len++] = {OP_VAL, setF((float)size)}; program_bank[target][len++] = {OP_VEC, 0}; expect_op = true;
            } 
            else if (btype == 3) {
                program_bank[target][len++] = {OP_ALLOC, 0}; expect_op = true;
            }
            else { 
                program_bank[target][len++] = {OP_AT, 0}; expect_op = true;
            }
            paren_depth--; p++;
        }
        else if (*p == '.') {
            if (!expect_op) P_ERR("ERR: .", "Compile Error: Unexpected '.' operator");
            p++;
            wordBuffer = "";
            while (isalpha(*p) || isdigit(*p) || *p == '_') wordBuffer += *p++;
            if (wordBuffer == "sum" || wordBuffer == "gen" || wordBuffer == "map" || wordBuffer == "reduce" || wordBuffer == "filter") {
                os[++ot] = OP_LOOP_PREP;
                int ltype = 0;
                if (wordBuffer == "gen") ltype = 1;
                else if (wordBuffer == "map") ltype = 2;
                else if (wordBuffer == "reduce") ltype = 3;
                else if (wordBuffer == "filter") ltype = 4;
                os_id[ot] = ltype;
                expect_op = false; 
            } else {
                P_ERR("ERR: ."+wordBuffer, "Compile Error: Unrecognized array method '." + wordBuffer + "'");
            }
        }
        else if (*p == '{') { P_ERR("ERR: {", "Compile Error: Unexpected opening brace '{'"); } 
        else if (*p == '}') {
            if (ol_ptr >= 0 && open_lambdas[ol_ptr].uses_braces) {
                auto& lam = open_lambdas[ol_ptr];
                flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true);
                if (ot >= 0 && os[ot] == OP_NONE) ot--; 
                int assigns_to_keep[64]; int atk_cnt = 0;
                while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                    if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
                }
                for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, (int32_t)lam.p_ids[i]};
                program_bank[target][len++] = {OP_RET, 0}; 
                program_bank[target][lam.start_pc].val = (int32_t)(len - lam.start_pc); 
                for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, (int32_t)assigns_to_keep[i]};
                ol_ptr--; p++; expect_op = true; 
            } else P_ERR("ERR: {", "Compile Error: Unmatched closing brace '}'");
        }
        else if (*p == '?') { 
            if (!expect_op) P_ERR("ERR: ?", "Compile Error: Unexpected ternary '?' operator"); 
            expect_op = false;
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, getPrecedence(OP_COND) + 1, true); 
            os[++ot] = OP_COND; cond_depth++; p++; program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
            if (cs_ptr < 127) cond_starts[++cs_ptr] = len - 1;
        }
        else if (*p == ':') { 
            if (!expect_op) P_ERR("ERR: :", "Compile Error: Unexpected ternary ':' operator"); 
            expect_op = false; 
            if (cond_depth <= 0) P_ERR("ERR: ?", "Compile Error: Unmatched ':' (missing '?')"); 
            
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_COND, -1, true); 
            
            if (ot < 0 || os[ot] != OP_COND) P_ERR("ERR: ?", "Compile Error: Invalid ternary operation structure");
            os[ot] = OP_COLON;
            
            program_bank[target][len++] = {OP_RET, 0}; 
            if (cs_ptr < 0) P_ERR("ERR: ?", "Compile Error: Ternary condition stack underflow");
            int start = cond_starts[cs_ptr--];
            program_bank[target][start].val = (int32_t)(len - start); 
            
            program_bank[target][len++] = {OP_PUSH_FUNC, 0}; 
            if (cs_ptr < 127) cond_starts[++cs_ptr] = len - 1; 
            
            cond_depth--; p++; 
        }
        else if (*p == ';') { expect_op = false; flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true); program_bank[target][len++] = {OP_POP, 0}; p++; }
        else if (*p == ',') { 
            if (!expect_op) P_ERR("ERR: ,", "Compile Error: Unexpected comma ','"); 
            expect_op = false; 
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true); 
            
            bool is_func_call = (ot > 0 && (os[ot-1] == OP_DYN_CALL || os[ot-1] == OP_DYN_CALL_IF_FUNC || (os[ot-1] >= OP_SIN && os[ot-1] <= OP_POW) || os[ot-1] == OP_RAND || os[ot-1] == OP_INT || os[ot-1] == OP_LOOP_PREP));
            bool is_array = (bt_ptr >= 0 && bracket_types[bt_ptr] == 1);
            if (is_func_call) call_arg_counts[cac_ptr]++; 
            else if (is_array) array_counts[ac_ptr]++; 
            else program_bank[target][len++] = {OP_POP, 0}; 
            
            p++; 
        }
        else if (*p == '=' && *(p+1) != '=' && *(p+1) != '>') {
            if (len > 0 && program_bank[target][len - 1].op == OP_AT) {
                len--; os[++ot] = OP_STORE_AT; os_id[ot] = 0; p++; expect_op = false;
            } else {
                if (!applyCompoundAssign(target, len, os.get(), os_id.get(), ot, OP_ASSIGN_VAR)) P_ERR("ERR: ?=", "Compile Error: Invalid left-hand side in assignment '='");
                p++; expect_op = false;
            }
        }
        else {
            OpCode compOp;
            int adv = 0;
            if (parseCompoundOperator(p, compOp, adv)) {
                if (!applyCompoundAssign(target, len, os.get(), os_id.get(), ot, compOp)) P_ERR("ERR: =?", "Compile Error: Invalid left-hand side in compound assignment");
                p += adv; 
                expect_op = false;
            } else {
                String opStr = ""; opStr += *p;
                if (*(p+1) && *(p+2) && strchr("=<>!&|*", *(p+1)) && *(p+2) == '=') { String test3 = opStr + *(p+1) + *(p+2); OpCode dummy; if (getOpCode(test3, dummy)) opStr = test3; }
                if (opStr.length() == 1 && *(p+1) && strchr("=<>!&|*", *(p+1))) { String test2 = opStr + *(p+1); OpCode dummy; if (getOpCode(test2, dummy)) opStr = test2; }
                OpCode cur = OP_NONE; getOpCode(opStr, cur); if (cur == OP_SUB && !expect_op) cur = OP_NEG;
                if (cur != OP_NONE) { 
                    bool is_unary = (cur == OP_NOT || cur == OP_NEG || cur == OP_BNOT);
                    if (!expect_op && !is_unary) P_ERR("ERR: "+opStr, "Compile Error: Unexpected operator '" + opStr + "'"); 
                    expect_op = false;
                    int prec = getPrecedence(cur); if (cur == OP_POW) prec++; 
                    flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, prec, true); 
                    if (cur == OP_SC_AND || cur == OP_SC_OR) { program_bank[target][len++] = {cur, 0}; os[++ot] = cur; os_id[ot] = len - 1; } 
                    else { os[++ot] = cur; os_id[ot] = 0; }
                    p += opStr.length(); 
                } else {
                    P_ERR("ERR: "+*p, String("Compile Error: Unrecognized character or token '") + *p + "'"); 
                }
            }
        }
    }
    
    while (ol_ptr >= 0) {
        auto& lam = open_lambdas[ol_ptr];
        if (!lam.uses_braces) {
            flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr, OP_NONE, -1, true); if (ot >= 0 && os[ot] == OP_NONE) ot--; 
            int assigns_to_keep[64]; int atk_cnt = 0;
            while (ot >= 0 && (os[ot] == OP_STORE || os[ot] == OP_ASSIGN_VAR || os[ot] == OP_STORE_AT || (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN))) {
                if (atk_cnt < 64) assigns_to_keep[atk_cnt++] = os_id[ot]; ot--;
            }
            for (int i = lam.p_cnt - 1; i >= 0; i--) program_bank[target][len++] = {OP_UNBIND, (int32_t)lam.p_ids[i]};
            program_bank[target][len++] = {OP_RET, 0}; 
            program_bank[target][lam.start_pc].val = (int32_t)(len - lam.start_pc); 
            for (int i = 0; i < atk_cnt; i++) program_bank[target][len++] = {OP_STORE_KEEP, (int32_t)assigns_to_keep[i]};
            ol_ptr--;
        } else P_ERR("ERR: =>{", "Compile Error: Unclosed brace in lambda definition"); 
    }

    if (paren_depth != 0 || cond_depth != 0) P_ERR("ERR: )?", "Compile Error: Unmatched parentheses or ternary operators"); 
    flushOps(target, len, os.get(), os_id.get(), ot, cond_starts.get(), cs_ptr);
    
    if (!validateProgram(target, len)) return false;
    
    if (reset_t) t_raw = 0; prog_len_bank[target] = len; active_bank = target; 
    return true;
}

/**
 * Compiles an RPN formula into VM bytecode.
 * @param input The RPN expression to compile
 * @return true if compilation succeeds, false otherwise
 */
bool compileRPN(String input) {
    initCompilerState();
    last_vm_error = "";
    
    uint8_t target = 1 - active_bank; 
    memset(program_bank[target], 0, sizeof(Instruction) * 512);
    int len = 0; 
    int rpn_func_arity[64]; for (int i = 0; i < 64; i++) rpn_func_arity[i] = -1;
    int block_arity_stack[32]; int bas_ptr = -1; int last_completed_arity = 0;
    
    std::unique_ptr<String[]> tokens(new String[512]); 
    int tok_cnt = tokenize(input, tokens.get(), 512);
    
    int block_starts[32]; int bs_ptr = -1;
    int block_params[32][8]; int bp_counts[32]; int bp_ptr = -1;
    int current_params[8]; int cp_cnt = 0;
    bool parsing_params = false;

    for (int i = 0; i < tok_cnt; i++) {
        if (len >= 512) P_ERR("ERR: PROG>512", "RPN Error: Program exceeds 512 instructions");
        String s = tokens[i];
        bool negate = false;
        if (s.startsWith("-") && s.length() > 1 && !isdigit(s[1]) && s[1] != '.' && s != "-=") { negate = true; s = s.substring(1); }

        if (s.startsWith("&") && s.length() > 1 && isalpha(s[1])) {
            int id = getVarId(s.substring(1));
            program_bank[target][len++] = {OP_LOAD, (int32_t)id};
        }
        else if (s == "sum" || s == "gen" || s == "map" || s == "reduce" || s == "filter") {
            int ltype = 0;
            if (s == "gen") ltype = 1; else if (s == "map") ltype = 2; else if (s == "reduce") ltype = 3; else if (s == "filter") ltype = 4;
            program_bank[target][len++] = {OP_LOOP_PREP, ltype};
            int start_pc = len;
            program_bank[target][len++] = {OP_LOOP_EVAL, 0};
            int eval_pc = len - 1;
            program_bank[target][len++] = {OP_LOOP_DONE, 0};
            program_bank[target][eval_pc].val = (int32_t)(len - eval_pc);
            program_bank[target][len - 1].val = (int32_t)(len - start_pc + 1);
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
                if (bs_ptr < 0) P_ERR("ERR: (", "RPN Error: Unmatched closing parenthesis ')'");
                int start = block_starts[bs_ptr--]; bp_ptr--; 
                program_bank[target][len++] = {OP_RET, 0}; 
                program_bank[target][start].val = (int32_t)(len - start); 
            }
        }
        else if (s == "{") {
            if (bs_ptr < 31) block_starts[++bs_ptr] = len; 
            program_bank[target][len++] = {OP_PUSH_FUNC, 0};
            for (int k = cp_cnt - 1; k >= 0; k--) program_bank[target][len++] = {OP_BIND, (int32_t)current_params[k]};
            if (bp_ptr < 31) {
                bp_ptr++; bp_counts[bp_ptr] = cp_cnt;
                for (int k = 0; k < cp_cnt; k++) block_params[bp_ptr][k] = current_params[k];
            }
            if (bas_ptr < 31) block_arity_stack[++bas_ptr] = cp_cnt;
            cp_cnt = 0;
        }
        else if (s == "}") {
            if (bs_ptr < 0) P_ERR("ERR: {", "RPN Error: Unmatched closing brace '}'");
            int start = block_starts[bs_ptr--]; 
            int p_cnt = bp_counts[bp_ptr];
            for (int k = 0; k < p_cnt; k++) program_bank[target][len++] = {OP_UNBIND, (int32_t)block_params[bp_ptr][k]};
            bp_ptr--;
            program_bank[target][len++] = {OP_RET, 0}; 
            program_bank[target][start].val = (int32_t)(len - start); 
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
                                program_bank[target][k+1].val = (int32_t)last_completed_arity;
                            }
                        }
                    }
                } 
                else if ((s == "=" || s == ":=") && len >= 2 && program_bank[target][len-2].op == OP_LOAD) {
                    int src_id = program_bank[target][len-2].val;
                    if (src_id < 64 && var_id < 64 && rpn_func_arity[src_id] != -1) rpn_func_arity[var_id] = rpn_func_arity[src_id];
                }
            } else P_ERR("ERR: =", "RPN Error: Invalid assignment target");
        }
        else if (s.startsWith("'") && s.endsWith("'")) {
            int count = 0;
            for (size_t k = 1; k < s.length() - 1; k++) {
                char c = s[k]; float val;
                if (c >= '0' && c <= '9') val = c - '0';
                else if (c >= 'a' && c <= 'z') val = c - 'a' + 10;
                else if (c >= 'A' && c <= 'Z') val = c - 'A' + 10;
                else val = c;
                program_bank[target][len++] = {OP_VAL, setF(val)}; count++;
            }
            program_bank[target][len++] = {OP_VAL, setF((float)count)}; program_bank[target][len++] = {OP_VEC, 0}; 
        }
        else if (parsing_params) { if (cp_cnt < 8) current_params[cp_cnt++] = getVarId(s); }
        else {
            if (isdigit(s[0]) || (s[0] == '-' && isdigit(s[1])) || (s[0] == '.' && isdigit(s[1])) || (s.startsWith("-.") && s.length() > 2 && isdigit(s[2]))) {
                program_bank[target][len++] = {OP_VAL, setF(strtof(s.c_str(), NULL))};
            }
            else if (s == ";") program_bank[target][len++] = {OP_POP, 0}; 
            else if (s == "?") program_bank[target][len++] = {OP_COND, 0};
            else if (s.startsWith("call")) {
                int args = 0;
                if (s.length() > 4) args = s.substring(4).toInt();
                program_bank[target][len++] = {OP_DYN_CALL, (int32_t)args};
            }
            else {
                bool is_math = false;
                for (int _m = 0; _m < mathLibrarySize; _m++) {
                    if (s == mathLibrary[_m].name) { program_bank[target][len++] = {mathLibrary[_m].code, 0}; is_math = true; break; }
                }
                
                if (!is_math && !isVarDefined(s)) {
                    bool next_is_assign = false;
                    if (i + 1 < tok_cnt) {
                        String nxt = tokens[i+1];
                        if (nxt == "=" || nxt == ":=" || nxt == "+=" || nxt == "-=" || nxt == "*=" || nxt == "/=" || nxt == "%=" || nxt == "&=" || nxt == "|=" || nxt == "^=" || nxt == "<<=" || nxt == ">>=" || nxt == "**=") {
                            next_is_assign = true;
                        }
                    }
                    if (!next_is_assign) {
                        for (int _s = 0; _s < shorthandsSize; _s++) {
                            if (s == shorthands[_s].name) { program_bank[target][len++] = {shorthands[_s].code, 0}; is_math = true; break; }
                        }
                    }
                }
                
                OpCode opc;
                if (!is_math) { 
                    if (getOpCode(s, opc)) { program_bank[target][len++] = {opc, 0}; is_math = true; } 
                }
                if (!is_math) {
                    int id = getVarId(s); program_bank[target][len++] = {OP_LOAD, (int32_t)id};
                    if (id < 64 && rpn_func_arity[id] != -1) program_bank[target][len++] = {OP_DYN_CALL, (int32_t)rpn_func_arity[id]};
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

/**
 * Initializes the compiler state by clearing arrays and setting up math constants.
 */
void initCompilerState() {
    clear_global_array(); 
    var_count = 0; 
    memset(vars, 0, sizeof(vars)); 
    
    vars[getVarId("t")] = {0, 0}; 
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
}

/**
 * Checks if a given string pointer represents a lambda definition.
 * @param p Pointer to the source string
 * @param params Array to store extracted parameter names
 * @param param_cnt Reference to store the parameter count
 * @param consume_len Reference to store the consumed string length
 * @return true if a lambda definition is found, false otherwise
 */
static bool isLambdaDef(const char* p, String* params, int& param_cnt, int& consume_len) {
    if (*p != '(') return false;
    const char* q = p + 1;
    
    String curr;

    #if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
        curr.reserve(16); // Cardputer optimization
    #endif

    while (*q && *q != ')') {
        if (*q == '(') return false; 
        if (*q == ',') { 
            curr.trim(); 
            if(curr.length()>0 && param_cnt < 8) params[param_cnt++] = curr; 
            curr = "";
        }
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

/**
 * Parses a compound assignment operator from the input string.
 * @param p Pointer to the current character in the source string
 * @param outOp Reference to store the resolved OpCode
 * @param advanceBy Reference to store how many characters to advance
 * @return true if a compound operator is parsed, false otherwise
 */
static bool parseCompoundOperator(const char* p, OpCode& outOp, int& advanceBy) {
    if (p[0] && p[1] == '=') {
        if (p[0] == '+') { outOp = OP_ADD_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '-') { outOp = OP_SUB_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '*') { outOp = OP_MUL_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '/') { outOp = OP_DIV_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '%') { outOp = OP_MOD_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '^') { outOp = OP_XOR_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '&' && p[2] != '&') { outOp = OP_AND_ASSIGN; advanceBy = 2; return true; }
        if (p[0] == '|' && p[2] != '|') { outOp = OP_OR_ASSIGN; advanceBy = 2; return true; }
    }
    if (p[0] == '*' && p[1] == '*' && p[2] == '=') { outOp = OP_POW_ASSIGN; advanceBy = 3; return true; }
    if (p[0] == '<' && p[1] == '<' && p[2] == '=') { outOp = OP_SHL_ASSIGN; advanceBy = 3; return true; }
    if (p[0] == '>' && p[1] == '>' && p[2] == '=') { outOp = OP_SHR_ASSIGN; advanceBy = 3; return true; }
    return false;
}

/**
 * Tokenizes an input string for RPN compilation.
 * @param input The input string to tokenize
 * @param tokens Array to store the resulting tokens
 * @param max_tokens Maximum number of tokens allowed
 * @return The number of tokens parsed
 */
static int tokenize(const String& input, String* tokens, int max_tokens) {
    int tok_cnt = 0;
    const char* p = input.c_str();
    
    String w;

    #if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
        w.reserve(32);     // Cardputer optimization
    #endif

    while (*p && tok_cnt < max_tokens) {
        if (isspace(*p)) p++;
        else if (*p == '&' && isalpha(*(p+1))) {
            w = "&"; // Resets length, reuses reserved buffer
            p++;
            while (isalpha(*p) || isdigit(*p) || *p == '_') w += *p++;
            tokens[tok_cnt++] = w;
        }
        else if (*p == '-' && *(p+1) && !isspace(*(p+1))) {
            w = "-"; 
            p++;
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
            w = "'"; 
            p++;
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
            w = ""; // Clear buffer for general words/numbers
            while (*p && !isspace(*p) && !strchr("(){}=,;<>!+-*/%&|^~@_#$:", *p) && *p != '\'') { 
                w += *p++;
                if ((w.endsWith("e") || w.endsWith("E")) && (*p == '+' || *p == '-')) { w += *p++; }
            }
            if (w.length() > 0) tokens[tok_cnt++] = w;
        }
    }
    return tok_cnt;
}

/**
 * Finds the start index of an expression in the program bank.
 * @param target The target bank index
 * @param end_pc The end program counter index
 * @return The starting program counter index of the expression
 */
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
            if (op == OP_POP || op == OP_STORE || op == OP_JMP || op == OP_BIND || op == OP_UNBIND || op == OP_RET || op == OP_LOOP_DONE || op == OP_LOOP_EVAL) produces = 0;
            
            int consumes = 0;
            if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD || op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_SHL || op == OP_SHR || op == OP_LT || op == OP_GT || op == OP_EQ || op == OP_NEQ || op == OP_LTE || op == OP_GTE || op == OP_MIN || op == OP_MAX || op == OP_POW || op == OP_SC_AND || op == OP_SC_OR || op == OP_AT || op == OP_LOOP_PREP) consumes = 2;
            else if (op == OP_STORE_AT || op == OP_COND || op == OP_COLON) consumes = 3;
            else if (op >= OP_ADD_ASSIGN && op <= OP_POW_ASSIGN) consumes = 1;
            else if (op == OP_NEG || op == OP_NOT || op == OP_BNOT || (op >= OP_SIN && op <= OP_ATAN) || op == OP_STORE || op == OP_STORE_KEEP || op == OP_POP || op == OP_ASSIGN_VAR || op == OP_BIND || op == OP_ALLOC || op == OP_INT) consumes = 1;
            else if (op == OP_RAND || op == OP_LOOP_DONE || op == OP_LOOP_EVAL) consumes = 0; 
            else if (op == OP_DYN_CALL) consumes = program_bank[target][pc].val + 1;
            else if (op == OP_DYN_CALL_IF_FUNC) consumes = 1;
            else if (op == OP_VEC) consumes = (int32_t)getF(program_bank[target][pc-1].val) + 1; 
            
            stack_need = stack_need - produces + consumes;
        }
        pc--;
    }
    return pc + 1;
}

/**
 * Flushes operators from the operator stack to the program bank.
 * @param target The target bank index
 * @param len Reference to the current program length
 * @param os Array of operators
 * @param os_id Array of operator IDs
 * @param ot Reference to the top index of the operator stack
 * @param cond_starts Array of condition start pointers
 * @param cs_ptr Reference to the condition start pointer index
 * @param stopAt The operator to stop flushing at
 * @param minPrec The minimum precedence level to flush
 * @param stopAtMarker Whether to stop at a store marker
 */
static void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, int* cond_starts, int& cs_ptr, OpCode stopAt, int minPrec, bool stopAtMarker) {
    while (ot >= 0 && os[ot] != stopAt) {
        if (stopAtMarker && os[ot] == OP_STORE) break;
        if (os[ot] == OP_DYN_CALL) break; 
        if (minPrec != -1 && getPrecedence(os[ot]) < minPrec) break;
        
        if (len < 512) {
            if (os[ot] == OP_COLON || os[ot] == OP_COND) {
                if (cs_ptr >= 0) {
                    program_bank[target][len++] = {OP_RET, 0};
                    int start = cond_starts[cs_ptr--];
                    program_bank[target][start].val = (int32_t)(len - start); 
                }
                program_bank[target][len++] = {OP_COND, 0}; 
                ot--;
            }
            else if (os[ot] == OP_LOOP_PREP) {
                program_bank[target][len++] = {OP_LOOP_PREP, (int32_t)os_id[ot]};
                int start_pc = len;
                program_bank[target][len++] = {OP_LOOP_EVAL, 0};
                int eval_pc = len - 1;
                program_bank[target][len++] = {OP_LOOP_DONE, 0};
                
                program_bank[target][eval_pc].val = (int32_t)(len - eval_pc);
                program_bank[target][len - 1].val = (int32_t)(len - start_pc + 1);
                ot--;
            }
            else if (os[ot] == OP_ASSIGN_VAR) { program_bank[target][len++] = {OP_STORE_KEEP, (int32_t)os_id[ot--]}; }
            else if (os[ot] == OP_STORE) { program_bank[target][len++] = {OP_STORE, (int32_t)os_id[ot--]}; }
            else if (os[ot] == OP_STORE_AT) {
                program_bank[target][len++] = {OP_STORE_AT, 0}; 
                ot--;
            }
            else if (os[ot] == OP_SC_AND || os[ot] == OP_SC_OR) {
                int start = os_id[ot];
                program_bank[target][start].val = (int32_t)(len - start); 
                ot--;
            }
            else if (os[ot] >= OP_ADD_ASSIGN && os[ot] <= OP_SHR_ASSIGN) { program_bank[target][len++] = {os[ot], (int32_t)os_id[ot]}; ot--; }
            else { program_bank[target][len++] = {os[ot--], 0}; }
        } else ot--;
    }
}

/**
 * Applies a compound assignment operator dynamically.
 * @param target The target bank index
 * @param len Reference to the current program length
 * @param os Array of operators
 * @param os_id Array of operator IDs
 * @param ot Reference to the top index of the operator stack
 * @param assignOp The compound assignment operator to apply
 * @return true if successfully applied, false otherwise
 */
static bool applyCompoundAssign(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, OpCode assignOp) {
    int load_idx = -1;
    for (int k = len - 1; k >= 0 && k >= len - 10; k--) {
        if (program_bank[target][k].op == OP_LOAD) { load_idx = k; break; }
    }
    if (load_idx != -1) {
        int var_id = program_bank[target][load_idx].val;
        for (int k = load_idx; k < len - 1; k++) {
            program_bank[target][k] = program_bank[target][k+1];
        }
        len--; 
        os[++ot] = assignOp; 
        os_id[ot] = var_id; 
        return true;
    }
    return false;
}
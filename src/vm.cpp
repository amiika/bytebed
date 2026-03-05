#include "vm.h"
#include <math.h>

struct MathFunc { String name; OpCode code; bool unary; };
std::vector<MathFunc> mathLibrary = {
    {"sin",  OP_SIN,  true},  {"cos",  OP_COS,  true},  {"tan",  OP_TAN,  true},
    {"sqrt", OP_SQRT, true},  {"log",  OP_LOG,  true},  {"exp",  OP_EXP,  true},
    {"min",  OP_MIN,  false}, {"max",  OP_MAX,  false}, {"pow",  OP_POW,  false}
};

struct OpData { OpCode code; bool unary; };
std::map<char, OpData> opMap = {
    {'+', {OP_ADD, false}}, {'-', {OP_SUB, false}}, {'*', {OP_MUL, false}},
    {'/', {OP_DIV, false}}, {'%', {OP_MOD, false}}, {'&', {OP_AND, false}},
    {'|', {OP_OR,  false}}, {'^', {OP_XOR, false}}, {'<', {OP_LT,  false}},
    {'>', {OP_GT,  false}}, {'!', {OP_NOT, true}}
};

std::map<OpCode, int> precMap = {
    {OP_COND, 0}, {OP_OR, 1}, {OP_XOR, 2}, {OP_AND, 3}, {OP_LT, 4}, {OP_GT, 4},
    {OP_SHL, 5},  {OP_SHR, 5}, {OP_ADD, 6}, {OP_SUB, 6}, {OP_MUL, 7}, {OP_DIV, 7}, {OP_MOD, 7}
};

struct Instruction { OpCode op; int32_t val; };
Instruction program_bank[2][256];
int prog_len_bank[2] = {0, 0};
volatile uint8_t active_bank = 0;

int getPrecedence(OpCode op) { return precMap.count(op) ? precMap[op] : 10; }

String getOpSym(OpCode op) {
    if (op == OP_SHL) return "<<"; if (op == OP_SHR) return ">>"; if (op == OP_COND) return "?"; 
    for (auto const& f : mathLibrary) if (f.code == op) return f.name; 
    for (auto const& pair : opMap) if (pair.second.code == op) return String(pair.first); 
    return "";
}

void saveUndo() { 
    if (input_buffer == undo_stack[undo_ptr]) return; 
    undo_ptr = (undo_ptr + 1) % UNDO_DEPTH; 
    undo_stack[undo_ptr] = input_buffer; 
    undo_max = undo_ptr; 
}

uint8_t execute_vm(int32_t t) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    if (len == 0) return 128;
    
    static int32_t stack[64]; int sp = -1;
    for (int i = 0; i < len; i++) {
        switch (program_bank[bank][i].op) {
            case OP_VAL: stack[++sp] = program_bank[bank][i].val; break;
            case OP_T:   stack[++sp] = t; break;
            case OP_NEG: stack[sp] = -stack[sp]; break;
            case OP_NOT: stack[sp] = !stack[sp]; break;
            case OP_SIN: stack[sp] = (int32_t)(128.0f + sinf(stack[sp]/128.0f*M_PI)*127.0f); break;
            case OP_COS: stack[sp] = (int32_t)(128.0f + cosf(stack[sp]/128.0f*M_PI)*127.0f); break;
            case OP_TAN: stack[sp] = (int32_t)(128.0f + tanf(stack[sp]/128.0f*M_PI)*127.0f); break;
            case OP_COND: { int32_t f=stack[sp--], tv=stack[sp--], c=stack[sp--]; stack[++sp]=c?tv:f; } break;
            case OP_ADD: stack[sp-1] += stack[sp]; sp--; break;
            case OP_SUB: stack[sp-1] -= stack[sp]; sp--; break;
            case OP_MUL: stack[sp-1] *= stack[sp]; sp--; break;
            case OP_DIV: stack[sp-1] = stack[sp] ? stack[sp-1] / stack[sp] : 0; sp--; break;
            case OP_MOD: stack[sp-1] = stack[sp] ? stack[sp-1] % stack[sp] : 0; sp--; break;
            case OP_AND: stack[sp-1] &= stack[sp]; sp--; break;
            case OP_OR:  stack[sp-1] |= stack[sp]; sp--; break;
            case OP_XOR: stack[sp-1] ^= stack[sp]; sp--; break;
            case OP_SHL: stack[sp-1] <<= stack[sp]; sp--; break;
            case OP_SHR: stack[sp-1] >>= stack[sp]; sp--; break;
            case OP_LT:  stack[sp-1] = (stack[sp-1] < stack[sp]); sp--; break;
            case OP_GT:  stack[sp-1] = (stack[sp-1] > stack[sp]); sp--; break;
            case OP_MIN: stack[sp-1] = std::min(stack[sp-1], stack[sp]); sp--; break;
            case OP_MAX: stack[sp-1] = std::max(stack[sp-1], stack[sp]); sp--; break;
            case OP_POW: stack[sp-1] = (int32_t)powf((float)stack[sp-1], (float)stack[sp]); sp--; break;
            case OP_SQRT: stack[sp] = stack[sp] >= 0 ? (int32_t)sqrtf((float)stack[sp]) : 0; break;
            case OP_LOG:  stack[sp] = stack[sp] > 0 ? (int32_t)logf((float)stack[sp]) : 0; break;
            case OP_EXP:  stack[sp] = (int32_t)expf((float)stack[sp]); break;
            default: break;
        }
    }
    return (uint8_t)stack[sp];
}

struct Node { String s; int p; };
String decompile(bool to_rpn) {
    uint8_t bank = active_bank;
    int len = prog_len_bank[bank];
    if (len == 0) return "";
    
    if (to_rpn) {
        String res = ""; for (int i = 0; i < len; i++) {
            if (program_bank[bank][i].op == OP_VAL) res += String(program_bank[bank][i].val) + " ";
            else if (program_bank[bank][i].op == OP_T) res += "t "; else res += getOpSym(program_bank[bank][i].op) + " ";
        }
        res.trim();
        return res;
    }
    
    std::vector<Node> st;
    for (int i = 0; i < len; i++) {
        OpCode op = program_bank[bank][i].op; 
        int cur_p = getPrecedence(op);
        
        if (op == OP_VAL) st.push_back({String(program_bank[bank][i].val), 10});
        else if (op == OP_T) st.push_back({"t", 10});
        else if (op == OP_COND) {
            if (st.size() < 3) return ""; Node f=st.back(); st.pop_back(); Node tv=st.back(); st.pop_back(); Node c=st.back(); st.pop_back();
            st.push_back({c.s + "?" + tv.s + ":" + f.s, 0});
        } else {
            String sym = getOpSym(op); 
            bool u = (op == OP_NEG || op == OP_NOT); 
            bool is_bin_func = false; // Flag to catch min, max, pow
            
            for (auto const& f : mathLibrary) {
                if (f.code == op) {
                    if (f.unary) u = true;
                    else is_bin_func = true; 
                    break;
                }
            }
            
            if (st.empty()) continue;
            if (u) { 
                Node a = st.back(); st.pop_back(); 
                st.push_back({sym + "(" + a.s + ")", 10}); 
            } else {
                if (st.size() < 2) continue; 
                Node b = st.back(); st.pop_back(); 
                Node a = st.back(); st.pop_back();
                
                if (is_bin_func) {
                    st.push_back({sym + "(" + a.s + "," + b.s + ")", 10}); 
                } else {
                    String l = (a.p < cur_p) ? "(" + a.s + ")" : a.s; 
                    String r = (b.p <= cur_p) ? "(" + b.s + ")" : b.s;
                    st.push_back({l + sym + r, cur_p}); 
                }
            }
        }
    }
    return st.empty() ? "" : st.back().s;
}

bool validateProgram(uint8_t bank, int len) {
    if (len == 0 || len > 256) return false;
    int sp = 0;
    for (int i = 0; i < len; i++) {
        switch (program_bank[bank][i].op) {
            case OP_VAL: 
            case OP_T:   
                sp++; 
                break;
            case OP_NEG: 
            case OP_NOT: 
            case OP_SIN: 
            case OP_COS: 
            case OP_TAN: 
            case OP_SQRT: 
            case OP_LOG: 
            case OP_EXP: 
                if (sp < 1) return false; 
                break;
            case OP_COND: 
                if (sp < 3) return false; 
                sp -= 2; 
                break;
            default: 
                if (sp < 2) return false; 
                sp--; 
                break;
        }
        if (sp > 64) return false; 
    }
    return sp >= 1; 
}

bool compileRPN(String input) {
    uint8_t target = 1 - active_bank;
    int len = 0; char buf[input.length() + 1]; strcpy(buf, input.c_str()); char* tok = strtok(buf, " ");
    
    while (tok) {
        if (len >= 256) return false; 
        String s = String(tok);
        if (isdigit(tok[0]) || (tok[0] == '-' && isdigit(tok[1]))) program_bank[target][len++] = {OP_VAL, (int32_t)atoi(tok)};
        else if (s == "t") program_bank[target][len++] = {OP_T, 0};
        else if (s == "?") program_bank[target][len++] = {OP_COND, 0};
        else if (s == "<<") program_bank[target][len++] = {OP_SHL, 0};
        else if (s == ">>") program_bank[target][len++] = {OP_SHR, 0};
        else {
            bool fnd = false; 
            for (auto const& f : mathLibrary) if (s == f.name) { program_bank[target][len++] = {f.code, 0}; fnd = true; break; }
            if (!fnd) {
                for (auto const& pair : opMap) if (s == String(pair.first)) { program_bank[target][len++] = {pair.second.code, 0}; fnd = true; break; }
            }
            if (!fnd) return false; 
        }
        tok = strtok(NULL, " ");
    }
    
    if (!validateProgram(target, len)) return false; 
    
    prog_len_bank[target] = len;
    active_bank = target; 
    return true;
}

void flushOps(uint8_t target, int& len, OpCode* os, int& ot, OpCode stopAt = OP_NONE, int minPrec = -1) {
    while (ot >= 0 && os[ot] != stopAt) {
        if (minPrec != -1 && getPrecedence(os[ot]) < minPrec) break;
        if (len < 256) program_bank[target][len++] = {os[ot--], 0};
        else ot--;
    }
}

bool compileInfix(String input, bool reset_t) {
    uint8_t target = 1 - active_bank;
    int len = 0; OpCode os[32]; int ot = -1; const char* p = input.c_str();
    
    int paren_depth = 0;
    int cond_depth = 0;

    while (*p) {
        if (len >= 256 || ot >= 31) return false; 
        if (isspace(*p)) { p++; continue; }
        if (isdigit(*p)) { program_bank[target][len++] = {OP_VAL, (int32_t)strtol(p, (char**)&p, 10)}; }
        else if (*p == 't') { program_bank[target][len++] = {OP_T, 0}; p++; }
        else if (isalpha(*p)) { 
            String word = ""; while (isalpha(*p)) word += *p++; 
            bool found = false;
            for (auto const& f : mathLibrary) {
                if (word == f.name) { os[++ot] = f.code; found = true; break; }
            }
            if (!found) return false; 
        }
        else if (*p == '(') { os[++ot] = OP_NONE; paren_depth++; p++; }
        else if (*p == ')') { 
            if (paren_depth <= 0) return false; 
            flushOps(target, len, os, ot, OP_NONE); 
            if (ot >= 0) ot--; 
            paren_depth--; p++; 
        }
        else if (*p == '?') { flushOps(target, len, os, ot, OP_NONE); os[++ot] = OP_COND; cond_depth++; p++; }
        else if (*p == ':') { 
            if (cond_depth <= 0) return false; 
            flushOps(target, len, os, ot, OP_COND); 
            cond_depth--; p++; 
        }
        else if (*p == ',') { 
            flushOps(target, len, os, ot, OP_NONE); 
            p++; 
        }
        else {
            int op_len = ((*p == '<' || *p == '>') && *(p+1) == *p) ? 2 : 1;
            OpCode cur = (op_len == 2) ? (*p == '<' ? OP_SHL : OP_SHR) : (opMap.count(*p) ? opMap[*p].code : OP_NONE);
            if (cur != OP_NONE) { 
                flushOps(target, len, os, ot, OP_NONE, getPrecedence(cur)); 
                os[++ot] = cur; 
                p += op_len; 
            } else return false; 
        }
    }
    
    if (paren_depth != 0 || cond_depth != 0) return false; 
    flushOps(target, len, os, ot);
    
    if (!validateProgram(target, len)) return false;
    
    if (reset_t) t_raw = 0;
    prog_len_bank[target] = len;
    active_bank = target; 
    return true;
}
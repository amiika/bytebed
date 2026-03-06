#include "vm.h"
#include <math.h>

Instruction program_bank[2][256];
int prog_len_bank[2] = {0, 0};
volatile uint8_t active_bank = 0;

Val vars[64][8];
int vsp[64];
int var_count = 0;
std::map<String, int> symTable;

std::vector<MathFunc> mathLibrary = {
    {"sin",  OP_SIN,  true},  {"cos",  OP_COS,  true},  {"tan",  OP_TAN,  true},
    {"sqrt", OP_SQRT, true},  {"log",  OP_LOG,  true},  {"exp",  OP_EXP,  true},
    {"min",  OP_MIN,  false}, {"max",  OP_MAX,  false}, {"pow",  OP_POW,  false}
};

std::map<char, OpInfo> opMap = {
    {'+', {OP_ADD, 6}}, {'-', {OP_SUB, 6}}, {'*', {OP_MUL, 7}},
    {'/', {OP_DIV, 7}}, {'%', {OP_MOD, 7}}, {'&', {OP_AND, 3}},
    {'|', {OP_OR,  1}}, {'^', {OP_XOR, 2}}, {'<', {OP_LT,  4}},
    {'>', {OP_GT,  4}}, {'!', {OP_NOT, true}}
};

static std::map<OpCode, int> precMap = {
    {OP_NONE, -3}, {OP_DYN_CALL, -3}, {OP_DYN_CALL_IF_FUNC, -3},
    {OP_STORE, -2}, {OP_STORE_KEEP, -2}, {OP_ASSIGN_VAR, -1}, 
    {OP_COND, 0}, {OP_OR, 1}, {OP_XOR, 2}, {OP_AND, 3}, {OP_LT, 4}, {OP_GT, 4},
    {OP_SHL, 5},  {OP_SHR, 5}, {OP_ADD, 6}, {OP_SUB, 6}, {OP_MUL, 7}, {OP_DIV, 7}, {OP_MOD, 7}
};

// --- Shared Helpers ---
int getVarId(String name) {
    if (symTable.count(name)) return symTable[name];
    if (var_count >= 63) return 63; 
    symTable[name] = var_count;
    return var_count++;
}

String getVarName(int id) {
    for (auto const& pair : symTable) if (pair.second == id) return pair.first;
    return "v" + String(id);
}

int getPrecedence(OpCode op) { return precMap.count(op) ? precMap[op] : 10; }

String getOpSym(OpCode op) {
    if (op == OP_SHL) return "<<"; if (op == OP_SHR) return ">>"; if (op == OP_COND) return "?"; 
    for (auto const& f : mathLibrary) if (f.code == op) return f.name; 
    for (auto const& pair : opMap) if (pair.second.code == op) return String(pair.first); 
    return "";
}

uint8_t IRAM_ATTR execute_vm(int32_t t) {
    uint8_t bank = active_bank; 
    int len = prog_len_bank[bank];
    if (len == 0) return 128;
    
    memset(vsp, 0, sizeof(vsp));
    
    static Val stack[256]; int sp = -1;
    static int32_t call_stack[256]; int csp = -1;

    for (int pc = 0; pc < len; pc++) {
        Instruction& inst = program_bank[bank][pc];
        switch (inst.op) {
            case OP_VAL: stack[++sp] = {0, inst.val}; break;
            case OP_T:   stack[++sp] = {0, t}; break;
            case OP_LOAD: stack[++sp] = vars[inst.val][vsp[inst.val]]; break;
            case OP_STORE: if(sp>=0) vars[inst.val][vsp[inst.val]] = stack[sp--]; break;
            case OP_STORE_KEEP: if(sp>=0) vars[inst.val][vsp[inst.val]] = stack[sp]; break;
            case OP_POP: if(sp>=0) sp--; break;
            case OP_JMP: pc = inst.val - 1; break;
            case OP_PUSH_FUNC: stack[++sp] = {1, pc + 1}; pc = inst.val - 1; break;
            case OP_DYN_CALL:
                if (stack[sp].type == 1) { 
                    if (csp < 255) { call_stack[++csp] = pc; pc = stack[sp--].v - 1; }
                    else sp--; 
                } else {
                    int args = inst.val;
                    if (sp >= args) sp -= (args + 1); else sp = -1;
                    stack[++sp] = {0, 0}; 
                }
                break;
            case OP_DYN_CALL_IF_FUNC:
                if (stack[sp].type == 1) { 
                    if (csp < 255) { call_stack[++csp] = pc; pc = stack[sp--].v - 1; }
                    else sp--;
                }
                break;
            case OP_RET:
                if (csp >= 0) pc = call_stack[csp--];
                else pc = len; 
                break;
            case OP_BIND: 
                if (sp >= 0) {
                    if (vsp[inst.val] < 7) vsp[inst.val]++; 
                    vars[inst.val][vsp[inst.val]] = stack[sp--];
                }
                break;
            case OP_UNBIND: 
                if (vsp[inst.val] > 0) vsp[inst.val]--; 
                break;
            case OP_NEG: stack[sp].v = -stack[sp].v; break;
            case OP_NOT: stack[sp].v = !stack[sp].v; break;
            case OP_SIN: stack[sp].v = (int32_t)(128.0f + sinf(stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_COS: stack[sp].v = (int32_t)(128.0f + cosf(stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_TAN: stack[sp].v = (int32_t)(128.0f + tanf(stack[sp].v/128.0f*M_PI)*127.0f); break;
            case OP_COND: { 
                Val f = stack[sp--];
                Val tv = stack[sp--];
                Val c = stack[sp--]; 
                Val target = c.v ? tv : f;
                if (target.type == 1) { 
                    if (csp < 255) { call_stack[++csp] = pc; pc = target.v - 1; }
                } else { stack[++sp] = target; }
                break; 
            }
            case OP_ADD: stack[sp-1].v += stack[sp].v; sp--; break;
            case OP_SUB: stack[sp-1].v -= stack[sp].v; sp--; break;
            case OP_MUL: stack[sp-1].v *= stack[sp].v; sp--; break;
            case OP_DIV: stack[sp-1].v = stack[sp].v ? stack[sp-1].v / stack[sp].v : 0; sp--; break;
            case OP_MOD: stack[sp-1].v = stack[sp].v ? stack[sp-1].v % stack[sp].v : 0; sp--; break;
            case OP_AND: stack[sp-1].v &= stack[sp].v; sp--; break;
            case OP_OR:  stack[sp-1].v |= stack[sp].v; sp--; break;
            case OP_XOR: stack[sp-1].v ^= stack[sp].v; sp--; break;
            case OP_SHL: stack[sp-1].v <<= stack[sp].v; sp--; break;
            case OP_SHR: stack[sp-1].v >>= stack[sp].v; sp--; break;
            case OP_LT:  stack[sp-1].v = (stack[sp-1].v < stack[sp].v); sp--; break;
            case OP_GT:  stack[sp-1].v = (stack[sp-1].v > stack[sp].v); sp--; break;
            case OP_MIN: stack[sp-1].v = std::min(stack[sp-1].v, stack[sp].v); sp--; break;
            case OP_MAX: stack[sp-1].v = std::max(stack[sp-1].v, stack[sp].v); sp--; break;
            case OP_POW: stack[sp-1].v = (int32_t)powf((float)stack[sp-1].v, (float)stack[sp].v); sp--; break;
            case OP_SQRT: stack[sp].v = stack[sp].v >= 0 ? (int32_t)sqrtf((float)stack[sp].v) : 0; break;
            case OP_LOG:  stack[sp].v = stack[sp].v > 0 ? (int32_t)logf((float)stack[sp].v) : 0; break;
            case OP_EXP:  stack[sp].v = (int32_t)expf((float)stack[sp].v); break;
            default: break;
        }
    }
    return sp >= 0 ? (uint8_t)stack[sp].v : 128;
}
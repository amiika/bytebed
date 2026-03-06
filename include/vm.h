#pragma once

#include "state.h" // Absolute single source of truth for Enums and Globals

struct Instruction {
    OpCode op;
    int32_t val;
};

struct Val {
    uint8_t type; 
    int32_t v;
}; 

struct MathFunc {
    String name;
    OpCode code;
    bool unary;
};

struct OpInfo {
    OpCode code;
    int prec;
};

// --- Dictionaries & Tables ---
extern std::vector<MathFunc> mathLibrary;
extern std::map<char, OpInfo> opMap;

// --- VM Program Memory ---
extern Instruction program_bank[2][256];
extern int prog_len_bank[2];
extern volatile uint8_t active_bank;

// --- Symbols and Scoped Memory ---
extern std::map<String, int> symTable;
extern int var_count;
// HIGH IMPACT FIX: 64 variables, max depth 8.
extern Val vars[64][8]; 
extern int vsp[64];

// --- Shared Helpers ---
int getVarId(String name); 
String getVarName(int id); 
String getOpSym(OpCode op); 
int getPrecedence(OpCode op);

// --- Core Logic ---
bool validateProgram(uint8_t bank, int len);

// HIGH IMPACT FIX: Pin VM execution to IRAM for maximum speed
uint8_t IRAM_ATTR execute_vm(int32_t t); 

bool compileRPN(String input);
bool compileInfix(String input, bool reset_t);
String decompile(bool to_rpn);
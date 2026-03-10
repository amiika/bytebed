#pragma once
#include "state.h"

struct Instruction {
    OpCode op;
    int32_t val;
};

struct Val {
    uint8_t type; 
    int32_t v;
};

struct MathFunc {
    const char* name;
    OpCode code;
    bool unary;
};

struct OpInfo {
    const char* sym;
    OpCode code;
    int precedence;
};

extern Instruction program_bank[2][256];
extern int prog_len_bank[2];
extern volatile uint8_t active_bank;

extern Val vars[64][32];
extern int vsp[64];

extern int var_count;

extern const MathFunc mathLibrary[9];
extern const int mathLibrarySize;
extern const OpInfo opList[23];
extern const int opListSize;

bool getOpCode(const String& sym, OpCode& outCode);

int getVarId(String name);
String getVarName(int id);
int getPrecedence(OpCode op);
String getOpSym(OpCode op);

bool compileRPN(String input);
bool compileInfix(String input, bool reset_t = false);
String decompile(bool to_rpn);

bool validateProgram(uint8_t bank, int len);

uint8_t execute_vm(int32_t t);
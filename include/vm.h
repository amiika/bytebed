#pragma once
#include "state.h"

static inline float getF(int32_t v) { union { int32_t i; float f; } u; u.i = v; return u.f; }
template <typename T>
static inline int32_t setF(T f) { union { int32_t i; float f; } u; u.f = static_cast<float>(f); return u.i; }

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

extern Val vars[64];
extern int var_count;

extern const MathFunc mathLibrary[9];
extern const int mathLibrarySize;

// FIXED: Updated from [23] to [34] to accommodate the new compound operators
extern const OpInfo opList[34]; 
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
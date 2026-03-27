#pragma once

#include "state.h"
#include "fast_math.h"

#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
#include <Arduino.h>
#else
#include "Arduino.h" 
#endif

static inline float getF(int32_t v) { union { int32_t i; float f; } u; u.i = v; return u.f; }
template <typename T>
static inline int32_t setF(T f) { union { int32_t i; float f; } u; u.f = static_cast<float>(f); return u.i; }

struct Instruction {
    OpCode op;
    int32_t val;
};

struct Val {
    uint8_t type; 
    union {
        int32_t v;
        float f;
    };
};

/**
 * Compiles an infix formula into VM bytecode.
 * @param input The infix expression to compile
 * @param reset_t Determines if the playback time should be reset
 * @return true if compilation succeeds, false otherwise
 */
bool compileInfix(String input, bool reset_t = false);

/**
 * Compiles an RPN formula into VM bytecode.
 * @param input The RPN expression to compile
 * @return true if compilation succeeds, false otherwise
 */
bool compileRPN(String input);

/**
 * Validates a compiled virtual machine program to ensure safety.
 * @param bank The program bank index to validate
 * @param len The length of the compiled program
 * @return true if the program is safe to execute, false otherwise
 */
bool validateProgram(uint8_t bank, int len);

/**
 * Decompiles the current program bank into syntax.
 * @param to_rpn If true, decompiles to RPN; otherwise, to infix
 * @return The decompiled formula string
 */
String decompile(bool to_rpn);

/**
 * Executes the VM for a single discrete time step.
 * @param t The absolute time step
 * @return The generated audio sample byte
 */
uint8_t execute_vm(int32_t t);

/**
 * Executes a block of bytecode for audio generation.
 * @param start_t The starting time step
 * @param length The number of samples to generate
 * @param out_buf The output buffer to write to
 */
void execute_vm_block(int32_t start_t, int length, uint8_t* out_buf);

/**
 * Updates IMU variables in the VM state.
 */
void updateIMUVars(float ax, float ay, float az, float gx, float gy, float gz);

/**
 * Updates mouse variables in the VM state.
 */
void updateMouseVars(float mx, float my, float mv);

extern Instruction program_bank[2][512];
extern int prog_len_bank[2];
extern volatile uint8_t active_bank;

extern Val vars[64];
extern int var_count;

extern float* global_array_mem;
extern int32_t global_array_capacity;

void clear_global_array();
void ensure_global_array(int32_t req_size);

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

extern const MathFunc mathLibrary[19];
extern const int mathLibrarySize;

extern const MathFunc shorthands[5];
extern const int shorthandsSize;

extern const OpInfo opList[37]; 
extern const int opListSize;

bool getOpCode(const String& sym, OpCode& outCode);
int getVarId(const String& name);
String getVarName(int id);
bool isVarDefined(const String& name); 
int getPrecedence(OpCode op);
String getOpSym(OpCode op);
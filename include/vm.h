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
    uint8_t type; // 0=Float, 1=Func, 2=LocalArr, 3=GlobalArr, 4=String
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
 * @return The generated 32-bit native audio sample stream word or mask
 */
uint32_t execute_vm(int32_t t);

/**
 * Executes a block of bytecode for audio generation.
 * @param start_t The starting time step (allows fractional alignment)
 * @param t_step The timeline increment per sample
 * @param length The number of samples to generate
 * @param out_buf The output buffer to write to
 */
void execute_vm_block(float start_t, float t_step, int length, uint32_t* out_buf);

/**
 * Updates IMU variables in the VM state.
 * @param ax Accelerometer X
 * @param ay Accelerometer Y
 * @param az Accelerometer Z
 * @param gx Gyroscope X
 * @param gy Gyroscope Y
 * @param gz Gyroscope Z
 */
void updateIMUVars(float ax, float ay, float az, float gx, float gy, float gz);

/**
 * Updates mouse variables in the VM state.
 * @param mx Mouse X position
 * @param my Mouse Y position
 * @param mv Mouse click state/velocity
 */
void updateMouseVars(float mx, float my, float mv);

/**
 * Updates the virtual machine memory with active MIDI parameters.
 * @param freq The frequency of the active MIDI note in Hertz
 * @param gate The velocity/gate state (0.0 for off, >0.0 for active)
 * @param note Ocean raw MIDI note number (0-127)
 */
void updateMIDIVars(float freq, float gate, float note);

extern Instruction program_bank[2][512];
extern int prog_len_bank[2];
extern volatile uint8_t active_bank;

extern Val vars[64];
extern int var_count;

extern String string_table[64];
extern int string_table_count;

extern float* global_array_mem;
extern int32_t global_array_capacity;

extern float anchor_bpm;
extern float anchor_beat;
extern int32_t anchor_t;
extern int anchor_sample_rate;

/**
 * Clears the global array memory.
 */
void clear_global_array();

/**
 * Ensures the global array has the requested capacity.
 * @param req_size The required capacity size
 */
void ensure_global_array(int32_t req_size);

extern String last_vm_error;

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

extern const MathFunc mathLibrary[29];
extern const int mathLibrarySize;

extern const MathFunc shorthands[5];
extern const int shorthandsSize;

extern const OpInfo opList[43];
extern const int opListSize;

/**
 * Gets the OpCode for a given symbol.
 * @param sym The symbol string
 * @param outCode Reference to store the OpCode
 * @return true if found, false otherwise
 */
bool getOpCode(const String& sym, OpCode& outCode);

/**
 * Gets or creates an ID for a variable name.
 * @param name The name of the variable
 * @return The ID of the variable
 */
int getVarId(const String& name);

/**
 * Gets the name of a variable by its ID.
 * @param id The ID of the variable
 * @return The name of the variable
 */
String getVarName(int id);

/**
 * Checks if a variable is defined.
 * @param name To check
 * @return true if defined, false otherwise
 */
bool isVarDefined(const String& name); 

/**
 * Gets the precedence of a given OpCode.
 * @param op The OpCode
 * @return The precedence level
 */
int getPrecedence(OpCode op);

/**
 * Gets the symbol string for a given OpCode.
 * @param op The OpCode
 * @return The symbol string
 */
String getOpSym(OpCode op);
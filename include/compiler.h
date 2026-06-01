#ifndef COMPILER_H
#define COMPILER_H

#include "vm.h"
#include <math.h>

#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
#include <Arduino.h>
#else
#include "Arduino.h" 
#endif

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

/**
 * Context for lambda function parsing and binding.
 */
struct LambdaCtx {
    int depth;
    int p_ids[8];
    int p_cnt;
    int start_pc;
    bool uses_braces;
};

/**
 * System constant structure shared between compiler and decompiler.
 */
struct SystemConst {
    const char* name;
    float val;
    bool init_on_startup;
};

/**
 * Shared central definition table for math constants and environment registers.
 */
inline const SystemConst systemConstantsTable[] = {
    {"steps",     0.0f,                  false},
    {"sign",      0.0f,                  false},
    {"beats",     0.0f,                  false},
    {"bars",      0.0f,                  false},
    {"step",      0.0f,                  true},
    {"bar",       0.0f,                  true},
    {"beat",      0.0f,                  true},
    {"bpm",       120.0f,                true},
    {"e",         (float)M_E,            true},
    {"invpi",     (float)M_1_PI,         true},
    {"invpi2",    (float)M_2_PI,         true},
    {"invsqrtpi", (float)M_2_SQRTPI,     true},
    {"ln10",      (float)M_LN10,         true},
    {"ln2",       (float)M_LN2,          true},
    {"log10e",    (float)M_LOG10E,       true},
    {"log2e",     (float)M_LOG2E,        true},
    {"pi",        (float)M_PI,           true},
    {"pi2",       (float)M_PI_2,         true},
    {"pi4",       (float)M_PI_4,         true},
    {"sqrt12",    (float)M_SQRT1_2,      true},
    {"sqrt2",     (float)M_SQRT2,        true},
    {"sr",        8000.0f,               true},
    {"t",         0.0f,                  true},
    {"tau",       (float)(M_PI * 2.0),   true}
};
inline constexpr size_t systemConstantsCount = sizeof(systemConstantsTable) / sizeof(systemConstantsTable[0]);

/**
 * Symbol validation checker.
 */
inline bool isReservedSymbol(const String& name) {
    int low = 0;
    int high = (int)systemConstantsCount - 1;
    
    while (low <= high) {
        int mid = low + ((high - low) >> 1);
        int cmp = strcasecmp(name.c_str(), systemConstantsTable[mid].name);
        
        if (cmp == 0) return true;
        if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return false;
}

/**
 * Initializes the compiler state by clearing arrays and setting up math constants.
 */
void initCompilerState();

/**
 * Compiles an infix formula into VM bytecode.
 */
bool compileInfix(String input, bool reset_t);

/**
 * Compiles an RPN formula into VM bytecode.
 */
bool compileRPN(String input);

/**
 * Checks if a given string pointer represents a lambda definition and parses optional defaults.
 */
bool isLambdaDef(const char* p, String* params, bool* has_defaults, float* default_vals, int& param_cnt, int& consume_len);

/**
 * Parses a compound assignment operator from the input string.
 */
bool parseCompoundOperator(const char* p, OpCode& outOp, int& advanceBy);

/**
 * Tokenizes an input string for RPN compilation.
 */
int tokenize(const String& input, String* tokens, int max_tokens);

/**
 * Finds the start index of an expression in the program bank.
 */
int get_expr_start(uint8_t target, int end_pc);

/**
 * Flushes operators from the operator stack to the program bank.
 */
void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, int* cond_starts, int& cs_ptr, OpCode stopAt = OP_NONE, int minPrec = -1, bool stopAtMarker = false);

/**
 * Applies a compound assignment operator dynamically.
 */
bool applyCompoundAssign(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, OpCode assignOp);

#endif
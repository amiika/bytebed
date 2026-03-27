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
 * Initializes the compiler state by clearing arrays and setting up math constants.
 */
void initCompilerState();

/**
 * Checks if a given string pointer represents a lambda definition.
 * @param p Pointer to the source string
 * @param params Array to store extracted parameter names
 * @param param_cnt Reference to store the parameter count
 * @param consume_len Reference to store the consumed string length
 * @return true if a lambda definition is found, false otherwise
 */
static bool isLambdaDef(const char* p, String* params, int& param_cnt, int& consume_len);

/**
 * Parses a compound assignment operator from the input string.
 * @param p Pointer to the current character in the source string
 * @param outOp Reference to store the resolved OpCode
 * @param advanceBy Reference to store how many characters to advance
 * @return true if a compound operator is parsed, false otherwise
 */
static bool parseCompoundOperator(const char* p, OpCode& outOp, int& advanceBy);

/**
 * Tokenizes an input string for RPN compilation.
 * @param input The input string to tokenize
 * @param tokens Array to store the resulting tokens
 * @param max_tokens Maximum number of tokens allowed
 * @return The number of tokens parsed
 */
static int tokenize(const String& input, String* tokens, int max_tokens);

/**
 * Finds the start index of an expression in the program bank.
 * @param target The target bank index
 * @param end_pc The end program counter index
 * @return The starting program counter index of the expression
 */
static int get_expr_start(uint8_t target, int end_pc);

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
static void flushOps(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, int* cond_starts, int& cs_ptr, OpCode stopAt = OP_NONE, int minPrec = -1, bool stopAtMarker = false);

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
static bool applyCompoundAssign(uint8_t target, int& len, OpCode* os, int* os_id, int& ot, OpCode assignOp);

#endif
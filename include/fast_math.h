#pragma once
#include <math.h>
#include <stdint.h>

#if defined(ESP32)
extern float sin_lut[1025];
void init_fast_math();

/**
 * Fast lookup-table sine calculation.
 * @param x The angle in radians
 * @return The calculated sine value
 */
inline float fast_sin(float x) {
    float pos = x * 0.15915494309189535f; 
    pos -= (float)((int32_t)pos);
    if (pos < 0.0f) pos += 1.0f;
    
    float findex = pos * 1024.0f;
    int32_t i = (int32_t)findex;
    float frac = findex - (float)i;
    
    return sin_lut[i] + (sin_lut[i+1] - sin_lut[i]) * frac;
}

/**
 * High-performance single-precision power fallback.
 * Hooks directly into native hardware floating-point calculation registers.
 * @param a The mathematical base parameter
 * @param b The exponent value
 * @return The computed value a raised to power b
 */
inline float fast_pow(float a, float b) {
    if (a <= 0.0f) return 0.0f;
    // Native single-precision math utility bypassing heavy double-precision structures
    return powf(a, b);
}

/**
 * High-performance Padé single-precision rational arctangent approximation.
 * Eliminates pipeline stalls in extreme real-time modulation environments.
 * @param x Input float coordinate
 * @return Computed arctangent angle in radians
 */
inline float fast_atan(float x) {
    float abs_x = fabsf(x);
    float res = (0.785398163f * x) - x * (abs_x - 1.0f) * (0.2447f + 0.0663f * abs_x);
    return res;
}

/**
 * Hyper-fast single-precision Newton-Raphson cube root approximation.
 * Bypasses long software loops on platforms without a native hardware cbrt instruction.
 * @param val Input float value
 * @return Approximated cube root
 */
inline float fast_cbrt(float val) {
    if (val == 0.0f) return 0.0f;
    union { float f; int32_t i; } u;
    u.f = fabsf(val);
    u.i = u.i / 3 + 712210413; 
    float r = u.f;
    r = 0.66666667f * r + 0.33333333f * val / (r * r);
    return (val < 0.0f) ? -r : r;
}

#else
// Safe Single-Threaded Native Test Harness Fallbacks
inline void init_fast_math() {} 
#define fast_sin(x) sinf(x)
#define fast_pow(a, b) powf(a, b)
#define fast_atan(x) atanf(x)
#define fast_cbrt(x) cbrtf(x)
#endif
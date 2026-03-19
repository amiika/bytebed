#pragma once
#include <math.h>
#include <stdint.h>

#if defined(ESP32)
extern float sin_lut[1025];
void init_fast_math();

inline float fast_sin(float x) {
    float pos = x * 0.159154943f; // x * (1 / 2PI)
    pos -= (float)((int)pos);
    if (pos < 0) pos += 1.0f;
    float findex = pos * 1024.0f;
    int i = (int)findex;
    float frac = findex - (float)i;
    return sin_lut[i] + (sin_lut[i+1] - sin_lut[i]) * frac;
}

inline float fast_pow(float a, float b) {
    if (a <= 0) return 0;
    union { float f; int32_t i; } u = { a };
    float log2_a = (float)(u.i - 0x3f800000) * 1.1920928955078125e-7f;
    float res_log2 = b * log2_a;
    union { int32_t i; float f; } v = { (int32_t)(res_log2 * 8388608.0f + 0x3f800000) };
    return v.f;
}
#else
// WASM targets bypass the LUT to use the browser's high-speed native FPU math
inline void init_fast_math() {} 
#define fast_sin(x) sinf(x)
#define fast_pow(a, b) powf(a, b)
#endif
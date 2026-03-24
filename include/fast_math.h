#pragma once
#include <math.h>
#include <stdint.h>

#if defined(ESP32)
extern float sin_lut[1025];
void init_fast_math();

inline float fast_sin(float x) {
    float pos = x * 0.15915494309189535f; 
    pos -= (float)((int32_t)pos);
    if (pos < 0.0f) pos += 1.0f;
    
    float findex = pos * 1024.0f;
    int32_t i = (int32_t)findex;
    float frac = findex - (float)i;
    
    return sin_lut[i] + (sin_lut[i+1] - sin_lut[i]) * frac;
}

inline float fast_pow(float a, float b) {
    if (a <= 0.0f) return 0.0f;
    
    union { float f; uint32_t i; } u = { a };
    float log2_a;
    
    if (a > 0.95f && a < 1.05f) {
        log2_a = (a - 1.0f) * 1.44269504089f; 
    } else {
        log2_a = (float)((int32_t)u.i - 0x3f800000) * 1.1920928955078125e-7f;
    }

    float res_log2 = b * log2_a;
    
    if (res_log2 < -126.0f) return 0.0f;
    
    union { uint32_t i; float f; } v = { (uint32_t)(res_log2 * 8388608.0f + 0x3f800000) };
    return v.f;
}
#else
inline void init_fast_math() {} 
#define fast_sin(x) sinf(x)
#define fast_pow(a, b) powf(a, b)
#endif
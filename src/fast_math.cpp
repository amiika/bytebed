#include "fast_math.h"

#if defined(ESP32)
float sin_lut[1025]; 

void init_fast_math() {
    for (int i = 0; i <= 1024; i++) {
        sin_lut[i] = sinf((float)i / 1024.0f * 2.0f * 3.14159265f);
    }
}
#endif
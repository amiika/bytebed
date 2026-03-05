#ifndef TESTS_H
#define TESTS_H

#include <Arduino.h>
#include "state.h" 
#include "vm.h"

struct TestCase {
    const char* expr;
    int32_t t;
    uint8_t expected;
    bool isRpn;
};

String runBytebeatTestSuite() {
    TestCase suite[] = {
        // --- Basic & Bitwise ---
        {"t", 128, 128, false},             // Identity: t=128 returns 128
        {"t<<1", 64, 128, false},           // Left Shift: 64 * 2 = 128
        {"t 8 >>", 256, 1, true},           // Right Shift (RPN): 256 / 256 = 1
        {"t&t>>8", 511, 1, false},          // Masking: 511 & (511 >> 8) -> 511 & 1 = 1

        // --- Conditionals (Ternary) ---
        {"t>10?255:0", 11, 255, false},    // Ternary True: 11 > 10 is true
        {"t>10?255:0", 5, 0, false},       // Ternary False: 5 > 10 is false
        
        // --- Nested Conditionals (Recursive-style logic) ---
        // Logic: (t<10 ? 1 : (t<20 ? 2 : 3))
        {"t 10 < 1 t 20 < 2 3 ? ?", 5, 1, true},   // Nested RPN: t=5  -> 1
        {"t 10 < 1 t 20 < 2 3 ? ?", 15, 2, true},  // Nested RPN: t=15 -> 2
        {"t 10 < 1 t 20 < 2 3 ? ?", 25, 3, true},  // Nested RPN: t=25 -> 3
        
        // --- Operator Precedence (Standard C Rules) ---
        // Hierarchy: Mul (5) > Add (4) > Shift (3) > Logic (1)
        
        // (10*5) + 10 = 60. Then 60 >> 2 = 15.
        {"t*5+t>>2", 10, 15, false},       
        
        // (10*5) = 50. Then 50 >> 2 = 12.
        {"t*5>>2", 10, 12, false},         

        // 2048 * ( (2048>>8) | (2048>>11) ) -> 2048 * (8 | 1) = 18432. 
        // 18432 % 256 = 0.
        {"t*(t>>8|t>>11)", 2048, 0, false}, 
        
        // --- Math Library ---
        {"sin(0)", 0, 128, false},          // Trig: sin(0) centers at 128 in 8-bit space
        {"min(10,20)", 0, 10, false},       // Stack: Smaller of two values (checks pop order)
        {"max(10,20)", 0, 20, false},       // Stack: Larger of two values (checks pop order)
        
        // --- Bitwise & for Logic ---
        // Logic: (7>5 is 1) & (7<10 is 1) -> 1 (True)
        {"(t>5&t<10)?255:0", 7, 255, false}, 
        // Logic: (12>5 is 1) & (12<10 is 0) -> 0 (False)
        {"(t>5&t<10)?255:0", 12, 0, false},

        // --- Stack Stress Test (Deep nesting) ---
        {"((((t+1)+1)+1)+1)", 10, 14, false} // Stack: Verifies depth of operator/value stacks
    };

    for (int i = 0; i < (int)(sizeof(suite) / sizeof(suite[0])); i++) {
        if (suite[i].isRpn) compileRPN(suite[i].expr);
        else compileInfix(suite[i].expr, true);

        uint8_t result = execute_vm(suite[i].t);
        
        if (result != suite[i].expected) {
            String err = "TEST FAIL #" + String(i + 1) + "\n";
            err += "Expr: " + String(suite[i].expr) + "\n";
            err += "T: " + String(suite[i].t) + "\n";
            err += "Exp: " + String(suite[i].expected) + " Got: " + String(result);
            return err;
        }
    }
    return ""; 
}

void runTests(LGFX_Sprite &canvas) {
    String testError = runBytebeatTestSuite();

    if (testError != "") {
        canvas.fillScreen(TFT_RED);
        canvas.setTextColor(TFT_WHITE);
        canvas.setCursor(10, 10);
        canvas.setTextSize(2);
        canvas.println("UNIT TEST FAIL");
        canvas.drawFastHLine(0, 35, 240, TFT_WHITE);
        canvas.setTextSize(1);
        canvas.setCursor(10, 50);
        canvas.println(testError);
        canvas.pushSprite(0, 0);
        while (true) { delay(1000); } 
    }
}

#endif
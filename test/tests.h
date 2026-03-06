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
    bool checkRoundTrip; 
};

String runBytebeatTestSuite() {
    TestCase suite[] = {
        // Basic arithmetic tests
        {"t",                 128, 128, false, true},
        {"t << 1",             64, 128, false, true},
        {"t 8 >>",            256,   1, true,  true},
        {"t & t >> 8",        511,   1, false, true},

        // Ternary operator tests
        {"t > 10 ? 255 : 0",   11, 255, false, true},
        {"t > 10 ? 255 : 0",    5,   0, false, true},
        {"t 10 < 1 t 20 < 2 3 ? ?", 15, 2, true, true},
        
        // Operand ordering and precedence tests
        {"t * 5 + t >> 2",     10,  15, false, true},       
        {"t * 5 >> 2",         10,  12, false, true},         
        {"t * (t >> 8 | t >> 11)", 2048, 0, false, true}, 
        
        // Stack tests
        {"((((t + 1) + 1) + 1) + 1)",  10,  14, false, true},

        // Random tests
        {"min(10, 20)",         0,  10, false, true},
        {"max(10, 20)",         0,  20, false, true},
        {"a = 10; t + a",       5,  15, false, true},
        {"10 a = t a +",        5,  15, true,  true},
        {"f(x) { x * 2 }; f(t)", 10,  20, false, true},
        {"add(a, b) { a + b }; add(t, 5)", 10, 15, false, true},
        {"a = 5; f(x) { x + a }; f(t)", 10,  15, false, true},
        {"( x ) { x 2 * } f = t f",   10,  20, true,  true},
        {"() { 255 } f = t f +",       0, 255, true,  true},
        
        // Functional testing
        {"tune = 50; acid(n) { (n * 2) + tune }; acid(10)", 0, 70, false, true},
        {"apply(f, x) { f(x) }; double(n) { n * 2 }; apply(double, 10)", 0, 20, false, true},
        {"(f x) { x f } apply = (n) { n 2 * } 10 apply", 0, 20, true, true},
        {"v = 0; d = 1; seq() { d = v > 2 ? 0 - 1 : v < 1 ? 1 : d; v = v + d; v }; yield = seq; yield() + yield() + yield() + yield()", 0, 8, false, true},
        {"f(n) { n < 5 ? 100 : 200 }; f(2)",   2, 100, false, true}, 
        {"f(n) { n < 5 ? 100 : 200 }; f(10)", 10, 200, false, true}, 
        {"( n ) { n 5 < () { 100 } () { 200 } ? } f = 2 f",   2, 100, true, true},
        {"( n ) { n 5 < () { 100 } () { 200 } ? } f = 10 f", 10, 200, true, true},

        // Nested conditionals
        {"t < 10 ? 1 : t < 20 ? 2 : 3",  5, 1, false, true}, 
        {"t < 10 ? 1 : t < 20 ? 2 : 3", 15, 2, false, true}, 
        {"t < 10 ? 1 : t < 20 ? 2 : 3", 25, 3, false, true},

        // Recursive functions
        {"fib(n) { n < 2 ? 1 : fib(n - 1) + fib(n - 2) }; fib(t % 10)", 4, 5, false, true},
        {"( n ) { n 2 < () { 1 } () { n 1 - fib n 2 - fib + } ? } fib = t 10 % fib", 4, 5, true, true}, 
        {"fac(n) { n < 2 ? 1 : n * fac(n - 1) }; fac(t % 10)", 4, 24, false, true},

        // General function tests
        {"g(n) { n < 1 ? 1 : n % 2 < 1 ? g(n / 2) : 2 * g(n / 2) }; g(7)", 0, 8, false, true},
        {"tm(n) { n < 1 ? 0 : n % 2 < 1 ? tm(n / 2) : 1 - tm(n / 2) }; tm(7)", 0, 1, false, true},
        {"osc(n, p) { n % p }; osc(t, 100)", 150, 50, false, true},
        {"s(x) { x & (x >> 8) }; s(t)", 511, 1, false, true}
    };

    int num_tests = sizeof(suite) / sizeof(suite[0]);

    for (int i = 0; i < num_tests; i++) {
        bool compOk = suite[i].isRpn ? compileRPN(suite[i].expr) : compileInfix(suite[i].expr, true);
        if (!compOk) return "FAIL #" + String(i+1) + " (Init Compile)\nExpr: " + String(suite[i].expr) + "\nEXP: Compile OK\nGOT: Compile Rejected";
        
        uint8_t res1 = execute_vm(suite[i].t);
        if (res1 != suite[i].expected) return "FAIL #" + String(i+1) + " (Init Exec)\nExpr: " + String(suite[i].expr) + "\nEXP: " + String(suite[i].expected) + "\nGOT: " + String(res1);

        if (suite[i].checkRoundTrip) {
            String oppStr = decompile(!suite[i].isRpn);
            bool oppOk = (!suite[i].isRpn) ? compileRPN(oppStr) : compileInfix(oppStr, true);
            if (!oppOk) return "FAIL #" + String(i+1) + " (Opp Compile)\nOrig: " + String(suite[i].expr) + "\nEXP: Compile OK\nGOT: Rejected on ->\n" + oppStr;
            
            uint8_t res2 = execute_vm(suite[i].t);
            if (res2 != suite[i].expected) return "FAIL #" + String(i+1) + " (Opp Exec)\nTranspiled: " + oppStr + "\nEXP: " + String(suite[i].expected) + "\nGOT: " + String(res2);

            String origStr = decompile(suite[i].isRpn);
            bool origOk = suite[i].isRpn ? compileRPN(origStr) : compileInfix(origStr, true);
            if (!origOk) return "FAIL #" + String(i+1) + " (RT Compile)\nOrig: " + String(suite[i].expr) + "\nEXP: Compile OK\nGOT: Rejected on ->\n" + origStr;
            
            uint8_t res3 = execute_vm(suite[i].t);
            if (res3 != suite[i].expected) return "FAIL #" + String(i+1) + " (RT Exec)\nRT Expr: " + origStr + "\nEXP: " + String(suite[i].expected) + "\nGOT: " + String(res3);
        }
    }
    return ""; 
}

void runTests(LGFX_Sprite &canvas) {
    String testError = runBytebeatTestSuite();

    if (testError != "") {
        canvas.fillScreen(TFT_RED);
        canvas.setTextColor(TFT_WHITE);
        canvas.setCursor(5, 5);
        canvas.setTextSize(2);
        canvas.println("UNIT TEST FAIL");
        
        canvas.drawFastHLine(0, 25, 240, TFT_WHITE);
        
        canvas.setTextSize(1);
        canvas.setCursor(5, 35);
        
        int char_limit = 38;
        for (size_t i = 0; i < testError.length(); i += char_limit) {
            canvas.println(testError.substring(i, i + char_limit));
        }
        
        canvas.pushSprite(0, 0);
        while (true) { delay(1000); } 
    }
}

#endif
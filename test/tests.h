#ifndef TESTS_H
#define TESTS_H

#include <Arduino.h>
#include "state.h" 
#include "vm.h"

#if defined(NATIVE_BUILD) || defined(__EMSCRIPTEN__)
#include <stdio.h>
#endif

struct TestCase {
    const char* expr;
    int32_t t;
    uint8_t expected;
    bool isRpn;
    bool checkRoundTrip; 
};

String runBytebeatTestSuite() {
    TestCase suite[] = {
        {"t",                 128, 128, false, true},
        {"t << 1",             64, 128, false, true},
        {"t 8 >>",            256,   1, true,  true},
        {"t & t >> 8",        511,   1, false, true},
        {"t > 10 ? 255 : 0",   11, 255, false, true},
        {"t > 10 ? 255 : 0",    5,   0, false, true},
        {"t 10 < 1 t 20 < 2 3 ? ?", 15, 2, true, true},
        {"t * 5 + t >> 2",     10,  15, false, true},       
        {"t * 5 >> 2",         10,  12, false, true},         
        {"t * (t >> 8 | t >> 11)", 2048, 0, false, true}, 
        {"((((t + 1) + 1) + 1) + 1)",  10,  14, false, true},
        {"min(10, 20)",         0,  10, false, true},
        {"max(10, 20)",         0,  20, false, true},
        {"a = 10; t + a",       5,  15, false, true},
        {"10 a = t a +",        5,  15, true,  true},
        
        {"f = (x) => x * 2; f(t)", 10,  20, false, true},
        {"add = (a, b) => a + b; add(t, 5)", 10, 15, false, true},
        {"a = 5; f = x => x + a; f(t)", 10,  15, false, true},
        {"( x ) { x 2 * } f = t f",   10,  20, true,  true},
        {"() { 255 } f = t f +",       0, 255, true,  true},
        {"tune = 50; acid = (n) => (n * 2) + tune; acid(10)", 0, 70, false, true},
        
        // --- HIGHER ORDER FUNCTIONS ---
        {"apply = (f, x) => f(x); double = (n) => n * 2; apply(double, 10)", 0, 20, false, true},
        {"(f x) { x f } apply = (n) { n 2 * } 10 apply", 0, 20, true, true},
        {"v = 0; d = 1; seq = () => { d = v > 2 ? 0 - 1 : v < 1 ? 1 : d; v = v + d; v }; yield = seq; yield() + yield() + yield() + yield()", 0, 8, false, true},
        
        // --- UNARY & NEGATIVE TESTS ---
        {"-5",                    0, 251, false, true}, 
        {"-(t * 5)",              2, 246, false, true},
        {"-(-t * 5)",             2,  10, false, true},
        {"t 5 * -1 *",            2, 246, true,  true}, 
        {"-t 5 *",                2, 246, true,  true}, 
        {"t -5 *",                2, 246, true,  true},
        
        // --- DECIMAL & TERNARY FLOAT TESTS ---
        {"v = -10.0; t < 5 ? v * -2.0 : v",  0,  20, false, true},
        {"v = -10.0; t < 5 ? v * -2.0 : v", 10, 246, false, true},
        {"a = -1.5; b = -2.0; t ? a * b : -5.0", 1, 3, false, true},
        {"a = -1.5; b = -2.0; t ? a * b : -5.0", 0, 251, false, true},
        {"v = t * 0.05; f = (x) => x < 0.5 ? -100.0 : 100.0; f(v % 1.0)", 0, 156, false, true},
        {"v = t * 0.05; f = (x) => x < 0.5 ? -100.0 : 100.0; f(v % 1.0)", 15, 100, false, true},
        
        // --- NEW FEATURES: COMPOUND ASSIGNMENT, SCIENTIFIC NOTATION, COMMA ---
        {"a = 10; a += 5; a",    0,  15, false, true},
        {"a = 20; a -= 5; a",    0,  15, false, true},
        {"a = 10; a *= 2; a",    0,  20, false, true},
        {"a = 20; a /= 2; a",    0,  10, false, true},
        {"a = 15; a %= 4; a",    0,   3, false, true},
        {"a = 15; a &= 7; a",    0,   7, false, true},
        {"a = 4;  a |= 3; a",    0,   7, false, true},
        {"a = 15; a ^= 7; a",    0,   8, false, true},
        {"a = 2;  a <<= 2; a",   0,   8, false, true},
        {"a = 16; a >>= 2; a",   0,   4, false, true},
        {"a = 3;  a **= 2; a",   0,   9, false, true},
        
        {"10 a = 5 a += a",      0,  15, true,  true},
        {"2 a = 2 a <<= a",      0,   8, true,  true},
        
        {"a = 5, b = 10, a + b", 0,  15, false, true},
        
        {"1e2 + 50",             0, 150, false, true},
        {"1e2 50 +",             0, 150, true,  true},
        {"2.5e1 * 2",            0,  50, false, true},
        {"2.5e1 2 *",            0,  50, true,  true},
        {"1e-1 * 100",           0,  10, false, true},
        
        // --- NEW FEATURES: SHORT CIRCUIT LOGIC ---
        {"0 && 5",               0,   0, false, true},
        {"1 && 5",               0,   5, false, true},
        {"1 || 5",               0,   1, false, true},
        {"0 || 5",               0,   5, false, true},
        {"a = 1; 0 && (a = 2); a", 0, 1, false, false}, // Explicitly ensures `a` is blocked from assignment
        {"a = 1; 1 || (a = 2); a", 0, 1, false, false}, // Explicitly ensures `a` is blocked from assignment

        // --- LOGIC & ARRAYS ---
        {"f = (n) => n < 5 ? 100 : 200; f(2)",   2, 100, false, true}, 
        {"f = (n) => n < 5 ? 100 : 200; f(10)", 10, 200, false, true}, 
        {"( n ) { n 5 < () { 100 } () { 200 } ? } f = 2 f",   2, 100, true, true},
        {"( n ) { n 5 < () { 100 } () { 200 } ? } f = 10 f", 10, 200, true, true},
        {"t < 10 ? 1 : t < 20 ? 2 : 3",  5, 1, false, true}, 
        {"t < 10 ? 1 : t < 20 ? 2 : 3", 15, 2, false, true}, 
        {"t < 10 ? 1 : t < 20 ? 2 : 3", 25, 3, false, true},
        {"fib = (n) => n < 2 ? 1 : fib(n - 1) + fib(n - 2); fib(t % 10)", 4, 5, false, true},
        {"( n ) { n 2 < () { 1 } () { n 1 - fib n 2 - fib + } ? } fib = t 10 % fib", 4, 5, true, true}, 
        {"fac = (n) => n < 2 ? 1 : n * fac(n - 1); fac(t % 10)", 4, 24, false, true},
        {"g = (n) => n < 1 ? 1 : n % 2 < 1 ? g(n / 2) : 2 * g(n / 2); g(7)", 0, 8, false, true},
        {"tm = (n) => n < 1 ? 0 : n % 2 < 1 ? tm(n / 2) : 1 - tm(n / 2); tm(7)", 0, 1, false, true},
        {"osc = (n, p) => n % p; osc(t, 100)", 150, 50, false, true},
        {"s = (x) => x & (x >> 8); s(t)", 511, 1, false, true},
        {"t == 100 ? 255 : 0", 100, 255, false, true},
        {"t == 100 ? 255 : 0",  99,   0, false, true},
        {"t != 100 ? 255 : 0", 100,   0, false, true},
        {"t != 100 ? 255 : 0",  99, 255, false, true},
        {"t <= 10 ? 255 : 0",   10, 255, false, true},
        {"t <= 10 ? 255 : 0",    9, 255, false, true},
        {"t <= 10 ? 255 : 0",   11,   0, false, true},
        {"t >= 10 ? 255 : 0",   10, 255, false, true},
        {"t >= 10 ? 255 : 0",   11, 255, false, true},
        {"t >= 10 ? 255 : 0",    9,   0, false, true},
        {"t % 2 == 0 ? 255 : 0", 4, 255, false, true}, 
        {"t % 2 == 0 ? 255 : 0", 5,   0, false, true}, 
        {"t 3 % 0 == 1 0 ?",     6,   1, true,  true}, 
        {"t > 5 && t < 15 ? 1 : 0", 10,  1, false, true}, 
        {"t > 5 && t < 15 ? 1 : 0", 20,  0, false, true},
        {"t < 5 || t > 15 ? 1 : 0", 20,  1, false, true}, 
        {"!(t == 10) ? 1 : 0",      11,  1, false, true}, 
        {"t 10 == ! 1 0 ?",         11,  1, true,  true},
        {"~t",                   0, 255, false, true},
        {"-t",                   1, 255, false, true},
        {"(-t & 127)",           1, 127, false, true},
        {"t ~",                  0, 255, true,  true},
        {"[10, 20, 30][1]",      0,  20, false, true}, 
        {"[10, 20, 30][t]",      2,  30, false, true}, 
        {"[10, 20, 30][-1]",     0,  30, false, true}, 
        {"[10, 20, 30][-2]",     0,  20, false, true}, 
        {"[10, 20, 30][4]",      0,  20, false, true}, 
        {"'1112'[3]",            0,   2, false, true}, 
        {"'5'[0]",               0,   5, false, true}, 
        {"10 20 30 3 _ 1 @",     0,  20, true,  true},
        {"'1112' 3 @",           0,   2, true,  true},
        {"10 20 30 3_1@",        0,  20, true,  true}, 
        {"2 ** 3",               0,   8, false, true},
        
        // --- COMPLEX MELODIES & SYNTHESIS ---
        {"seq = [0, 2, 4, 7, 12, 7, 4, 2]; n = seq[(t >> 12) % 8]; f = t * n; (f ^ (f >> 8)) & 255", 4106, 52, false, true},
        {"s = (x) => x % 255; c = (freq) => t * freq; m = (freq, amt) => s(c(freq)) * amt; s(c(440) + m(220, 2)) / 2 + 128", 0, 128, false, true},
        {"o1 = t * [3, 5, 6, 9][(t >> 13) % 4]; o2 = t * [6, 10, 12, 18][(t >> 13) % 4]; ((o1 & o1 >> 8) + (o2 & o2 >> 8)) / 2", 8202, 48, false, true},
        {"kick = t % 4096 < 100 ? 255 - (t % 4096) * 2 : 0; hat = t % 2048 < 50 ? 255 : 0; seq = [1, 0, 1, 0, 1, 1, 0, 1][(t >> 12) % 8]; seq ? kick : hat", 4106, 255, false, true},
        {"( n ) { t n n 4 >> | * 8 >> } m = ; 3 5 7 12 4 _ t 10 >> 4 % @ m", 2058, 56, true, true},
        {"a=[1,2,3][(t>>10)%4]*[2,4,5,6][(t>>16)%4]*t,b=a%32+t>>a,c=b%t+a,[(t>>a)+b,c]", 100, 100, false, true},
        {"a=t*2**([0,0,-2,-2,3,3,-2,3][t>>13&7]/12),b=t*2**([-5,1,2,3,3][t>>11&63]/12+2),(3*a^t>>6&256/'1112'[t>>14&3]-1|a)%256*2/3+(b^b*2)%256/3", 8192, 85, false, true},
        {"f=x=>((t*304.444*(x**.25/1024)&128)%256-64)/256/2.666,[f(120)+f(220),f(300)+f(t>>8|t>>4)]", 0, 0, false, true}
    };

    int num_tests = sizeof(suite) / sizeof(suite[0]);

    for (int i = 0; i < num_tests; i++) {
        bool compOk = suite[i].isRpn ? compileRPN(suite[i].expr) : compileInfix(suite[i].expr, true);
        if (!compOk) return "Fail " + String(i+1) + ": Init compile\nExp: " + String(suite[i].expected) + "\nGOT: Compile rejected\nOn:\n" + String(suite[i].expr);
        
        uint8_t res1 = execute_vm(suite[i].t);
        if (res1 != suite[i].expected) return "Fail " + String(i+1) + ": Init exec\nExp: " + String(suite[i].expected) + "\nGOT: " + String(res1) + "\nOn:\n" + String(suite[i].expr);

        if (suite[i].checkRoundTrip) {
            String oppStr = decompile(!suite[i].isRpn);

            bool oppOk = (!suite[i].isRpn) ? compileRPN(oppStr) : compileInfix(oppStr, true);
            if (!oppOk) return "Fail " + String(i+1) + ": Opp compile\nExp: " + String(suite[i].expected) + "\nGOT: Compile rejected\nOn:\n" + oppStr;
            
            uint8_t res2 = execute_vm(suite[i].t);
            if (res2 != suite[i].expected) return "Fail " + String(i+1) + ": Opp exec\nExp: " + String(suite[i].expected) + "\nGOT: " + String(res2) + "\nOn:\n" + oppStr;

            String origStr = decompile(suite[i].isRpn);

            bool origOk = suite[i].isRpn ? compileRPN(origStr) : compileInfix(origStr, true);
            if (!origOk) return "Fail " + String(i+1) + ": RT compile\nExp: " + String(suite[i].expected) + "\nGOT: Compile rejected\nOn:\n" + origStr;
            
            uint8_t res3 = execute_vm(suite[i].t);
            if (res3 != suite[i].expected) {
#if defined(NATIVE_BUILD) || defined(__EMSCRIPTEN__)
                printf("\n======================================================\n");
                printf("ROUNDTRIP FAILURE DETECTED ON TEST %d\n", i + 1);
                printf("======================================================\n");
                printf("Phase 0: Original Expr (%s)\n", suite[i].isRpn ? "RPN" : "INFIX");
                printf(" -> %s\n\n", suite[i].expr);
                printf("Phase 1: Translated to %s\n", !suite[i].isRpn ? "RPN" : "INFIX");
                printf(" -> %s\n\n", oppStr.c_str());
                printf("Phase 2: Re-translated back to %s\n", suite[i].isRpn ? "RPN" : "INFIX");
                printf(" -> %s\n\n", origStr.c_str());
                printf("Execution Results:\n");
                printf("Expected VM Output: %d\n", suite[i].expected);
                printf("Got VM Output     : %d\n", res3);
                printf("======================================================\n\n");
                fflush(stdout);
#endif
                return "Fail " + String(i+1) + ": RT exec\nExp: " + String(suite[i].expected) + "\nGOT: " + String(res3) + "\nOn:\n" + origStr;
            }
        }
    }
    return ""; 
}

#if !defined(NATIVE_BUILD) && !defined(__EMSCRIPTEN__)
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
        for (size_t i = 0; i < testError.length(); i += char_limit) canvas.println(testError.substring(i, i + char_limit));
        canvas.pushSprite(0, 0);
        while (true) { delay(1000); } 
    }
}
#else
void runTestsConsole() {
    String testError = runBytebeatTestSuite();
    if (testError != "") {
        printf("\n========== BYTEBED TEST FAILURE ==========\n");
        printf("%s\n", testError.c_str());
        printf("==========================================\n\n");
    } else {
        printf("\n========== BYTEBED SYSTEM CHECK ==========\n");
        printf("ALL UNIT TESTS PASSED SUCCESSFULLY!\n");
        printf("==========================================\n\n");
    }
}
#endif

#endif
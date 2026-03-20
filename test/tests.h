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
        // --- 1. CORE OPERATORS ---
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
        
        // --- 2. FUNCTIONS & LAMBDAS ---
        {"f = (x) => x * 2; f(t)", 10,  20, false, true},
        {"add = (a, b) => a + b; add(t, 5)", 10, 15, false, true},
        {"a = 5; f = x => x + a; f(t)", 10,  15, false, true},
        {"( x ) { x 2 * } f = t f",   10,  20, true,  true},
        {"() { 255 } f = t f +",       0, 255, true,  true},
        {"tune = 50; acid = (n) => (n * 2) + tune; acid(10)", 0, 70, false, true},
        {"apply = (f, x) => f(x); double = (n) => n * 2; apply(double, 10)", 0, 20, false, true},
        {"(f x) { x f } apply = (n) { n 2 * } 10 apply", 0, 20, true, true},
        {"v = 0; d = 1; seq = () => { d = v > 2 ? 0 - 1 : v < 1 ? 1 : d; v = v + d; v }; yield = seq; yield() + yield() + yield() + yield()", 0, 8, false, true},
        
        // --- 3. NEGATION & FLOATS ---
        {"-5",                    0, 251, false, true}, 
        {"-(t * 5)",              2, 246, false, true},
        {"-(-t * 5)",             2,  10, false, true},
        {"t 5 * -1 *",            2, 246, true,  true}, 
        {"-t 5 *",                2, 246, true,  true}, 
        {"t -5 *",                2, 246, true,  true},
        {"v = -10.0; t < 5 ? v * -2.0 : v",  0,  20, false, true},
        {"v = -10.0; t < 5 ? v * -2.0 : v", 10, 246, false, true},
        {"a = -1.5; b = -2.0; t ? a * b : -5.0", 1, 3, false, true},
        {"a = -1.5; b = -2.0; t ? a * b : -5.0", 0, 251, false, true},
        {"v = t * 0.05; f = (x) => x < 0.5 ? -100.0 : 100.0; f(v % 1.0)", 0, 156, false, true},
        {"v = t * 0.05; f = (x) => x < 0.5 ? -100.0 : 100.0; f(v % 1.0)", 15, 100, false, true},
        
        // --- 4. ASSIGNMENT OPERATORS ---
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
        
        // --- 5. CONSTANTS & SCIENTIFIC ---
        {"1e2 + 50",             0, 150, false, true},
        {"1e2 50 +",             0, 150, true,  true},
        {"2.5e1 * 2",            0,  50, false, true},
        {"2.5e1 2 *",            0,  50, true,  true},
        {"1e-1 * 100",           0,  10, false, true},
        {"PI * 10",               0,  31, false, true}, 
        {"pi 10 *",               0,  31, true,  true},
        {"TAU * 10",              0,  62, false, true},
        {"E * 10",                0,  27, false, true},
        {"SQRT2 * 100",           0, 141, false, true},
        {"SQRT12 * 100",          0,  70, false, true},
        {"INVPI2 * 100",          0,  63, false, true},
        {"INVPI * 100",           0,  31, false, true},
        {"PI2 * 10",              0,  15, false, true},
        {"PI4 * 100",             0,  78, false, true},
        {"LN2 * 100",             0,  69, false, true},
        {"LN10 * 10",             0,  23, false, true},
        {"LOG2E * 100",           0, 144, false, true},
        {"LOG10E * 100",          0,  43, false, true},
        {"INVSQRTPI * 100",       0, 112, false, true},
        
        // --- 6. LOGIC & COMPARISON ---
        {"0 && 5",               0,   0, false, true},
        {"1 && 5",               0,   5, false, true},
        {"1 || 5",               0,   1, false, true},
        {"0 || 5",               0,   5, false, true},
        {"a = 1; 0 && (a = 2); a", 0, 1, false, false}, 
        {"a = 1; 1 || (a = 2); a", 0, 1, false, false}, 
        {"t == 100 ? 255 : 0", 100, 255, false, true},
        {"t != 100 ? 255 : 0", 100,   0, false, true},
        {"t <= 10 ? 255 : 0",   10, 255, false, true},
        {"t >= 10 ? 255 : 0",   11, 255, false, true},
        {"t > 5 && t < 15 ? 1 : 0", 10,  1, false, true}, 
        {"!(t == 10) ? 1 : 0",      11,  1, false, true}, 
        
        // --- 7. PERSISTENT ARRAYS ($) ---
        {"L = $[8191]; L[5] = 100; L[5]", 0, 100, false, true},
        {"8191 $ L = 100 5 L # 5 L @", 0, 100, true, true},
        {"L = $[m = 10]; m", 0, 10, false, true},
        {"L = $[10]; L[5] = 0; L[5] || 99", 0, 99, false, true},
        {"L = $[10]; D = (e) => L[e] || 42; L[0] = D(5); L[0]", 0, 42, false, true},
        {"t ? 100 : $[m=8191][0]", 0, 0, false, false},
        {"L=$[10]; L[0]=100; L[9]=50; L[0] = (L[0]+L[9])/2; L[0]", 0, 75, false, true},
        {"M=$[1],M[0]=10,M[0]",   0,  10, false, false},       

        // --- 8. SCRATCHPAD ARRAYS ([]) ---
        {"[10, 20, 30][1]",      0,  20, false, true}, 
        {"[10, 20, 30][t]",      2,  30, false, true}, 
        {"[10, 20, 30][-1]",     0,  30, false, true}, 
        {"1 10 20 30 3 _ @",     0,  20, true,  true},
        {"m = [10, 20, 42]; m[2]", 0, 42, false, false},
        {"m = [10, 20]; m[0] + m[1]", 0, 30, false, false},
        {"v += (sin(t * 0.1) - p - v * 3) / 200, p += v, p * 100", 1000, 104, false, false},

        // --- 9. SMART STRINGS ---
        {"'1112'[3]",            0,  2, false, true}, 
        {"'5'[0]",               0,  5, false, true}, 
        {"'abc'[0]",             0,  10, false, true}, 
        {"' '[0]",               0,  32, false, true}, 
        {"m = 'aa hh'; m[3]",    0, 17, false, true}, 
        {"'123'[0]",             0,   1, false, true}, 
        {"'5'[0] * 10",          0,  50, false, true}, 
        {"'a'[0] + 97",          0,   107, false, true},
        {"3 '1112' @",           0,  2, true,  true},

        // --- 10. MULTIDIMENSIONAL & NESTED (NEW) ---
        {"[[10,20],[30,40]][1][0]", 0, 30, false, true}, 
        {"0 1 10 20 2 _ 30 40 2 _ 2 _ @ @", 0, 30, true, true},
        {"[[[42]]][0][0][0]", 0, 42, false, true}, 
        {"m = ['abc','def']; m[1][1]", 0, 14, false, true}, 
        {"m = [[1,2,3], 10, [4,5]]; m[2][0]", 0, 4, false, true}, 

        // --- 11. COMPLEX FORMULAS ---
        {"p=floor(t/4000)%16;m=[2,4,5,9,2,4,5,9,0,4,5,9,0,4,5,9][p];f=(n,s)=>(t*(n+s)%256)/128-1;(f(m,40)+f(m,60)+f(m,80))/3", 100, 0, false, true},
        {"a=t*[1,1,1,1,1.5,1.5,1.33,1.33][t>>15&7],b=t*[1,1.33][t>>14&1],sin(t*'6868686834343434'[t>>13&15]/41)/8+4*b%256/256/4+atan(tan(a/41)+cos(a/100))/[4,8,16,32][t>>13&3]", 1000, 9, false, true},
        {"sin(100*2**(-t/2048%8))/2+tan(cbrt(sin(t*[1,0,2,4,0,2,3,2,1.5,2,1,0,2,3,2,1.5][t>>13&15]/41)))/[2,3,4,6,8,12,16,24][t/[1,1.5][t>>12&1]>>10&7]/4+cbrt(asin(sin(t/[2,3,2.5,4][t>>16&3]/41)))/6", 1000, 144, false, true},
        {"a=t*2**([0,0,-2,-2,3,3,-2,3][t>>13&7]/12),b=t*2**([-5,1,2,3,3][t>>11&63]/12+2),(3*a^t>>6&256/'1112'[t>>14&3]-1|a)%256*2/3+(b^b*2)%256/3", 8192, 114, false, true},
        {"m=8191; q=1; u=0; t+5-(896>>q/4)/(q?8:1)&m^~m*q", 0, 149, false, false},
        {"L=t?L:$[m=8191],M=t/34e4,[(X=q=>q<14&&X(q+1)+(x=M*(16<<q/4)+q/4,u=x%1*8,D=e=>L[t+e-(896>>q/4)/(q?(8+[2-(M&3&M/4)%3,7,4][q%3])*(~M/2&1|8)/40:1+9/9**u)&m^~m*q]||0,L[t&m^~m*q]=D(R+=D(49*q))/2+D(m/9**M)*.45+sin(40/1e9**(u*u)*(q>2?tan(x/64%4):2))/(4+q)))(R=0),R]", 0, 118, false, false},

        // --- 12. SHORTHANDS (NEW) ---
        {"s(0)",                0, 128, false, true},
        {"0 s",                 0, 128, true,  true},
        {"c(0)",                0, 255, false, true},
        {"0 c",                 0, 255, true,  true},
        {"f(1.9)",              0,   1, false, true},
        {"1.9 f",               0,   1, true,  true},
        {"i(1.9)",              0,   1, false, true},
        {"1.9 i",               0,   1, true,  true},
        {"r() * 0",             0,   0, false, true},
        {"0 r *",               0,   0, true,  true},
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
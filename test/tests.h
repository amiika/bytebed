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
    uint32_t expected; 
    bool isRpn;
    bool checkRoundTrip; 
    PlayMode mode;

    TestCase(const char* e, int32_t _t, uint32_t exp, bool rpn, bool rt, PlayMode m = MODE_BYTEBEAT)
        : expr(e), t(_t), expected(exp), isRpn(rpn), checkRoundTrip(rt), mode(m) {}
};

String runBytebeatTestSuite() {
    current_sample_rate = 8000;
    
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
        {"pi 10 *",               0,  31, true,  true},
        {"tau * 10",              0,  62, false, true},
        {"e * 10",                0,  27, false, true},
        {"sqrt2 * 100",           0, 141, false, true},
        {"sqrt12 * 100",          0,  70, false, true},
        {"invpi2 * 100",          0,  63, false, true},
        {"invpi * 100",           0,  31, false, true},
        {"pi2 * 10",              0,  15, false, true},
        {"pi4 * 100",             0,  78, false, true},
        {"ln2 * 100",             0,  69, false, true},
        {"ln10 * 10",             0,  23, false, true},
        {"log2e * 100",          0, 144, false, true},
        {"log10e * 100",          0,  43, false, true},
        {"invsqrtpi * 100",       0, 112, false, true},
        
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
        
        // --- 7. PERSISTENT ARRAYS ---
        {"L = $[8191]; L[5] = 100; L[5]", 0, 100, false, true},
        {"8191 $ L = L 5 100 # L 5 @", 0, 100, true, true}, 
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
        {"10 20 30 3 _ 1 @",     0,  20, true,  true}, 
        {"m = [10, 20, 42]; m[2]", 0, 42, false, false},
        {"m = [10, 20]; m[0] + m[1]", 0, 30, false, false},
        {"v += (sin(t * 0.1) - p - v * 3) / 200, p += v, p * 100", 1000, 104, false, false},

        // --- 9. STRING TABLE & BASE62 ---
        {"'1112'[3]",            0,  2, false, true}, 
        {"'5'[0]",               0,  5, false, true}, 
        {"'abc'[0]",             0,  10, false, true}, 
        {"' '[0]",               0,  0, false, true},
        {"'-'[0]",               0,  0, false, true}, 
        {"'0'[0]",               0,  0, false, true}, 
        {"m = 'aa hh'; m[3]",    0, 17, false, true}, 
        {"'123'[0]",             0,   1, false, true}, 
        {"'5'[0] * 10",          0,  50, false, true}, 
        {"'a'[0] + 97",          0,   107, false, true},
        {"'1112' 3 @",           0,  2, true,  true}, 
        {"'aA0'[0]",             0, 10, false, true},
        {"'aA0'[1]",             0, 36, false, true},
        {"'aA0'[2]",             0,  0, false, true},
        {"'Z'[0]",               0, 61, false, true},
        {"[10, 20, 30]['c'[0] - 10]", 0, 30, false, true},
        
        {"t*[0,8/9,1,9/8,6/5,4/3,3/2]['1242660660555555123155055044444412424405503333332211555555444444'[t>>10]]*4", 1025, 4, false, true},
        {"t*[0,8/9,1,9/8,6/5,4/3,3/2]['1242660660555555123155055044444412424405503333332211555555444444'[t>>10]]*4", 2048, 102, false, true},

        // --- 10. MULTIDIMENSIONAL & NESTED ---
        {"[[10,20],[30,40]][1][0]", 0, 30, false, true}, 
        {"10 20 2 _ 30 40 2 _ 2 _ 1 @ 0 @", 0, 30, true, true}, 
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

        // --- 12. SHORTHANDS ---
        {"s(0)",                0, 128, false, true},
        {"0 s",                 0, 128, true,  true},
        {"c(0)",                0, 255, false, true},
        {"0 c",                 0, 255, true,  true},
        {"r() * 0",             0,   0, false, true},
        {"0 r *",               0,   0, true,  true},

        // --- 13. FUNCTIONAL LOOP OPERATORS ---
        {"sum(5, (i) => 2)",             0,  10, false, true},
        {"5 ( i ) { 2 } sum",            0,  10, true,  true},
        {"sum(4, (i) => i)",             0,   6, false, true},
        {"sum([10, 20, 30], x => x + 1)", 0, 63, false, true},
        {"gen(4, (i) => i * 10)[2]",     0,  20, false, true},
        {"a = gen(3, (i) => i + 5); a[0] + a[2]", 0, 12, false, true},
        {"map([1, 2, 3], (x) => x * 10)[1]", 0, 20, false, true},
        {"map(gen(3, i => i + 1), x => x * 2)[2]", 0, 6, false, true},
        {"reduce([10, 20, 30], (a, v) => a + v)", 0, 60, false, true},
        {"[2, 3, 4].reduce((a, v) => a == 0 ? v : a * v)", 0, 24, false, true}, 
        {"filter([10, 0, 20], x => x > 0)[1]", 0, 20, false, true},
        {"[10, 0, 20].filter(x => x > 0)[1]", 0, 20, false, true},
        {"[0, 0, 0].filter(x => x > 0)[0]", 0, 0, false, false}, 
        {"[1, 2, 3].map(x => x * 10)[1]", 0, 20, false, true},
        {"[1, 2].sum(x => x)", 0, 3, false, true},
        {"sum(gen(3, i => i + 1), x => sum(gen(2, j => j), y => x + y))", 0, 15, false, true}, 
        {"fm = (x, a) => x + (x * a); fm(10, 2) + 128", 0, 158, false, true}, 
        {"x = (lr) => sum(4, (i) => i * lr); [x(1), x(2)][0]", 0, 6, false, true}, 

        // --- 14. NEGATIVE ARRAYS & MATH STRESS TESTS ---
        {"[-10, -20][-1]",             0, 236, false, true}, 
        {"[-10, -20][1]",              0, 236, false, true}, 
        {"m = [-5, -15]; m[0] + m[1]", 0, 236, false, false}, 
        {"[[-1, -2], [-3, -4]][0][1]", 0, 254, false, true}, 
        {"[[-1, -2], [-3, -4]][1][0]", 0, 253, false, true}, 
        {"(-10) * (-2)",               0,  20, false, true},
        {"-10 / -2",                   0,   5, false, true},
        {"a = -5; b = -5; a + b",      0, 246, false, true}, 
        {"[-5, 5].reduce((a,v) => a + v)", 0, 0, false, true}, 
        {"[-5, -5].map(x => x * -2)[0]",   0, 10, false, true}, 
        {"2 ** -1 * 10",               0,   5, false, true},

        // --- 15. COMMENTS & DOUBLE QUOTES ---
        {"t + 5 // + 100\n+ 10",       10,  25, false, true},
        {"t + /* inline */ 5",         10,  15, false, true},
        {"\"1234\"[2]",                 0,   3, false, true},
        {"\"5678\" 2 @",                0,   7, true,  true},
        {"a = \"//\"; 50",              0,  50, false, true},
        {"a = '/*'; 60",                0,  60, false, true},

        // --- 16. STACK MANIPULATION ---
        {"5 dup +",                    0,  10, true,  false}, 
        {"10 2 swap -",                0, 248, true,  false}, 
        {"1 2 3 rot",                  0,   1, true,  false}, 
        {"10 2 5 rot * +",             0,  52, true,  false}, 
        {"10 2 over + *",              0, 120, true,  false}, 
        {"10 20 over swap ;",          0,  10, true,  false}, 
        {"5 ++ +",                     0,  10, true,  false}, 
        {"10 2 <> -",                  0, 248, true,  false}, 
        {"1 2 3 @@",                   0,   1, true,  false}, 
        {"10 2 5 @@ * +",              0,  52, true,  false}, 
        {"10 2 ^^ + *",                0, 120, true,  false}, 
        {"10 20 ^^ <> --",             0,  10, true,  false}, 
        {"10 20 over over + + +",      0,  60, true,  false}, 
        {"10 20 ^^ ^^ + + +",          0,  60, true,  false}, 
        {"5 10 swap",                  0,   5, true,  false}, 
        {"5 10 <>",                    0,   5, true,  false}, 
        {"10 20 swap over - -",        0,  30, true,  false}, 
        {"10 20 <> ^^ - -",            0,  30, true,  false}, 
        {"5 10 15 <> @@ ^^ + + +",     0,  40, true,  false}, 
        {"100 ++ ++ ++ + + +",         0, 144, true,  false}, 
        
        {"() { swap over } tuck = 10 20 tuck * +", 0, 220, true, false},
        {"( a b ) { b a b } tuck = 10 20 tuck * +", 0, 220, true, false},

        // --- Custom Helpers & Renamed (in / as / to) ---
        {"v = [1, 2, 3] + [1, 2]; v[0]", 0, 2, false, true},
        {"v = [1, 2, 3] + [1, 2]; v[3]", 0, 3, false, true},
        {"v = [1, 2, 3] + [1, 2]; v[5]", 0, 5, false, true},
        {"v = [10, 20] * 2; v[1]",       0, 40, false, true},
        
        {"beat = 1.5; in(4.0, 4.0) * 10", 0, 15, false, false},
        {"beat = 2.5; in(4.0, 8.0) * 100", 0, 175, false, false},
        {"beat = 4.0; in(6.0, 2.0)",   0, 4, false, false},

        {"env(0.5, 1.0, 1.0) * 100",     0,  0, false, false},
        {"env(0.5, 0.5, 2.0) * 100",     0,  0, false, false},
        {"env(0.5, 1.0, 1.0)",     0, 128, false, false},
        {"env(0.25, 1.0, 1.0)",    0,  96, false, false},
        {"0.5 1.0 1.0 3 : env",    0, 128, true,  true},
        {"osc(0.25, 0)",                 0, 255, false, false},
        {"osc(0.5, 2)",                  0,  0, false, false},
        
        {"pc(0, 0, 0, 12, 440.0, 0) / 100", 0, 2, false, false},
        {"pc(1, 3, 0, 12, 220.0, 0) / 100", 0, 1, false, false},
        
        {"euclid(2, 4, 0)",            0, 10, false, false}, 
        {"euclid(2, 4, 1)",            0, 5,  false, false}, 
        {"euclid(2, 4, 2)",            0, 10, false, false}, 
        {"euclid(2, 4, 4)",            0, 10, false, false}, 
        {"euclid(3, 5, 0)",            0, 21, false, false},

        {"euclid(1, 4)",               0, 8,   false, false}, // 0b1000
        {"euclid(2, 8)",            0, 136, false, false}, // 0b10001000
        {"euclid(3, 6)",            0, 42,  false, false}, // 0b101010
        {"euclid(9, 16)",           0, 43733, false, false}, // 0b1011011010110110
        {"euclid(4, 8)",            0, 170, false, false}, // 0b10101010
        {"euclid(3, 8)",            0, 146, false, false}, // 0b10010010

        {"beat = 0.0; a = euclid(2, 4, 0); on(a, 4)",  0, 1, false, false}, // Slot 0 is ON (10 & 8 > 0)
        {"beat = 0.25; a = euclid(2, 4, 0); on(a, 4)", 0, 0, false, false}, // Slot 1 is OFF (10 & 4 = 0)
        
        {"beat = 0.0;  10 * on(euclid(4, 8), 8)",      0, 10, false, false},  // Slot 0 is ON
        {"beat = 0.25; 10 * on(euclid(4, 8), 8)",      0,  0, false, false},  // Slot 1 is OFF
        {"beat = 0.5;  10 * on(euclid(4, 8), 8)",      0, 10, false, false},  // Slot 2 is ON
     
        {"beat = 0.5; in(4.0, 4.0) * 10", 0, 5, false, false},
        {"beat = 3.0; in(6.0, 6.0) * 10", 0, 20, false, false},
        {"bpm = 250; beat = 0.0; [1,2,3,4,5][in(1, 2, 2, 4)]*t", 10, 10, false, false},
        {"bpm = 250; beat = 0.3; [1,2,3,4,5][in(1, 2, 2, 4)]*t", 10, 20, false, false},
        {"bpm = 250; beat = 1.0; [1,2,3,4,5][in(1, 2, 2, 4)]*t", 10, 30, false, false},
        {"bpm = 250; beat = 1.5; [1,2,3,4,5][in(1, 2, 2, 4)]*t", 10, 40, false, false},
        {"bpm = 250; beat = 3.0; [1,2,3,4,5][in(1, 2, 2, 4)]*t", 10, 20, false, false},

        // --- 17. DYNAMIC ARGUMENT COUNT & EXPLICIT DEFAULT PARAMETERS ---
        {"0 0 0 12 440 0 6 : pc 100 /", 0,   2, true,  true},  
        {"beat = 4.0; in(6.0, 2.0)",  0,   4, false, true},  
        {"4.0 beat = 6.0 2.0 2 : in", 0,   4, true,  true},  
        {"4.0 beat = 4.0 4.0 2 : in 10 *", 0, 40, true,  true},  
        {"2 4 0 3 : euclid",             0, 10, true, true}, // Updated to reflect standard 0b1010 right-alignment
        {"0.5 1.0 1.0 3 : env 100 *",    0,  0, true,  true},
        {"( x y ) { x y + } myfunc = 10 20 2 : myfunc", 0, 30, true, true},
        {"( x ) { x 5 * } h = 10 1 : h", 0, 50, true, true},
        
        {"( x 2 y = ) { x y + } h = 1 1 : h", 0, 3, true, true}, 
        {"( x 3 y = ) { x y + } h = 1 1 : h", 0, 4, true, true}, 
        {"foo = (a, b=1, c=2)=>{a+b+c}; foo(1)", 0, 4, false, true},
        {"f=(a,b=1)=>{a+b}; f(1)", 0, 2, false, true},
        {"foo=(a,b=1,c=2)=>{a+b+c}; foo(1)", 0, 4, false, true},
        {"foo=(a,b=1,c=2)=>{a+b+c}; foo(1, 10)", 0, 13, false, true},

        // --- 18. SEQUENCER HELPERS (AS, TO, AT) ---
        {"as(11, 4)[0] * 10",           0, 20, false, false},
        {"as(11, 4)[1] * 10",           0, 10, false, false},
        {"as(11, 4)[2] * 10",           0, 10, false, false},
        {"11 4 1.0 1.0 4 : as 2 @ 10 *", 0, 10, true, false},
        
        // FIXED t=1 expectations
        {"to(0, 0, 0, 12, 440.0, 0)",    1, 8, false, false},
        {"to(1, 3, 0, 12, 220.0, 0)",    1, 4, false, false}, 
        {"1 3 0 12 220.0 0 6 : to",      1, 4, true,  false}, 
        
        {"beat = 0.0; at(11, 4) * 10",   0,  0, false, false},
        {"beat = 0.25; at(11, 4) * 10",  0,  0, false, false},
        {"beat = 0.5; at(11, 4) * 10",   0, 10, false, false},
        {"beat = 0.75; at(11, 4) * 10",  0, 20, false, false},
        {"beat = 1.25; at(11, 4) * 10",  0, 30, false, false}, 
        {"0.5 beat = 11 4 2 : at 10 *",  0, 10, true,  false},

        // --- 19. DIATONIC DEGREE TESTS (Mask 2773 = 0b101011010101) ---
        {"to(0, 2741, 0, 12, 440.0, 0)", 1, 8,  false, false}, 
        {"to(1, 2741, 0, 12, 440.0, 0)", 1, 9,  false, false}, 
        {"to(2, 2741, 0, 12, 440.0, 0)", 1, 10, false, false}, 
        {"to(3, 2741, 0, 12, 440.0, 0)", 1, 11, false, false}, 
        {"to(4, 2741, 0, 12, 440.0, 0)", 1, 12, false, false}, 
        {"to(5, 2741, 0, 12, 440.0, 0)", 1, 14, false, false}, 
        {"to(6, 2741, 0, 12, 440.0, 0)", 1, 15, false, false}, 
        {"to(7, 2741, 0, 12, 440.0, 0)", 1, 16, false, false}, 

        // --- 20. FLOATBEAT RAW FREQUENCY TESTS ---
        {"abs(pc(0, 2741, 5, 12, 440.0, 0) - 440.0) < 0.1 ? 0.0 : 1.0", 0, 0x80000000, false, false, MODE_FLOATBEAT},
        {"abs(pc(0, 2741, 0, 12, 440.0, 0) - 261.62) < 0.1 ? 0.0 : 1.0", 0, 0x80000000, false, false, MODE_FLOATBEAT},
        {"abs(pc(5, 2741, 0, 12, 440.0, 0) - 440.0) < 0.1 ? 0.0 : 1.0", 0, 0x80000000, false, false, MODE_FLOATBEAT},
    };

    int num_tests = sizeof(suite) / sizeof(suite[0]);
    int failed_count = 0;
    String first_error_msg = "";

    for (int i = 0; i < num_tests; i++) {
        current_play_mode = suite[i].mode;
        
        bool compOk = suite[i].isRpn ? compileRPN(suite[i].expr) : compileInfix(suite[i].expr, true);
        if (!compOk) {
            failed_count++;
            printf("CRITICAL ERROR: Test %d [%s] failed compilation initialization.\n", i + 1, suite[i].expr);
            if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Init compile\nOn:\n" + String(suite[i].expr);
            continue;
        }
        
        uint32_t res1 = executeVm(suite[i].t);
        if (res1 != suite[i].expected) {
            failed_count++;
            printf("DEBUG: Test %d [%s] failed at t=%d.\n  -> Expected: 0x%08X, Got: 0x%08X\n", i + 1, suite[i].expr, suite[i].t, suite[i].expected, res1);
            if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Init exec\nExp: " + String((unsigned long)suite[i].expected) + "\nGOT: " + String((unsigned long)res1) + "\nOn:\n" + String(suite[i].expr);
            continue;
        }

        if (suite[i].checkRoundTrip) {
            String oppStr = decompile(!suite[i].isRpn);
            bool oppOk = (!suite[i].isRpn) ? compileRPN(oppStr) : compileInfix(oppStr, true);
            if (!oppOk) {
                failed_count++;
                printf("DEBUG: Test %d [%s] failed opposite round-trip compiler pass.\n", i + 1, suite[i].expr);
                if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Opp RT Compile\nDecompiled:\n" + oppStr;
                continue;
            }
            
            uint32_t res2 = executeVm(suite[i].t);
            if (res2 != suite[i].expected) {
                failed_count++;
                printf("DEBUG: Test %d [%s] failed round-trip check execution pass.\n", i + 1, suite[i].expr);
                if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Opp RT Exec\nExp: " + String((unsigned long)suite[i].expected) + "\nGOT: " + String((unsigned long)res2) + "\nDecomp:\n" + oppStr;
                continue;
            }

            String origStr = decompile(suite[i].isRpn);
            bool origOk = suite[i].isRpn ? compileRPN(origStr) : compileInfix(origStr, true);
            if (!origOk) {
                failed_count++;
                printf("DEBUG: Test %d [%s] failed native restoration compiler pass.\n", i + 1, suite[i].expr);
                if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Native Rest. Compile\nRestored:\n" + origStr;
                continue;
            }
            
            uint32_t res3 = executeVm(suite[i].t);
            if (res3 != suite[i].expected) {
                failed_count++;
                printf("DEBUG: Test %d [%s] failed native restoration execution validation.\n", i + 1, suite[i].expr);
                if (first_error_msg == "") first_error_msg = "Fail " + String(i+1) + ": Native Rest. Exec\nExp: " + String((unsigned long)suite[i].expected) + "\nGOT: " + String((unsigned long)res3) + "\nRestored:\n" + origStr;
                continue;
            }
        }
    }

    current_play_mode = MODE_BYTEBEAT;

    if (failed_count > 0) {
        printf("\n>>> TOTAL VERIFICATION FAILURE SUMMARY: %d out of %d tests failed. <<<\n\n", failed_count, num_tests);
        return first_error_msg;
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
        printf("COMPILATION FINISHED WITH METRIC ERRORS.\n");
        printf("==========================================\n\n");
    } else {
        printf("\n========== BYTEBED SYSTEM CHECK ==========\n");
        printf("ALL UNIT TESTS PASSED SUCCESSFULLY!\n");
        printf("==========================================\n\n");
    }
}
#endif

#endif
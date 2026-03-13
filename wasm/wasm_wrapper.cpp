#include "vm.h"
#include <emscripten.h>

int32_t t_raw = 0; 

// --- Instantiate the globals required by the VM for WebAssembly ---
PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = 8000;

extern "C" {
    char wasm_input_buffer[2048]; 

    EMSCRIPTEN_KEEPALIVE char* get_input_buffer() { 
        return wasm_input_buffer; 
    }

    EMSCRIPTEN_KEEPALIVE bool wasm_compile(bool is_rpn) {
        String expr(wasm_input_buffer);
        return is_rpn ? compileRPN(expr) : compileInfix(expr, true);
    }

    EMSCRIPTEN_KEEPALIVE uint8_t wasm_execute(int32_t t) {
        return execute_vm(t);
    }

    EMSCRIPTEN_KEEPALIVE const char* wasm_decompile(bool to_rpn) {
        static String last_decomp;
        last_decomp = decompile(to_rpn);
        return last_decomp.c_str();
    }

    // --- Floatbeat & Sample Rate Setters ---
    EMSCRIPTEN_KEEPALIVE void wasm_set_sample_rate(int rate) {
        current_sample_rate = rate;
    }

    EMSCRIPTEN_KEEPALIVE void wasm_set_play_mode(int mode) {
        current_play_mode = (PlayMode)mode;
    }
}

EMSCRIPTEN_KEEPALIVE int main() {
    return 0;
}
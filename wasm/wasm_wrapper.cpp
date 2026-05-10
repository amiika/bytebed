#include "vm.h"
#include <emscripten.h>

// Note: t_raw, current_play_mode, and current_sample_rate are no longer 
// defined here because they are natively linked and managed by state.cpp.

extern "C" {
    char wasm_input_buffer[2048]; 

    /**
     * Helper to keep the WASM runtime variables injected and intact.
     * This protects system variables from being wiped during compilation.
     */
    void inject_runtime_vars() {
        int i_sr = getVarId("sr");
        vars[i_sr].type = 0;
        vars[i_sr].f = (float)current_sample_rate;
    }

    /**
     * Gets the input buffer pointer for JavaScript string passing.
     * @return Pointer to the allocated char buffer
     */
    EMSCRIPTEN_KEEPALIVE char* get_input_buffer() { 
        return wasm_input_buffer; 
    }

    /**
     * Compiles the expression currently in the input buffer.
     * @param is_rpn Flag indicating if the expression is in RPN format
     * @return true if compilation succeeds, false otherwise
     */
    EMSCRIPTEN_KEEPALIVE bool wasm_compile(bool is_rpn) {
        String expr(wasm_input_buffer);
        bool success = is_rpn ? compileRPN(expr) : compileInfix(expr, true);
        inject_runtime_vars(); 
        return success;
    }

    /**
     * Gets the last compilation or runtime error.
     * @return String literal containing the error message
     */
    EMSCRIPTEN_KEEPALIVE const char* wasm_get_last_error() {
        return last_vm_error.c_str();
    }

    /**
     * Executes the virtual machine for a given discrete time step.
     * @param t Absolute time step index
     * @return Resulting audio sample byte
     */
    EMSCRIPTEN_KEEPALIVE uint8_t wasm_execute(int32_t t) {
        return execute_vm(t);
    }

    /**
     * Decompiles the active VM bytecode back into a string formula.
     * @param to_rpn Flag indicating target decompile format
     * @return Decompiled formula string
     */
    EMSCRIPTEN_KEEPALIVE const char* wasm_decompile(bool to_rpn) {
        static String last_decomp;
        last_decomp = decompile(to_rpn);
        return last_decomp.c_str();
    }

    /**
     * Sets the active sample rate and injects it into the VM memory.
     * @param rate Sample rate in Hz
     */
    EMSCRIPTEN_KEEPALIVE void wasm_set_sample_rate(int rate) {
        current_sample_rate = rate;
        inject_runtime_vars();
    }

    /**
     * Sets the active playback mode.
     * @param mode Integer mapping to PlayMode enum (0 = Bytebeat, 1 = Floatbeat)
     */
    EMSCRIPTEN_KEEPALIVE void wasm_set_play_mode(int mode) {
        current_play_mode = (PlayMode)mode;
    }

    /**
     * Resets the virtual machine, clearing global variables and arrays.
     */
    EMSCRIPTEN_KEEPALIVE void wasm_reset_vm() {
        var_count = 0;
        memset(vars, 0, sizeof(vars));
        clear_global_array();
        inject_runtime_vars();
    }

    /**
     * Passes hardware IMU state into the virtual machine memory.
     * @param ax Accelerometer X axis
     * @param ay Accelerometer Y axis
     * @param az Accelerometer Z axis
     * @param gx Gyroscope X axis
     * @param gy Gyroscope Y axis
     * @param gz Gyroscope Z axis
     */
    EMSCRIPTEN_KEEPALIVE void wasm_set_imu(float ax, float ay, float az, float gx, float gy, float gz) {
        updateIMUVars(ax, ay, az, gx, gy, gz);
    }

    /**
     * Passes UI pointer state into the virtual machine memory.
     * @param mx Mouse normalized X coordinate
     * @param my Mouse normalized Y coordinate
     * @param mv Mouse velocity or click magnitude
     */
    EMSCRIPTEN_KEEPALIVE void wasm_set_mouse(float mx, float my, float mv) {
        updateMouseVars(mx, my, mv);
    }

    /**
     * Updates the virtual machine memory with active MIDI parameters.
     * @param freq The frequency of the active MIDI note in Hertz
     * @param gate The velocity/gate state (0.0 for off, >0.0 for active)
     * @param note The raw MIDI note number (0-127)
     */
    EMSCRIPTEN_KEEPALIVE void wasm_set_midi(float freq, float gate, float note) {
        updateMIDIVars(freq, gate, note);
    }

    /**
     * Fetches a formula string from the C++ preset banks.
     * @param bank Bank index (0-9)
     * @param patch Patch index (0-9)
     * @return Pointer to the formula string
     */
    EMSCRIPTEN_KEEPALIVE const char* wasm_get_preset_formula(int bank, int patch) {
        if (bank >= 0 && bank < 10 && patch >= 0 && patch < 10) {
            return defaultBanks[bank][patch].formula;
        }
        return "";
    }

    /**
     * Fetches the default sample rate for a specific preset.
     * @param bank Bank index (0-9)
     * @param patch Patch index (0-9)
     * @return Sample rate in Hz
     */
    EMSCRIPTEN_KEEPALIVE int wasm_get_preset_rate(int bank, int patch) {
        if (bank >= 0 && bank < 10 && patch >= 0 && patch < 10) {
            return defaultBanks[bank][patch].sample_rate;
        }
        return 8000;
    }

    /**
     * Fetches the play mode for a specific preset.
     * @param bank Bank index (0-9)
     * @param patch Patch index (0-9)
     * @return Play mode (0 = Bytebeat, 1 = Floatbeat)
     */
    EMSCRIPTEN_KEEPALIVE int wasm_get_preset_mode(int bank, int patch) {
        if (bank >= 0 && bank < 10 && patch >= 0 && patch < 10) {
            return (int)defaultBanks[bank][patch].mode;
        }
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE int main() {
    return 0;
}
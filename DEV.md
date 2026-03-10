# Build wasm

In wasm folder:

emcc ../src/vm.cpp ../src/compiler.cpp ../src/validator.cpp ../src/decompiler.cpp wasm_wrapper.cpp -I. -I../include -I../src -s EXPORTED_FUNCTIONS="['_get_input_buffer', '_wasm_compile', '_wasm_execute']" -s EXPORTED_RUNTIME_METHODS="['UTF8ToString']" -O3 -s WASM=1 -s STANDALONE_WASM --no-entry -o bytebed.wasm

xxd -i bytebed.wasm > ../include/wasm_binary.h
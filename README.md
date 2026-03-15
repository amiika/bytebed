# Bytebed - Embedded Bytebeats

Woop woop. It beeps! Now with functions!

# DONE

+ Classic C-style bytebeats
+ Infix and RPN compiler & bytecode vm
+ Bytecode decompiler (changes between Infix & RPN via tab)
+ Syntax validator (Prevents crashes)
+ Load & Save slots opt/alt + 0-9
+ Undo & Redo ctrl + z / r
+ Classic visualizations fn + 1-4
+ History view fn + 0
+ Theme fn + t
+ Synced captive wifi portal fn + w
+ Global variables
+ Custom function support
+ Variable chaining and array support

# TODO

- Floating point & Floatbeat support?
- Documentation huh? Maybe just few words about the new function and rpn syntax.

# Development

## WASM

To compile to wasm and wasm binary header file, run this in wasm folder:

emcc ../src/vm.cpp ../src/compiler.cpp ../src/validator.cpp ../src/decompiler.cpp wasm_wrapper.cpp -I. -I../include -I../src -s EXPORTED_FUNCTIONS="['_get_input_buffer', '_wasm_compile', '_wasm_execute']" -s EXPORTED_RUNTIME_METHODS="['UTF8ToString']" -O3 -s WASM=1 -s STANDALONE_WASM --no-entry -o bytebed.wasm

xxd -i bytebed.wasm > ../include/wasm_binary.h

## TESTS

Testing native and cardputer using platformio: 

pio test -e native

For more verbose testing:

pio test -e native -v 


For testing the hardware:

pio test -e cardputer


# Contributions

Sure, why not.

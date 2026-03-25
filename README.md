# Bytebed - Embedded Bytebeats

Woop woop. It beeps! Now with functions!

# DONE

Features:
+ Classic C-style bytebeats
+ Infix and RPN compiler & bytecode vm
+ Bytecode decompiler (changes between Infix & RPN via tab)
+ Syntax validator (Prevents crashes)
+ Global variables
+ Custom function support
+ Variable chaining and array support
+ Some sort of floatbeat support

Variables & functions:
+ All math: sin/s, cos/c, floor, int, cbrt and constants like pi, e ... etc ...
+ Accelerometer: ax, ay, az
+ Gyroscope: gx, gy, gz
+ Mouse (for wasm): mx, my, mv (velocity)

Controls & Modes:
+ ctrl + 0-9: Select bank (Presets & User patches)
+ opt + 0-9: Load patch from current bank
+ alt + 0-9: Save patch to location
+ ctrl + z/r: Undo & Redo
+ fn + 1-4: Classic visualizations
+ fn + 0: History view
+ fn + t: Change theme color
+ fn + s: Change samplerate
+ fn + f: Toggle bytebeat / floatbeat
+ fn + w: Wifi sync (Captive portal & bytebeat.local)
+ fn + l: Slave mode: Listen (ESP-NOW sync mode)
+ fn + m: Master mode: Sync (ESP-NOW sync mode)

# TODO / IDEAS

- Documentation huh? Meanwhile look at test/tests.h ... and vm.cpp.

# Development

## WASM

To compile to wasm and wasm binary header file, run this in wasm folder:

emcc ../src/vm.cpp ../src/compiler.cpp ../src/validator.cpp ../src/decompiler.cpp wasm_wrapper.cpp -I. -I../include -I../src -s EXPORTED_FUNCTIONS="['_main', '_get_input_buffer', '_wasm_compile', '_wasm_execute', '_wasm_decompile', '_wasm_set_sample_rate', '_wasm_set_play_mode']" -O3 -s WASM=1 -s STANDALONE_WASM --no-entry -o bytebed.wasm

xxd -i bytebed.wasm | sed 's/unsigned char/const unsigned char/g' > ../include/wasm_binary.h

## TESTS

Testing native and cardputer using platformio: 

pio test -e native

For more verbose testing:

pio test -e native -v 

For testing the hardware:

pio test -e cardputer


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

# TODO

- Variables as global state?
- Variable chaining and additions?
- Functions as separate macro stack?
- Floating point & Floatbeat support?

# Contributions

Sure, why not.

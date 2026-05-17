# Bytebed - Embedded Bytebeats

Bytebeat virtual machine for M5Stack Cardputers.

Running bytebeat on ESP32 boards using [i2s api](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2s.html) can be simple as:

```
#include <ESP_I2S.h>

I2SClass i2s;
int t = 0;

void setup() {
  i2s.setPins(41, 43, 42); 
  i2s.begin(I2S_MODE_STD, 8000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

void loop() {
  uint8_t b = t*(((t>>12)|(t>>8))&(63&(t>>4)));
  int16_t s = (b - 128) * 50; 
  uint32_t frame = (s & 0xFFFF) | (s << 16);
  i2s.write((uint8_t*)&frame, 4); 
  t++;
}
```

Upload this to your ESP board and boom! You have some classic bytebeat going on. If you are happy with this do not bother reading further. Just be happy and move on.

However ... If you want to program bytebeat and floatbeat on the fly on your tiny winey cardputer ... Keep on reading then, I suppose.

# Bytebed features

Bytebed started as a simple bytecode virtual machine for the bytebeat. Main feature is still on the fly support for interpreting and playing classic bytebeats. It also has more extensive and experimental features like:

+ Dual syntax: Switching between infix and postfix on the fly using tab key
+ Opionated floatbeat support: No while or for, just map and reduce
+ JS-like arrow functions
+ Strings interpreted as Base 62 number arrays
+ Magic global variables and arrays for delays, feedback and such
+ Forth like stack operators in prefix mode (dup, swap, over, rot)
+ Comments // and /* */
+ Syntax validator and error messages
+ Other undocumented features (See tests)

# Variables & functions

+ All math: sin/s, cos/c, floor, int, cbrt and constants like pi, e ... etc ...
+ Accelerometer: ax, ay, az (Cardputer ADV only)
+ Gyroscope: gx, gy, gz (Cardputer ADV only)
+ Mouse (for wasm): mx, my, mv (velocity)
+ MIDI: mf (freq), mn (note), mg (gate)

# Controls & Modes

+ ctrl + 0-9: Select bank (Presets & User patches)
+ opt + 0-9: Load patch from current bank
+ alt + 0-9: Save patch to location
+ ctrl + z/r: Undo & Redo
+ fn + 1-4: Classic visualizations
+ fn + 0: History view
+ fn + t: Change theme color
+ fn + s: Change samplerate
+ fn + f: Change between: bytebeat / floatbeat

## Multiple cardputers

Syncing the clock (ESP-NOW sync mode) with multiple cardputers works by setting one cardputer as the master and others as slaves. Pressing fn+m will also overwrite the current function to all the slaves that are listening.

+ fn + p: Master mode: Push clock to all listeners 
+ fn + l: Slave mode: Listen clock

## USB Midi input

+ fn + m: Start USB MIDI server to listen incoming events

Use **mn** as midi note or **mf** as incoming frequency in hz. Use **mg** as a gate.

See banks 6-8 for examples using midi in.

Pitch bend is also supported using **mf** and fixed to range between -2 to 2 semitones to support pitch bend tuning.

# Installation

Compile & Upload using Platform.io or by using [M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro) (Search for Bytebed). Latest firmware also available in Firmware folder.

# TODO / IDEAS

- Better tests and more systematic approach for supporting floatbeat?
- Documentation huh? Meanwhile look at test/tests.h ... and vm.cpp
- Some bytebeat / floatbeat tutorial maybe?

# Development

Use platform.io and see platform.ini for configuration.

## WASM

To compile to wasm and wasm binary header file, run this in wasm folder:

emcc ../src/state.cpp ../src/vm.cpp ../src/compiler.cpp ../src/validator.cpp ../src/decompiler.cpp wasm_wrapper.cpp -I. -I../include -I../src -s EXPORTED_FUNCTIONS="['_main', '_get_input_buffer', '_wasm_compile', '_wasm_execute', '_wasm_decompile', '_wasm_set_sample_rate', '_wasm_set_play_mode', '_wasm_set_midi', '_wasm_set_imu', '_wasm_set_mouse', '_wasm_reset_vm', '_wasm_get_last_error', '_wasm_get_preset_formula', '_wasm_get_preset_rate', '_wasm_get_preset_mode']" -O3 -s WASM=1 -s STANDALONE_WASM --no-entry -o bytebed.wasm

## TESTS

Testing native and cardputer using platformio: 

pio test -e native

For more verbose testing:

pio test -e native -v 


For testing the hardware:

pio test -e cardputer

# Contributions

Brave testers needed. Free slots for your bytebeats are available!

Just do a pull request directly to free slots in state.cpp or post Issue.

Bug reports are also welcome.

# Credits

* viznut: [For the very first bytebeats](https://countercomplex.blogspot.com/2011/10/algorithmic-symphonies-from-one-line-of.html)
* SthephanShinkufag: [Bytebeat-composer](https://github.com/SthephanShinkufag/bytebeat-composer)
* All the rest with examples from: https://dollchan.net/bytebeat/
* tanakamasayuki: [EspHttpServer](https://github.com/tanakamasayuki/EspHttpServer)

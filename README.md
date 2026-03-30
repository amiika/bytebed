# Bytebed - Embedded Bytebeats

Bytebeat virtual machine for M5Stack Cardputers.

Running bytebeat on ESP32 boards can be simple as:

```
#include <ESP_I2S.h>

I2SClass i2s;
int t = 0;

void setup() {
  i2s.setPins(41, 43, 42); 
  i2s.begin(I2S_MODE_STD, 8000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

void loop() {

  uint8_t b = (t>>7|t|t>>6)*10+4*(t&t>>13|t>>6);

  int16_t s = (b - 128) * 50; 
  uint32_t frame = (s & 0xFFFF) | (s << 16);
  i2s.write((uint8_t*)&frame, 4); 
  
  t++;
}
```

Just upload this and boom! You have some bytebeats going on. If you are happy with this do not bother reading further. Just be happy.

However ... If you want to program bytebeat and floatbeat on the fly on your tiny winey cardputer ... Keep on reading, I suppose.

# Bytebed features

+ On the fly playing and visualization
+ Classic C-style bytebeats
+ Opionated floatbeat support
+ Infix and postfix syntax compiler decompiler
+ Syntax validator
+ Global variables
+ Custom function support
+ Variable chaining and array support
+ Error messages from validator

# Variables & functions

+ All math: sin/s, cos/c, floor, int, cbrt and constants like pi, e ... etc ...
+ Accelerometer: ax, ay, az (Cardputer ADV only)
+ Gyroscope: gx, gy, gz (Cardputer ADV only)
+ Mouse (for wasm): mx, my, mv (velocity)

# Controls & Modes

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
+ fn + b: Bluetooth keyboard

# TODO / IDEAS

- Add comments, // and /* */
- Better tests and more systematic approach for supporting floatbeat?
- Publish in M5Burner?
- Documentation huh? Meanwhile look at test/tests.h ... and vm.cpp
- Some bytebeat / floatbeat tutorial

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

# Contributions

Sure, why not.

# Credits

* viznut: [For the very first bytebeats](https://countercomplex.blogspot.com/2011/10/algorithmic-symphonies-from-one-line-of.html)
* SthephanShinkufag: [Bytebeat-composer](https://github.com/SthephanShinkufag/bytebeat-composer)
* All the rest with examples from: https://dollchan.net/bytebeat/
* tanakamasayuki: [EspHttpServer](https://github.com/tanakamasayuki/EspHttpServer)

#include "ble_keyboard.h"

extern String input_buffer;

BleKeyboard bleKeyboard("Bytebed", "M5Stack", 100);
bool ble_active = false;

/**
 * Synchronizes the remote browser editor with the current Cardputer buffer.
 */
void syncPatchToBLE() {
    if (ble_active && bleKeyboard.isConnected()) {
        sendBLECombo('a'); 
        delay(150);
        bleKeyboard.write(KEY_BACKSPACE);
        delay(150);
        bleKeyboard.print(input_buffer);
        delay(100);
        bleKeyboard.write(KEY_RETURN); 
    }
}

/**
 * Sends a modifier combo to the BLE host.
 * @param key The character to press with modifiers
 */
void sendBLECombo(char key) {
    if (!ble_active || !bleKeyboard.isConnected()) return;
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(key);
    delay(100);
    bleKeyboard.releaseAll();
    delay(100); 
}
#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <Arduino.h>
#include <BleKeyboard.h>

extern BleKeyboard bleKeyboard;
extern bool ble_active;

/**
 * Sends a modifier combo (like Ctrl+Key or Cmd+Key) to the BLE host.
 * Holds keys long enough for macOS to register them properly.
 * @param key The character to press with modifiers
 */
void sendBLECombo(char key);

/**
 * Synchronizes the remote browser editor with the current Cardputer buffer.
 * Strategy: Select All -> Delay -> Overwrite by typing -> Enter.
 */
void syncPatchToBLE();

#endif
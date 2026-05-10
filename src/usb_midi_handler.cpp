#include "usb_midi_handler.h"

/**
 * @brief Initializes the underlying TinyUSB stack.
 */
void USBMidiHandler::begin() {
    _usbMidi.begin();
    USB.begin();
}

/**
 * @brief Assigns the Note On callback.
 * @param cb The callback function
 */
void USBMidiHandler::setNoteOnCallback(void (*cb)(uint8_t, uint8_t, uint8_t)) {
    _noteOnCallback = cb;
}

/**
 * @brief Assigns the Note Off callback.
 * @param cb The callback function
 */
void USBMidiHandler::setNoteOffCallback(void (*cb)(uint8_t, uint8_t, uint8_t)) {
    _noteOffCallback = cb;
}

/**
 * @brief Assigns the Pitch Bend callback.
 * @param cb The callback function
 */
void USBMidiHandler::setPitchBendCallback(void (*cb)(uint8_t, uint16_t)) {
    _pitchBendCallback = cb;
}

/**
 * @brief Reads the hardware buffer. Triggers assigned callbacks if standard MIDI 1.0 events are detected.
 */
void USBMidiHandler::poll() {
    midiEventPacket_t rx;
    
    // Drain the hardware buffer completely on every poll
    while (_usbMidi.readPacket(&rx)) {
        uint8_t status = rx.byte1;
        uint8_t note   = rx.byte2;
        uint8_t vel    = rx.byte3;
        
        uint8_t eventType = status & 0xF0;
        uint8_t channel   = (status & 0x0F) + 1; // Normalize to 1-16

        if (eventType == 0x90 && vel > 0) {
            if (_noteOnCallback) {
                _noteOnCallback(channel, note, vel);
            }
        } 
        else if (eventType == 0x80 || (eventType == 0x90 && vel == 0)) {
            if (_noteOffCallback) {
                _noteOffCallback(channel, note, vel);
            }
        }
        else if (eventType == 0xE0) {
            uint16_t bendValue = (vel << 7) | note;
            if (_pitchBendCallback) {
                _pitchBendCallback(channel, bendValue);
            }
        }
    }
}
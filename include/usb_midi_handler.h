#pragma once
#include <Arduino.h>
#include <USB.h>
#include <USBMIDI.h>

/**
 * @brief Handles Native USB MIDI hardware polling and event routing.
 */
class USBMidiHandler {
public:
    /**
     * @brief Initializes the USB hardware and MIDI stack.
     */
    void begin();

    /**
     * @brief Polls the USB interface for incoming MIDI packets.
     */
    void poll();

    /**
     * @brief Sets the callback function for MIDI Note On events.
     * @param cb Function pointer to the callback.
     */
    void setNoteOnCallback(void (*cb)(uint8_t, uint8_t, uint8_t));

    /**
     * @brief Sets the callback function for MIDI Note Off events.
     * @param cb Function pointer to the callback.
     */
    void setNoteOffCallback(void (*cb)(uint8_t, uint8_t, uint8_t));

    /**
     * @brief Sets the callback function for MIDI Pitch Bend events.
     * @param cb Function pointer to the callback.
     */
    void setPitchBendCallback(void (*cb)(uint8_t, uint16_t));

private:
    USBMIDI _usbMidi;
    void (*_noteOnCallback)(uint8_t, uint8_t, uint8_t) = nullptr;
    void (*_noteOffCallback)(uint8_t, uint8_t, uint8_t) = nullptr;
    void (*_pitchBendCallback)(uint8_t, uint16_t) = nullptr;
};
#ifndef SYNC_H
#define SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Global Transport State
extern bool is_playing;
extern int32_t t_raw;

// Safe Async Code Transfer State
extern volatile bool pending_code_update;
extern char pending_code_buffer[2048];
extern uint8_t pending_flags;

// Network Configuration
const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

enum SyncCommand : uint8_t {
    CMD_PLAY = 0,
    CMD_STOP = 1,
    CMD_SYNC = 2,
    CMD_CODE = 3
};

// 5 Bytes (Minimal payload for clock syncs)
typedef struct __attribute__((packed)) {
    uint8_t cmd;
    int32_t t_value;
} SyncPacket;

typedef struct __attribute__((packed)) {
    uint8_t cmd;
    uint8_t flags; // Bit 0 = RPN, Bit 1 = Floatbeat
    uint8_t chunk_idx;
    uint8_t total_chunks;
    char text[240];
} CodePacket;

SyncPacket outgoingPacket;
SyncPacket incomingPacket;
static int current_rx_len = 0;

// Callbacks
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    uint8_t cmd = incomingData[0];

    if (cmd == CMD_PLAY || cmd == CMD_STOP || cmd == CMD_SYNC) {
        if (len != sizeof(SyncPacket)) return;
        memcpy(&incomingPacket, incomingData, sizeof(SyncPacket));
        
        switch (incomingPacket.cmd) {
            case CMD_PLAY:
                t_raw = incomingPacket.t_value;
                is_playing = true;
                break;
            case CMD_STOP:
                is_playing = false;
                t_raw = 0;
                break;
            case CMD_SYNC:
                if (is_playing && abs(t_raw - incomingPacket.t_value) > 50) {
                    t_raw = incomingPacket.t_value;
                }
                break;
        }
    } 
    else if (cmd == CMD_CODE) {
        if (len != sizeof(CodePacket)) return;
        CodePacket* cp = (CodePacket*)incomingData;
        
        // Start of a new code transmission
        if (cp->chunk_idx == 0) {
            memset((void*)pending_code_buffer, 0, 2048);
            current_rx_len = 0;
        }
        
        // Stitch the chunk into the buffer safely
        int chunk_len = strnlen(cp->text, 239);
        if (current_rx_len + chunk_len < 2047) {
            memcpy((void*)&pending_code_buffer[current_rx_len], cp->text, chunk_len);
            current_rx_len += chunk_len;
        }
        
        // Final chunk received! Tell the main thread to compile it.
        if (cp->chunk_idx == cp->total_chunks - 1) {
            pending_flags = cp->flags;
            pending_code_update = true;
        }
    }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

// --- API ---
void initESPNowSync() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 
    if (esp_now_init() != ESP_OK) return;
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0; 
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void broadcastPlay(int32_t current_t) {
    outgoingPacket.cmd = CMD_PLAY;
    outgoingPacket.t_value = current_t;
    esp_now_send(broadcastAddress, (uint8_t *) &outgoingPacket, sizeof(SyncPacket));
}

void broadcastStop() {
    outgoingPacket.cmd = CMD_STOP;
    outgoingPacket.t_value = 0;
    esp_now_send(broadcastAddress, (uint8_t *) &outgoingPacket, sizeof(SyncPacket));
}

void broadcastSync(int32_t current_t) {
    outgoingPacket.cmd = CMD_SYNC;
    outgoingPacket.t_value = current_t;
    esp_now_send(broadcastAddress, (uint8_t *) &outgoingPacket, sizeof(SyncPacket));
}

void broadcastCode(String code, bool is_rpn, bool is_floatbeat) {
    CodePacket cp;
    cp.cmd = CMD_CODE;
    cp.flags = (is_rpn ? 1 : 0) | (is_floatbeat ? 2 : 0);
    
    int len = code.length();
    cp.total_chunks = (len / 239) + 1;
    
    // Slice the string and blast it out in chunks
    for (int i = 0; i < cp.total_chunks; i++) {
        cp.chunk_idx = i;
        memset(cp.text, 0, 240);
        String chunk = code.substring(i * 239, (i + 1) * 239);
        strncpy(cp.text, chunk.c_str(), 239);
        
        esp_now_send(broadcastAddress, (uint8_t *)&cp, sizeof(CodePacket));
        delay(8); // Tiny 8ms breather to prevent flooding the Wi-Fi hardware FIFO
    }
}

#endif
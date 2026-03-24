#include "captive.h"
#include "vm.h"
#include "ui.h"
#include <WiFi.h>
#include "index_html.h"
#include "wasm_binary.h"

void startDnsHijack(void *pvParameters) {
    dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
    while(1) {
        dnsServer.processNextRequest();
        vTaskDelay(10); 
    }
}

static String escapeJSON(String s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return s;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;
    
    httpd_ws_frame_t ws_pkt;
    uint8_t buf[2] = {0}; 
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = buf;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 1);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.payload[0] == 'P') {
        String safe_formula = escapeJSON(input_buffer);
        String eval_formula = escapeJSON(active_eval_formula); 
        String top_text = escapeJSON(current_top_text);
        
        String sm_text = "";
        if (millis() < status_timer && status_msg != "") sm_text = escapeJSON(status_msg);

        char jsonStr[1024]; 
        snprintf(jsonStr, sizeof(jsonStr), 
                 "{\"f\":\"%s\",\"ef\":\"%s\",\"v\":%d,\"r\":%d,\"g\":%d,\"b\":%d,\"s\":%d,\"p\":%s,\"cp\":%d,\"t\":%ld,\"m\":\"%s\",\"sm\":\"%s\",\"pm\":%d,\"sr\":%d}",
                 safe_formula.c_str(), eval_formula.c_str(), current_vis, theme.r_base, theme.g_base, theme.b_base, 
                 drawScale, is_playing ? "true" : "false", cursor_pos, (long)t_raw, top_text.c_str(), sm_text.c_str(),
                 (int)current_play_mode, current_sample_rate);

        httpd_ws_frame_t ws_text;
        memset(&ws_text, 0, sizeof(httpd_ws_frame_t));
        ws_text.type = HTTPD_WS_TYPE_TEXT;
        ws_text.payload = (uint8_t*)jsonStr;
        ws_text.len = strlen(jsonStr);
        httpd_ws_send_frame(req, &ws_text);
    }
    return ESP_OK;
}

static esp_err_t htmlHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wasmHandler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/wasm");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000");
    return httpd_resp_send(req, (const char*)bytebed_wasm, bytebed_wasm_len);
}

static esp_err_t captive_portal_handler(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void initBytebeatServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192; 

    httpd_uri_t html_uri = { .uri = "/", .method = HTTP_GET, .handler = htmlHandler, .user_ctx = NULL };
    httpd_uri_t wasm_uri = { .uri = "/bytebed.wasm", .method = HTTP_GET, .handler = wasmHandler, .user_ctx = NULL };
    httpd_uri_t ws_uri   = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .user_ctx = NULL, .is_websocket = true };

    if (httpd_start(&stream_server, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_server, &html_uri);
        httpd_register_uri_handler(stream_server, &wasm_uri); 
        httpd_register_uri_handler(stream_server, &ws_uri);
        httpd_register_err_handler(stream_server, HTTPD_404_NOT_FOUND, captive_portal_handler); 
    }
}

void stopBytebeatServer() {
    if (stream_server) {
        httpd_stop(stream_server);
        stream_server = NULL;
    }
    dnsServer.stop();
}
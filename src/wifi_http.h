#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi Configuration
#define WIFI_SSID "CINQ_LAP"
#define WIFI_PASS "iloveyou"

// Khởi tạo Wi-Fi Station
void wifi_init_sta(void);

// Khởi tạo HTTP Server cho MJPEG stream
void start_webserver(void);

// Hàm đẩy Frame MJPEG vào Queue để HTTP Server đọc
void push_frame_to_http(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

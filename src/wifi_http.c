#include "wifi_http.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "ai_gesture_html.h"

static const char *TAG = "WIFI_HTTP";

// Frame Buffer variables
static uint8_t *frame_buffer = NULL;
static size_t frame_len = 0;
static SemaphoreHandle_t frame_mutex = NULL;
static bool new_frame_ready = false;

#define MAX_FRAME_SIZE (120 * 1024) // 120KB max for MJPEG frame

// Wi-Fi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected from Wi-Fi. Reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    if (frame_mutex == NULL) {
        frame_mutex = xSemaphoreCreateMutex();
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void push_frame_to_http(const uint8_t *data, size_t len) {
    if (frame_mutex == NULL) return;
    
    if (frame_buffer == NULL) {
        frame_buffer = (uint8_t *)malloc(MAX_FRAME_SIZE);
        if (!frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer");
            return;
        }
    }

    if (len > MAX_FRAME_SIZE) {
        ESP_LOGE(TAG, "Frame too large: %d", len);
        return;
    }

    if (xSemaphoreTake(frame_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(frame_buffer, data, len);
        frame_len = len;
        new_frame_ready = true;
        xSemaphoreGive(frame_mutex);
    }
}

// HTTP Server stream handler
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    char *part_buf[64];
    uint8_t *local_buf = (uint8_t *)malloc(MAX_FRAME_SIZE);
    if (!local_buf) {
        ESP_LOGE(TAG, "Failed to allocate local buffer for HTTP stream");
        return ESP_ERR_NO_MEM;
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        free(local_buf);
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        bool has_frame = false;
        size_t current_len = 0;

        if (xSemaphoreTake(frame_mutex, portMAX_DELAY) == pdTRUE) {
            if (new_frame_ready && frame_buffer != NULL) {
                memcpy(local_buf, frame_buffer, frame_len);
                current_len = frame_len;
                new_frame_ready = false;
                has_frame = true;
            }
            xSemaphoreGive(frame_mutex);
        }

        if (has_frame) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            if (res == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, current_len);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
                if (res == ESP_OK) {
                    res = httpd_resp_send_chunk(req, (const char *)local_buf, current_len);
                }
            }
        }

        if (!has_frame) {
            vTaskDelay(pdMS_TO_TICKS(10)); // wait for new frame
        } else if (res != ESP_OK) {
            break;
        }
    }
    free(local_buf);
    return res;
}

// HTTP Server index handler
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, ai_gesture_html, HTTPD_RESP_USE_STRLEN);
}

void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);
        
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
        ESP_LOGI(TAG, "HTTP Server started at / and /stream");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server!");
    }
}

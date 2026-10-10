#include <stdio.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/uvc_host.h"
#include "wifi_http.h"
static const char *TAG = "UVC_TEST_APP";

#define UVC_DESC_DWFRAMEINTERVAL_TO_FPS(dwFrameInterval) (((dwFrameInterval) != 0) ? 10000000 / ((float)(dwFrameInterval)) : 0)

static const char *FORMAT_STR[] = {
    "FORMAT_UNDEFINED",
    "FORMAT_MJPEG",
    "FORMAT_YUY2",
    "FORMAT_H264",
    "FORMAT_H265",
};

static uvc_host_frame_info_t *frame_info_list = NULL;
static size_t frame_info_list_size = 0;

static bool uvc_frame_cb(const uvc_host_frame_t *frame, void *user_ctx)
{
    if (frame->data && frame->data_len > 0) {
        push_frame_to_http(frame->data, frame->data_len);
    }
    return true; // return true means frame is processed and ownership returns to driver immediately
}

typedef struct {
    uint8_t dev_addr;
    uint8_t stream_index;
    uvc_host_frame_info_t frame_info;
} stream_open_args_t;

static void start_stream_task(void *arg)
{
    stream_open_args_t *args = (stream_open_args_t *)arg;
    
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Opening UVC stream (Format: %d, %dx%d)...", args->frame_info.format, args->frame_info.h_res, args->frame_info.v_res);
    
    uvc_host_stream_hdl_t stream_hdl = NULL;
    uvc_host_stream_config_t stream_config = {
        .frame_cb = uvc_frame_cb,
        .user_ctx = NULL,
        .usb = {
            .dev_addr = args->dev_addr,
            .uvc_stream_index = args->stream_index,
        },
        .vs_format = {
            .format = args->frame_info.format,
            .h_res = args->frame_info.h_res,
            .v_res = args->frame_info.v_res,
            .fps = (uint32_t)UVC_DESC_DWFRAMEINTERVAL_TO_FPS(args->frame_info.default_interval)
        },
        .advanced = {
            .number_of_frame_buffers = 2,
            .frame_size = 500 * 1024,
            .number_of_urbs = 3,
            .urb_size = 0,
        }
    };

    esp_err_t err = uvc_host_stream_open(&stream_config, portMAX_DELAY, &stream_hdl);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stream opened. Starting...");
        err = uvc_host_stream_start(stream_hdl);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Streaming started successfully");
        } else {
            ESP_LOGE(TAG, "Failed to start streaming: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to open stream: %s", esp_err_to_name(err));
    }
    
    free(args);
    vTaskDelete(NULL);
}

static void uvc_event_cb(const uvc_host_driver_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case UVC_HOST_DRIVER_EVENT_DEVICE_CONNECTED: {
        ESP_LOGI(TAG, "===========================================");
        ESP_LOGI(TAG, "UVC Device Connected!");
        ESP_LOGI(TAG, "Address: %d, Stream Index: %d", event->device_connected.dev_addr, event->device_connected.uvc_stream_index);

        int stream_index = event->device_connected.uvc_stream_index;
        
        frame_info_list_size = event->device_connected.frame_info_num;
        frame_info_list = calloc(frame_info_list_size, sizeof(uvc_host_frame_info_t));
        if (frame_info_list) {
            uvc_host_get_frame_list(event->device_connected.dev_addr, stream_index, (uvc_host_frame_info_t (*)[])frame_info_list, &frame_info_list_size);
            ESP_LOGI(TAG, "Supported Formats:");
            uvc_host_frame_info_t target_frame_info = frame_info_list[0];
            for (int i = 0; i < frame_info_list_size; i++) {
                ESP_LOGI(TAG, " - %s %d*%d @ %.1ffps",
                         FORMAT_STR[frame_info_list[i].format],
                         frame_info_list[i].h_res,
                         frame_info_list[i].v_res,
                         UVC_DESC_DWFRAMEINTERVAL_TO_FPS(frame_info_list[i].default_interval));
                
                if (frame_info_list[i].h_res == 640 && frame_info_list[i].v_res == 480) {
                    target_frame_info = frame_info_list[i];
                }
            }
            free(frame_info_list);
            frame_info_list = NULL;

            // Spawn task to avoid deadlocking the UVC driver event task
            stream_open_args_t *args = malloc(sizeof(stream_open_args_t));
            args->dev_addr = event->device_connected.dev_addr;
            args->stream_index = stream_index;
            args->frame_info = target_frame_info;
            xTaskCreate(start_stream_task, "start_stream", 4096, args, 14, NULL);
        }
        ESP_LOGI(TAG, "===========================================");
        break;
    }
    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB: All devices freed");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, " UVC Camera Test Environment (Stage 1)");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Internal RAM: %u bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "PSRAM: %u bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_LOGI(TAG, "Initializing Wi-Fi and HTTP Server...");
    wifi_init_sta();
    start_webserver();

    ESP_LOGI(TAG, "Initializing USB Host...");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 15, NULL, tskNO_AFFINITY);
    if(task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create USB lib task");
        return;
    }

    ESP_LOGI(TAG, "Installing UVC driver...");
    const uvc_host_driver_config_t uvc_driver_config = {
        .driver_task_stack_size = 4 * 1024,
        .driver_task_priority = 16,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
        .event_cb = uvc_event_cb,
    };
    ESP_ERROR_CHECK(uvc_host_install(&uvc_driver_config));
    
    ESP_LOGI(TAG, "UVC Driver installed! Waiting for camera connection on Native USB port (GPIO19/20)...");
}

#include "http_client.h"
#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_client.h"


static const char *TAG = "HTTP_CLIENT ";


bool fetch_data(const char *url, char * buffer, int buff_size)
{
    
    //ESP_LOGI(TAG, "fetch url:\n%s", url);

    esp_http_client_config_t config = {
        .url = url,
        #if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
        #endif
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "init failed");
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    //ESP_LOGI(TAG, "content_length = %d", content_length);
    if(content_length > buff_size) {
        ESP_LOGE(TAG, "fetching buffer too small content_length = %d", content_length);
        return false;
    }
    if(content_length < 0) {
        ESP_LOGE(TAG, "content length error content_length = %d", content_length);
        return false;
    }

    int total = 0;
    while (1) {
        int to_read = buff_size - total;
        if (to_read <= 0) { 
            ESP_LOGE(TAG, "fetching buffer still too small"); return false; 
        }
        int r = esp_http_client_read(client, buffer + total, to_read);
        if (r <= 0) break;
        total += r;
    }
    buffer[total] = '\0';
    int status = esp_http_client_get_status_code(client);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "error with status %d, for url = %s", status, url);
        return false;
    }

    ESP_LOGD(TAG, "payload:\n%s",buffer);

    return true;
}
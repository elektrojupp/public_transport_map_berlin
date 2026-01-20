/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "time_server.h"

static const char *TAG = "TIME_SERVER";
static void print_servers(void);

static void print_servers(void)
{
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
        } else {
            char buff[48]; // 48 bc is can be IPv6
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, 48) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

uint32_t time_server(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    esp_netif_sntp_init(&config);
    esp_netif_sntp_start();
    
    print_servers();
    
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    if(retry == retry_count) {
        ESP_LOGE(TAG, "possibly no network connection as time server is not reached");
        return 1;
    }

    struct tm t = {0};
    int64_t now = -1;
    time(&now);
    localtime_r(&now, &t);
    
    esp_netif_sntp_deinit();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    return 0;
}

void print_time(void)
{
    char buf[32] = {0};
    int64_t now = get_unix_seconds();
    if(unix_time_to_string(now, buf, 32) != NULL) {
        ESP_LOGE(TAG, "print time failed in unix_time_to_string");
    }
    ESP_LOGI(TAG, "time now = %s", buf);
    ESP_LOGI(TAG, "unix seconds: %lld", now);
}

int64_t get_unix_seconds(void)
{
    time_t now_t = 0;  // correct type for time()
    ESP_LOGD(TAG, "time to get unix seconds");

    if (time(&now_t) == (time_t)-1) {
        ESP_LOGE(TAG, "time failed");
        return -1;
    }

    int64_t now = (int64_t)now_t;
    ESP_LOGD(TAG, "time returned unix seconds = %lld", (long long)now);
    return now;
}


int64_t parse_iso8601_to_unix(const char *timestamp_str) 
{
    if (timestamp_str == NULL) {
        ESP_LOGE(TAG, "parse_iso8601_to_unix argument error");
        return -1;
    }

    struct tm t = {0};
    ESP_LOGD(TAG, "strptime to parse string into tm struct time string = %s", timestamp_str);

    if (strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S", &t) == NULL) {
        ESP_LOGE(TAG, "strptime to parse string into tm struct time string = %s", timestamp_str);
        ESP_LOGE(TAG, "strptime failed");
        return -1;
    }

    ESP_LOGD(TAG,
        "strptime returned :\n"
        "    t.tm_sec = %02d \n"
        "    t.tm_min = %02d \n"
        "    t.tm_hour = %02d \n"
        "    t.tm_mday = %02d \n"
        "    t.tm_mon = %02d \n"
        "    t.tm_year = %04d \n"
        "    t.tm_wday = %1d \n"
        "    t.tm_yday = %03d \n"
        "    t.tm_isdst = %1d",
        t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst);

    // No tm_year/tm_mon adjustments when using strptime.
    t.tm_isdst = -1; // let mktime determine DST if available

    ESP_LOGD(TAG, "mktime to convert to unix seconds");
    time_t ts_t = mktime(&t);                  // interprets t as local time
    if (ts_t == (time_t)-1) {
        ESP_LOGE(TAG,
            "strptime returned :\n"
            "    t.tm_sec = %02d \n"
            "    t.tm_min = %02d \n"
            "    t.tm_hour = %02d \n"
            "    t.tm_mday = %02d \n"
            "    t.tm_mon = %02d \n"
            "    t.tm_year = %04d \n"
            "    t.tm_wday = %1d \n"
            "    t.tm_yday = %03d \n"
            "    t.tm_isdst = %1d",
            t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst);
        ESP_LOGE(TAG, "mktime failed");
        return -1;
    }

    int64_t ts = (int64_t)ts_t;
    ESP_LOGD(TAG, "mktime returned: %lld", (long long)ts);
    return ts;
}

const char *unix_time_to_string(int64_t ts, char *buf, uint32_t buf_size)
{
    // Needs 20 bytes: "YYYY-MM-DD HH:MM:SS" + NUL
    if (buf == NULL || buf_size < 20) {
        ESP_LOGE(TAG, "unix_time_to_string argument error, buff = %u, buffsize = %u",
                 (uint32_t)buf, buf_size);
        return NULL;
    }

    ESP_LOGD(TAG, "localtime_r to convert unix seconds into tm struct, ts = %lld", (long long)ts);

    time_t ts_t = (time_t)ts;                  // correct type for localtime_r
    struct tm t = {0};
    if (localtime_r(&ts_t, &t) == NULL) {
        ESP_LOGE(TAG, "localtime_r to convert unix seconds into tm struct, ts = %lld", (long long)ts);
        ESP_LOGE(TAG, "localtime_r failed");
        return NULL;
    }

    ESP_LOGD(TAG,
        "localtime_r returned \n"
        "    t.tm_sec = %02d \n"
        "    t.tm_min = %02d \n"
        "    t.tm_hour = %02d \n"
        "    t.tm_mday = %02d \n"
        "    t.tm_mon = %02d \n"
        "    t.tm_year = %04d \n"
        "    t.tm_wday = %1d \n"
        "    t.tm_yday = %03d \n"
        "    t.tm_isdst = %1d",
        t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst);

    ESP_LOGD(TAG, "strftime to convert tm struct into string");
    if (strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &t) == 0) {
        ESP_LOGE(TAG,
            "localtime_r returned \n"
            "    t.tm_sec = %02d \n"
            "    t.tm_min = %02d \n"
            "    t.tm_hour = %02d \n"
            "    t.tm_mday = %02d \n"
            "    t.tm_mon = %02d \n"
            "    t.tm_year = %04d \n"
            "    t.tm_wday = %1d \n"
            "    t.tm_yday = %03d \n"
            "    t.tm_isdst = %1d",
            t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst);
        ESP_LOGE(TAG, "strftime failed");
        return NULL;
    }

    ESP_LOGD(TAG, "strftime returned = %s", buf);
    return buf;
}

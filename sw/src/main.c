#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "cap_touch.h"
#include "led.h"
#include "line_state.h"
#include "time_server.h"
#include "tripring.h"
#include "requests.h"
#include "provisioning.h"

//static const char * TAG = "APP_INIT";

static void cap_touch_task(void *pvParameters)
{
    // the first one runs until the init sequence is completed
    // enables to possibility to restart provisioning
    cap_touch_check();
    cap_touch_run();
}
static void led_task(void *pvParameters)
{
    display_chance_to_reset_provisioning_pattern();
    fiddle_as_lon_as_init();
    led_stripe_run();
}

static void http_request_task(void *pvParameters)
{
    BVG_run();
}


void app_main() 
{
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_ERROR);
    esp_log_level_set("BVG_FETCHER", ESP_LOG_VERBOSE);
    
    line_state_init();
    line_state_set_init_mode();
    led_stripe_init();
    cap_touch_init();
    tr_init();

    // got into the check if provisioning can be reset mode
    line_state_set_reset_provisioning_mode();
    xTaskCreatePinnedToCore(cap_touch_task, "cap_touch_task", 4096, NULL, 5, NULL, 1);

    xTaskCreatePinnedToCore(cap_touch_task, "cap_touch_task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(http_request_task, "http_request_task", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(led_task, "led_task", 4096, NULL, 5, NULL, 1);

    // wait init sequence
    vTaskDelay(pdMS_TO_TICKS(3000));
    line_state_release_reset_provisioning_mode();

    // wait for 3 seconds while displaying key pattern to indicate that credentials can be deleted
    bool reset = cap_touch_check_is_pressed();
    provisioning(reset);
    
    time_server();
    print_time();

    line_state_release_init_mode();
}
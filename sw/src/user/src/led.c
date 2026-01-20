/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "line_data.h"
#include "tripring.h"
#include "time_server.h"
#include "freertos/task.h"
#include "led.h"
#include "led_strip.h"
#include "cap_touch.h"
#include "line_state.h"

// GPIO assignment
#define LED_STRIP_GPIO_PIN  27
// Numbers of the LED in the strip
#define LED_STRIP_LED_COUNT 320

static led_strip_handle_t led_strip = NULL;

static const char *TAG = "LED";

static bool parse_trips_into_leds(void);



void led_stripe_init(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        // set the color order of the strip: GRB
        .color_component_format = {
            .format = {
                .r_pos = 1, // red is the second byte in the color data
                .g_pos = 0, // green is the first byte in the color data
                .b_pos = 2, // blue is the third byte in the color data
                .num_components = 3, // total 3 color components
            },
        },
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: SPI
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
        .spi_bus = SPI2_HOST,           // SPI bus ID
        .flags = {
            .with_dma = true, // Using DMA can improve performance and help drive more LEDs
        }
    };

    // LED Strip object handle
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with SPI backend");

    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 1, 0));
    }
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    // while(1)
    // {
    //     draw_all_numbers(&led_strip);
    // }

    //led_state_draw_all_lines(&led_strip, 1000, 100);
}




static void decide_led(uint8_t led_encoded, uint32_t i, uint32_t loop_cnt)
{

    uint8_t nr_of_trains = led_encoded & 0b00111111;
    uint8_t direction = led_encoded & 0b01000000;
    uint8_t blink = led_encoded & 0b10000000;

    if(nr_of_trains != 0)
    {
        ESP_LOGD(TAG, "nr of trains = %d, direction = %d", nr_of_trains, direction ? 1 : -1);
    }

    if(nr_of_trains == 1)
    {
        // green when direction is positive
        if(direction != 0) ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 1, 0, 0));
        // else red
        else ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 1, 0));
    }
    // blue for more than one train at a station
    else if(nr_of_trains >= 2)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, 1));
    }

    // set to 0 if blink and loopcount
    if(blink && loop_cnt)
    {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, 0));
    }
}

// Simple color wheel: pos 0..255 -> r,g,b
static inline void color_wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Classic NeoPixel wheel
    pos = 255 - pos;
    if (pos < 85) {
        *r = 255 - pos * 3;
        *g = 0;
        *b = pos * 3;
    } else if (pos < 170) {
        pos -= 85;
        *r = 0;
        *g = pos * 3;
        *b = 255 - pos * 3;
    } else {
        pos -= 170;
        *r = pos * 3;
        *g = 255 - pos * 3;
        *b = 0;
    }
}

void display_chance_to_reset_provisioning_pattern(void)
{
    uint8_t on = 1;

    while (1) {
        // Clear strip
        for (uint32_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, 0));
        }
        for (uint32_t i = 0; i < 8; i++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, on));
        }
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        if (line_state_check_reset_provisioning_mode() == true) {
            return;
        }
        on = on ? 0 : 1;
        // Small pause to make it visible & feed watchdog
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void fiddle_as_lon_as_init(void)
{
    uint32_t k = 0;          // moving pixel index
    uint8_t hue = 0;         // 0..255 color wheel
    uint8_t r,g,b;

    while (1) {
        // Clear strip
        for (uint32_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, 0));
        }

        // Advance pixel and color
        k   = (k + 1) % LED_STRIP_LED_COUNT;   // use LED_STRIP_LED_COUNT, not a hard 320
        hue = (uint8_t)(hue + 1);              // cycle colors

        color_wheel(hue, &r, &g, &b);

        // If your strip expects GRB order, swap here: led_strip_set_pixel(..., g, r, b);
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, k, r, g, b));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        if (line_state_check_init_mode() == true) {
            return;
        }

        // Small pause to make it visible & feed watchdog
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}


//static line_state_t last = { .line = -128, .pressed = false }; // force first change
static line_state_t line_state = {0};

void led_stripe_run(void)
{
    const TickType_t period = pdMS_TO_TICKS(100); // 100 ms
    TickType_t last_wake = xTaskGetTickCount();   // initialize BEFORE the loop
    int line_name_printed = 0;
    int k = 0;

    static uint32_t loop_cnt = 0;
    while(1)
    {
        tr_take();
        parse_trips_into_leds();
        tr_release();

        

        for (uint32_t i = 0; i < LED_STRIP_LED_COUNT; i++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, 0, 0, 0));
        }

        k = (k+1)%64;
        //k = (k+1)%319;
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, k>>3, 0, 0, 1));

        line_state_get(&line_state);
        if (line_state.pressed) {
                if(line_name_printed == 0)
                {
                    line_name_printed = 1;

                    // ESP_LOGI(TAG, " ");
                    // ESP_LOGI(TAG, " ");
                    // ESP_LOGI(TAG, "line number = %d", line_state.line);

                    led_state_draw_line(&led_strip, line_state.line, 600, 50);
                    last_wake = xTaskGetTickCount();   // initialize BEFORE the loop
                }
                else
                {
                    print_line(&led_strip, line_state.line);
                }
        }
        else {
            for(int i = 0; i < 320; i ++) {
                decide_led(led_active[i], i, loop_cnt%2);
            }
            line_name_printed = 0;
        }

        // Flush RGB values to LEDs
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        
        vTaskDelayUntil(&last_wake, period);
        loop_cnt ++;
    }
}



static bool parse_trips_into_leds(void)
{
    Trip *t;
    int64_t now = get_unix_seconds();
    // remove trips that are arrivd since 60 seconds
    // tr_free_old(now + 60);

    // reset active leds
    memset(led_active, 0, sizeof(led_active));
    // iterate over all trips
    ESP_LOGD(TAG, "check trip size: %d", tr_get_size());
    ESP_LOGD(TAG, "number of trips = %d", tr_get_size());
    for(int x = 0; x < tr_get_size(); x++)
    {
        t = tr_get_trip(x);
        ESP_LOGD(TAG, "trip number %d id = %s", x, t->trip_id);
        // interate over all stops
        int64_t dt = INT64_MAX;
        uint32_t index = 0;
        for(int i = 0; i < t->num_stops; i ++)
        {
            // take arrival time or departure time
            int64_t st = t->stops[i].arr_ts; 
            if(st == 0) st = t->stops[i].dep_ts;

            ESP_LOGD(TAG, "timestamp of stop = %lld, now = %lld", st, now);
            
            // check station with nearest arrival or departure
            int64_t delta = st - now;
            if(delta < 0) delta = -delta;

            // if delta is smallest then safe index
            if(delta < dt)
            {
                dt = delta;
                index = t->stops[i].station_id;
            }
            
        }
        
        // check if something valid as been found
        if(index == 0)
        {
            ESP_LOGE(TAG, "no fastest station found for trip: %s", t->trip_id);
            print_trips_here(t, now);
            continue;
        }

        // now select the led to light up
        int found = -1;
        for(int j = 0; j < line_data_number_of_stations(); j ++)
        {
            if(index == stations[j].station_id)
            {
                found = stations[j].line_pos;
            }
        }

        if(found == -1)
        {
            ESP_LOGE(TAG, "no matching id found for trip: %s, selected id = %d", t->trip_id, index);
            print_trips_here(t, now);
            continue;           
        }
        led_active[found] ++;
        
        if(dt < 2)
        {
            led_active[found] = led_active[found] | 0x80; 
        }

        if(t->direction == 1)
        {
            led_active[found] = led_active[found] | (0x80 >> 1); 
        }
        ESP_LOGD(TAG, "active led = %d with direction %d", led_active[found], t->direction);
        ESP_LOGD(TAG, "station id %d at led pos %d is the next one", index, found);
    }
    return true;
}

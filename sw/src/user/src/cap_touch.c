#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/touch_sens.h"
#include "line_state.h"
#include "line_data.h"

typedef enum {
    TSTATE_RELEASED = 0,
    TSTATE_PENDING_ACTIVE,
    TSTATE_ACTIVE,
    TSTATE_PENDING_RELEASE
} tstate_t;

typedef struct {
    touch_channel_handle_t ch;
    uint32_t               baseline;
    uint32_t               thresh_abs;       // V1 uses absolute threshold (baseline * (1 - ratio))
    tstate_t               st;
    int64_t                dwell_start_us;
} tdebounce_t;

#define TOUCH_NUM_CHANNELS       3
#define TOUCH_THRESH_RATIO       0.20f   // trigger when value drops 20% from baseline
#define TOUCH_ACTIVE_DWELL_MS    60      // must stay below threshold this long to count as "touched"
#define TOUCH_RELEASE_DWELL_MS   60      // must stay above threshold this long to count as "released"
#define TOUCH_POLL_PERIOD_MS     10      // sampling period of the poll task

/*
   T0=GPIO4,  T1=GPIO0,  T2=GPIO2,  T3=GPIO15, T4=GPIO13,
   T5=GPIO12, T6=GPIO14, T7=GPIO27, T8=GPIO33, T9=GPIO32   */
static const int s_touch_chan_ids[TOUCH_NUM_CHANNELS] = { 0, 3, 6};

static touch_sensor_handle_t s_touch = NULL;
static tdebounce_t s_db[TOUCH_NUM_CHANNELS] = {0};

// pressed is used to indicate if some button has been pressed and released during the initalization phase
// and to check weather or not delete previously saved wifi credentials
uint32_t check_pressed = 0;

static const char *TAG = "CAPACITIVE_TOUCH";

void on_touch(int chan_id) {
    if (chan_id == 3) {
        (void)line_state_add_wrap(+1, 0, line_data_number_of_lines()-1);
    } else if (chan_id == 0) {
        (void)line_state_add_wrap(-1, 0, line_data_number_of_lines()-1);
    }
    line_state_set_pressed(true);
    line_state_t s;
    line_state_get(&s);
    ESP_LOGI(TAG, "line = %d pressed = %d, channel = %d", s.line, s.pressed, chan_id);
}

void on_release(int chan_id) {
    line_state_set_pressed(false);
    line_state_t s;
    line_state_get(&s);
    ESP_LOGI(TAG, "line = %d, pressed = %d, channel = %d", s.line, s.pressed, chan_id);
}

void cap_touch_init(void)
{
    ESP_LOGI(TAG, "INIT");
    ESP_LOGI(TAG, "Channels = T%d, T%d, T%d", s_touch_chan_ids[0], s_touch_chan_ids[1], s_touch_chan_ids[2]);

    touch_sensor_sample_config_t sample_cfg[] = {
        TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7),
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &s_touch));

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(s_touch, &filter_cfg));

    touch_channel_config_t ch_base_cfg = {
        .abs_active_thresh = {1000},
        .charge_speed      = TOUCH_CHARGE_SPEED_3,
        .init_charge_volt  = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        .group             = TOUCH_CHAN_TRIG_GROUP_BOTH,
    };

    // create a channel for each pin
    for (int i = 0; i < TOUCH_NUM_CHANNELS; i++) {
        ESP_ERROR_CHECK(touch_sensor_new_channel(s_touch, s_touch_chan_ids[i], &ch_base_cfg, &s_db[i].ch));
        s_db[i].st = TSTATE_RELEASED;
        s_db[i].dwell_start_us = 0;
    }

    uint32_t vals[TOUCH_NUM_CHANNELS] = {0};
    uint32_t scans = 100;

    // start touch sensor
    ESP_ERROR_CHECK(touch_sensor_enable(s_touch));
    // make a few readings for sensor to settle
    for (int i = 0; i < scans; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(s_touch, 2000));
        for (int j = 0; j < TOUCH_NUM_CHANNELS; j++) {
            uint32_t val = 0;
            ESP_ERROR_CHECK(touch_channel_read_data(s_db[j].ch, TOUCH_CHAN_DATA_TYPE_SMOOTH, &val));
        }
    }
    ESP_ERROR_CHECK(touch_sensor_disable(s_touch));

    // start touch sensor
    ESP_ERROR_CHECK(touch_sensor_enable(s_touch));
    // scan to get the mean value
    for (int i = 0; i < scans; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(s_touch, 2000));
        for (int j = 0; j < TOUCH_NUM_CHANNELS; j++) {
            uint32_t val = 0;
            ESP_ERROR_CHECK(touch_channel_read_data(s_db[j].ch, TOUCH_CHAN_DATA_TYPE_SMOOTH, &val));
            vals[j] += val;
        }
    }
    ESP_ERROR_CHECK(touch_sensor_disable(s_touch));

    // compute mean and threshold for the sensor
    for(int i = 0; i < TOUCH_NUM_CHANNELS; i ++) {
        vals[i] /= scans;
        s_db[i].baseline  = vals[i];
        s_db[i].thresh_abs = (uint32_t)(vals[i] * (1.0f - TOUCH_THRESH_RATIO));
        //ch_base_cfg.abs_active_thresh[i] = s_db[i].thresh_abs;
        // do i need to do this ??? lets test
        //ESP_ERROR_CHECK(touch_sensor_reconfig_channel(s_db[i].ch, &ch_base_cfg));
        ESP_LOGI(TAG, "T%d mean reading = %d, threshold = %d", s_touch_chan_ids[0], s_db[i].baseline, s_db[i].thresh_abs);
    }

    // Start continuous scanning
    ESP_ERROR_CHECK(touch_sensor_enable(s_touch));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(s_touch));
}


void cap_touch_run(void)
{
    const int64_t ACTIVE_DWELL_US  = TOUCH_ACTIVE_DWELL_MS  * 1000LL;
    const int64_t RELEASE_DWELL_US = TOUCH_RELEASE_DWELL_MS * 1000LL;

    ESP_LOGI(TAG, "started");

    while (1) {
        for (int i = 0; i < TOUCH_NUM_CHANNELS; i++) {
            tdebounce_t *d = &s_db[i];

            uint32_t val = 0;
            // Runtime we read smoothed value
            if (touch_channel_read_data(d->ch, TOUCH_CHAN_DATA_TYPE_SMOOTH, &val) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
                continue;
            }

            //ESP_LOGI(TAG, "channel %d reading = %d", i, val);

            bool below = (val < d->thresh_abs);
            int64_t now = esp_timer_get_time();

            switch (d->st) {
                case TSTATE_RELEASED:
                    if (below) {
                        d->st = TSTATE_PENDING_ACTIVE;
                        d->dwell_start_us = now;
                    }
                    break;

                case TSTATE_PENDING_ACTIVE:
                    if (!below) {
                        d->st = TSTATE_RELEASED; // bounce ended early
                    } else if (now - d->dwell_start_us >= ACTIVE_DWELL_US) {
                        d->st = TSTATE_ACTIVE;
                        on_touch(s_touch_chan_ids[i]);     // debounced PRESS
                    }
                    break;

                case TSTATE_ACTIVE:
                    if (!below) {
                        d->st = TSTATE_PENDING_RELEASE;
                        d->dwell_start_us = now;
                    }
                    break;

                case TSTATE_PENDING_RELEASE:
                    if (below) {
                        d->st = TSTATE_ACTIVE; // bounce back into active
                    } else if (now - d->dwell_start_us >= RELEASE_DWELL_US) {
                        d->st = TSTATE_RELEASED;
                        on_release(s_touch_chan_ids[i]);   // debounced RELEASE
                    }
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
    }
}


bool cap_touch_check_is_pressed(void)
{
    return check_pressed ? true : false;
}


void cap_touch_check(void)
{
    const int64_t ACTIVE_DWELL_US  = TOUCH_ACTIVE_DWELL_MS  * 1000LL;
    const int64_t RELEASE_DWELL_US = TOUCH_RELEASE_DWELL_MS * 1000LL;

    ESP_LOGI(TAG, "started");

    while (1) {
        for (int i = 0; i < TOUCH_NUM_CHANNELS; i++) {
            tdebounce_t *d = &s_db[i];

            uint32_t val = 0;
            // Runtime we read smoothed value
            if (touch_channel_read_data(d->ch, TOUCH_CHAN_DATA_TYPE_SMOOTH, &val) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
                continue;
            }

            //ESP_LOGI(TAG, "channel %d reading = %d", i, val);

            bool below = (val < d->thresh_abs);
            int64_t now = esp_timer_get_time();

            switch (d->st) {
                case TSTATE_RELEASED:
                    if (below) {
                        d->st = TSTATE_PENDING_ACTIVE;
                        d->dwell_start_us = now;
                    }
                    break;

                case TSTATE_PENDING_ACTIVE:
                    if (!below) {
                        d->st = TSTATE_RELEASED; // bounce ended early
                    } else if (now - d->dwell_start_us >= ACTIVE_DWELL_US) {
                        d->st = TSTATE_ACTIVE;
                    }
                    break;

                case TSTATE_ACTIVE:
                    if (!below) {
                        d->st = TSTATE_PENDING_RELEASE;
                        d->dwell_start_us = now;
                    }
                    break;

                case TSTATE_PENDING_RELEASE:
                    if (below) {
                        d->st = TSTATE_ACTIVE; // bounce back into active
                    } else if (now - d->dwell_start_us >= RELEASE_DWELL_US) {
                        d->st = TSTATE_RELEASED;
                        check_pressed = 1;
                    }
                    break;
            }
        }
        if (line_state_check_reset_provisioning_mode() == true) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
    }
}

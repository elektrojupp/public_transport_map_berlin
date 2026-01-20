#include "line_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static line_state_t        s_state = {0};
static SemaphoreHandle_t   s_mutex = NULL;
static SemaphoreHandle_t   init_mutex = NULL;
static SemaphoreHandle_t   reset_provisioning_mutex = NULL;

static inline int8_t wrap_into(int8_t v, int8_t min, int8_t max)
{
    if (min > max) { int8_t t=min; min=max; max=t; }
    int range = (int)max - (int)min + 1;
    int off   = (int)v - (int)min;
    off %= range;
    if (off < 0) off += range;
    return (int8_t)(min + off);
}

void line_state_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!init_mutex) {
        init_mutex = xSemaphoreCreateMutex();
    }
    if (!reset_provisioning_mutex) {
        reset_provisioning_mutex = xSemaphoreCreateMutex();
    }    
    configASSERT(s_mutex != NULL);
    configASSERT(init_mutex != NULL);
    configASSERT(reset_provisioning_mutex != NULL);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.line = 0;
    s_state.pressed = false;
    xSemaphoreGive(s_mutex);
}

void line_state_set_init_mode(void)
{
    xSemaphoreTake(init_mutex, portMAX_DELAY);
}

void line_state_release_init_mode(void)
{
    xSemaphoreGive(init_mutex);
}

bool line_state_check_init_mode(void)
{
    if(xSemaphoreTake( init_mutex, pdMS_TO_TICKS(1)) != pdTRUE ){
        return false;
    }
    else {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreGive(init_mutex);
        return true;
    }
}


void line_state_set_reset_provisioning_mode(void)
{
    xSemaphoreTake(reset_provisioning_mutex, portMAX_DELAY);
}

void line_state_release_reset_provisioning_mode(void)
{
    xSemaphoreGive(reset_provisioning_mutex);
}

bool line_state_check_reset_provisioning_mode(void)
{
    if(xSemaphoreTake( reset_provisioning_mutex, pdMS_TO_TICKS(1)) != pdTRUE ){
        return false;
    }
    else {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreGive(reset_provisioning_mutex);
        return true;
    }
}


void line_state_get(line_state_t *out_state)
{
    configASSERT(out_state);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out_state = s_state; // struct copy
    xSemaphoreGive(s_mutex);
}

bool line_state_try_get(line_state_t *out_state)
{
    configASSERT(out_state);
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    *out_state = s_state;
    xSemaphoreGive(s_mutex);
    return true;
}

void line_state_set(const line_state_t *new_state)
{
    configASSERT(new_state);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = *new_state; // struct copy
    xSemaphoreGive(s_mutex);
}

bool line_state_try_set(const line_state_t *new_state)
{
    configASSERT(new_state);
    if (xSemaphoreTake(s_mutex, 0) != pdTRUE) return false;
    s_state = *new_state;
    xSemaphoreGive(s_mutex);
    return true;
}

void line_state_update(void (*fn)(line_state_t *s))
{
    configASSERT(fn);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    fn(&s_state);
    xSemaphoreGive(s_mutex);
}

/* convenience helpers */
void line_state_set_pressed(bool pressed)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.pressed = pressed;
    xSemaphoreGive(s_mutex);
}

int8_t line_state_add_wrap(int8_t delta, int8_t min, int8_t max)
{
    int8_t nv;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.line = wrap_into((int8_t)(s_state.line + delta), min, max);
    nv = s_state.line;
    xSemaphoreGive(s_mutex);
    return nv;
}

bool line_state_changed_since(line_state_t *last_snapshot)
{
    configASSERT(last_snapshot);
    bool changed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (last_snapshot->line != s_state.line) {
        *last_snapshot = s_state; // overwrite caller's snapshot
        changed = true;
    }
    xSemaphoreGive(s_mutex);
    return changed;
}


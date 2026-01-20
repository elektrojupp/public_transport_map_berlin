#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "multi_heap.h"
#include "tripring.h"
#include "time_server.h"

static const char * TAG = "TRIPRING";

void print_trips_here(Trip * t, int64_t now)
{
    char buf[32];
    ESP_LOGI("",""); 
    ESP_LOGI(TAG, "");
    ESP_LOGI("    ", "trip:");
    ESP_LOGI("    ", "unix time now:  %lld = %s", now, unix_time_to_string(now, buf, 32));
    ESP_LOGI("    ", "departure time: %lld = %s", t->dep_ts, unix_time_to_string(t->dep_ts, buf, 32));
    ESP_LOGI("    ", "arrival time:   %lld = %s", t->arr_ts, unix_time_to_string(t->arr_ts, buf, 32));
    ESP_LOGI("    ", "stations:");
    for(int i = 0; i < t->num_stops; i ++)
    {
        ESP_LOGI("    ", "departure time: %lld = %s", t->stops[i].dep_ts, unix_time_to_string(t->stops[i].dep_ts, buf, 32));
        ESP_LOGI("    ", "arrival time:   %lld = %s", t->stops[i].arr_ts, unix_time_to_string(t->stops[i].arr_ts, buf, 32));
    }
    ESP_LOGI("","");
}



void tripring_print_complete_trip(Trip * trip)
{
    if (!trip) {
        ESP_LOGW(TAG, "tripring_print_complete_trip: NULL trip");
        return;
    }

    ESP_LOGI(TAG,
        "\n\
    TRIP_ID:                 %s\n\
    origin station id      = %u\n\
    destination station id = %u\n\
    departure timestamp    = %lld\n\
    arrival timestamp      = %lld\n\
    line code              = %u\n\
    num stops              = %u\n",
        trip->trip_id,
        (unsigned)trip->origin_station_id,
        (unsigned)trip->dest_station_id,
        (long long)trip->dep_ts,
        (long long)trip->arr_ts,
        (unsigned)trip->line_code,
        (unsigned)trip->num_stops
    );

    for (uint16_t x = 0; x < trip->num_stops; x++) {
        ESP_LOGI(TAG,
            "  stop[%u]\n\
    station id             = %u\n\
    arrival timestamp      = %lld\n\
    departure timestamp    = %lld\n",
            (unsigned)x,
            (unsigned)trip->stops[x].station_id,
            (long long)trip->stops[x].arr_ts,
            (long long)trip->stops[x].dep_ts
        );
    }
}







#define MAX_TRIPS 150
#define PRIVATE_HEAP_SIZE (32768)
typedef struct {
    Trip   **tr;
    uint32_t index;
    uint32_t size;
}tr_struct_t;

static Trip * tr[MAX_TRIPS] = {NULL};
static tr_struct_t tr_state = {0};
static uint8_t heap_buff[PRIVATE_HEAP_SIZE] __attribute__((aligned(8)));
static multi_heap_handle_t heap_handle;  // registered on init
static SemaphoreHandle_t tr_mutex = NULL;


static int32_t tr_is_in_ring(Trip * t, tr_struct_t * state);
static void tr_arange_trp_pointer(tr_struct_t * state);
static uint32_t tr_size(Trip *t);
static void *tr_malloc(uint32_t len);
static void tr_free(Trip *t);

void tr_init(void)
{
    // set all pointer to NULL
    for(int i = 0; i < MAX_TRIPS; i ++) tr[i] = NULL;
    tr_state.tr = tr;
    tr_state.index = 0;
    tr_state.size = 0;
    
    // init private heap
    heap_handle = multi_heap_register(heap_buff, sizeof(heap_buff));
    ESP_LOGI(TAG, "Tripring heap created: trip capacity=%d, heap bytes =%d",MAX_TRIPS, PRIVATE_HEAP_SIZE);

    // create mutex to lock access
    tr_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Tripring mutex created");
}


void tr_take(void)
{
    xSemaphoreTake(tr_mutex, portMAX_DELAY);
}

void tr_release(void)
{
    xSemaphoreGive(tr_mutex);
}

static int32_t tr_is_in_ring(Trip * t, tr_struct_t * state)
{
    
    if (t == NULL || state == NULL) {
        ESP_LOGE(TAG, "tr_is_in_ring argument error: t=%p, state=%p", t, state);
        return -1;
    }

    // if (t->trip_id == NULL) {
    //     ESP_LOGE(TAG, "tr_is_in_ring argument error: trip_id is NULL");
    //     return -1;
    // }


    ESP_LOGD(TAG, "tr_is_in_ring: state->size = %d", state->size);
    ESP_LOGD(TAG, "tr_is_in_ring: searching for trip_id=%s", t->trip_id);

    int32_t index = -1;

    for(int i = 0; i < state->size; i ++)
    {
        if (state->tr[i] == NULL) {
            ESP_LOGD(TAG, "tr_is_in_ring: state->tr[%d] is NULL, stopping search", i);
            break;
        }

        // if (state->tr[i]->trip_id == NULL) {
        //     ESP_LOGE(TAG, "tr_is_in_ring: state->tr[%d] has NULL trip_id", i);
        //     return -1;
        // }
        
        ESP_LOGD(TAG, "tr_is_in_ring: comparing index=%d tripId1=%s tripId2=%s",
                 i, t->trip_id, state->tr[i]->trip_id);

        if (strncmp(t->trip_id, state->tr[i]->trip_id, 32) == 0) 
        {
            ESP_LOGD(TAG, "tr_is_in_ring: match found at index=%d", i);
            index = i;
            break;
        }
    }

    if (index == -1) {
        ESP_LOGD(TAG, "tr_is_in_ring: no match found for trip_id=%s", t->trip_id);
    }

    return index;
}

// makes that trip pointer array is contiguous -> easier to organize
static void tr_arange_trp_pointer(tr_struct_t *state)
{
    if (state == NULL) {
        ESP_LOGE(TAG, "tr_arange_trp_pointer argument error: state is NULL");
        return;
    }

    ESP_LOGD(TAG, "tr_arange_trp_pointer: start (size=%u, index=%u, MAX_TRIPS=%u)",
             state->size, state->index, (unsigned)MAX_TRIPS);

    uint32_t head = 0;
    uint32_t moves = 0;

    // Compact in-place: move non-NULL entries down to eliminate gaps.
    for (uint32_t i = 0; i < (uint32_t)MAX_TRIPS; i++) {
        Trip *p = state->tr[i];
        if (p != NULL) {
            if (head != i) {
                ESP_LOGD(TAG, "tr_arange_trp_pointer: move [%u] -> [%u], trip_id=%s", i, head, p->trip_id);

                state->tr[head] = p;
                state->tr[i]    = NULL;   // prevent duplicates
                moves++;
            }
            head++;
        } else {
            // Optional trace (comment out if too chatty):
            // ESP_LOGD(TAG, "tr_arange_trp_pointer: slot [%u] is NULL", i);
        }
    }

    // Ensure the tail is clean (protects against pre-existing garbage past 'head')
    for (uint32_t j = head; j < (uint32_t)MAX_TRIPS; j++) {
        if (state->tr[j] != NULL) {
            ESP_LOGD(TAG, "tr_arange_trp_pointer: clearing stale pointer at [%u]", j);
            state->tr[j] = NULL;
        }
    }

    // Validate and correct size; 'head' is the actual count of non-NULL entries
    if (state->size != head) {
        ESP_LOGE(TAG, "tr_arange_trp_pointer: trip size mismatch (reported=%u, actual=%u) — correcting -> not allowed",
        state->size, head);
        state->size = head;  // keep invariant: size == number of non-NULL entries
    }

    // Update next insert index to the first free slot
    state->index = head;

    ESP_LOGD(TAG, "tr_arange_trp_pointer: done (moves=%u, new size=%u, new index=%u)",
             moves, state->size, state->index);
}


static uint32_t tr_size(Trip *t) {
    return sizeof(Trip) + (size_t)t->num_stops * sizeof(Stopover);
}

static void *tr_malloc(uint32_t len) { 
    return multi_heap_malloc(heap_handle, len); 
}

static void tr_free(Trip *t) { 
    multi_heap_free(heap_handle, t); 
}

void tr_put(Trip *t)
{
    if (t == NULL) {
        ESP_LOGE(TAG, "tr_put argument error: t is NULL");
        return;
    }

    // will always evaluate as false says compiler
    // if (t->trip_id == NULL) {
    //     ESP_LOGE(TAG, "tr_put argument error: t->trip_id is NULL");
    //     return;
    // }

    ESP_LOGD(TAG, "tr_put: begin for trip_id=%s (size=%u, index=%u, MAX_TRIPS=%u)",
             t->trip_id, tr_state.size, tr_state.index, (unsigned)MAX_TRIPS);

    // If trip is already in ring then free old one and copy new one in case data changes
    int32_t idx = tr_is_in_ring(t, &tr_state);
    if (idx != -1) {
        ESP_LOGD(TAG, "tr_put: existing trip found at index=%d, freeing old", (int)idx);
        tr_free_idx((uint32_t)idx, true);  // will compact and fix size/index
        ESP_LOGD(TAG, "tr_put: after free/compact (size=%u, index=%u)", tr_state.size, tr_state.index);
    }

    // Capacity check (avoid overflow)
    if (tr_state.size >= MAX_TRIPS) {
        ESP_LOGE(TAG, "tr_put: ring full (size=%u >= MAX_TRIPS=%u) — cannot insert trip_id=%s",
                 tr_state.size, (unsigned)MAX_TRIPS, t->trip_id);
        return;
    }
    if (tr_state.index >= MAX_TRIPS) {
        ESP_LOGE(TAG, "tr_put: index out of range (index=%u, MAX_TRIPS=%u)",
                 tr_state.index, (unsigned)MAX_TRIPS);
        return;
    }

    // // Measure heap before alloc — use heap_caps_get_info()
    // multi_heap_info_t info_before = {0};
    // heap_caps_get_info(&info_before, MALLOC_CAP_8BIT);
    // ESP_LOGI(TAG, "heap before alloc: free=%u bytes, largest=%u bytes, min_free_ever=%u",
    //          (unsigned)info_before.total_free_bytes,
    //          (unsigned)info_before.largest_free_block,
    //          (unsigned)info_before.minimum_free_bytes);
    multi_heap_info_t info_before = {0};
    multi_heap_get_info(heap_handle, &info_before);
    ESP_LOGI(TAG, "private heap BEFORE: free=%u, largest=%u, min_free_ever=%u",
            (unsigned)info_before.total_free_bytes,
            (unsigned)info_before.largest_free_block,
            (unsigned)info_before.minimum_free_bytes);


    // Compute size and allocate
    uint32_t sz = tr_size(t);
    ESP_LOGD(TAG, "tr_put: tr_size=%u", (unsigned)sz);

    Trip *dst = tr_malloc(sz);
    if (!dst) {
        multi_heap_info_t info_fail = {0};
        //heap_caps_get_info(&info_fail, MALLOC_CAP_8BIT);
        multi_heap_get_info(heap_handle, &info_fail);
        ESP_LOGE(TAG, "tr_malloc(%u) failed! free=%u, largest=%u, min_free_ever=%u",
                 (unsigned)sz,
                 (unsigned)info_fail.total_free_bytes,
                 (unsigned)info_fail.largest_free_block,
                 (unsigned)info_fail.minimum_free_bytes);
        return;
    }

    memcpy(dst, t, sz);
    ESP_LOGD(TAG, "tr_put: memcpy done for trip_id=%s into new block=%p", t->trip_id, (void*)dst);

    // Save pointer to newest index
    ESP_LOGD(TAG, "tr_put: storing at index=%u", tr_state.index);
    tr_state.tr[tr_state.index] = dst;
    tr_state.index++;
    tr_state.size++;

    // // Measure heap after alloc — use heap_caps_get_info()
    // multi_heap_info_t info_after = {0};
    // heap_caps_get_info(&info_after, MALLOC_CAP_8BIT);

    // int delta = (int)info_after.total_free_bytes - (int)info_before.total_free_bytes;
    // ESP_LOGI(TAG, "heap after alloc:  free=%u bytes, largest=%u bytes, min_free_ever=%u, delta=%d bytes, max trips=%u, free slots=%d",
    //          (unsigned)info_after.total_free_bytes,
    //          (unsigned)info_after.largest_free_block,
    //          (unsigned)info_after.minimum_free_bytes,
    //          delta,
    //          (unsigned)MAX_TRIPS,
    //          (int)((int)MAX_TRIPS - (int)tr_state.size));

    multi_heap_info_t info_after = {0};
    multi_heap_get_info(heap_handle, &info_after);
    int delta = (int)info_after.total_free_bytes - (int)info_before.total_free_bytes;

    ESP_LOGI(TAG,
            "private heap AFTER:  free=%u, largest=%u, min_free_ever=%u, delta=%d",
            (unsigned)info_after.total_free_bytes,
            (unsigned)info_after.largest_free_block,
            (unsigned)info_after.minimum_free_bytes,
            delta);

    ESP_LOGD(TAG, "tr_put: end (size=%u, index=%u)", tr_state.size, tr_state.index);
}


void tr_free_idx(uint32_t index, bool arange)
{
    ESP_LOGD(TAG, "tr_free_idx: begin (index=%u, arange=%s, size=%u, cur_index=%u)",
             index, arange ? "true" : "false", tr_state.size, tr_state.index);

    if (index >= (uint32_t)MAX_TRIPS) {
        ESP_LOGE(TAG, "tr_free_idx: index out of range (index=%u, MAX_TRIPS=%u)",
                 index, (unsigned)MAX_TRIPS);
        return;
    }

    Trip *t = tr_state.tr[index];
    if (t == NULL) {
        ESP_LOGE(TAG, "tr_free_idx: slot[%u] is already NULL", index);
        return;
    }

    int sz = (int)sizeof(Trip) + t->num_stops * (int)sizeof(Stopover);
    ESP_LOGD(TAG, "tr_free_idx: freeing trip_id=%s at [%u], bytes=%d (num_stops=%d)",
             t->trip_id, index, sz, t->num_stops);

    memset(t, 0, (size_t)sz);
    tr_free(t);
    tr_state.tr[index] = NULL;

    // Adjust size first; index will be handled below
    if (tr_state.size > 0) {
        tr_state.size--;
    } else {
        ESP_LOGE(TAG, "tr_free_idx: size underflow prevented (size already 0)");
    }

    if (arange) {
        // Let compaction fix layout and correct index/size if needed
        tr_arange_trp_pointer(&tr_state);
        ESP_LOGD(TAG, "tr_free_idx: after compact (size=%u, index=%u)", tr_state.size, tr_state.index);
    }

    ESP_LOGD(TAG, "tr_free_idx: end");
}



void tr_free_old(int64_t now)
{
    ESP_LOGD(TAG, "tr_free_old: begin now=%lld (size=%u, index=%u)", (long long)now, tr_state.size, tr_state.index);

    if (tr_state.size == 0) {
        ESP_LOGD(TAG, "tr_free_old: ring is empty");
        return;
    }

    uint32_t removed = 0;

    // Iterate over full range because there may be NULL gaps; compaction
    // inside the loop changes indices, so handle carefully.
    for (uint32_t i = 0; i < (uint32_t)MAX_TRIPS; i++) {
        Trip *tp = tr_state.tr[i];
        if(tp == NULL) {
            continue;
        }

        if (tp->arr_ts < now) {
            ESP_LOGI(TAG, "tr_free_old: removing expired trip id=%s arr_ts=%lld now=%lld at index=%u",
                     tp->trip_id, (long long)tp->arr_ts, (long long)now, i);
            tr_free_idx(i, true);  // compacts array and updates size/index

            removed++;
            // After compaction, items shift left — re-check the same index
            if (i > 0) { i--; }
        }
    }
    ESP_LOGD(TAG, "tr_free_old: end (removed=%u, size=%u, index=%u)", removed, tr_state.size, tr_state.index);
}


void tr_clear_all(void)
{
    multi_heap_info_t info_before = {0};
    multi_heap_get_info(heap_handle, &info_before);

    uint32_t removed = 0;
    for (uint32_t i = 0; i < (uint32_t)MAX_TRIPS; i++) {
        Trip *t = tr_state.tr[i];
        if (t) {
            // (Optional) wipe the struct before freeing
            size_t sz = sizeof(Trip) + (size_t)t->num_stops * sizeof(Stopover);
            memset(t, 0, sz);

            tr_free(t);            // free from private heap
            tr_state.tr[i] = NULL; // clear slot
            removed++;
        }
    }

    // Reset ring state
    tr_state.size  = 0;
    tr_state.index = 0;

    multi_heap_info_t info_after = {0};
    multi_heap_get_info(heap_handle, &info_after);

    int delta = (int)info_after.total_free_bytes - (int)info_before.total_free_bytes;
    ESP_LOGI(TAG,
             "tr_clear_all: removed=%u, private heap free=%u -> %u (delta=%d), largest=%u",
             (unsigned)removed,
             (unsigned)info_before.total_free_bytes,
             (unsigned)info_after.total_free_bytes,
             delta,
             (unsigned)info_after.largest_free_block);
}


uint32_t tr_get_size(void)
{
    return tr_state.size;
}

Trip * tr_get_trip(uint32_t index)
{
    return tr_state.tr[index];
}

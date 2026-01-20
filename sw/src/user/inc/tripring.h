#ifndef __TRIPRING_H_
#define __TRIPRING_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// --- Your domain types (unchanged) -------------------------------------------
typedef struct {
    uint32_t station_id;   // numeric VBB id
    int64_t  arr_ts;       // arrival (Unix seconds, -1 if missing)
    int64_t  dep_ts;       // departure (Unix seconds, -1 if missing)
} Stopover;

typedef struct {
    char     trip_id[32];        // like "1|64231|42|86|21092025"
    uint32_t origin_station_id;
    uint32_t dest_station_id;
    int32_t  direction;
    int64_t  dep_ts;             // first departure
    int64_t  arr_ts;             // last arrival
    uint16_t line_code;          // U1=1, S7=101, etc.
    uint16_t num_stops;
    Stopover stops[];            // flexible array of stops
} Trip;

// --- API ---------------------------------------------------------------------

void tr_put(Trip * t);
void tr_free_idx(uint32_t index, bool arange);
void tr_free_old(int64_t now);
uint32_t tr_get_size(void);
Trip * tr_get_trip(uint32_t index);
void tr_init(void);
void tr_take(void);
void tr_release(void);
void tr_clear_all(void);

void print_trips_here(Trip * t, int64_t now);

#endif //__TRIPRING_H_

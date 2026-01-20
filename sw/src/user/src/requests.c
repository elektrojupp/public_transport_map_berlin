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
#include "json_parser.h"   // add this include


#include "requests.h"
#include "http_client.h"
#include "tripring.h"
#include "time_server.h"
#include "line_data.h"
#include "led.h"
#include "line_state.h"

#define HTTP_RESPONSE_BUFFER_SIZE (32768+16384)
static char response_buffer[HTTP_RESPONSE_BUFFER_SIZE + 1];
#define HTTP_URL_BUFFER_SIZE 399
static char url_buffer[HTTP_URL_BUFFER_SIZE + 1];
#define MAX_TRIP_ID_LEN 32
#define MAX_NR_TRIP_IDS 32
static char trip_ids[MAX_NR_TRIP_IDS][MAX_TRIP_ID_LEN];
#define MAX_LINES 26
static const char line_names_data [MAX_LINES][4] = {
{"U1"},
{"U2"},
{"U3"},
{"U4"},
{"U5"},
{"U6"},
{"U7"},
{"U8"},
{"U9"},
{"S1"},
{"S2"},
{"S25"},
{"S26"},
{"S3"},
{"S5"},
{"S7"},
{"S75"},
{"S8"},
{"S85"},
{"S9"},
{"S41"},
{"S42"},
{"S45"},
{"S46"},
{"S47"},
{"SXX"}
};

static const char line_operator_names [MAX_LINES][40] = {
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"Berliner%20Verkehrsbetriebe"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"},
{"S-Bahn%20Berlin%20GmbH"}
};

static const char * TAG = "BVG_FETCHER";
size_t trip_array[sizeof(Trip) + 45 * sizeof(Stopover)] = {0};

static int collect_trip_ids_from_list(const char *json, char (*out_ids)[MAX_TRIP_ID_LEN], int max_out);
static bool build_trip_url(const char * trip, char * buffer, int size);
static bool decode_and_print_trip(const char *json, Trip *trip);




// Parse trips list JSON, print brief info, collect up to max_out trip IDs.
// Returns number of collected IDs.
static int collect_trip_ids_from_list(const char *json,
                                      char (*out_ids)[MAX_TRIP_ID_LEN],
                                      int max_out)
{
    if (!json || !out_ids || max_out <= 0) return 0;

    jparse_ctx_t jctx;
    int ret = json_parse_start(&jctx, json, strlen(json));
    if (ret != 0) {
        ESP_LOGE(TAG, "JSON parse start failed: %d", ret);
        return 0;
    }

    int trip_count = 0;
    int num_trips = 0;

    // trips[]
    ret = json_obj_get_array(&jctx, "trips", &num_trips);
    if (ret != 0 || num_trips <= 0) {
        ESP_LOGE(TAG, "`trips` missing or empty (ret=%d, n=%d)", ret, num_trips);
        json_parse_end(&jctx);
        return 0;
    }

    for (int i = 0; i < num_trips; ++i) {
        if (trip_count >= max_out) break;

        // trips[i] -> object
        if (json_arr_get_object(&jctx, i) != 0) {
            ESP_LOGE(TAG, "trips[%d] not an object", i);
            continue;
        }

        // read fields we care about from trip object
        char trip_id[MAX_TRIP_ID_LEN] = {0};
        char dep[40] = {0};
        char arr[40] = {0};

        // prefer realtime, fall back to planned
        if (json_obj_get_string(&jctx, "departure", dep, sizeof(dep)) != 0) {
            (void)json_obj_get_string(&jctx, "plannedDeparture", dep, sizeof(dep));
        }
        if (json_obj_get_string(&jctx, "arrival", arr, sizeof(arr)) != 0) {
            (void)json_obj_get_string(&jctx, "plannedArrival", arr, sizeof(arr));
        }
        (void)json_obj_get_string(&jctx, "id", trip_id, sizeof(trip_id));

        // origin/destination (for debug log only)
        char o_name[64] = {0}, o_id[16] = {0};
        char d_name[64] = {0}, d_id[16] = {0};

        if (json_obj_get_object(&jctx, "origin") == 0) {
            (void)json_obj_get_string(&jctx, "name", o_name, sizeof(o_name));
            (void)json_obj_get_string(&jctx, "id",   o_id,   sizeof(o_id));
            json_obj_leave_object(&jctx);
        }
        if (json_obj_get_object(&jctx, "destination") == 0) {
            (void)json_obj_get_string(&jctx, "name", d_name, sizeof(d_name));
            (void)json_obj_get_string(&jctx, "id",   d_id,   sizeof(d_id));
            json_obj_leave_object(&jctx);
        }

        ESP_LOGD(TAG, "#%d  %s (id=%s) -> %s (id=%s)  dep=%s  arr=%s  tripId=%s",
                 i,
                 o_name[0] ? o_name : "(?)", o_id[0] ? o_id : "(?)",
                 d_name[0] ? d_name : "(?)", d_id[0] ? d_id : "(?)",
                 dep[0] ? dep : "-", arr[0] ? arr : "-", trip_id[0] ? trip_id : "-");

        if (trip_id[0]) {
            // copy out, ensure NUL
            strncpy(out_ids[trip_count], trip_id, MAX_TRIP_ID_LEN - 1);
            out_ids[trip_count][MAX_TRIP_ID_LEN - 1] = '\0';
            trip_count++;
        }

        // leave trips[i] object
        json_arr_leave_object(&jctx);
    }

    // leave trips[]
    json_obj_leave_array(&jctx);

    json_parse_end(&jctx);
    return trip_count;
}

static bool fill_stop_array(Trip * trip, 
                            const char * station_id,
                            const char * dep_ts,
                            const char * arr_ts)
{

    Stopover * pStop = &trip->stops[trip->num_stops];

    // convert origin station id
    if(station_id == NULL)
    {
        ESP_LOGE(TAG,"trip without origin station id");
        return false;
    }

    int len = strnlen(station_id, 32);
    if(len > 9)
    {
        ESP_LOGE(TAG,"origin station id wrong length: length = %d", len);
        return false;
    }
    pStop->station_id = atoi(station_id);

    if(dep_ts == NULL)
    {
        //ESP_LOGE(TAG,"trip without departure timestamp");
        //return false;
        pStop->dep_ts = 0;
    }
    else
    {
        uint64_t timee = parse_iso8601_to_unix(dep_ts); 
        if(timee == 0)
        {
            ESP_LOGE(TAG,"wrong departure timestamp");
            return false;       
        }
        pStop->dep_ts = timee;
    }
    
    if(arr_ts == NULL)
    {
        //ESP_LOGE(TAG,"trip without arrival timestamp");
        //return false;
        pStop->arr_ts = 0;
    }
    else
    {
        uint64_t timee = parse_iso8601_to_unix(arr_ts); 
        if(timee == 0)
        {
            ESP_LOGE(TAG,"wrong departure timestamp");
            return false;       
        }
        pStop->arr_ts = timee;
    }

    trip->num_stops++;
    return true;

}


static bool fill_trip_array(Trip * trip, 
                            const char * trip_id,
                            const char * origin_station_id,
                            const char * dest_station_id,
                            const char * dep_ts,
                            const char * arr_ts,
                            const char * line_name)
{
    int x = 0;
    trip->num_stops = 0;
    //trip->stops = (Stopover *)(trip + sizeof(Trip));

    // convert trip id
    if(trip_id == NULL) 
    {
        ESP_LOGE(TAG,"trip without trip id");
        return false;
    }
    for(x = 0; x < strnlen(trip_id, 32); x ++)
    {
        trip->trip_id[x] = trip_id[x];
    }
    for(x = x; x < 32; x ++)
    {
        trip->trip_id[x] = 0;
    }

    // convert origin station id
    if(origin_station_id == NULL)
    {
        ESP_LOGE(TAG,"trip without origin station id");
        return false;
    }

    int len = strnlen(origin_station_id, 32);
    if(len > 9)
    {
        ESP_LOGE(TAG,"origin station id wrong length: length = %d", len);
        return false;
    }
    trip->origin_station_id = atoi(origin_station_id);

    // convert dest station id
    if(dest_station_id == NULL)
    {
        ESP_LOGE(TAG,"trip without destination station id");
        return false;
    }
    len = strnlen(dest_station_id, 32);
    if(len > 9)
    {
        ESP_LOGE(TAG,"destination station id wrong length: length = %d", len);
        return false;
    }
    trip->dest_station_id = atoi(dest_station_id);

    if(dep_ts == NULL)
    {
        //ESP_LOGE(TAG,"trip without departure timestamp");
        //return false;
        trip->dep_ts = 0;
    }
    else
    {
        uint64_t timee = parse_iso8601_to_unix(dep_ts); 
        if(timee == 0)
        {
            ESP_LOGE(TAG,"wrong departure timestamp");
            return false;       
        }
        trip->dep_ts = timee;
    }
    
    if(arr_ts == NULL)
    {
        //ESP_LOGE(TAG,"trip without arrival timestamp");
        //return false;
        trip->arr_ts = 0;
    }
    else
    {
        uint64_t timee = parse_iso8601_to_unix(arr_ts); 
        if(timee == 0)
        {
            ESP_LOGE(TAG,"wrong arrival timestamp");
            return false;       
        }
        trip->arr_ts = timee;
    }

    if(line_name == NULL)
    {
        ESP_LOGE(TAG,"no line name found error");
        return false;
    }
    bool found = false;
    int index = 0;
    // check all line names ! so the s41 s42 is ok
    for(int x = 0; x < MAX_LINES; x ++)
    {
        if(strncmp(line_name, line_names_data[x], 4) == 0)
        {
            found = true;
            index = x;
            break;
        }
    }
    if(found == false)
    {
        ESP_LOGE(TAG,"no machting line found error");
        return false;       
    }
    trip->line_code = index;


    return true;

}

#include <math.h>

// Returns +1 if A→B is positive (east or north), -1 if negative (west or south)
int get_direction(float lat1, float lon1, float lat2, float lon2) {
    float dlat = lat2 - lat1;
    float dlon = lon2 - lon1;

    // Compare absolute differences
    if (fabsf(dlon) > fabsf(dlat)) {
        // East-West dominant
        return (dlon > 0.0f) ? +1 : -1;  // east = +, west = -
    } else {
        // North-South dominant
        return (dlat > 0.0f) ? +1 : -1;  // north = +, south = -
    }
}


static bool decode_and_print_trip(const char *json, Trip *trip_data)
{
    if (!json || !trip_data) return false;

    jparse_ctx_t jctx;
    int ret = json_parse_start(&jctx, (char *)json, strlen(json));
    if (ret != 0) {
        ESP_LOGE(TAG, "JSON parse error near: %.64s", json);
        return false;
    }

    bool entered_trip_obj = false;

    // Some responses are { "trip": { ... } }, others are a plain { ... }
    if (json_obj_get_object(&jctx, "trip") == 0) {
        entered_trip_obj = true; // we're now inside trip{}
    }

    // ------ top-level trip fields ------
    char trip_id[MAX_TRIP_ID_LEN] = {0};
    char dep_time[40] = {0};
    char arr_time[40] = {0};
    char line_name[16] = {0};  // S3, U5, etc.

    // Prefer realtime, fall back to planned
    if (json_obj_get_string(&jctx, "departure", dep_time, sizeof(dep_time)) != 0) {
        (void)json_obj_get_string(&jctx, "plannedDeparture", dep_time, sizeof(dep_time));
    }
    if (json_obj_get_string(&jctx, "arrival", arr_time, sizeof(arr_time)) != 0) {
        (void)json_obj_get_string(&jctx, "plannedArrival", arr_time, sizeof(arr_time));
    }
    (void)json_obj_get_string(&jctx, "id", trip_id, sizeof(trip_id));

    // line.name
    if (json_obj_get_object(&jctx, "line") == 0) {
        (void)json_obj_get_string(&jctx, "name", line_name, sizeof(line_name));
        json_obj_leave_object(&jctx);
    }

    // origin / destination
    char from_id[32] = {0}, to_id[32] = {0};
    char from_name[64] = {0}, to_name[64] = {0};
    float origin_lat = 0;
    float origin_lon = 0;
    float destination_lat = 0;
    float destination_lon = 0;

    if (json_obj_get_object(&jctx, "origin") == 0) {
        (void)json_obj_get_string(&jctx, "id",   from_id, sizeof(from_id));
        (void)json_obj_get_string(&jctx, "name", from_name, sizeof(from_name));

        // add location here
        if (json_obj_get_object(&jctx, "location") == 0) {
            (void)json_obj_get_float(&jctx, "latitude",  &origin_lat);
            (void)json_obj_get_float(&jctx, "longitude", &origin_lon);
            json_obj_leave_object(&jctx);
        }

        json_obj_leave_object(&jctx);
    }
    if (json_obj_get_object(&jctx, "destination") == 0) {
        (void)json_obj_get_string(&jctx, "id",   to_id, sizeof(to_id));
        (void)json_obj_get_string(&jctx, "name", to_name, sizeof(to_name));

        // add location here 
        if (json_obj_get_object(&jctx, "location") == 0) {
            (void)json_obj_get_float(&jctx, "latitude",  &destination_lat);
            (void)json_obj_get_float(&jctx, "longitude", &destination_lon);
            json_obj_leave_object(&jctx);
        }

        json_obj_leave_object(&jctx);
    }

    ESP_LOGI(TAG,
        "from %s, %s, lat %f, lon %f, at %s ,\n                       to   %s, %s, lat %f, lon %f, at %s\n                       direction = %d",
        from_name[0] ? from_name : "(unknown)", from_id[0] ? from_id : "(?)",
        origin_lat, origin_lon, 
        dep_time[0] ? dep_time : "-",
        to_name[0] ? to_name : "(unknown)", to_id[0] ? to_id : "(?)",
        destination_lat, destination_lon, 
        arr_time[0] ? arr_time : "-",
        get_direction(origin_lat, origin_lon, destination_lat, destination_lon));

    if (!fill_trip_array(trip_data,
                         trip_id[0] ? trip_id : NULL,
                         from_id[0] ? from_id : NULL,
                         to_id[0]   ? to_id   : NULL,
                         dep_time[0] ? dep_time : NULL,
                         arr_time[0] ? arr_time : NULL,
                         line_name[0] ? line_name : NULL)) {
        if (entered_trip_obj) json_obj_leave_object(&jctx);
        json_parse_end(&jctx);
        return false;
    }

    trip_data->direction = get_direction(origin_lat, origin_lon, destination_lat, destination_lon); 

    // ------ stopovers[] ------
    int n_stopovers = 0;
    if (json_obj_get_array(&jctx, "stopovers", &n_stopovers) == 0 && n_stopovers > 0) {
        for (int i = 0; i < n_stopovers; ++i) {
            if (json_arr_get_object(&jctx, i) != 0) {
                // skip malformed element
                continue;
            }

            char dep[40] = {0}, arr[40] = {0};
            char sid[32] = {0};
            char sname[64] = {0};

            // prefer realtime, fall back to planned
            if (json_obj_get_string(&jctx, "departure", dep, sizeof(dep)) != 0) {
                (void)json_obj_get_string(&jctx, "plannedDeparture", dep, sizeof(dep));
            }
            if (json_obj_get_string(&jctx, "arrival", arr, sizeof(arr)) != 0) {
                (void)json_obj_get_string(&jctx, "plannedArrival", arr, sizeof(arr));
            }

            // stop{id,name}
            if (json_obj_get_object(&jctx, "stop") == 0) {
                (void)json_obj_get_string(&jctx, "id", sid, sizeof(sid));
                (void)json_obj_get_string(&jctx, "name", sname, sizeof(sname));
                json_obj_leave_object(&jctx);
            }

            static int idx = 0;
            ESP_LOGD(TAG, "%02d) %s, %s  arr=%s  dep=%s",
                     idx++,
                     sname[0] ? sname : "(unknown)",
                     sid[0]   ? sid   : "(?)",
                     arr[0] ? arr : "-",
                     dep[0] ? dep : "-");

            if (!fill_stop_array(trip_data,
                                 sid[0] ? sid : NULL,
                                 dep[0] ? dep : NULL,
                                 arr[0] ? arr : NULL)) {
                json_arr_leave_object(&jctx);
                json_obj_leave_array(&jctx);
                if (entered_trip_obj) json_obj_leave_object(&jctx);
                json_parse_end(&jctx);
                return false;
            }

            json_arr_leave_object(&jctx); // leave stopovers[i]
        }
        json_obj_leave_array(&jctx); // leave stopovers[]
    } else {
        ESP_LOGW(TAG, "No stopovers[] in trip");
    }

    if (entered_trip_obj) json_obj_leave_object(&jctx); // leave trip{}
    json_parse_end(&jctx);
    return true;
}


// https://v6.bvg.transport.rest/trips?lineName=S3&operatorNames=S-Bahn%20Berlin%20GmbH&onlyCurrentlyRunning=true&stopovers=false&remarks=false&subStops=false&entrances=false&suburban=true&subway=false&tram=false&bus=false&ferry=false&express=false&regional=false&pretty=false
// https://v6.bvg.transport.rest/trips?lineName=S3&onlyCurrentlyRunning=true&stopovers=false&remarks=false&subStops=false&entrances=false&subway=true&suburban=true&tram=false&bus=false&ferry=false&express=false&regional=false&pretty=false
static bool build_line_url(const char * line, char * buffer, const char * operator, int size)
{
    // Fetch currently running trips for a given line (e.g., "U1")
    int n = snprintf(
        buffer, size,
        "https://v6.bvg.transport.rest/trips?"
        "lineName=%s&operatorNames=%s&onlyCurrentlyRunning=true&"
        "stopovers=false&remarks=false&subStops=false&entrances=false&"
        "subway=true&suburban=true&tram=false&bus=false&ferry=false&express=false&regional=false&"
        "pretty=false",
        line,operator
    );
    return (n >= 0 && n < size);
}


static bool build_trip_url(const char * trip, char * buffer, int size)
{
    // Fetch a specific trip by id (include stopovers if you want positions/times)
    int n = snprintf(
        buffer, size,
        "https://v6.bvg.transport.rest/trips/%s?"
        "stopovers=true&remarks=false&pretty=false",
        trip
    );
    return (n >= 0 && n < size);
}


static void heap_info(void)
{
    
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Free heap:          %u bytes", (unsigned int)free_heap);
    ESP_LOGI(TAG, "Minimum free heap:  %u bytes", (unsigned int)min_free_heap);
    ESP_LOGI(TAG, "Largest free block: %u bytes", (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

}


static void fetching_line_name(const char * in_line, char * out_line)
{
    if(strncmp(in_line, "SXX", 4) == 0) // matches
    {
        if(strncmp(out_line, "S41", 4) == 0)
        {
            sprintf(out_line, "S42");
        }
        else
        {
            sprintf(out_line, "S41");
        }
    }
    else
    {
        sprintf(out_line, "%s", in_line);
    }
}


static line_state_t last = { .line = -128, .pressed = false }; // force first change


void BVG_run(void)
{
    static int line_nr = 0;
    Trip * trip = (Trip *)trip_array; // use preallocated memory  
    char line_name[8] = {0};

    // wait for the init sequence to be completed
    while(line_state_check_init_mode() == false)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }

    while(1)
    {
        heap_info();
        
        if (line_state_changed_since(&last)) {
            line_nr = last.line;
            tr_take();
            tr_clear_all();
            tr_release();           
        }

        
        // compute line name
        fetching_line_name(line_names_data[line_nr], line_name);
        
        ESP_LOGI(TAG, "fetch trips on the line %s", line_name);

        // wait a bit to reduce stress on api, has been more stable
        vTaskDelay(pdMS_TO_TICKS(100)); 
        // build url and fetch trip ids on line xy
        build_line_url(line_name, url_buffer, line_operator_names[line_nr], HTTP_URL_BUFFER_SIZE);
        if(fetch_data(url_buffer, response_buffer, HTTP_RESPONSE_BUFFER_SIZE) == false) continue;

        // check if trips are on the line
        int trip_id_nrs = collect_trip_ids_from_list(response_buffer, trip_ids, MAX_NR_TRIP_IDS);
        if(trip_id_nrs == 0)
        {
            continue;
        }
        else {
            for(int x = 0; x < trip_id_nrs; x ++) {
                ESP_LOGD(TAG, "got trip with id = %s", trip_ids[x]);
            }
        }


        // now o through all trip ids
        for(int y = 0; y < trip_id_nrs; y ++) {
            
            if (line_state_changed_since(&last)) {
                line_nr = last.line;
                tr_take();
                tr_clear_all();
                tr_release();  
                break;         
            }

            vTaskDelay(pdMS_TO_TICKS(100));
            // build url and fetch trip from id
            build_trip_url(trip_ids[y], url_buffer, HTTP_URL_BUFFER_SIZE);
            if(fetch_data(url_buffer, response_buffer, HTTP_RESPONSE_BUFFER_SIZE) == false) continue;
            // decode trip
            if(decode_and_print_trip(response_buffer, trip) == false) continue;

            // if decoding was successful safe trip in tripring
            tr_take();
            tr_put(trip);
            int64_t now = get_unix_seconds();
            tr_free_old(now);
            tr_release();
        }  
    }
}

























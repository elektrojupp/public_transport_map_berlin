#ifndef __LINEDATA_H__
#define __LINEDATA_H__

#include <time.h>

typedef struct
{
  const char name[4];

  uint8_t r,g,b;

  uint8_t    led_pos_size;
  uint16_t * led_pos;

  uint8_t    line_pos_size;
  uint16_t * line_pos;

  uint8_t    id_size;
  const char     ** id;
}line_struct_t;

typedef struct {
    uint32_t station_id;
    uint16_t line_pos;
} station_line_t;


typedef struct 
{
  uint16_t pos_station;
  uint16_t pos_linie;
}line_pos_struct_t;

typedef struct
{
  char name[9];
	uint8_t r;
	uint8_t g;
	uint8_t b;
  uint32_t pos_size;
  line_pos_struct_t *pos;
}line_data_struct_t;


extern station_line_t stations[];
extern uint8_t led_active[320];
extern line_data_struct_t leds[];
uint32_t line_data_number_of_stations(void);
uint32_t line_data_number_of_lines(void);

#include "led_strip.h"
void line_data_draw_string(char * str, uint32_t len, led_strip_handle_t *led_strip, uint8_t r, uint8_t g, uint8_t b, uint32_t on_time, uint32_t off_time);
void led_state_draw_all_lines(led_strip_handle_t *led_strip, uint32_t on_time, uint32_t off_time);
void led_state_draw_line(led_strip_handle_t *led_strip, uint32_t line_nr, uint32_t on_time, uint32_t off_time);
void draw_all_numbers(led_strip_handle_t *led_strip);
void print_line(led_strip_handle_t *led_strip, int8_t line_nr);
#endif // __LINEDATA_H__
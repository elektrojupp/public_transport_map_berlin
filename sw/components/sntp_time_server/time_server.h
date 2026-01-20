#ifndef __TIME_SERVER_H_
#define __TIME_SERVER_H_

uint32_t time_server(void);
void print_time(void);
int64_t parse_iso8601_to_unix(const char *timestamp_str);
int64_t get_unix_seconds(void);
const char *unix_time_to_string(int64_t ts, char *buf, uint32_t buf_size);


#endif //__TIME_SERVER_H_
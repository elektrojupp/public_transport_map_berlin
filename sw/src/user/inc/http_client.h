#ifndef __HTTP_CLIENT_H_
#define __HTTP_CLIENT_H_

#include <stdbool.h>
bool fetch_data(const char *url, char * buffer, int buff_size);

#endif //__HTTP_CLIENT_H_
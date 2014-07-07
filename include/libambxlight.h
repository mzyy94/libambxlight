#ifndef _LIBAMBXLIGHT_H__
#define _LIBAMBXLIGHT_H__

#include "../include/ambxlight_params.h"

#ifdef __cplusplus
extern "C" {
#endif

int ambxlight_device_open(int index);
int ambxlight_device_open_all(int *file_discriptors, int max_devices);
int ambxlight_device_close(int file_discriptor);
int ambxlight_device_close_all(int *file_discriptors, int devices);
int ambxlight_start_color_mode(int file_discriptor);
int ambxlight_end_color_mode(int file_discriptor);
int ambxlight_write_color_boost(int file_discriptor, unsigned char* color);
int ambxlight_change_color_with_fade(int file_discriptor, unsigned char* color, unsigned int speed);
int ambxlight_set_state(int file_discriptor, unsigned char state);
int ambxlight_set_intensity(int file_discriptor, unsigned char intensity);
int ambxlight_set_height(int file_discriptor, unsigned char height);
int ambxlight_set_location(int file_discriptor, unsigned char location);
int ambxlight_get_params(int file_discriptor, union ambxlight_params* params);

#ifdef __cplusplus
};
#endif

#endif

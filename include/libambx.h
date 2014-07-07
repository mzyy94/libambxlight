#ifndef _LIBAMBX_H__
#define _LIBAMBX_H__

#include "../include/ambxlight_params.h"

#ifdef __cplusplus
extern "C" {
#endif

int ambx_device_open(int index);
int ambx_device_open_all(int *file_discriptors, int max_devices);
int ambx_device_close(int file_discriptor);
int ambx_device_close_all(int *file_discriptors, int devices);
int ambx_start_color_mode(int file_discriptor);
int ambx_end_color_mode(int file_discriptor);
int ambx_write_color_boost(int file_discriptor, unsigned char* color);
int ambx_change_color_with_fade(int file_discriptor, unsigned char* color, unsigned int speed);
int ambx_set_state(int file_discriptor, unsigned char state);
int ambx_set_intensity(int file_discriptor, unsigned char intensity);
int ambx_set_height(int file_discriptor, unsigned char height);
int ambx_set_location(int file_discriptor, unsigned char location);
int ambx_get_params(int file_discriptor, union ambxlight_params* params);

#ifdef __cplusplus
};
#endif

#endif

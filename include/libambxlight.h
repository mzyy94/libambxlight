#ifndef _LIBAMBXLIGHT_H__
#define _LIBAMBXLIGHT_H__

#include <stddef.h>
#include <libambxlight/ambxlight_device.h>

#define MODE_CHANGE_REQUIRED 0x04

#ifdef __cplusplus
extern "C" {
#endif

int ambxlight_set_color_mode(struct ambxlight_device *device);
int ambxlight_set_raw_mode(struct ambxlight_device *device);
int ambxlight_set_hexstring_mode(struct ambxlight_device *device);
int ambxlight_change_color_boost(struct ambxlight_device device, unsigned char *color);
int ambxlight_change_color_with_fade(struct ambxlight_device device, unsigned char *color, unsigned int speed);
int ambxlight_set_state(struct ambxlight_device *device, unsigned char state);
int ambxlight_set_intensity(struct ambxlight_device *device, unsigned char intensity);
int ambxlight_set_height(struct ambxlight_device *device, unsigned char height);
int ambxlight_set_location(struct ambxlight_device *device, unsigned char location);
int ambxlight_get_params(struct ambxlight_device *device);
struct ambxlight_device ambxlight_device_open(int index);
size_t ambxlight_device_open_all(struct ambxlight_device *devices, size_t max_size);
int ambxlight_device_close(struct ambxlight_device device);
int ambxlight_device_close_all(struct ambxlight_device *devices, size_t size);

#ifdef __cplusplus
};
#endif

#endif

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "../include/ambxlight_ioctl.h"
#include "../include/ambxlight_params.h"
#include "../include/ambxlight_device.h"

#define MODE_CHANGE_REQUIRED 0x04
#define CHECK_AMBXLIGHT_MODE_COLOR(mode) do { \
	if (mode != AMBXLIGHT_MODE_COLOR) \
		return -MODE_CHANGE_REQUIRED; \
} while (0)

#define CHECK_AMBXLIGHT_MODE_RAW(mode) do { \
	if (mode != AMBXLIGHT_MODE_RAW) \
		return -MODE_CHANGE_REQUIRED; \
} while (0)

#define CHECK_AMBXLIGHT_MODE_HEXSTRING(mode) do { \
	if (mode != AMBXLIGHT_MODE_HEXSTRING) \
		return -MODE_CHANGE_REQUIRED; \
} while (0)


int ambxlight_set_color_mode(struct ambxlight_device *device) {
	device->mode = AMBXLIGHT_MODE_COLOR;
	return ioctl(device->fd, AMBXLIGHT_IOCTL_SET, &device->mode);
}

int ambxlight_set_raw_mode(struct ambxlight_device *device) {
	device->mode = AMBXLIGHT_MODE_RAW;
	return ioctl(device->fd, AMBXLIGHT_IOCTL_SET, &device->mode);
}

int ambxlight_set_hexstring_mode(struct ambxlight_device *device) {
	device->mode = AMBXLIGHT_MODE_HEXSTRING;
	return ioctl(device->fd, AMBXLIGHT_IOCTL_SET, &device->mode);
}

int ambxlight_change_color_boost(struct ambxlight_device device, unsigned char *color) {
	CHECK_AMBXLIGHT_MODE_COLOR(device.mode);
	return write(device.fd, color, 3);
}

int ambxlight_change_color_with_fade(struct ambxlight_device device, unsigned char *color, unsigned int speed) {
	CHECK_AMBXLIGHT_MODE_RAW(device.mode);
	unsigned char data[9] = {
		0xa2,
		0x00,
		color[0],
		color[1],
		color[2],
		speed & 0xff,
		(speed > 8) & 0xff,
		0x00,
		0x00
	};
	return write(device.fd, data, sizeof(data));
}

int ambxlight_set_state(struct ambxlight_device *device, unsigned char state) {
	CHECK_AMBXLIGHT_MODE_RAW(device->mode);
	unsigned char data[3] = {
		0xa1,
		0x00,
		state
	};
	device->params.param.enabled = state;
	return write(device->fd, data, sizeof(data));
}

int ambxlight_set_intensity(struct ambxlight_device *device, unsigned char intensity) {
	CHECK_AMBXLIGHT_MODE_RAW(device->mode);
	unsigned char data[3] = {
		0xa6,
		0x00,
		intensity
	};
	device->params.param.intensity = intensity;
	return write(device->fd, data, sizeof(data));
}

int ambxlight_set_height(struct ambxlight_device *device, unsigned char height) {
	CHECK_AMBXLIGHT_MODE_RAW(device->mode);
	unsigned char data[3] = {
		0xa5,
		0x00,
		height
	};
	device->params.param.height = height;
	return write(device->fd, data, sizeof(data));
}

int ambxlight_set_location(struct ambxlight_device *device, unsigned char location) {
	CHECK_AMBXLIGHT_MODE_RAW(device->mode);
	unsigned char data[4] = {
		0xa4,
		0x00,
		location,
		location ? 0x00 : 0x01
	};
	device->params.param.location = location;
	device->params.param.center = location ? 0x00 : 0x01;
	return write(device->fd, data, sizeof(data));
}

int ambxlight_get_params(struct ambxlight_device *device) {
	return read(device->fd, &device->params, sizeof(device->params));
}

struct ambxlight_device ambxlight_device_open(int index) {
	char name[20];
	int retval;
	struct stat file_stat;
	struct ambxlight_device device;
	sprintf(name, "/dev/ambx_light%d", index);
	if (stat(name, &file_stat) != 0) {
		return (struct ambxlight_device){-1};
	}
	if (!S_ISCHR(file_stat.st_mode)) {
		return (struct ambxlight_device){-2};
	}
	if ( !(((file_stat.st_mode & S_IRWXU) & S_IWUSR) &&
				((file_stat.st_mode & S_IRWXU ) & S_IRUSR))) {
		return (struct ambxlight_device){-4};
	}
	device.fd = open(name, O_RDWR);
	if (device.fd > 1) {
		device.mode = AMBXLIGHT_MODE_RAW;
		ioctl(device.fd, AMBXLIGHT_IOCTL_SET, &device.mode);
		ambxlight_get_params(&device);
	}
	return device;
}

size_t ambxlight_device_open_all(struct ambxlight_device *devices, size_t max_size) {
	unsigned int i;
	unsigned int size = 0;
	for (i = 0; i < max_size; i++) {
		struct ambxlight_device device = ambxlight_device_open(i);
		if (device.fd > 0) {
			size++;
			if (devices == NULL) {
				devices = (struct ambxlight_device *)malloc(sizeof(struct ambxlight_device));
			} else {
				devices = (struct ambxlight_device *)realloc(devices, sizeof(struct ambxlight_device) * size);
			}
			devices[size - 1] = device;
		}
	}
	return size;
}

int ambxlight_device_close(struct ambxlight_device device) {
	device.mode = AMBXLIGHT_MODE_HEXSTRING;
	ioctl(device.fd, AMBXLIGHT_IOCTL_SET, &device.mode);
	return close(device.fd);
}

int ambxlight_device_close_all(struct ambxlight_device *devices, size_t size) {
	unsigned int i;
	for (i = 0; i < size; i++) {
		ambxlight_device_close(devices[i]);
	}
	return 0;
}

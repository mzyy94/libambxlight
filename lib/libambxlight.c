#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdlib.h>

#include <libambxlight/libambxlight.h>
#include <libambxlight/version.h>

const struct libambxlight_version libambxlight_get_version() {
	const struct libambxlight_version version = {
		.major = LIBAMBXLIGHT_MAJOR,
		.minor = LIBAMBXLIGHT_MINOR,
		.micro = LIBAMBXLIGHT_MICRO,
	};

	return version;
}

ssize_t libambxlight_get_device_list(libambxlight_device ***list) {
	unsigned int i;
	unsigned int size = 0;
	const unsigned int max_size = 128;
	unsigned int fds[max_size];

	for (i = 0; i < max_size; i++) {
		libambxlight_device tmp_device = {
			.minor = i
		};
		int retval = libambxlight_device_open(&tmp_device);
		if (retval == 0) {
			libambxlight_device_close(tmp_device);
			fds[size] = i;
			size++;
		}
	}

	*list = (libambxlight_device **)malloc(sizeof(libambxlight_device *));
	for (i = 0; i < size; i++) {
		*list[i] = (libambxlight_device *)malloc(sizeof(libambxlight_device));
		(*list[i])->minor = fds[i];
	}

	return size;
}

void libambxlight_free_device_list(libambxlight_device **list) {
	free(list);
}

int libambxlight_device_open(libambxlight_device *device) {
	char name[20];
	int retval;
	struct stat file_stat;

	sprintf(name, "/dev/ambx_light%d", device->minor);
	if (stat(name, &file_stat) != 0) {
		return -1;
	}
	if (!S_ISCHR(file_stat.st_mode)) {
		return -2;
	}
	if ( !(((file_stat.st_mode & S_IRWXU) & S_IWUSR) &&
				((file_stat.st_mode & S_IRWXU ) & S_IRUSR))) {
		return -4;
	}

	device->fd = open(name, O_RDWR);
	if (device->fd > 1) {
		device->mode = (enum libambxlight_device_write_mode)RAW & 0xf;
		ioctl(device->fd, AMBXLIGHT_IOCTL_SET, &device->mode);
		libambxlight_get_params(device);
	} else {
		close(device->fd);
		return -8;
	}

	return 0;
}

void libambxlight_device_close(libambxlight_device device) {
	device.mode = (enum libambxlight_device_write_mode)HEXSTRING & 0xf;
	ioctl(device.fd, AMBXLIGHT_IOCTL_SET, &device.mode);
	close(device.fd);
	device.fd = -1;
}

void libambxlight_set_device_write_mode(libambxlight_device *device, enum libambxlight_device_write_mode mode) {
	device->mode = mode & 0xff;
	ioctl(device->fd, AMBXLIGHT_IOCTL_SET, &device->mode);
}

enum libambxlight_device_write_mode libambxlight_get_device_write_mode(libambxlight_device *device) {
	ioctl(device->fd, AMBXLIGHT_IOCTL_GET, &device->mode);
	return (enum libambxlight_device_write_mode)device->mode;
}

void libambxlight_change_color_rgb(libambxlight_device device, unsigned char r, unsigned char g, unsigned char b) {
	unsigned char data[9] = {
		0xa2,
		0x00,
		r,
		g,
		b,
		0x00,
		0x00,
		0x00,
		0x00
	};
	write(device.fd, data, sizeof(data));
}

void libambxlight_change_color_rgb_with_fade(libambxlight_device device, unsigned char r, unsigned char g, unsigned char b, unsigned int msec) {
	unsigned char data[9] = {
		0xa2,
		0x00,
		r,
		g,
		b,
		msec & 0xff,
		(msec >> 8) & 0xff,
		0x00,
		0x00
	};
	write(device.fd, data, sizeof(data));
}

void libambxlight_set_device_state(libambxlight_device *device, unsigned char state) {
	unsigned char data[3] = {
		0xa1,
		0x00,
		state
	};
	device->params.param.enabled = state;
	write(device->fd, data, sizeof(data));
}

void libambxlight_set_device_intensity(libambxlight_device *device, unsigned char intensity) {
	unsigned char data[3] = {
		0xa6,
		0x00,
		intensity
	};
	device->params.param.intensity = intensity;
	write(device->fd, data, sizeof(data));
}

void libambxlight_set_device_height(libambxlight_device *device, unsigned char height) {
	unsigned char data[3] = {
		0xa5,
		0x00,
		height
	};
	device->params.param.height = height;
	write(device->fd, data, sizeof(data));
}

void libambxlight_set_device_location(libambxlight_device *device, unsigned char location) {
	unsigned char data[4] = {
		0xa4,
		0x00,
		location,
		location ? 0x00 : 0x01
	};
	device->params.param.location = location;
	device->params.param.center = location ? 0x00 : 0x01;
	write(device->fd, data, sizeof(data));
}

int libambxlight_get_params(libambxlight_device *device) {
	return read(device->fd, &device->params, sizeof(device->params));
}

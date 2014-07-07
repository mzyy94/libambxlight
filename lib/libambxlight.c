#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "../include/ambxlight_ioctl.h"
#include "../include/ambxlight_params.h"

int __check_device_access(const char* name) {
	struct stat file_stat;
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
	return 0;
}

int ambxlight_device_open(int index) {
	char name[20];
	int retval;
	int fd;
	sprintf(name, "/dev/ambx_light%d", index);
	if ((retval = __check_device_access(name)) < 0) {
		return retval;
	}
	fd = open(name, O_RDWR);
	if (fd > 1) {
		unsigned char mode = AMBXLIGHT_MODE_RAW;
		ioctl(fd, AMBXLIGHT_IOCTL_SET, &mode);
	}
	return fd;
}

int ambxlight_device_open_all(int *file_discriptors, int max_devices) {
	unsigned int i;
	unsigned int index = 0;
	int fd;
	for (i = 0; i < max_devices; i++) {
		fd = ambxlight_device_open(i);
		if (fd > 0) {
			file_discriptors[index++] = fd;
		}
	}
	return index;
}

int ambxlight_device_close_all(int *file_discriptors, int devices) {
	unsigned int i;
	for (i = 0; i < devices; i++) {
		close(file_discriptors[i]);
	}
	return 0;
}

int ambxlight_device_close(int file_discriptor) {
	unsigned char mode = AMBXLIGHT_MODE_HEXSTRING;
	ioctl(file_discriptor, AMBXLIGHT_IOCTL_SET, &mode);
	return close(file_discriptor);
}

int ambxlight_start_color_mode(int file_discriptor) {
	unsigned char mode = AMBXLIGHT_MODE_COLOR;
	return ioctl(file_discriptor, AMBXLIGHT_IOCTL_SET, &mode);
}

int ambxlight_end_color_mode(int file_discriptor) {
	unsigned char mode = AMBXLIGHT_MODE_RAW;
	return ioctl(file_discriptor, AMBXLIGHT_IOCTL_SET, &mode);
}

int ambxlight_write_color_boost(int file_discriptor, unsigned char *color) {
	return write(file_discriptor, color, 3);
}

int ambxlight_change_color_with_fade(int file_discriptor, unsigned char* color, unsigned int speed) {
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
	return write(file_discriptor, data, sizeof(data));
}

int ambxlight_set_state(int file_discriptor, unsigned char state) {
	unsigned char data[3] = {
		0xa1,
		0x00,
		state
	};
	return write(file_discriptor, data, sizeof(data));
}

int ambxlight_set_intensity(int file_discriptor, unsigned char intensity) {
	unsigned char data[3] = {
		0xa6,
		0x00,
		intensity
	};
	return write(file_discriptor, data, sizeof(data));
}

int ambxlight_set_height(int file_discriptor, unsigned char height) {
	unsigned char data[3] = {
		0xa5,
		0x00,
		height
	};
	return write(file_discriptor, data, sizeof(data));
}

int ambxlight_set_location(int file_discriptor, unsigned char location) {
	unsigned char data[4] = {
		0xa4,
		0x00,
		location,
		location ? 0x00 : 0x01
	};
	return write(file_discriptor, data, sizeof(data));
}

int ambxlight_get_params(int file_discriptor, union ambxlight_params* params) {
	return read(file_discriptor, params, sizeof(params));
}

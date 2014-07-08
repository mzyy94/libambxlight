#ifndef _AMBXLIGHT_DEVICE_H__
#define _AMBXLIGHT_DEVICE_H__

#include <ambxlight_params.h>

struct ambxlight_device {
	int fd; /* file discriptor */
	union ambxlight_params params; /* device parameters */
	unsigned char mode; /* ioctl mode */
};


#endif

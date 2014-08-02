#ifndef _LIBAMBXLIGHT_H__
#define _LIBAMBXLIGHT_H__

#include <stddef.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* libambxlight version */
struct libambxlight_version {
	const unsigned int major;
	const unsigned int minor;
	const unsigned int micro;
};

/* amBX device parameters */
union libambxlight_device_params {
	unsigned char raw[9];
	struct {
		unsigned char opcode; /* 0x0b */
		unsigned char p1; /* unknown */
		unsigned char p2; /* unknown */
		unsigned char p3; /* unknown || 0x01 */
		unsigned char location;
		unsigned char center;	/* 0x01 || 0x00 */
		unsigned char height;
		unsigned char intensity;
		unsigned char enabled;	/* 0x01 || 0x00 */
	} param;
};

/* amBX device structure */
struct libambxlight_device {
	int fd; /* file discriptor */
	int minor; /* device minor */
	union libambxlight_device_params params; /* device parameters */
	unsigned char mode; /* ioctl mode */
};

/* Location parameter values */
enum libambxlight_device_location {
	C = 0x00,
	N = 0x01,
	NE = 0x02,
	E = 0x04,
	SE = 0x08,
	S = 0x10,
	SW = 0x20,
	W = 0x40,
	NW = 0x80,
};

/* Height parameter values */
enum libambxlight_device_height {
	ANY = 0x00,
	HIGH = 0x02,
	MIDDLE = 0x04,
	LOW = 0x08,
};

#define AMBXLIGHT_IOCTL_MAGIC  0xAB

/* define ioctl option */
#define AMBXLIGHT_IOCTL_TELL   _IOC( _IOC_NONE,  AMBXLIGHT_IOCTL_MAGIC, 0x00, 0)
#define AMBXLIGHT_IOCTL_SET    _IOC( _IOC_WRITE, AMBXLIGHT_IOCTL_MAGIC, 0x01, sizeof(char))
#define AMBXLIGHT_IOCTL_QUERY  _IOC( _IOC_NONE,  AMBXLIGHT_IOCTL_MAGIC, 0x02, 0)
#define AMBXLIGHT_IOCTL_GET    _IOC( _IOC_READ,  AMBXLIGHT_IOCTL_MAGIC, 0x03, sizeof(char))
#define AMBXLIGHT_IOCTL_RESET  _IOC( _IOC_NONE,  AMBXLIGHT_IOCTL_MAGIC, 0x04, 0)

/* Define transfer mode */
enum libambxlight_device_write_mode {
	RAW = 0x01,
	COLOR = 0x02,
	HEXSTRING = 0x04,
};

typedef struct libambxlight_version libambxlight_version;
typedef struct libambxlight_device libambxlight_device;


/* libambxlight */

const struct libambxlight_version libambxlight_get_version();

ssize_t libambxlight_get_device_list(libambxlight_device ***list);
void libambxlight_free_device_list(libambxlight_device **list);

int libambxlight_device_open(libambxlight_device *device);
void libambxlight_device_close(libambxlight_device device);

void libambxlight_set_device_write_mode(libambxlight_device *device, enum libambxlight_device_write_mode mode);
enum libambxlight_device_write_mode libambxlight_get_device_write_mode(libambxlight_device *device);
void libambxlight_change_color_rgb(libambxlight_device device, unsigned char r, unsigned char g, unsigned char b);
void libambxlight_change_color_rgb_with_fade(libambxlight_device device, unsigned char r, unsigned char g, unsigned char b, unsigned int msec);
void libambxlight_set_device_state(libambxlight_device *device, unsigned char state);
void libambxlight_set_device_intensity(libambxlight_device *device, unsigned char intensity);
void libambxlight_set_device_height(libambxlight_device *device, unsigned char height);
void libambxlight_set_device_location(libambxlight_device *device, unsigned char location);
int libambxlight_get_params(libambxlight_device *device);

#ifdef __cplusplus
};
#endif

#endif

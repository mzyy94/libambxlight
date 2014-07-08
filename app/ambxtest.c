#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <libambxlight/ambxlight.h>

struct ambxlight_device *devices;
int device_size;

static void exit_safe(int signal)
{
	ambxlight_device_close_all(devices, device_size);
	printf("closed\n");
	exit(0);
}

int main(int argc, char const* argv[])
{
	Display* dpy;
	Window root;
	struct ambxlight_device *left_dev = NULL;
	struct ambxlight_device *right_dev = NULL;
	unsigned int i, j;
	XImage *ximage;
	XWindowAttributes attributes;
	devices = malloc(sizeof(struct ambxlight_device) * 4);

	dpy = XOpenDisplay(":0");
	root = DefaultRootWindow(dpy);
	if ((device_size = ambxlight_device_open_all(devices, 4)) <= 0) {
		fprintf(stderr, "Opening device error\n");
		return -1;
	}
	if (device_size < 2) {
		fprintf(stderr, "Connected device count is not enough\n");
		return -1;
	}
	ambxlight_get_params(&devices[0]);
	ambxlight_get_params(&devices[0]);

	for (i = 0; i < device_size; i++) {
		printf("device[%d] LOCATION = 0x%02x\n", i, devices[i].params.param.location);
		printf("  intensity = %d\n", devices[i].params.param.intensity);
		printf("  fd = %d\n", devices[i].fd);
		printf("  mode = %02x\n", devices[i].mode);
		if (devices[i].params.param.location == LOCATION_NE) {
			right_dev = &devices[i];
		} else if (devices[i].params.param.location == LOCATION_NW) {
			left_dev= &devices[i];
		}
	}
	if (right_dev == NULL || left_dev == NULL) {
		fprintf(stderr, "Device locations are not completed\n");
		return -1;
	}

	XGetWindowAttributes(dpy, root, &attributes);

	const static int step = 143;
	const static int padding = 37;

	printf("w = %d\n", attributes.width);
	printf("h = %d\n", attributes.height);

	struct sigaction act;

	memset(&act, 0, sizeof(act));
	act.sa_handler = exit_safe;
	act.sa_flags = SA_RESETHAND;

	if (sigaction(SIGINT, &act, NULL) < 0) {
		printf("Error: sigaction() %s\n", strerror(errno));
		return(-1);
	}

	if (SIG_ERR == signal(SIGTERM, exit_safe)) {
		return EXIT_FAILURE;
	}

	if (SIG_ERR == signal(SIGSEGV, exit_safe)) {
		return EXIT_FAILURE;
	}

	unsigned long long int r;
	unsigned long long int g;
	unsigned long long int b;
	unsigned int count;
	unsigned long color;
	unsigned char c_l[3], c_r[3];

	count = ((attributes.width - padding) / 2 / step + 1) * ((attributes.height - padding) / step + 1);

	while (1) {
		r = g = b = 0;
		for (i = padding; i < attributes.width / 2; i += step) {
			for (j = padding; j < attributes.height; j += step) {
				ximage = XGetImage(dpy, root,
						i, j, 1, 1,
						0x00FFFFFF, XYPixmap);
				color = XGetPixel(ximage, 0, 0);
				XDestroyImage(ximage);
				r += (color >> 16) & 0xff;
				g += (color >> 8) & 0xff;
				b += color & 0xff;
			}
		}
		c_l[0] = r / count;
		c_l[1] = g / count;
		c_l[2] = b / count;

		r = g = b = 0;
		for (i = attributes.width / 2; i < attributes.width - padding; i += step) {
			for (j = padding; j < attributes.height - padding; j += step) {
				ximage = XGetImage(dpy, root,
						i, j, 1, 1,
						0x00FFFFFF, XYPixmap);
				color = XGetPixel(ximage, 0, 0);
				XDestroyImage(ximage);
				r += (color >> 16) & 0xff;
				g += (color >> 8) & 0xff;
				b += color & 0xff;
			}
		}
		c_r[0] = r / count;
		c_r[1] = g / count;
		c_r[2] = b / count;

		ambxlight_change_color_with_fade(*left_dev, c_l, 10);
		ambxlight_change_color_with_fade(*right_dev, c_r, 10);

		usleep(1000000/60);
	}
	ambxlight_device_close_all(devices, device_size);
	return 0;
}

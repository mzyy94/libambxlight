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
	unsigned long color;
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
	for (i = 0; i < device_size; i++) {
		ambxlight_set_color_mode(&devices[i]);
		ambxlight_get_params(&devices[1]);
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
	printf("left = %ld\n", left_dev);
	printf("right = %ld\n", right_dev);

	XGetWindowAttributes(dpy, root, &attributes);

	unsigned long long int r;
	unsigned long long int g;
	unsigned long long int b;
	unsigned int count;
	const static int step = 100;

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

	while (0) {	
		ximage = XGetImage(dpy, root,
				0, 0, attributes.width, attributes.height,
				AllPlanes, XYPixmap);
		XDestroyImage(ximage);
		printf("color %02x%02x%02x\n", r, g, b);
		//usleep(1000000/60);
	}
		int cnt = ((attributes.width / 2 / step ) * (attributes.height / step));
	   	// ((step - 1) * (step - 1));
	while (1) {
		count = 0;
		r = g = b = 0;
		for (i = 10; i < attributes.width / 2; i+=step) {
			for (j = 10; j < attributes.height; j+=step) {
				ximage = XGetImage(dpy, root,
						i, j, 1, 1,
						AllPlanes, XYPixmap);
				color = XGetPixel(ximage, 0, 0);
				XDestroyImage(ximage);
				r += (color >> 16) & 0xff;
				g += (color >> 8) & 0xff;
				b += color & 0xff;
				count++;
			}
		}
		r /= count;
		g /= count;
		b /= count;
		unsigned char c_l[3] = {r, g, b};

		count = 0;
		r = g = b = 0;
		for (i = attributes.width / 2; i < attributes.width; i += step) {
			for (j = 0; j < attributes.height; j += step) {
				ximage = XGetImage(dpy, root,
						i, j, 1, 1,
						AllPlanes, XYPixmap);
				color = XGetPixel(ximage, 0, 0);
				XDestroyImage(ximage);
				r += (color >> 16) & 0xff;
				g += (color >> 8) & 0xff;
				b += color & 0xff;
				count++;
			}
		}
		r /= count;
		g /= count;
		b /= count;
		unsigned char c_r[3] = {r, g, b};
		ambxlight_change_color_boost(*left_dev, c_l);
		ambxlight_change_color_boost(*right_dev, c_r);

		usleep(1000000/60);
		//usleep(100000/60);
	}
	ambxlight_device_close_all(devices, device_size);
	return 0;
}

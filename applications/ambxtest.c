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

struct ambxlight_device *devices = NULL;
int device_size;

static void exit_safe(int signal)
{
	ambxlight_device_close_all(devices, device_size);
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

	dpy = XOpenDisplay(":0");
	root = DefaultRootWindow(dpy);
	if ((device_size = ambxlight_device_open_all(devices, 9)) <= 0) {
		fprintf(stderr, "Opening device error\n");
		return -1;
	}
	if (device_size < 2) {
		fprintf(stderr, "Connected device count is not enough\n");
		return -1;
	}
	for (i = 0; i < device_size; i++) {
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

	unsigned long long int r;
	unsigned long long int g;
	unsigned long long int b;
	unsigned int count;
	const static int step = 20;

	printf("w = %d\n", attributes.width);
	printf("h = %d\n", attributes.height);

	struct sigaction act;

	/* シグナル設定 */
	memset(&act, 0, sizeof(act));
	act.sa_handler = exit_safe; /* 関数ポインタを指定する */
	act.sa_flags = SA_RESETHAND;   /* ハンドラの設定は一回だけ有効 */

	if (sigaction(SIGINT, &act, NULL) < 0) {
		printf("Error: sigaction() %s\n", strerror(errno));
		return(-1);
	}

	if (SIG_ERR == signal(SIGTERM, exit_safe)) {
		return EXIT_FAILURE;
	}

	while (1) {	
		ximage = XGetImage(dpy, root,
				0, 0, attributes.width, attributes.height,
				AllPlanes, XYPixmap);
		count = 0;
		r = g = b = 0;
		for (i = 0; i < attributes.width / 2; i+=step) {
			for (j = 0; j < attributes.height; j+=step) {
				color = XGetPixel(ximage, i, j);
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
		ambxlight_change_color_boost(*left_dev, c_l);

		count = 0;
		r = g = b = 0;
		for (i = attributes.width / 2; i < attributes.width; i += step) {
			for (j = 0; j < attributes.height; j += step) {
				color = XGetPixel(ximage, i, j);
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
		ambxlight_change_color_boost(*left_dev, c_r);

		XDestroyImage(ximage);
		usleep(1000000/60);
	}
	ambxlight_device_close_all(devices, device_size);
	return 0;
}

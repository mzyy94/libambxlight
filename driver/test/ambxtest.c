#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char const* argv[])
{
	Display* dpy;
	Window root;
	unsigned long color;
	time_t t;
	FILE *fp0, *fp1;
	XImage *ximage;
	XWindowAttributes attributes;

	dpy = XOpenDisplay(":0");
	root = DefaultRootWindow(dpy);
	fp0 = fopen("/dev/ambx_light0", "w");
	fp1 = fopen("/dev/ambx_light1", "w");
	XGetWindowAttributes(dpy, root, &attributes);

	unsigned long long int r;
	unsigned long long int g;
	unsigned long long int b;
	unsigned int i, j;
	unsigned int count;
	const static int step = 20;

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
		fprintf(fp0, "%c%c%c%c%c%c%c%c%c",
				0xa2, 0x00, r, g, b, 0x00, 0x00, 0x00, 0x00);
		fflush(fp0);
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
		fprintf(fp1, "%c%c%c%c%c%c%c%c%c",
				0xa2, 0x00, r, g, b, 0x00, 0x00, 0x00, 0x00);
		fflush(fp1);
		XDestroyImage(ximage);
		usleep(1000000/60);
	}
	fclose(fp0);
	fclose(fp1);
	return 0;
}

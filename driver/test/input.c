#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char const* argv[])
{
	FILE *fp;
	int i,j,k;
	if (argc == 2) {
		printf("off\n");
		fp = fopen("/dev/ambx_light0", "w");
		fprintf(fp, "%c%c%c",
				0xa1, 0x00, 0x00);
		fclose(fp);
		return 0;
	}
	if (argc == 3) {
		printf("on\n");
		fp = fopen("/dev/ambx_light0", "w");
		fprintf(fp, "%c%c%c",
				0xa1, 0x00, 0x01);
		fclose(fp);
		return 0;
	}
	if (argc == 4) {
		fp = fopen("/dev/ambx_light0", "r");
		char str[512];
		fscanf(fp, "%s", str);
		printf("%x\n", str);
		fclose(fp);
		return 0;
	}
	for (i = 0; i < 255; i++) {
		for (j = 0; j < 255; j++) {
			for (k = 0; k < 255; k++) {
	fp = fopen("/dev/ambx_light0", "w");
				fprintf(fp, "%c%c%c%c%c%c%c%c%c",
						0xa2, 0x00, j%3 == 1 ? k : j, j % 4 == 1 ? k : j, k, 0x00, 0x00, 0x00, 0x00);
	fclose(fp);
				usleep(1000000/60);
			}
		}
	}

	return 0;
}

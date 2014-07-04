#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char const* argv[])
{
	FILE *fp;
	int i,j,k;
	fp = fopen("/dev/ambx_light0", "w");
	fprintf(fp, "%c", 0xb0);
	fflush(fp);
	fclose(fp);
	return 0;
}

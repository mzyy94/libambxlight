#ifndef _AMBXLIGHT_PARAMS_H__
#define _AMBXLIGHT_PARAMS_H__

/* Location parameter values */
#define LOCATION_C	0x00
#define LOCATION_N	0x01
#define LOCATION_NE 0x02
#define LOCATION_E	0x04
#define LOCATION_SE	0x08
#define LOCATION_S	0x10
#define LOCATION_SW	0x20
#define LOCATION_W	0x40
#define LOCATION_NW 0x80

/* Height parameter values */
#define HEIGHT_ANY 0x00
#define HEIGHT_LOW 0x08
#define HEIGHT_MIDDLE 0x04
#define HEIGHT_HIGH 0x02

struct ambxlight_params {
	unsigned char enabled;	/* 0x01 || 0x00 */
	unsigned char location;
	unsigned char center;	/* 0x01 || 0x00 */
	unsigned char height;
	unsigned char intensity;
};


#endif

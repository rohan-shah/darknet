#include "tiffio.h"
struct image_data {
  float  xres;
  float  yres;
  uint32 width;
  uint32 length;
  uint16 res_unit;
  uint16 bps;
  uint16 spp;
  uint16 planar;
  uint16 photometric;
  uint16 orientation;
  uint16 compression;
  uint16 adjustments;
};
#define STRIP    1
#define TILE     2
#define MAX_SAMPLES   8
static int    little_endian = 1;
int  loadImage(TIFF *, image_data *, unsigned char **);

#include "image.h"
extern "C"
{
	void save_image_custom(image im, const char *name);
	image load_image_custom(char* filename, int channels);
}
int main(int argc, char **argv)
{
	image dummy = load_image_custom(argv[1], 6);
	save_image_custom(dummy, argv[2]);
}


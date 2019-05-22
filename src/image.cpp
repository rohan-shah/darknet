#include <darknet.h>
#include <libtiff.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cmath>
extern "C"
{
	void handler(const char *module, const char *fmt, va_list ap)
	{
		printf(fmt, ap);
	}
    image load_image_custom(char* filename, int channels)
    {
	TIFFSetErrorHandler(handler);
        int filenameLen = strlen(filename);
        char* filename1 = filename;
        //Copy filename and change one character
        char* filename2 = (char*)malloc(sizeof(char)*(filenameLen+1));
        strcpy(filename2, filename1);
        //original is abc.1.png
        filename2[filenameLen - 5] = '2';

	image_data image1, image2;
        TIFF* tif1 = TIFFOpen(filename1, "r");
	unsigned char* raster1 = NULL;
	loadImage(tif1, &image1, &raster1);
	uint16 pixelsize1;
	pixelsize1 = (image1.bps * image1.spp + 7) / 8;
	uint32 rowsize1 = ((image1.bps * image1.spp * image1.width) + 7) / 8;

        TIFF* tif2 = TIFFOpen(filename2, "r");
	unsigned char* raster2 = NULL;
	loadImage(tif2, &image2, &raster2);
	uint16 pixelsize2;
	pixelsize2 = (image2.bps * image2.spp + 7) / 8;
	uint32 rowsize2 = ((image2.bps * image2.spp * image2.width) + 7) / 8;
	
	//destination image
        image result = make_image(image1.width, image1.length, 6);
        int i, j, k;
	float max_uint32 = UINT32_MAX;
        
        for(k = 0; k < 3; ++k)
	{
		for(j = 0; j < image1.length; ++j)
		{
			for(i = 0; i < image1.width; ++i)
			{
			    int dst_index1 = i + image1.width*j + image1.width*image1.length*k;
			    int dst_index2 = i + image1.width*j + image1.width*image1.length*(k+3);
				uint32 firstValue = *(uint32*)&raster1[k * (image1.bps/8) + i * pixelsize1 + j * rowsize1];
				if(firstValue == 0) result.data[dst_index1] = 0;
			    else result.data[dst_index1] = std::log2f(firstValue) / 32;

				uint32 secondValue = *(uint32*)&raster2[k * (image2.bps/8) + i * pixelsize2 + j * rowsize2];
				if(secondValue == 0) result.data[dst_index2] = 0;
			    else result.data[dst_index2] = std::log2f(secondValue) / 32;
			}
		}
        }
	_TIFFfree(raster1);
	_TIFFfree(raster2);
        TIFFClose(tif1);
        TIFFClose(tif2);
        return result;
    }
    void save_image_custom(image im, const char *name)
    {
	std::stringstream ss1;
	ss1 << name << ".1.tif";
	std::string filename1 = ss1.str();

	std::stringstream ss2;
	ss2 << name << ".2.tif";
	std::string filename2 = ss2.str();

	float whitepoint[2];
	whitepoint[0] = 0.3127;
	whitepoint[1] = 0.329;

	float chromaticities[6];
	chromaticities[0] = 0.640000;
	chromaticities[1] = 0.330000;
	chromaticities[2] = 0.300000;
	chromaticities[3] = 0.600000;
	chromaticities[4] = 0.150000;
	chromaticities[5] = 0.060000;
	std::vector<uint32_t> scanlineData(im.w*im.h*3);

	//TIFFSetErrorHandler(handler);
	TIFF* output = TIFFOpen(filename1.c_str(), "w");
		TIFFSetField(output, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
		TIFFSetField(output, TIFFTAG_IMAGEWIDTH, im.w);
		TIFFSetField(output, TIFFTAG_IMAGELENGTH, im.h);
		TIFFSetField(output, TIFFTAG_BITSPERSAMPLE, 32);
		TIFFSetField(output, TIFFTAG_SAMPLESPERPIXEL, 3);
		TIFFSetField(output, TIFFTAG_ROWSPERSTRIP, im.h);
		TIFFSetField(output, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
		TIFFSetField(output, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
		TIFFSetField(output, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		TIFFSetField(output, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
		TIFFSetField(output, TIFFTAG_WHITEPOINT, whitepoint);


		for(int scanline = 0; scanline < im.h; scanline++)
		{
			for(int column = 0; column < im.w; column++)
			{
				for(int channel = 0; channel < 3; channel++)
				{
					scanlineData[channel + column*3 + scanline*im.w*3]= std::exp2(im.data[column + im.w*scanline + im.h*im.w*(channel+3)]*32);
				}
			}
		}
		TIFFWriteEncodedStrip(output, 0, &scanlineData.front(),sizeof(uint32_t)*scanlineData.size());
		TIFFSetField(output, TIFFTAG_PRIMARYCHROMATICITIES, chromaticities);
		TIFFCheckpointDirectory(output);
	TIFFClose(output);
       	output = TIFFOpen(filename2.c_str(), "w");
		TIFFSetField(output, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
		TIFFSetField(output, TIFFTAG_IMAGEWIDTH, im.w);
		TIFFSetField(output, TIFFTAG_IMAGELENGTH, im.h);
		TIFFSetField(output, TIFFTAG_BITSPERSAMPLE, 32);
		TIFFSetField(output, TIFFTAG_SAMPLESPERPIXEL, 3);
		TIFFSetField(output, TIFFTAG_ROWSPERSTRIP, im.h);
		TIFFSetField(output, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
		TIFFSetField(output, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
		TIFFSetField(output, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		TIFFSetField(output, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
		TIFFSetField(output, TIFFTAG_WHITEPOINT, whitepoint);
		for(int scanline = 0; scanline < im.h; scanline++)
		{
			for(int column = 0; column < im.w; column++)
			{
				for(int channel = 0; channel < 3; channel++)
				{
					scanlineData[channel + column*3 + scanline*im.w*3]= std::exp2(im.data[column + im.w*scanline + im.h*im.w*(channel+3)]*32);
				}
			}
		}
		TIFFWriteEncodedStrip(output, 0, &scanlineData.front(),sizeof(uint32_t)*scanlineData.size());
		TIFFSetField(output, TIFFTAG_PRIMARYCHROMATICITIES, chromaticities);
		TIFFCheckpointDirectory(output);
	TIFFClose(output);
 /*sprintf(buff, "%s.1.tiff", name);
        unsigned char* data = (unsigned char*)calloc(im.w * im.h * 3, sizeof(unsigned char));
        int i, k;
        for (k = 0; k < 3; ++k) {
        for (i = 0; i < im.w*im.h; ++i) {
            data[i*3 + k] = (unsigned char)(255 * im.data[i + k*im.w*im.h]);
        }
        }
        int success = 0;
        success = stbi_write_png(buff, im.w, im.h, 3, data, im.w*3);
        if (!success) fprintf(stderr, "Failed to write image %s\n", buff);

        for (k = 0; k < 3; ++k) {
        for (i = 0; i < im.w*im.h; ++i) {
            data[i*3 + k] = (unsigned char)(255 * im.data[i + (k+3)*im.w*im.h]);
        }
        }
        sprintf(buff, "%s.2.tiff", name);
        success = stbi_write_png(buff, im.w, im.h, 3, data, im.w*3);
        free(data);
        if (!success) fprintf(stderr, "Failed to write image %s\n", buff);*/
    }
}

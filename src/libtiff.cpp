#include "libtiff.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
static int 
combineSeparateTileSamplesBytes (unsigned char *srcbuffs[], unsigned char *out,
                                 uint32 cols, uint32 rows, uint32 imagewidth,
                                 uint32 tw, uint16 spp, uint16 bps,
                                 int format, int level)
  {
  int i, bytes_per_sample;
  uint32 row, col, col_offset, src_rowsize, dst_rowsize, src_offset;
  unsigned char *src;
  unsigned char *dst;
  tsample_t s;

  src = srcbuffs[0];
  dst = out;
  if ((src == NULL) || (dst == NULL))
    {
    TIFFError("combineSeparateTileSamplesBytes","Invalid buffer address");
    return (1);
    }

  bytes_per_sample = (bps + 7) / 8; 
  src_rowsize = ((bps * tw) + 7) / 8;
  dst_rowsize = imagewidth * bytes_per_sample * spp;
  for (row = 0; row < rows; row++)
    {
    dst = out + (row * dst_rowsize);
    src_offset = row * src_rowsize;
#ifdef DEVELMODE
    TIFFError("","Tile row %4d, Src offset %6d   Dst offset %6d", 
              row, src_offset, dst - out);
#endif
    for (col = 0; col < cols; col++)
      {
      col_offset = src_offset + (col * (bps / 8)); 
      for (s = 0; (s < spp) && (s < MAX_SAMPLES); s++)
        {
        src = srcbuffs[s] + col_offset; 
        for (i = 0; i < bytes_per_sample; i++)
          *(dst + i) = *(src + i);
        dst += bytes_per_sample;
        }   
      }
    }

  return (0);
  } /* end combineSeparateTileSamplesBytes */
static int  readSeparateTilesIntoBuffer (TIFF* in, uint8 *obuf, 
					 uint32 imagelength, uint32 imagewidth, 
                                         uint32 tw, uint32 tl,
                                         uint16 spp, uint16 bps)
  {
  int     i, status = 1, sample;
  int     shift_width, bytes_per_pixel;
  uint16  bytes_per_sample;
  uint32  row, col;     /* Current row and col of image */
  uint32  nrow, ncol;   /* Number of rows and cols in current tile */
  uint32  row_offset, col_offset; /* Output buffer offsets */
  tsize_t tbytes = 0, tilesize = TIFFTileSize(in);
  tsample_t s;
  uint8*  bufp = (uint8*)obuf;
  unsigned char *srcbuffs[MAX_SAMPLES];
  unsigned char *tbuff = NULL;

  bytes_per_sample = (bps + 7) / 8;

  for (sample = 0; (sample < spp) && (sample < MAX_SAMPLES); sample++)
    {
    srcbuffs[sample] = NULL;
    tbuff = (unsigned char *)_TIFFmalloc(tilesize + 8);
    if (!tbuff)
      {
      TIFFError ("readSeparateTilesIntoBuffer", 
                 "Unable to allocate tile read buffer for sample %d", sample);
      for (i = 0; i < sample; i++)
        _TIFFfree (srcbuffs[i]);
      return 0;
      }
    srcbuffs[sample] = tbuff;
    } 
  int ignore = 0;
  /* Each tile contains only the data for a single plane
   * arranged in scanlines of tw * bytes_per_sample bytes.
   */
  for (row = 0; row < imagelength; row += tl)
    {
    nrow = (row + tl > imagelength) ? imagelength - row : tl;
    for (col = 0; col < imagewidth; col += tw)
      {
      for (s = 0; s < spp && s < MAX_SAMPLES; s++)
        {  /* Read each plane of a tile set into srcbuffs[s] */
	tbytes = TIFFReadTile(in, srcbuffs[s], col, row, 0, s);
        if (tbytes < 0  && !ignore)
          {
	  TIFFError(TIFFFileName(in),
                 "Error, can't read tile for row %lu col %lu, "
		 "sample %lu",
		 (unsigned long) col, (unsigned long) row,
		 (unsigned long) s);
		 status = 0;
          for (sample = 0; (sample < spp) && (sample < MAX_SAMPLES); sample++)
            {
            tbuff = srcbuffs[sample];
            if (tbuff != NULL)
              _TIFFfree(tbuff);
            }
          return status;
	  }
	}
     /* Tiles on the right edge may be padded out to tw 
      * which must be a multiple of 16.
      * Ncol represents the visible (non padding) portion.  
      */
      if (col + tw > imagewidth)
        ncol = imagewidth - col;
      else
        ncol = tw;

      row_offset = row * (((imagewidth * spp * bps) + 7) / 8);
      col_offset = ((col * spp * bps) + 7) / 8;
      bufp = obuf + row_offset + col_offset;

      if ((bps % 8) == 0)
        {
        if (combineSeparateTileSamplesBytes(srcbuffs, bufp, ncol, nrow, imagewidth,
					    tw, spp, bps, 0, 0))
	  {
          status = 0;
          break;
      	  }
	}
      else
        {
          TIFFError ("readSeparateTilesIntoBuffer", "Unsupported bit depth: %d", bps);
                  status = 0;
                  break;
        }
      }
    }

  for (sample = 0; (sample < spp) && (sample < MAX_SAMPLES); sample++)
    {
    tbuff = srcbuffs[sample];
    if (tbuff != NULL)
      _TIFFfree(tbuff);
    }
 
  return status;
  }

static int
extractContigSamplesShifted16bits (uint8 *in, uint8 *out, uint32 cols, 
                                   tsample_t sample, uint16 spp, uint16 bps, 
  			           tsample_t count, uint32 start, uint32 end,
 	                           int shift)
  {
  int    ready_bits = 0, sindex = 0;
  uint32 col, src_byte, src_bit, bit_offset;
  uint16 maskbits = 0, matchbits = 0;
  uint16 buff1 = 0, buff2 = 0;
  uint8  bytebuff = 0;
  uint8 *src = in;
  uint8 *dst = out;
  
  if ((src == NULL) || (dst == NULL))
    {
    TIFFError("extractContigSamplesShifted16bits","Invalid input or output buffer");
    return (1);
    }

  if ((start > end) || (start > cols))
    {
    TIFFError ("extractContigSamplesShifted16bits", 
               "Invalid start column value %d ignored", start);
    start = 0;
    }
  if ((end == 0) || (end > cols))
    {
    TIFFError ("extractContigSamplesShifted16bits", 
               "Invalid end column value %d ignored", end);
    end = cols;
    }

  ready_bits = shift;
  maskbits = (uint16)-1 >> (16 - bps);
  for (col = start; col < end; col++)
    {    /* Compute src byte(s) and bits within byte(s) */
    bit_offset = col * bps * spp;
    for (sindex = sample; (sindex < spp) && (sindex < (sample + count)); sindex++)
      {
      if (sindex == 0)
        {
        src_byte = bit_offset / 8;
        src_bit  = bit_offset % 8;
        }
      else
        {
        src_byte = (bit_offset + (sindex * bps)) / 8;
        src_bit  = (bit_offset + (sindex * bps)) % 8;
        }

      src = in + src_byte;
      matchbits = maskbits << (16 - src_bit - bps); 
      if (little_endian)
        buff1 = (src[0] << 8) | src[1];
      else
        buff1 = (src[1] << 8) | src[0];

      if ((col == start) && (sindex == sample))
        buff2 = buff1 & ((uint16)-1) << (8 - shift);

      buff1 = (buff1 & matchbits) << (src_bit);

      if (ready_bits < 8) /* add another bps bits to the buffer */
        buff2 = buff2 | (buff1 >> ready_bits);
      else  /* If we have a full buffer's worth, write it out */
        {
        bytebuff = (buff2 >> 8);
        *dst++ = bytebuff;
        ready_bits -= 8;
        /* shift in new bits */
        buff2 = ((buff2 << 8) | (buff1 >> ready_bits));
        }

      ready_bits += bps;
      }
    }

  /* catch any trailing bits at the end of the line */
  while (ready_bits > 0)
    {
    bytebuff = (buff2 >> 8);
    *dst++ = bytebuff;
    ready_bits -= 8;
    }
  
  return (0);
  } /* end extractContigSamplesShifted16bits */


static int
extractContigSamplesShifted24bits (uint8 *in, uint8 *out, uint32 cols,
 	                           tsample_t sample, uint16 spp, uint16 bps, 
                                   tsample_t count, uint32 start, uint32 end,
	                           int shift)
  {
  int    ready_bits = 0, sindex = 0;
  uint32 col, src_byte, src_bit, bit_offset;
  uint32 maskbits = 0, matchbits = 0;
  uint32 buff1 = 0, buff2 = 0;
  uint8  bytebuff1 = 0, bytebuff2 = 0;
  uint8 *src = in;
  uint8 *dst = out;

  if ((in == NULL) || (out == NULL))
    {
    TIFFError("extractContigSamplesShifted24bits","Invalid input or output buffer");
    return (1);
    }

  if ((start > end) || (start > cols))
    {
    TIFFError ("extractContigSamplesShifted24bits", 
               "Invalid start column value %d ignored", start);
    start = 0;
    }
  if ((end == 0) || (end > cols))
    {
    TIFFError ("extractContigSamplesShifted24bits", 
               "Invalid end column value %d ignored", end);
    end = cols;
    }

  ready_bits = shift;
  maskbits =  (uint32)-1 >> ( 32 - bps);
  for (col = start; col < end; col++)
    {
    /* Compute src byte(s) and bits within byte(s) */
    bit_offset = col * bps * spp;
    for (sindex = sample; (sindex < spp) && (sindex < (sample + count)); sindex++)
      {
      if (sindex == 0)
        {
        src_byte = bit_offset / 8;
        src_bit  = bit_offset % 8;
        }
      else
        {
        src_byte = (bit_offset + (sindex * bps)) / 8;
        src_bit  = (bit_offset + (sindex * bps)) % 8;
        }

      src = in + src_byte;
      matchbits = maskbits << (32 - src_bit - bps); 
      if (little_endian)
	buff1 = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
      else
	buff1 = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];

      if ((col == start) && (sindex == sample))
        buff2 = buff1 & ((uint32)-1) << (16 - shift);

      buff1 = (buff1 & matchbits) << (src_bit);

      if (ready_bits < 16)  /* add another bps bits to the buffer */
        {
        bytebuff1 = bytebuff2 = 0;
        buff2 = (buff2 | (buff1 >> ready_bits));
        }
      else /* If we have a full buffer's worth, write it out */
        {
        bytebuff1 = (buff2 >> 24);
        *dst++ = bytebuff1;
        bytebuff2 = (buff2 >> 16);
        *dst++ = bytebuff2;
        ready_bits -= 16;

        /* shift in new bits */
        buff2 = ((buff2 << 16) | (buff1 >> ready_bits));
        }
      ready_bits += bps;
      }
    }

  /* catch any trailing bits at the end of the line */
  while (ready_bits > 0)
    {
    bytebuff1 = (buff2 >> 24);
    *dst++ = bytebuff1;

    buff2 = (buff2 << 8);
    bytebuff2 = bytebuff1;
    ready_bits -= 8;
    }
   
  return (0);
  } /* end extractContigSamplesShifted24bits */

static int
extractContigSamplesShifted32bits (uint8 *in, uint8 *out, uint32 cols,
                                   tsample_t sample, uint16 spp, uint16 bps, 
 			           tsample_t count, uint32 start, uint32 end,
	                           int shift)
  {
  int    ready_bits = 0, sindex = 0 /*, shift_width = 0 */;
  uint32 col, src_byte, src_bit, bit_offset;
  uint32 longbuff1 = 0, longbuff2 = 0;
  uint64 maskbits = 0, matchbits = 0;
  uint64 buff1 = 0, buff2 = 0, buff3 = 0;
  uint8  bytebuff1 = 0, bytebuff2 = 0, bytebuff3 = 0, bytebuff4 = 0;
  uint8 *src = in;
  uint8 *dst = out;

  if ((in == NULL) || (out == NULL))
    {
    TIFFError("extractContigSamplesShifted32bits","Invalid input or output buffer");
    return (1);
    }


  if ((start > end) || (start > cols))
    {
    TIFFError ("extractContigSamplesShifted32bits", 
               "Invalid start column value %d ignored", start);
    start = 0;
    }
  if ((end == 0) || (end > cols))
    {
    TIFFError ("extractContigSamplesShifted32bits", 
               "Invalid end column value %d ignored", end);
    end = cols;
    }

  /* shift_width = ((bps + 7) / 8) + 1; */ 
  ready_bits = shift;
  maskbits =  (uint64)-1 >> ( 64 - bps);
  for (col = start; col < end; col++)
    {
    /* Compute src byte(s) and bits within byte(s) */
    bit_offset = col * bps * spp;
    for (sindex = sample; (sindex < spp) && (sindex < (sample + count)); sindex++)
      {
      if (sindex == 0)
        {
        src_byte = bit_offset / 8;
        src_bit  = bit_offset % 8;
        }
      else
        {
        src_byte = (bit_offset + (sindex * bps)) / 8;
        src_bit  = (bit_offset + (sindex * bps)) % 8;
        }

      src = in + src_byte;
      matchbits = maskbits << (64 - src_bit - bps); 
      if (little_endian)
        {
	longbuff1 = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
	longbuff2 = longbuff1;
        }
      else
        {
	longbuff1 = (src[3] << 24) | (src[2] << 16) | (src[1] << 8) | src[0];
	longbuff2 = longbuff1;
	}

      buff3 = ((uint64)longbuff1 << 32) | longbuff2;
      if ((col == start) && (sindex == sample))
        buff2 = buff3 & ((uint64)-1) << (32 - shift);

      buff1 = (buff3 & matchbits) << (src_bit);

      if (ready_bits < 32)
        { /* add another bps bits to the buffer */
        bytebuff1 = bytebuff2 = bytebuff3 = bytebuff4 = 0;
        buff2 = (buff2 | (buff1 >> ready_bits));
        }
      else  /* If we have a full buffer's worth, write it out */
        {
        bytebuff1 = (buff2 >> 56);
        *dst++ = bytebuff1;
        bytebuff2 = (buff2 >> 48);
        *dst++ = bytebuff2;
        bytebuff3 = (buff2 >> 40);
        *dst++ = bytebuff3;
        bytebuff4 = (buff2 >> 32);
        *dst++ = bytebuff4;
        ready_bits -= 32;
                    
        /* shift in new bits */
        buff2 = ((buff2 << 32) | (buff1 >> ready_bits));
        }
      ready_bits += bps;
      }
    }
  while (ready_bits > 0)
    {
    bytebuff1 = (buff2 >> 56);
    *dst++ = bytebuff1;
    buff2 = (buff2 << 8);
    ready_bits -= 8;
    }
  
  return (0);
} /* end extractContigSamplesShifted32bits */
static int 
extractContigSamplesBytes (uint8 *in, uint8 *out, uint32 cols, 
                           tsample_t sample, uint16 spp, uint16 bps, 
                           tsample_t count, uint32 start, uint32 end)
  {
  int i, bytes_per_sample, sindex;
  uint32 col, dst_rowsize, bit_offset;
  uint32 src_byte /*, src_bit */;
  uint8 *src = in;
  uint8 *dst = out;

  if ((src == NULL) || (dst == NULL))
    {
    TIFFError("extractContigSamplesBytes","Invalid input or output buffer");
    return (1);
    }

  if ((start > end) || (start > cols))
    {
    TIFFError ("extractContigSamplesBytes", 
               "Invalid start column value %d ignored", start);
    start = 0;
    }
  if ((end == 0) || (end > cols))
    {
    TIFFError ("extractContigSamplesBytes", 
               "Invalid end column value %d ignored", end);
    end = cols;
    }

  dst_rowsize = (bps * (end - start) * count) / 8;

  bytes_per_sample = (bps + 7) / 8; 
  /* Optimize case for copying all samples */
  if (count == spp)
    {
    src = in + (start * spp * bytes_per_sample);
    _TIFFmemcpy (dst, src, dst_rowsize);
    }
  else
    {
    for (col = start; col < end; col++)
      {
      for (sindex = sample; (sindex < spp) && (sindex < (sample + count)); sindex++)
        {
        bit_offset = col * bps * spp;
        if (sindex == 0)
          {
          src_byte = bit_offset / 8;
          /* src_bit  = bit_offset % 8; */
          }
        else
          {
          src_byte = (bit_offset + (sindex * bps)) / 8;
          /* src_bit  = (bit_offset + (sindex * bps)) % 8; */
          }
        src = in + src_byte;
        for (i = 0; i < bytes_per_sample; i++)
            *dst++ = *src++;
        }
      }
    }

  return (0);
  } /* end extractContigSamplesBytes */

static int readContigTilesIntoBuffer (TIFF* in, uint8* buf, 
                                      uint32 imagelength, 
                                      uint32 imagewidth, 
                                      uint32 tw, uint32 tl,
                                      tsample_t spp, uint16 bps)
  {
  int status = 1;
  tsample_t sample = 0;
  tsample_t count = spp; 
  uint32 row, col, trow;
  uint32 nrow, ncol;
  uint32 dst_rowsize, shift_width;
  uint32 bytes_per_sample, bytes_per_pixel;
  uint32 trailing_bits, prev_trailing_bits;
  uint32 tile_rowsize  = TIFFTileRowSize(in);
  uint32 src_offset, dst_offset;
  uint32 row_offset, col_offset;
  uint8 *bufp = (uint8*) buf;
  unsigned char *src = NULL;
  unsigned char *dst = NULL;
  tsize_t tbytes = 0, tile_buffsize = 0;
  tsize_t tilesize = TIFFTileSize(in);
  unsigned char *tilebuf = NULL;

  bytes_per_sample = (bps + 7) / 8; 
  bytes_per_pixel  = ((bps * spp) + 7) / 8;

  if ((bps % 8) == 0)
    shift_width = 0;
  else
    {
    if (bytes_per_pixel < (bytes_per_sample + 1))
      shift_width = bytes_per_pixel;
    else
      shift_width = bytes_per_sample + 1;
    }

  tile_buffsize = tilesize;
  if (tilesize == 0 || tile_rowsize == 0)
  {
     TIFFError("readContigTilesIntoBuffer", "Tile size or tile rowsize is zero");
     exit(-1);
  }

  if (tilesize < (tsize_t)(tl * tile_rowsize))
    {
#ifdef DEBUG2
    TIFFError("readContigTilesIntoBuffer",
	      "Tilesize %lu is too small, using alternate calculation %u",
              tilesize, tl * tile_rowsize);
#endif
    tile_buffsize = tl * tile_rowsize;
    if (tl != (tile_buffsize / tile_rowsize))
    {
    	TIFFError("readContigTilesIntoBuffer", "Integer overflow when calculating buffer size.");
        exit(-1);
    }
    }

  /* Add 3 padding bytes for extractContigSamplesShifted32bits */
  if( (size_t) tile_buffsize > 0xFFFFFFFFU - 3U )
  {
      TIFFError("readContigTilesIntoBuffer", "Integer overflow when calculating buffer size.");
      exit(-1);
  }
  tilebuf = (unsigned char*)_TIFFmalloc(tile_buffsize + 3);
  if (tilebuf == 0)
    return 0;
  tilebuf[tile_buffsize] = 0;
  tilebuf[tile_buffsize+1] = 0;
  tilebuf[tile_buffsize+2] = 0;

  dst_rowsize = ((imagewidth * bps * spp) + 7) / 8;  
  int ignore = 0;
  for (row = 0; row < imagelength; row += tl)
    {
    nrow = (row + tl > imagelength) ? imagelength - row : tl;
    for (col = 0; col < imagewidth; col += tw)
      {
      tbytes = TIFFReadTile(in, tilebuf, col, row, 0, 0);
      if (tbytes < tilesize  && !ignore)
        {
	TIFFError(TIFFFileName(in),
		  "Error, can't read tile at row %lu col %lu, Read %lu bytes of %lu",
		  (unsigned long) col, (unsigned long) row, (unsigned long)tbytes,
                  (unsigned long)tilesize);
		  status = 0;
                  _TIFFfree(tilebuf);
		  return status;
	}
      
      row_offset = row * dst_rowsize;
      col_offset = ((col * bps * spp) + 7)/ 8;
      bufp = buf + row_offset + col_offset;

      if (col + tw > imagewidth)
	ncol = imagewidth - col;
      else
        ncol = tw;

      /* Each tile scanline will start on a byte boundary but it
       * has to be merged into the scanline for the entire
       * image buffer and the previous segment may not have
       * ended on a byte boundary
       */
      /* Optimization for common bit depths, all samples */
      if (((bps % 8) == 0) && (count == spp))
        {
	for (trow = 0; trow < nrow; trow++)
          {
	  src_offset = trow * tile_rowsize;
	  _TIFFmemcpy (bufp, tilebuf + src_offset, (ncol * spp * bps) / 8);
          bufp += (imagewidth * bps * spp) / 8;
	  }
        }
      else
        {
	/* Bit depths not a multiple of 8 and/or extract fewer than spp samples */
        prev_trailing_bits = trailing_bits = 0;
        trailing_bits = (ncol * bps * spp) % 8;

	/*	for (trow = 0; tl < nrow; trow++) */
	for (trow = 0; trow < nrow; trow++)
          {
	  src_offset = trow * tile_rowsize;
          src = tilebuf + src_offset;
	  dst_offset = (row + trow) * dst_rowsize;
          dst = buf + dst_offset + col_offset;
          switch (shift_width)
            {
            case 0: if (extractContigSamplesBytes (src, dst, ncol, sample,
                                                   spp, bps, count, 0, ncol))
                      {
		      TIFFError("readContigTilesIntoBuffer",
                                "Unable to extract row %d from tile %lu", 
				row, (unsigned long)TIFFCurrentTile(in));
		      return 1;
		      }
		    break;
            case 1: if (extractContigSamplesShifted16bits (src, dst, ncol,
                                                             sample, spp,
                                                             bps, count,
                                                             0, ncol,
                                                             prev_trailing_bits))
                        {
		        TIFFError("readContigTilesIntoBuffer",
                                  "Unable to extract row %d from tile %lu", 
			  	  row, (unsigned long)TIFFCurrentTile(in));
		        return 1;
		        }
	            break;
            case 2: if (extractContigSamplesShifted24bits (src, dst, ncol,
                                                           sample, spp,
                                                           bps, count,
                                                           0, ncol,
                                                           prev_trailing_bits))
                      {
		      TIFFError("readContigTilesIntoBuffer",
                                "Unable to extract row %d from tile %lu", 
		  	        row, (unsigned long)TIFFCurrentTile(in));
		      return 1;
		      }
		    break;
            case 3:
            case 4:
            case 5: if (extractContigSamplesShifted32bits (src, dst, ncol,
                                                           sample, spp,
                                                           bps, count,
                                                           0, ncol,
                                                           prev_trailing_bits))
                      {
		      TIFFError("readContigTilesIntoBuffer",
                                "Unable to extract row %d from tile %lu", 
			        row, (unsigned long)TIFFCurrentTile(in));
		      return 1;
		      }
		    break;
            default: TIFFError("readContigTilesIntoBuffer", "Unsupported bit depth %d", bps);
		     return 1;
	    }
          }
        prev_trailing_bits += trailing_bits;
        /* if (prev_trailing_bits > 7) */
	/*   prev_trailing_bits-= 8; */
	}
      }
    }

  _TIFFfree(tilebuf);
  return status;
}
static int 
combineSeparateSamplesBytes (unsigned char *srcbuffs[], unsigned char *out,
                             uint32 cols, uint32 rows, uint16 spp, uint16 bps)
{
  int i, bytes_per_sample;
  uint32 row, col, col_offset, src_rowsize, dst_rowsize, row_offset;
  unsigned char *src;
  unsigned char *dst;
  tsample_t s;

  src = srcbuffs[0];
  dst = out;
  if ((src == NULL) || (dst == NULL))
    {
    TIFFError("combineSeparateSamplesBytes","Invalid buffer address");
    return (1);
    }

  bytes_per_sample = (bps + 7) / 8; 

  src_rowsize = ((bps * cols) + 7) / 8;
  dst_rowsize = ((bps * spp * cols) + 7) / 8;
  for (row = 0; row < rows; row++)
    {
    dst = out + (row * dst_rowsize);
    row_offset = row * src_rowsize;
    for (col = 0; col < cols; col++)
      {
      col_offset = row_offset + (col * (bps / 8)); 
      for (s = 0; (s < spp) && (s < MAX_SAMPLES); s++)
        {
        src = srcbuffs[s] + col_offset; 
        for (i = 0; i < bytes_per_sample; i++)
          *(dst + i) = *(src + i);
        src += bytes_per_sample;
        dst += bytes_per_sample;
        }   
      }
    }

  return (0);
} /* end combineSeparateSamplesBytes */
static int readSeparateStripsIntoBuffer (TIFF *in, uint8 *obuf, uint32 length, 
                                         uint32 width, uint16 spp)
{
  int i, bytes_per_sample, bytes_per_pixel, shift_width, result = 1;
  uint32 j;
  int32  bytes_read = 0;
  uint16 bps = 0, planar;
  uint32 nstrips;
  uint32 strips_per_sample;
  uint32 src_rowsize, dst_rowsize, rows_processed, rps;
  uint32 rows_this_strip = 0;
  tsample_t s;
  tstrip_t  strip;
  tsize_t scanlinesize = TIFFScanlineSize(in);
  tsize_t stripsize    = TIFFStripSize(in);
  unsigned char *srcbuffs[MAX_SAMPLES];
  unsigned char *buff = NULL;
  unsigned char *dst = NULL;

  if (obuf == NULL)
    {
    TIFFError("readSeparateStripsIntoBuffer","Invalid buffer argument");
    return (0);
    }

  memset (srcbuffs, '\0', sizeof(srcbuffs));
  TIFFGetFieldDefaulted(in, TIFFTAG_BITSPERSAMPLE, &bps);
  TIFFGetFieldDefaulted(in, TIFFTAG_PLANARCONFIG, &planar);
  TIFFGetFieldDefaulted(in, TIFFTAG_ROWSPERSTRIP, &rps);
  if (rps > length)
    rps = length;

  bytes_per_sample = (bps + 7) / 8; 
  bytes_per_pixel  = ((bps * spp) + 7) / 8;
  if (bytes_per_pixel < (bytes_per_sample + 1))
    shift_width = bytes_per_pixel;
  else
    shift_width = bytes_per_sample + 1;

  src_rowsize = ((bps * width) + 7) / 8;
  dst_rowsize = ((bps * width * spp) + 7) / 8;
  dst = obuf;

  /* Libtiff seems to assume/require that data for separate planes are 
   * written one complete plane after another and not interleaved in any way.
   * Multiple scanlines and possibly strips of the same plane must be 
   * written before data for any other plane.
   */
  nstrips = TIFFNumberOfStrips(in);
  strips_per_sample = nstrips /spp;

  /* Add 3 padding bytes for combineSeparateSamples32bits */
  if( (size_t) stripsize > 0xFFFFFFFFU - 3U )
  {
      TIFFError("readSeparateStripsIntoBuffer", "Integer overflow when calculating buffer size.");
      exit(-1);
  }

  for (s = 0; (s < spp) && (s < MAX_SAMPLES); s++)
    {
    srcbuffs[s] = NULL;
    buff = (unsigned char*)_TIFFmalloc(stripsize + 3);
    if (!buff)
      {
      TIFFError ("readSeparateStripsIntoBuffer", 
                 "Unable to allocate strip read buffer for sample %d", s);
      for (i = 0; i < s; i++)
        _TIFFfree (srcbuffs[i]);
      return 0;
      }
    buff[stripsize] = 0;
    buff[stripsize+1] = 0;
    buff[stripsize+2] = 0;
    srcbuffs[s] = buff;
    }

  rows_processed = 0;
  int ignore = 0;
  for (j = 0; (j < strips_per_sample) && (result == 1); j++)
    {
    for (s = 0; (s < spp) && (s < MAX_SAMPLES); s++)
      {
      buff = srcbuffs[s];
      strip = (s * strips_per_sample) + j; 
      bytes_read = TIFFReadEncodedStrip (in, strip, buff, stripsize);
      rows_this_strip = bytes_read / src_rowsize;
      if (bytes_read < 0 && !ignore)
        {
        TIFFError(TIFFFileName(in),
              "Error, can't read strip %lu for sample %d",
               (unsigned long) strip, s + 1);
        result = 0;
        break;
        }
#ifdef DEVELMODE
      TIFFError("", "Strip %2d, read %5d bytes for %4d scanlines, shift width %d", 
        strip, bytes_read, rows_this_strip, shift_width);
#endif
      }

    if (rps > rows_this_strip)
      rps = rows_this_strip;
    dst = obuf + (dst_rowsize * rows_processed);
    if ((bps % 8) == 0)
      {
      if (combineSeparateSamplesBytes (srcbuffs, dst, width, rps,
                                       spp, bps))
        {
        result = 0;
        break;
    }
      }
    else
      {
	TIFFError ("readSeparateStripsIntoBuffer", "Unsupported bit depth: %d", bps);
                  result = 0;
                  break;
      }
 
    if ((rows_processed + rps) > length)
      {
      rows_processed = length;
      rps = length - rows_processed;
      }
    else
      rows_processed += rps;
    }

  /* free any buffers allocated for each plane or scanline and 
   * any temporary buffers 
   */
  for (s = 0; (s < spp) && (s < MAX_SAMPLES); s++)
    {
    buff = srcbuffs[s];
    if (buff != NULL)
      _TIFFfree(buff);
    }

  return (result);
} /* end readSeparateStripsIntoBuffer */

static int readContigStripsIntoBuffer (TIFF* in, uint8* buf)
{
        uint8* bufp = buf;
        int32  bytes_read = 0;
        uint32 strip, nstrips   = TIFFNumberOfStrips(in);
        uint32 stripsize = TIFFStripSize(in);
    int ignore = 0;
        uint32 rows = 0;
        uint32 rps = TIFFGetFieldDefaulted(in, TIFFTAG_ROWSPERSTRIP, &rps);
        tsize_t scanline_size = TIFFScanlineSize(in);

        if (scanline_size == 0) {
                TIFFError("", "TIFF scanline size is zero!");    
                return 0;
        }

        for (strip = 0; strip < nstrips; strip++) {
                bytes_read = TIFFReadEncodedStrip (in, strip, bufp, -1);
                rows = bytes_read / scanline_size;
                if ((strip < (nstrips - 1)) && (bytes_read != (int32)stripsize))
                        TIFFError("", "Strip %d: read %lu bytes, strip size %lu",
                                  (int)strip + 1, (unsigned long) bytes_read,
                                  (unsigned long)stripsize);

                if (bytes_read < 0 && !ignore) {
                        TIFFError("", "Error reading strip %lu after %lu rows",
                                  (unsigned long) strip, (unsigned long)rows);
                        return 0;
                }
                bufp += stripsize;
        }

        return 1;
} /* end readContigStripsIntoBuffer */
int loadImage(TIFF* in, image_data *image, unsigned char **read_ptr)
{
  uint32   i;
  float    xres = 0.0, yres = 0.0;
  uint32   nstrips = 0, ntiles = 0;
  uint16   planar = 0;
  uint16   bps = 0, spp = 0, res_unit = 0;
  uint16   orientation = 0;
  uint16   input_compression = 0, input_photometric = 0;
  uint16   subsampling_horiz, subsampling_vert;
  uint32   width = 0, length = 0;
  uint32   stsize = 0, tlsize = 0, buffsize = 0, scanlinesize = 0;
  uint32   tw = 0, tl = 0;       /* Tile width and length */
  uint32   tile_rowsize = 0;
  unsigned char *read_buff = NULL;
  unsigned char *new_buff  = NULL;
  int      readunit = 0;
  static   uint32  prev_readsize = 0;

  TIFFGetFieldDefaulted(in, TIFFTAG_BITSPERSAMPLE, &bps);
  TIFFGetFieldDefaulted(in, TIFFTAG_SAMPLESPERPIXEL, &spp);
  TIFFGetFieldDefaulted(in, TIFFTAG_PLANARCONFIG, &planar);
  TIFFGetFieldDefaulted(in, TIFFTAG_ORIENTATION, &orientation);
  if (! TIFFGetFieldDefaulted(in, TIFFTAG_PHOTOMETRIC, &input_photometric))
    TIFFError("loadImage","Image lacks Photometric interpreation tag");
  if (! TIFFGetField(in, TIFFTAG_IMAGEWIDTH,  &width))
    TIFFError("loadimage","Image lacks image width tag");
  if(! TIFFGetField(in, TIFFTAG_IMAGELENGTH, &length))
    TIFFError("loadimage","Image lacks image length tag");
  TIFFGetFieldDefaulted(in, TIFFTAG_XRESOLUTION, &xres);
  TIFFGetFieldDefaulted(in, TIFFTAG_YRESOLUTION, &yres);
  if (!TIFFGetFieldDefaulted(in, TIFFTAG_RESOLUTIONUNIT, &res_unit))
    res_unit = RESUNIT_INCH;
  if (!TIFFGetField(in, TIFFTAG_COMPRESSION, &input_compression))
    input_compression = COMPRESSION_NONE;

  scanlinesize = TIFFScanlineSize(in);
  image->bps = bps;
  image->spp = spp;
  image->planar = planar;
  image->width = width;
  image->length = length;
  image->xres = xres;
  image->yres = yres;
  image->res_unit = res_unit;
  image->compression = input_compression;
  image->photometric = input_photometric;
  image->orientation = orientation;
  image->adjustments = 0;
/*  switch (orientation)
    {
    case 0:
    case ORIENTATION_TOPLEFT:
         image->adjustments = 0;
     break;
    case ORIENTATION_TOPRIGHT:
         image->adjustments = MIRROR_HORIZ;
     break;
    case ORIENTATION_BOTRIGHT:
         image->adjustments = ROTATECW_180;
     break;
    case ORIENTATION_BOTLEFT:
         image->adjustments = MIRROR_VERT; 
     break;
    case ORIENTATION_LEFTTOP:
         image->adjustments = MIRROR_VERT | ROTATECW_90;
     break;
    case ORIENTATION_RIGHTTOP:
         image->adjustments = ROTATECW_90;
     break;
    case ORIENTATION_RIGHTBOT:
         image->adjustments = MIRROR_VERT | ROTATECW_270;
     break; 
    case ORIENTATION_LEFTBOT:
         image->adjustments = ROTATECW_270;
     break;
    default:
         image->adjustments = 0;
         image->orientation = ORIENTATION_TOPLEFT;
   }*/

  if ((bps == 0) || (spp == 0))
    {
    TIFFError("loadImage", "Invalid samples per pixel (%d) or bits per sample (%d)",
           spp, bps);
    return (-1);
    }

  if (TIFFIsTiled(in))
    {
    readunit = TILE;
    tlsize = TIFFTileSize(in);
    ntiles = TIFFNumberOfTiles(in);
    TIFFGetField(in, TIFFTAG_TILEWIDTH, &tw);
    TIFFGetField(in, TIFFTAG_TILELENGTH, &tl);

    tile_rowsize  = TIFFTileRowSize(in);      
    if (ntiles == 0 || tlsize == 0 || tile_rowsize == 0)
    {
    TIFFError("loadImage", "File appears to be tiled, but the number of tiles, tile size, or tile rowsize is zero.");
    exit(-1);
    }
    buffsize = tlsize * ntiles;
    if (tlsize != (buffsize / ntiles))
    {
    TIFFError("loadImage", "Integer overflow when calculating buffer size");
    exit(-1);
    }

    if (buffsize < (uint32)(ntiles * tl * tile_rowsize))
      {
      buffsize = ntiles * tl * tile_rowsize;
      if (ntiles != (buffsize / tl / tile_rowsize))
      {
    TIFFError("loadImage", "Integer overflow when calculating buffer size");
    exit(-1);
      }
      
      }
    }
  else
  {
    uint32 buffsize_check, rowsperstrip;
    readunit = STRIP;
    TIFFGetFieldDefaulted(in, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);
    stsize = TIFFStripSize(in);
    nstrips = TIFFNumberOfStrips(in);
    if (nstrips == 0 || stsize == 0)
    {
    TIFFError("loadImage", "File appears to be striped, but the number of stipes or stripe size is zero.");
    exit(-1);
    }

    buffsize = stsize * nstrips;
if (stsize != (buffsize / nstrips))
    {
	TIFFError("loadImage", "Integer overflow when calculating buffer size");
	exit(-1);
    }
    buffsize_check = ((length * width * spp * bps) + 7);
    if (length != ((buffsize_check - 7) / width / spp / bps))
    {
	TIFFError("loadImage", "Integer overflow detected.");
	exit(-1);
    }
    if (buffsize < (uint32) (((length * width * spp * bps) + 7) / 8))
      {
      buffsize =  ((length * width * spp * bps) + 7) / 8;
      }
   }
  int    jpegcolormode = JPEGCOLORMODE_RGB;
  if (input_compression == COMPRESSION_JPEG)
    {  /* Force conversion to RGB */
    jpegcolormode = JPEGCOLORMODE_RGB;
    TIFFSetField(in, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
    }
  /* The clause up to the read statement is taken from Tom Lane's tiffcp patch */
  else 
    {   /* Otherwise, can't handle subsampled input */
    if (input_photometric == PHOTOMETRIC_YCBCR)
      {
      TIFFGetFieldDefaulted(in, TIFFTAG_YCBCRSUBSAMPLING,
 		           &subsampling_horiz, &subsampling_vert);
      if (subsampling_horiz != 1 || subsampling_vert != 1)
        {
	TIFFError("loadImage", 
		"Can't copy/convert subsampled image with subsampling %d horiz %d vert",
                subsampling_horiz, subsampling_vert);
        return (-1);
        }
	}
    }
 
  read_buff = *read_ptr;
  /* +3 : add a few guard bytes since reverseSamples16bits() can read a bit */
  /* outside buffer */
  if (!read_buff)
  {
    if( buffsize > 0xFFFFFFFFU - 3 )
    {
        TIFFError("loadImage", "Unable to allocate/reallocate read buffer");
        return (-1);
    }
    read_buff = (unsigned char *)_TIFFmalloc(buffsize+3);
  }
  else
    {
    if (prev_readsize < buffsize)
    {
      if( buffsize > 0xFFFFFFFFU - 3 )
      {
          TIFFError("loadImage", "Unable to allocate/reallocate read buffer");
          return (-1);
      }
      new_buff = (unsigned char*)_TIFFrealloc(read_buff, buffsize+3);
      if (!new_buff)
        {
	free (read_buff);
        read_buff = (unsigned char *)_TIFFmalloc(buffsize+3);
        }
      else
        read_buff = new_buff;
      }
    }
  if (!read_buff)
    {
    TIFFError("loadImage", "Unable to allocate/reallocate read buffer");
    return (-1);
    }

  read_buff[buffsize] = 0;
  read_buff[buffsize+1] = 0;
  read_buff[buffsize+2] = 0;

  prev_readsize = buffsize;
  *read_ptr = read_buff;

  /* N.B. The read functions used copy separate plane data into a buffer as interleaved
   * samples rather than separate planes so the same logic works to extract regions
   * regardless of the way the data are organized in the input file.
   */
  switch (readunit) {
    case STRIP:
         if (planar == PLANARCONFIG_CONTIG)
           {
	     if (!(readContigStripsIntoBuffer(in, read_buff)))
	     {
	     TIFFError("loadImage", "Unable to read contiguous strips into buffer");
	     return (-1);
             }
           }
         else
           {
	   if (!(readSeparateStripsIntoBuffer(in, read_buff, length, width, spp)))
	     {
	     TIFFError("loadImage", "Unable to read separate strips into buffer");
	     return (-1);
             }
           }
         break;

    case TILE:
         if (planar == PLANARCONFIG_CONTIG)
           {
	   if (!(readContigTilesIntoBuffer(in, read_buff, length, width, tw, tl, spp, bps)))
	     {
	     TIFFError("loadImage", "Unable to read contiguous tiles into buffer");
	     return (-1);
             }
           }
         else
           {
	   if (!(readSeparateTilesIntoBuffer(in, read_buff, length, width, tw, tl, spp, bps)))
	     {
	     TIFFError("loadImage", "Unable to read separate tiles into buffer");
	     return (-1);
             }
           }
         break;
    default: TIFFError("loadImage", "Unsupported image file format");
          return (-1);
          break;
    }
  return (0);
  }   /* end loadImage */


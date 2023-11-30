#ifndef __JPEG_MEME_DST_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <jel/jel.h>


void jpeg_memory_dest (j_compress_ptr cinfo, unsigned char* data, int size);
int jpeg_mem_packet_size(j_compress_ptr cinfo);

  
#ifdef __cplusplus
}
#endif

#define __JPEG_MEME_DST_H__
#endif

#include "jel/jel.h"
#include "jel/jpeg-mem-dst.h"

#include "misc.h"


/*
 * This output manager interfaces jpeg I/O with memory buffers.
 * -Chris Connolly
 * 11/15/2002
 */

/* Expanded data destination object for output to memory */

typedef struct {
  struct jpeg_destination_mgr pub; /* public fields */
  long length;                  /* Keeps track of the number of output bytes. */

  unsigned char *outbuf;		/* target stream */
  int maxsize;
  JOCTET * buffer;		/* start of buffer */
} mem_destination_mgr;

typedef mem_destination_mgr * mem_dest_ptr;

#define OUTPUT_BUF_SIZE  4096	/* choose an efficiently fwrite'able size */

static int judp_errno = 0;

#define JUDP_ERR_CANTEMPTY     1
#define JUDP_ERR_TOOMUCHDATA   2

#if 0

GLOBAL(int) jpeg_mem_errno() {
  return judp_errno;
}

#endif


/*
 * Initialize destination --- called by jpeg_start_compress
 * before any data is actually written.
 */

METHODDEF(void)
init_destination (j_compress_ptr cinfo)
{
  mem_dest_ptr dest = (mem_dest_ptr) cinfo->dest;

  judp_errno = 0;

  dest->length = 0;

  dest->buffer = (JOCTET *)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				OUTPUT_BUF_SIZE * SIZEOF(JOCTET));

  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}


/*
 * Empty the output buffer --- called whenever buffer fills up.
 *
 * In typical applications, this should write the entire output buffer
 * (ignoring the current state of next_output_byte & free_in_buffer),
 * reset the pointer & count to the start of the buffer, and return TRUE
 * indicating that the buffer has been dumped.
 *
 * In applications that need to be able to suspend compression due to output
 * overrun, a FALSE return indicates that the buffer cannot be emptied now.
 * In this situation, the compressor will return to its caller (possibly with
 * an indication that it has not accepted all the supplied scanlines).  The
 * application should resume compression after it has made more room in the
 * output buffer.  Note that there are substantial restrictions on the use of
 * suspension --- see the documentation.
 *
 * When suspending, the compressor will back up to a convenient restart point
 * (typically the start of the current MCU). next_output_byte & free_in_buffer
 * indicate where the restart point will be if the current call returns FALSE.
 * Data beyond this point will be regenerated after resumption, so do not
 * write it out when emptying the buffer externally.
 */

METHODDEF(boolean)
empty_output_buffer (j_compress_ptr cinfo)
{
  int i, j;
  mem_dest_ptr dest = (mem_dest_ptr) cinfo->dest;

  j = dest->length;
  for (i = 0; i < OUTPUT_BUF_SIZE && j < dest->maxsize; i++) {
    dest->outbuf[j++] = dest->buffer[i];
  }

  dest->length = j;

  if (j >= dest->maxsize) {
    // printf("jpeg-mem-dst.c: empty_output_buffer exceeded the output buffer length.\n");
    judp_errno = JUDP_ERR_CANTEMPTY;
    return FALSE;
  }

  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

  return TRUE;
}


/*
 * Terminate destination --- called by jpeg_finish_compress
 * after all data has been written.  Usually needs to flush buffer.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

METHODDEF(void)
term_destination (j_compress_ptr cinfo)
{
  int i, j;
  mem_dest_ptr dest = (mem_dest_ptr) cinfo->dest;
  size_t datacount = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;

  /* Write any data remaining in the buffer */
  if (datacount > 0) {
    j = dest->length;
    for (i = 0; i < (int) datacount && j < dest->maxsize; i++)
      dest->outbuf[j++] = dest->buffer[i];

    dest->length = j;
    if (j >= dest->maxsize) {
      // printf("jpeg-mem-dst.c: term_destination exceeded the output buffer length.\n");
      judp_errno = JUDP_ERR_TOOMUCHDATA;
    }
  }
}


/*
 * Prepare for output to a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing compression.
 */

// Incompatible collision with jpeg_mem_dest in jpeg-9a/jdatadst.c
// How to resolve?
GLOBAL(void)
jpeg_memory_dest (j_compress_ptr cinfo, unsigned char* data, int size)
{
  mem_dest_ptr dest;

  /* The destination object is made permanent so that multiple JPEG images
   * can be written to the same file without re-executing jpeg_stdio_dest.
   * This makes it dangerous to use this manager and a different destination
   * manager serially with the same JPEG object, because their private object
   * sizes may be different.  Caveat programmer.
   */
  if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
    cinfo->dest = (struct jpeg_destination_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  SIZEOF(mem_destination_mgr));
  }

  dest = (mem_dest_ptr) cinfo->dest;
  dest->pub.init_destination = init_destination;
  dest->pub.empty_output_buffer = empty_output_buffer;
  dest->pub.term_destination = term_destination;
  dest->outbuf = data;
  dest->maxsize = size;
  dest->length = 0;
}

GLOBAL(int) jpeg_mem_packet_size(j_compress_ptr cinfo) {
  mem_dest_ptr dest;
  dest =  (mem_dest_ptr) cinfo->dest;
  return dest->length;
}


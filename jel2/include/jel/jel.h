/*
 * libjel: JPEG Embedding Library.  Embeds messages in selected
 * frequency components of a JPEG source, and emits the result on a
 * JPEG destination.  Sources and destinations can be named files,
 * FILE* streams, or memory areas.
 *
 * libjel header.  Defines basic structs and functions for libjel.
 *
 */


#ifndef __JEL_H__

#include <string.h>


//iam: these are needed for jpeglib.h
#include <stddef.h>

#if !defined __cplusplus
  #include <stdio.h>
#else
  #include <cstdio>
#endif

#include <jpeglib.h>
#include <jerror.h>

#include <stdarg.h>

#include <stdlib.h>

#include <stdbool.h>


/*  Define the component indices - quant tables and coefficient arrays
    are indexed by color channel as follows: */

#define YCOMP 0
#define UCOMP 1
#define VCOMP 2

/* This seems to make things slightly faster, using a ring buffer of PRNs: */
#define USE_PRN_CACHE 1

/* Offset to account for the density byte, 2 bytes of (short) length,
 * and 1 byte of checksum - unfortunately, jelbs is local to ijel.h,
 * at least for now:
 */

#define JELBS_HDR_SIZE 6
#define JEL_DEFAULT_PRN_CACHE_SIZE 62500

#ifdef __cplusplus
extern "C" {
#endif

  /* switches on verbose logging; defined and set in jel.c */
  extern bool jel_verbose;

#define JEL_NLEVELS 8  /* Default number of quanta for each freq. */

/*
 * A trivial frequency spec for now.  Later, we might want a way to
 * generate varying sequences based on a shared secret.  We will also
 * fold the bit packing strategy in this struct, when it becomes
 * necessary:
 */

  typedef struct {
    int init;            /* Will be 0 until the frequency indices are initialized. */
    int nfreqs;          /* Number of frequencies per MCU to use. */
    int maxfreqs;        /* Maximum number of frequencies made available. */
    int nlevels;
    int freqs[DCTSIZE2]; /* List of admissible frequency indices. */
    int in_use[DCTSIZE2];   /* List of frequency indices that we will use. */
  } jel_freq_spec;

/* ECC methods - for now, only libecc / rscode is supported.
 */
typedef enum jel_ecc_method
{
  JEL_ECC_NONE,
  JEL_ECC_RSCODE,

  _JEL_ECC_FIRST_METHOD = JEL_ECC_NONE,
  _JEL_ECC_LAST_METHOD  = JEL_ECC_RSCODE

} jel_ecc_method;

  /*
   * The addition of chroma has disturbed the PRN sequence when more
   * than one channel is being used.  PRNs are used for MCU selection
   * and frequency selection.  Our earlier solution precomputed the
   * MCU selection and then selected frequencies on the fly.  This no
   * longer works.
   */

#define CFG_RAND()      nrand48 (cfg->seed16v)
#define FORCE_BASELINE  TRUE

  /* #define JEL_ECC_BLKSIZE 200 */


typedef struct {
  int ncalls;
  int k;        /* Index to the next PRN to be used. */
  int nlist;    /* Number of PRNs in list.           */
  long* list;   /* List of PRNs - really a ring.     */
} prn_cache;

prn_cache* jelprn_create(int size, unsigned short seed[3]);
void       jelprn_destroy(prn_cache **p);
void       jelprn_reset(prn_cache *cache);
long       jelprn_next(prn_cache *cache);
void       jelprn_reload(prn_cache *cache, unsigned short seed[3]);


/*
 * jel_config struct contains information to be used during the
 * embedding and extraction operations.
 */

typedef struct jel_config {

  /* We will always need a source. */
  struct jpeg_decompress_struct srcinfo;
  FILE *srcfp;   /* Non-NULL iff. we are using filenames or FILEs. */

  /* For embedding, we need a destination.  NULL otherwise. */
  struct jpeg_compress_struct dstinfo;
  FILE *dstfp;   /* Non-NULL iff. we are using filenames or FILEs. */

  /* It might be necessary to allocate a separate coefficient array
   * for the destination: */
  jvirt_barray_ptr * coefs;
  jvirt_barray_ptr * dstcoefs;

  struct jpeg_error_mgr  jerr;

  FILE * logger;       /* FILE pointer for logging */

  int verbose;         /* Decide how much to spew out in the
                        * log. Default is 1.  3 is pretty chatty. */

  int quality;         /* Base JPEG quality.  If -1, then the
            * destination preserves the source quality
            * level.  Is set through the jel_setprop call,
            * which will automatically recompute output
            * quant tables.  If not -1, then message
            * embedding uses this quality level. */

  int extract_only;    /* If this is 1, then the dstinfo object is
              ignored and we will only extract a message.
              If 0, then we assume that we're
              embedding. */

  jel_freq_spec freqs; /* The frequency component indices we plan to
               use for embedding and extraction. */

  int embed_length;    /*  1 if the message length is embedded in the
               image. */

  int jpeglen;         /* Length of the JPEG-compressed data after embedding.
                          This is -1 if the length cannot be determined. */

  /* Total message and buffer lengths: */
  int len;             // Actual length of data
  int maxlen;          // Maximum length of data

  /* Pointer to buffer containing message of length 'len' or capable
     of holding maxlen bytes: */
  unsigned char *data;   // Data to be encoded

  /* Chroma is included by allowing the API to specify which channels
     can be used for steg and in what order.  The jel_set_message
     function will allot pointers and lengths. */

  /* These are per-channel pointers to message subsets: */
  unsigned char *data_ptr[3];

  /* Per-channel lengths of message subsets: */
  int data_lengths[3];
  int capacity[3];
  
  int jel_errno;
  int ecc_method;
  int ecc_blocklen;
  int bits_per_freq;
  // This goes away with the bitstream objec:
  //  int bits_per_mcu;  // Note change!  Used to be bytes_per_mcu.
  int maxmcus;                  // The number of MCUs available (a function of image size)
  int ethresh;                  // Energy threshold for MCU
  unsigned int seed;            // PRN randomization seed - used for all randomized things.
  unsigned short seed16v[3];    // nrand48 () state
  int mcu_density;              // In the interval [0,100] - percentage of MCUs that we want to use.
  int nmcus;                    // This is the number of MCUs to use for embedding = (maxmcus * mcu_density) / 100
  /* PRN calls: 

     1) For MCU maps for a single channel, CFG_RAND() is called n-1
     times, where n is the number of MCUs.  Since the MCU map is
     computed with the maximum MCU count, this is safe.

     2) For frequency selection, CFG_RAND() is called a variable
     number of times.  Specifically, the "embed" operation only calls
     it once per insertion, but the extraction operation could call it
     more often.  This mismatch will scrag multi-channel steg.  One
     solution is to precompute a fixed list of frequency permutations
     and just rotate through that list.
     
 */
  prn_cache *prn_cache;         // Precomputed PRNs for all per-frame operations - need n * (m+1) entries.
  unsigned int *mcu_list;       // This is a set of indices into the MCUs (for permutation)
  unsigned int *dc_values;      // This is a set of MCU's DC values derived from the source
  unsigned char *mcu_flag;      // This is the flag array.  Flags are 1 if the corresponding MCU can be used, 0 otherwise.
  int mcu_index;
  int randomize_mcus;           // If false, forbid any MCU randomization.
  int embed_bitstream_header;   // If true, embed the bitstream header, otherwise only embed message bytes.
  int set_dc;                   // If positive, set DC component to the value here for every MCU that gets touched.
                                // Note that this is the average pixel value, after unraveling DPCM for all MCUs
  int clear_ac;			// If true, clear all ac components before insertion of data.
  int dc_quant;			// DC quantization factor
  int debug_mcu;                // If non-negative, contains the index of an active MCU to be debugged
  JQUANT_TBL *qtable;           // Pointer to the active qtable if we are normalizing
  int normalize;                // If true(1), then act on true DCT
				// coefficients, else act on quantized
				// (compressed) DCT coefficient
				// values.
  int set_lsbs;	                // Set/get only the LSBs of frequency components, not their full values.
  int components[3];            // List of color channels to use (YUV).  Y=1, U=2, V=3.  Zero-terminated.
  /*
   * Prefilter function
   */
  int (*prefilter_func) (unsigned char *pBuffer, size_t nBuffer);   // called by ijel_unstuff_message() when decoding message
  int nPrefilter;

  int needFinishDecompress;
  int needFinishCompress;

  int copy_markers;            // 1 if source markers are to be copied, 0 otherwise

} jel_config;


#if !defined SWIG

jel_config * jel_init( int nlevels );  /* Initialize and requests at least 'nlevels' quanta for each freq. */
jel_config * _jel_init( int nlevels, jel_config *result );	/* Initialize statically allocated struct .*/

void jel_free( jel_config *cfg );   /* Free the jel_config struct. */

void jel_release( jel_config *cfg ); /* Release jel_config dynamic sub-structures */

#if 0
void jel_reset( jel_config *cfg );   /* Reset the jel_config struct for reuse */
#endif

void jel_copy_settings (jel_config *cfgIn, jel_config *cfgOut);	/* Copies parameters from one jel_config struct to another */

void jel_describe( jel_config *cfg, int level ); /* Describe the jel object in the log */

#endif /* notdef SWIG */

/*
 * Set source and destination objects - return 0 on success, negative
 * error code on failure.
 */
/*
 * Valid properties for getprop / setprop:
 */
typedef enum jel_property {
  JEL_PROP_QUALITY,
  JEL_PROP_EMBED_LENGTH,
  JEL_PROP_ECC_METHOD,
  JEL_PROP_ECC_BLOCKLEN,
  JEL_PROP_PRN_SEED,
  JEL_PROP_NLEVELS,
  JEL_PROP_NFREQS,
  JEL_PROP_MAXFREQS,
  JEL_PROP_BITS_PER_MCU,
  JEL_PROP_BITS_PER_FREQ,
  JEL_PROP_MCU_DENSITY,
  JEL_PROP_EMBED_HEADER,
  JEL_PROP_NORMALIZE,
  JEL_PROP_SET_DC,
  JEL_PROP_CLEAR_AC,
  _JEL_PROP_FIRST = JEL_PROP_QUALITY,
  _JEL_PROP_LAST  = JEL_PROP_NORMALIZE
} jel_property;

#if !defined SWIG

/*
 * Set source and destination objects - return 0 on success, negative
 * error code on failure.
 */
int jel_set_file_source(jel_config * cfg, char * filename);
int jel_set_fp_source(jel_config * cfg, FILE *fp);
int jel_set_mem_source(jel_config * cfg, unsigned char *mem, int len);

int jel_set_file_dest(jel_config * cfg, char *filename);
int jel_set_fp_dest(jel_config * cfg, FILE *fp);
int jel_set_mem_dest(jel_config * cfg, unsigned char *mem, int len);

int jel_set_components(jel_config *cfg, int comp1, int comp2, int comp3);

/* Get and set jel_config properties.
 *
 * Examples of properties: 
 *   frequency components
 *   quality
 *   source
 *   destination
 */

int jel_getprop( jel_config * cfg, jel_property prop );
int jel_setprop( jel_config *cfg, jel_property prop, int value );

/*
 * Set prefilter callback
 *
 * prefilter_func() is called by ijel_unstuff_message() when (at least) nPrefilter bytes have been decoded
 *   where:
 *
 *   pBuffer is the message buffer and
 *   nBuffer is the buffer length (>= nPrefilter)
 *
 *   Decoding terminates if prefilter_func() returns a non-zero value.
 */
int jel_set_prefilter_func(jel_config *cfg, int (*prefilter_func) (unsigned char *pBuffer, size_t nBuffer), int nPrefilter);

void jel_error( jel_config * cfg );
char *jel_version_string();

int jel_open_log( jel_config *cfg, char *filename);
int jel_close_log( jel_config *cfg);
int jel_log( jel_config *cfg, const char *format, ... );
int jel_vlog( jel_config *cfg, int level, const char *format, ... );
int jel_set_log_fd( jel_config *cfg, FILE *fd);

/* where:
 *
 * cfg       A properly initialized jel_config object
 *
 * Prints a string on stderr describing the most recent error.
 */


int    jel_error_code( jel_config * cfg );    /* Returns the most recent error code. */
char * jel_error_string( jel_config * cfg );  /* Returns the most recent error string. */


/*
 * jel_init_frequencies initializes the frequency selection API for embedding.
 *
 * cfg is a jel configuration object, flist is an pointer to an
 * array of ints that contains a list of desired frequency indices
 * for embedding, and len_flist is the length of flist.
 *
 * Returns the number of frequencies that will be used for embedding.
 */
int jel_init_frequencies(jel_config *cfg, int *flist, int len_flist);
int jel_set_frequencies(jel_config *cfg, int *flist, int len_flist);

/*
 * jel_capacity returns the number of plaintext bytes we can save in
 * the object.  This is computed taking into account any overhead
 * incurred by the chosen coding schemes (e.g., Reed-Solomon).
 *
 * jel_raw_capacity returns the total number of bytes that can be
 * stored in the object.
 */
int    jel_capacity( jel_config * cfg );      /* Returns the capacity in bytes of the source. */
int    jel_raw_capacity( jel_config * cfg );  /* Returns the "raw" capacity in bytes of the source. */

/* Allocates a buffer that is sufficient to hold any message
 * (per-frame) in the source.  Any such buffer can be passed to
 * free().
 */
void * jel_alloc_buffer( jel_config * cfg );

/*  Embed a message in an image: */

int jel_embed( jel_config * cfg, unsigned char * msg, int len);

/* where:
 *
 * cfg       A properly initialized jel_config object
 * msg       A region of memory containing bytes to be embedded
 * len       The number of bytes of msg to embed
 *
 * Returns the number of bytes that were embedded, or a negative error
 * code.  If the return value is positive but less than 'len', call
 * 'jel_error' for more information.
 */


/* Extract a message from an image. */

int jel_extract( jel_config * cfg, unsigned char * msg, int len);
/* where:
 *
 * cfg       A properly initialized jel_config object
 * msg       A region of memory to contain the extracted message
 * len       The maximum size in bytes of msg
 *
 * Returns the number of bytes that were extracted, or a negative error
 * code.  Note that if 'msg' is not large enough to hold the extracted
 * message, a negative error code will be returned, but msg will still
 * contain the bytes that WERE extracted.
 */

/*
 * Helper functions: LSB statistics, and setting LSBs to condition images.
 */
int jel_lsb_counts(jel_config *cfg, int *counts);
int jel_set_lsb(jel_config *cfg, int *mask);

void jel_perror( char *, int  );

#endif /* notdef SWIG */

#define JEL_LOG if (jel_verbose) jel_vlog

/*
 * Error Codes:
 */
typedef enum jel_error_enum {
    JEL_SUCCESS          =  0,
    JEL_ERR_JPEG         = -1,
    JEL_ERR_NOSUCHPROP   = -2,
    JEL_ERR_BADDIMS      = -3,
    JEL_ERR_NOMSG        = -4,
    JEL_ERR_CANTOPENLOG  = -5,
    JEL_ERR_CANTCLOSELOG = -6,
    JEL_ERR_CANTOPENFILE = -7,
    JEL_ERR_INVALIDFPTR  = -8,
    JEL_ERR_NODEST       = -9,
    JEL_ERR_MSG_OVERFLOW = -10,
    JEL_ERR_CREATE_MCU   = -11,
    JEL_ERR_ECC          = -12,
    JEL_ERR_CHECKSUM     = -13
} jel_error_enum;

#ifdef __cplusplus
} /* close extern "C" { */
#endif

#define __JEL_H__
#endif

/*
 * JPEG Embedding Library - jel.c
 *
 * This file defines the top-level API for libjel.
 *
 * -Chris Connolly
 * 
 * 1/20/2014
 *
 */

#include "jel/jel.h"

#include "misc.h"

#if !defined (JEL_VERSION)
  #define JEL_VERSION "1.2"
#endif

#define UNUSED(x) (void)(x)


#define WEDGEDEBUG 0
#define UNWEDGEDEBUG 0

bool jel_verbose = false;

/* 
 * iam: we need to be able to be able to fail more gracefully than exit
 * though maybe using setjmp and longjmp doesn't really qualify as
 * graceful. 
 */
#include <setjmp.h>

#include "jel/ijel.h"
#include "jel/ijel-ecc.h"
#include "jel/jpeg-mem-src.h"
#include "jel/jpeg-mem-dst.h"
#include "jel/jpeg-stdio-dst.h"


int ijel_max_mcus(jel_config *cfg, int component);

char* jel_error_strings[] = {
  "Success",
  "Unknown",
  "No such property"
};

/*
 * This library needs to be built with -DJEL_VERSION=XXX on the
 * command line.  Be aware that the library version number is
 * currently maintained in the cmake scripts, which will also define
 * the constant using -D.
 */
char *jel_version_string() {
  return(JEL_VERSION);
}

/*
 * Create and initialize a jel_config object.  This object controls
 * embedding and extraction.
 */

jel_config * jel_init( int nlevels ) {
  jel_config * result;

  /* Allocate: */
  result = calloc(1, sizeof(jel_config) );

  return _jel_init (nlevels, result);
}

/*
 * Create and initialize a jel_config object.  This object controls
 * embedding and extraction.
 */

jel_config * _jel_init( int nlevels, jel_config *result ) {
  struct jpeg_compress_struct *cinfo;

  /* Set everything to zero..... */
  memset(result, 0, (size_t) sizeof (struct jel_config));

  /* Initialize the quantum request: */
  result->freqs.nlevels = nlevels ? nlevels : JEL_NLEVELS;

  /* also zero the nfreqs (valgrind) */
  result->freqs.init = 0;             /* 0 until frequencies are chosen */
  result->freqs.nfreqs = 1;           /* Default to use only 1 freq. per MCU */
  result->freqs.maxfreqs = 6;         /* Defaulted to a max of 6 frequencies */

  /* Zero seed implies fixed set of frequencies.  Nonzero seed implies
   * use of srand() to generate frequencies. */
  //  result->seed = 0;

  /* better zero this too (valgrind); maybe calloc the whole structure?? */
  result->dstfp =  NULL;

  /* This is the output quality.  A quality of -1 means that the same
   * quant tables are used for input and output.  */

  result->verbose = 0;  /* "Normal" logging.  */
  result->quality = -1;

  /* Set up the decompression object: */
  result->srcinfo.err = jpeg_std_error(&result->jerr);
  jpeg_create_decompress( &(result->srcinfo) );
  //  jpeg_set_defaults( &(result->srcinfo) );

  
  /* Set up the compression object: */
  result->dstinfo.err = jpeg_std_error(&result->jerr);
  cinfo = &(result->dstinfo);
  jpeg_create_compress( cinfo );
  cinfo->in_color_space = JCS_GRAYSCALE;
  cinfo->input_components = 1;
  cinfo->dct_method = JDCT_ISLOW;
  jpeg_set_defaults( cinfo );
  jpeg_set_quality( cinfo, 75, FORCE_BASELINE );

  result->srcinfo.dct_method = JDCT_ISLOW; /* Force this as the default. */
  result->dstinfo.dct_method = JDCT_ISLOW; /* Force this as the default. */

  /* Get the quant table for luminance: */
  result->qtable = cinfo->quant_tbl_ptrs[0];
  
  /* Scan command line options, adjust parameters */

  /* Note that the compression object might not be used.  If
   * jel_set_XXX_dest() is called, then we assume that a message is to
   * be embedded and we will need it.  Otherwise, we assume that we
   * are extracting, and will not do anything else with it.
   */

#if 0
  /* Take a look at this and see if it's sufficient for us to use as
     an error handling mechanism for libjel: */

  /* Add some application-specific error messages (from cderror.h) */
  jerr.addon_message_table = cdjpeg_message_table;
  jerr.first_addon_message = JMSG_FIRSTADDONCODE;
  jerr.last_addon_message = JMSG_LASTADDONCODE;
#endif

  /* Some more defaults: */

  result->embed_length = TRUE;  /* Embed message length by default.  What about ECC? */
  result->jpeglen = 0;

  /* No messages yet, nor do we have a buffer. */
  result->len = 0;
  result->maxlen = 0;
  result->data = NULL;
  //  result->plain = NULL;
  /* Be careful.  We need to check error conditions. */
  result->jel_errno = 0;

  //result->ecc_method = JEL_ECC_RSCODE;
  result->ecc_method = JEL_ECC_NONE;		// MWF: for no particular reason
  result->ecc_blocklen = ijel_get_ecc_blocklen();

  result->set_lsbs = TRUE;                      // Default is to set only the LSBs of freq. components
  //  ijel_init_freq_spec(result->freqs);
  // jel_init_frequencies(result, NULL, 0);	// 20200414 - per Chris

  /* How to pack: */
  result->bits_per_freq = 1;
  result->mcu_density = -1;         // If -1, then we autocompute density
  result->maxmcus = 0;
  result->mcu_list = NULL;
  result->mcu_flag = NULL;

  result->embed_bitstream_header = FALSE;
  result->normalize = FALSE;
  result->set_dc = -1;
  result->clear_ac = FALSE;

  //  result->prn_cache = jelprn_create(JEL_DEFAULT_PRN_CACHE_SIZE, self->seed16v);
  result->prn_cache = NULL;

  // -1 means don't do anything. For k >=0 means debug MCU #k.  If -2,
  // -print every active MCU:
  result->debug_mcu = -1;

  result->components[0] = YCOMP;
  result->components[1] = -1;
  result->components[2] = -1;
  
  result->capacity[0] = 0;
  result->capacity[1] = 0;
  result->capacity[2] = 0;

  result->data_lengths[0] = 0;
  result->data_lengths[1] = 0;
  result->data_lengths[2] = 0;


  /* MCU energy threshold is 20.0: */
  //  result->ethresh = 700.0;
  result->ethresh = 40000;
  result->embed_bitstream_header = TRUE; 

  return result;
}



void jel_free( jel_config *cfg ) {
  jel_release (cfg);

  free(cfg);
  return;
}



void jel_release( jel_config *cfg ) {
  if (cfg->needFinishDecompress)
    (void) jpeg_finish_decompress(&cfg->srcinfo);
  if (cfg->needFinishCompress)
    (void) jpeg_finish_compress(&cfg->dstinfo);

  jpeg_destroy_decompress(&cfg->srcinfo);
  jpeg_destroy_compress(&cfg->dstinfo);
  /*
   * cfg->coefs and cfg->dstcoefs are "freed" by jpeg_destroy_decompress ()
   * and jpeg_destroy_compress (), above.
   */
  cfg->dstcoefs = (jvirt_barray_ptr *) NULL;

  cfg->freqs.init = 0;             /* 0 until frequencies are chosen */

  memset(&cfg->srcinfo, 0, sizeof(struct jpeg_decompress_struct));
  memset(&cfg->dstinfo, 0, sizeof(struct jpeg_compress_struct));

  if (cfg->mcu_list /* != (unsigned int *) NULL */) {
    memset(cfg->mcu_list, 0, (size_t) (cfg->maxmcus * (int) sizeof (unsigned int)));
    free(cfg->mcu_list);

    memset(cfg->mcu_flag, 0, (size_t) cfg->maxmcus);
    free(cfg->mcu_flag);

    memset(cfg->dc_values, 0, (size_t) cfg->maxmcus);
    free(cfg->dc_values);

    cfg->mcu_list = (unsigned int *)  NULL;
    cfg->mcu_flag = (unsigned char *) NULL;
    cfg->dc_values = (unsigned int *) NULL;

    cfg->srcfp    = (FILE *) NULL;
    cfg->dstfp    = (FILE *) NULL;
    cfg->data     = (unsigned char *) NULL;
    //    cfg->plain    = (unsigned char *) NULL;
    memset(cfg->seed16v, 0, 3 * sizeof (unsigned short));
  }
}


   static void
_makeSeed16v (unsigned long seed, unsigned short *seed16v)
{
  for (int i = 0; i < 2; i++) {
    seed16v[i] = (unsigned short) (seed & 0xFFFFUL);
    seed >>= 16;
  }

  seed16v[2] = seed16v[0] ^ seed16v[1];
}


#define _JEL_SET_PROP(_key, _prop)  (void) jel_setprop (cfgOut, _key, (int) cfgIn->_prop)

void jel_copy_settings (jel_config *cfgIn, jel_config *cfgOut) {
  _JEL_SET_PROP (JEL_PROP_QUALITY,       quality);
  _JEL_SET_PROP (JEL_PROP_EMBED_LENGTH,  embed_length);
  _JEL_SET_PROP (JEL_PROP_ECC_METHOD,    ecc_method);
  _JEL_SET_PROP (JEL_PROP_ECC_BLOCKLEN,  ecc_blocklen);
  _JEL_SET_PROP (JEL_PROP_PRN_SEED,      seed);
  _JEL_SET_PROP (JEL_PROP_NLEVELS,       freqs.nlevels);
  _JEL_SET_PROP (JEL_PROP_NFREQS,        freqs.nfreqs);
  _JEL_SET_PROP (JEL_PROP_MAXFREQS,      freqs.maxfreqs);
  _JEL_SET_PROP (JEL_PROP_MCU_DENSITY,   mcu_density);
  _JEL_SET_PROP (JEL_PROP_EMBED_HEADER,  embed_bitstream_header);
  _JEL_SET_PROP (JEL_PROP_NORMALIZE,     normalize);
  _JEL_SET_PROP (JEL_PROP_SET_DC,        set_dc);
  _JEL_SET_PROP (JEL_PROP_CLEAR_AC,      clear_ac);
  //  _JEL_SET_PROP (JEL_PROP_BITS_PER_MCU,  bits_per_mcu);
  _JEL_SET_PROP (JEL_PROP_BITS_PER_FREQ, bits_per_freq);
}


void jel_describe( jel_config *cfg, int v ) {
  int i, nf;
  JEL_LOG(cfg, v, "jel_config Object 0x%x {\n", cfg);
  JEL_LOG(cfg, v, "    srcfp = 0x%x,\n", cfg->srcfp);
  JEL_LOG(cfg, v, "    dstfp = 0x%x,\n", cfg->dstfp);
  JEL_LOG(cfg, v, "    srcinfo = 0x%x,\n", cfg->srcinfo);
  JEL_LOG(cfg, v, "    dstinfo = 0x%x,\n", cfg->dstinfo);
  JEL_LOG(cfg, v, "    quality = %d,\n", cfg->quality);
  JEL_LOG(cfg, v, "    verbose = %d,\n", cfg->verbose);
  JEL_LOG(cfg, v, "    extract_only = %d,\n", cfg->extract_only);
  JEL_LOG(cfg, v, "    freqs = ( ");
  nf = cfg->freqs.nfreqs;
  for (i = 0; i < nf; i++) JEL_LOG(cfg, v, " %d ", cfg->freqs.freqs[i]);
  JEL_LOG(cfg, v, "),\n");
  JEL_LOG(cfg, v, "    bits_per_freq = %d,\n", cfg->bits_per_freq);
  JEL_LOG(cfg, v, "    maxmcus = %d,\n", cfg->maxmcus);
  JEL_LOG(cfg, v, "    nmcus = %d,\n", cfg->nmcus);
  JEL_LOG(cfg, v, "    mcu_density = %d,\n", cfg->mcu_density);
  JEL_LOG(cfg, v, "    seed = %d,\n", cfg->seed);
  JEL_LOG(cfg, v, "    embed_length = %d,\n", cfg->embed_length);
  JEL_LOG(cfg, v, "    jpeglen = %d,\n", cfg->jpeglen);
  JEL_LOG(cfg, v, "    len = %d,\n", cfg->len);
  JEL_LOG(cfg, v, "    maxlen = %d,\n", cfg->maxlen);
  JEL_LOG(cfg, v, "    jel_errno = %d,\n", cfg->jel_errno);
  JEL_LOG(cfg, v, "    ecc_method = ");
  if (cfg->ecc_method == JEL_ECC_NONE) {
    JEL_LOG(cfg, v, "NONE,\n");
  } else if (cfg->ecc_method == JEL_ECC_RSCODE)  {
    JEL_LOG(cfg, v, "RSCODE,\n");
  } else {
    JEL_LOG(cfg, v, "UNRECOGNIZED,\n");
  }
  JEL_LOG(cfg, v, "    ecc_blocklen = %d\n", cfg->ecc_blocklen);
  JEL_LOG(cfg, v, "    embed_bitstream_header = %d,\n", cfg->embed_bitstream_header);
  JEL_LOG(cfg, v, "    set_dc = %d,\n", cfg->set_dc);
  JEL_LOG(cfg, v, "    dc_quant = %d,\n", cfg->dc_quant);
  JEL_LOG(cfg, v, "    clear_ac = %d,\n", cfg->clear_ac);
  JEL_LOG(cfg, v, "    normalize = %d,\n", cfg->normalize);
  JEL_LOG(cfg, v, "    randomize_mcus = %d,\n", cfg->randomize_mcus);
  JEL_LOG(cfg, v, "}\n");
}


typedef struct jel_error_mgr {
  struct jpeg_error_mgr mgr;    
  jmp_buf jmpbuff;  
} *jel_error_ptr;


/* iam: use this rather than exit - which is a bit rude as a library */
static void jel_error_exit (j_common_ptr cinfo){

  jel_error_ptr jelerr = (jel_error_ptr) cinfo->err;

  (*cinfo->err->output_message) (cinfo);

  longjmp(jelerr->jmpbuff, 1);

}

//clone of jround_up
static long round_up (long a, long b)
/* Compute a rounded up to next multiple of b, ie, ceil(a/b)*b */
/* Assumes a >= 0, b > 0 */
{
  a += b - 1L;
  return a - (a % b);
}


static void _ijel_prep_source (jel_config *cfg) {
  if (cfg->needFinishDecompress) {
    (void) jpeg_finish_decompress (&cfg->srcinfo);
    cfg->needFinishDecompress = FALSE;
  }
}


/*
 * Internal function to open the source and get coefficients:
 */
static int ijel_open_source(jel_config *cfg) {
  int ci;
  jvirt_barray_ptr *coef_arrays = NULL;
  jpeg_component_info *compptr;
  struct jpeg_decompress_struct *srcinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dstinfo = &(cfg->dstinfo);

  /* Read file header, set default decompression parameters */
  jpeg_read_header( srcinfo, TRUE);

  cfg->needFinishDecompress = TRUE;

  /* Read the file as arrays of DCT coefficients: */
  cfg->coefs = jpeg_read_coefficients( srcinfo );
  jpeg_copy_critical_parameters( srcinfo, dstinfo );

  coef_arrays = (jvirt_barray_ptr *)
      (*dstinfo->mem->alloc_small) ((j_common_ptr) srcinfo, JPOOL_IMAGE,
                                    SIZEOF(jvirt_barray_ptr) * (size_t) srcinfo->num_components);
  for (ci = 0; ci < srcinfo->num_components; ci++) {
    compptr = srcinfo->comp_info + ci;
    coef_arrays[ci] = (*dstinfo->mem->request_virt_barray)
      ((j_common_ptr) dstinfo, JPOOL_IMAGE, FALSE,
       (JDIMENSION) round_up((long) compptr->width_in_blocks,
                              (long) compptr->h_samp_factor),
       (JDIMENSION) round_up((long) compptr->height_in_blocks,
                              (long) compptr->v_samp_factor),
       (JDIMENSION) compptr->v_samp_factor);
  }
  cfg->dstcoefs = coef_arrays;
  

  /* Copy the source parameters to the destination object. This sets
   * up the default transcoding environment.  From this point on, the
   * caller can modify the compressor (destination) object as
   * needed. */

  jpeg_copy_critical_parameters( &(cfg->srcinfo), &(cfg->dstinfo) );

  if(jel_verbose){
    JEL_LOG(cfg, 2, "ijel_open_source: all done.\n");
  }

  cfg->jel_errno = 0;
  return 0;  /* TO DO: more detailed error reporting. */
}




/*
 * Set the source to be a FILE pointer:
 */
int jel_set_fp_source( jel_config *cfg, FILE *fpin ) {
  struct jel_error_mgr jerr;
  int j;

  if (fpin == NULL) return JEL_ERR_INVALIDFPTR;

  _ijel_prep_source (cfg);

  cfg->srcfp = fpin;
  /* Save Those Markers! */
  jpeg_save_markers(&(cfg->srcinfo), JPEG_COM, 0xffff);
  for (j=0;j<=15;j++) 
    jpeg_save_markers(&(cfg->srcinfo), JPEG_APP0+j, 0xffff);
  
  jpeg_stdio_src( &(cfg->srcinfo), fpin );

  /* graceful-ish exit on error  */

  cfg->srcinfo.err = jpeg_std_error (&jerr.mgr);
  jerr.mgr.error_exit = jel_error_exit;

  if (setjmp (jerr.jmpbuff)) { 
    JEL_LOG (cfg, 2, "jel_set_fp_source: caught a libjpeg error!\n");
    return -1; 
  }

  return ijel_open_source( cfg );
}




/*
 * Set the destination to be a FILE pointer:
 */
int jel_set_fp_dest( jel_config *cfg, FILE *fpout ) {

  if (fpout == NULL) return JEL_ERR_INVALIDFPTR;

  cfg->dstfp = fpout;
  jpeg_stdio_dest( &(cfg->dstinfo), fpout );

  cfg->jel_errno = 0;

  return 0;
}



/*
 * In-memory source and destination
 */
int jel_set_mem_source( jel_config *cfg, unsigned char *mem, int size ) {

  /* graceful-ish exit on error  */

  struct jel_error_mgr jerr;
  cfg->srcinfo.err = jpeg_std_error(&jerr.mgr);
  jerr.mgr.error_exit = jel_error_exit;

  if (setjmp(jerr.jmpbuff)) { 
    JEL_LOG(cfg, 2, "jel_set_mem_source: caught a libjpeg error!\n");
    jpeg_destroy((j_common_ptr) &(cfg->srcinfo));
    cfg->needFinishDecompress = 0;
    cfg->needFinishCompress = 0;
    return -1; 
  }

  _ijel_prep_source (cfg);

  jpeg_memory_src( &(cfg->srcinfo), mem, size );

  return ijel_open_source( cfg );
}


int jel_set_mem_dest( jel_config *cfg, unsigned char *mem, int size) {

  jpeg_memory_dest( &(cfg->dstinfo), mem, size );
  cfg->jel_errno = 0;

  return 0;
}



/*
 * Name a file to be used as source:
 */
int jel_set_file_source(jel_config *cfg, char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) return JEL_ERR_CANTOPENFILE;

  return jel_set_fp_source(cfg, fp);
}



/*
 * Name a file to be used as destination:
 */
int jel_set_file_dest(jel_config *cfg, char *filename) {
  FILE *fp = fopen(filename, "wb");
  if (fp == NULL) return JEL_ERR_CANTOPENFILE;

  return jel_set_fp_dest(cfg, fp);
}


/*
 * Set the components to be used.  comp1, comp2, and comp3 indicate
 * the components 0,1,2 in the preferred encoding order.  If a comp
 * argument is -1, then that component is not used.
 */

int jel_set_components(jel_config *cfg, int comp1, int comp2, int comp3) {
  /* Don't allow duplicates.  Also works if comp3 is -1: */
  JEL_LOG(cfg, 3, "jel_set_components:  Components in are %d %d %d\n", comp1, comp2, comp3);
  if (comp3 == comp2 || comp3 == comp1) comp3 = -1;

  if (comp2 == comp1) comp2 = -1;

  cfg->components[0] = comp1;
  cfg->components[1] = comp2;
  cfg->components[2] = comp3;

  return 1;
}


/*
 * Get properties of the config object - returns the value as a void*
 * (to be cast to the desired type, heh heh).  If the property is not
 * recognized, return the JEL_ERR_NOSUCHPROP error code and set
 * jel_errno to JEL_ERR_NOSUCHPROP.
 */
int jel_getprop( jel_config *cfg, jel_property prop ) {

  cfg->jel_errno = JEL_SUCCESS;

  switch( prop ) {

  case JEL_PROP_QUALITY:
    return cfg->quality;

  case JEL_PROP_EMBED_LENGTH:
    return cfg->embed_length;

  case JEL_PROP_ECC_METHOD:
    return cfg->ecc_method;

  case JEL_PROP_ECC_BLOCKLEN:
    return cfg->ecc_blocklen;

  case JEL_PROP_PRN_SEED:
    return (int) cfg->seed;

  case JEL_PROP_NFREQS:
    return cfg->freqs.nfreqs;

  case JEL_PROP_MAXFREQS:
    return cfg->freqs.maxfreqs;

  case JEL_PROP_MCU_DENSITY:
    return cfg->mcu_density;
    
  case JEL_PROP_BITS_PER_MCU:
    //return cfg->bits_per_mcu;
    return JEL_ERR_NOSUCHPROP;

  case JEL_PROP_BITS_PER_FREQ:
    return cfg->bits_per_freq;

  case JEL_PROP_EMBED_HEADER:
    return cfg->embed_bitstream_header;

  case JEL_PROP_NORMALIZE:
    return cfg->normalize;

  case JEL_PROP_SET_DC:
    return cfg->set_dc;

  default:
    cfg->jel_errno = JEL_ERR_NOSUCHPROP;
    return JEL_ERR_NOSUCHPROP;
  }

}


/*
 * Set properties of the config object - On success, returns the set
 * value as a void* (to be cast to the desired type, heh heh).  If the
 * property is not recognized, return the NOSUCHPROP error code.
 */
int jel_setprop( jel_config *cfg, jel_property prop, int value ) {
  /* What a mess.  Stuff this into an ijel function, or change the
   * freq selection API!! */
  // struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  // struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  //  JQUANT_TBL *qtable;

  cfg->jel_errno = JEL_SUCCESS;

  switch( prop ) {

  case JEL_PROP_QUALITY:
    cfg->quality = value;
    /* Since jel_init initializes a compressor, we can always do this,
     * even if we don't use it: */
    jpeg_set_quality( &(cfg->dstinfo), value, FORCE_BASELINE );
    return value;

  case JEL_PROP_EMBED_LENGTH:
    cfg->embed_length = value;
    return value;

  case JEL_PROP_ECC_METHOD:
    cfg->ecc_method = _JEL_ECC_FIRST_METHOD <= value && value <= _JEL_ECC_LAST_METHOD ? value : JEL_ECC_NONE;
    return value;

  case JEL_PROP_ECC_BLOCKLEN:
    cfg->ecc_blocklen = value;
    ijel_set_ecc_blocklen(value);
    return value;

  case JEL_PROP_PRN_SEED:
    cfg->seed = (unsigned int) value;  /* Redundant. */
    _makeSeed16v (cfg->seed, cfg->seed16v);
    //    cfg->prn_cache = jelprn_create(JEL_DEFAULT_PRN_CACHE_SIZE, cfg->seed16v);
    return value;

  case JEL_PROP_NLEVELS:
    cfg->freqs.nlevels = value;
    cfg->freqs.init    = 0;            /* 0 until frequencies are chosen */
    return value;

  case JEL_PROP_NFREQS:
    cfg->freqs.nfreqs = value;
    cfg->freqs.init   = 0;             /* 0 until frequencies are chosen */
#if 0
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];
    ijel_get_freq_indices(qtable, cfg->freqs.freqs, value, cfg->freqs.nlevels);
#endif
    return value;

  case JEL_PROP_MAXFREQS:
    cfg->freqs.maxfreqs = value;
    cfg->freqs.init     = 0;           /* 0 until frequencies are chosen */
#if 0
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];
    ijel_get_freq_indices(qtable, cfg->freqs.freqs, value, cfg->freqs.nlevels);
#endif
    return value;

  case JEL_PROP_MCU_DENSITY:
    cfg->mcu_density = value;
    return value;

  case JEL_PROP_BITS_PER_MCU:
    // cfg->bits_per_mcu = value;
    return JEL_ERR_NOSUCHPROP;

  case JEL_PROP_BITS_PER_FREQ:
    cfg->bits_per_freq = value;
    return value;

  case JEL_PROP_EMBED_HEADER:
    cfg->embed_bitstream_header = value;
    return value;

  case JEL_PROP_NORMALIZE:
    cfg->normalize = value;
    return value;

  case JEL_PROP_SET_DC:
    cfg->set_dc = value;
    return value;
    
  default:
    cfg->jel_errno = JEL_ERR_NOSUCHPROP;
    return JEL_ERR_NOSUCHPROP;
  }

}

int jel_set_prefilter_func(jel_config *cfg, int (*prefilter_func) (unsigned char *pBuffer, size_t nBuffer), int nPrefilter) {
  /* where:
   *
   * prefilter_func() is called by ijel_unstuff_message() when (at least) nPrefilter bytes have been decoded
   *   pBuffer is the message buffer and
   *   nBuffer is the buffer length (>= nPrefilter)
   *
   * Decoding terminates if prefilter_func() returns a non-zero value.
   */
    cfg->prefilter_func = prefilter_func;
    cfg->nPrefilter     = nPrefilter;

    return 0;
}


/*
 * Open a log file.  To use stderr, use jel_set_log_fd (), below.
 */
int jel_open_log(jel_config *cfg, char *filename) {
  cfg->logger = fopen( filename, "a+" );
  if ( cfg->logger == NULL ) {
    cfg->logger = stderr;
    cfg->jel_errno = JEL_ERR_CANTOPENLOG;
    return cfg->jel_errno;
  } else {
    cfg->jel_errno = 0;
    return 0;
  }
}


/*
 * Close the log file, if any.
 */
int jel_close_log(jel_config *cfg) {
  if (cfg->logger != stderr && cfg->logger != NULL) {
    FILE* tmp = cfg->logger;
    cfg->logger = NULL;
    if (!fclose(tmp)) {
      cfg->jel_errno = 0;
      return 0;
    } else {
      cfg->jel_errno = JEL_ERR_CANTCLOSELOG;
      return cfg->jel_errno;
    }
  }
  cfg->logger = NULL;
  cfg->jel_errno = 0;
  return 0;
}


int jel_log( jel_config *cfg, const char *format, ... ){
  int retval = 0;
  if ( cfg->logger != NULL ) {
    va_list arg;
    va_start(arg,format);
    retval = vfprintf(cfg->logger, format, arg); 
    va_end(arg);
    fflush(cfg->logger);
  }
  return retval;
}

/*
 * Log with a verbosity level.  The "current verbosity" is set within
 * the cfg struct.  A cfg->verbosity of 0 means no output.
 */
int jel_vlog( jel_config *cfg, int level, const char *format, ... ){
  int retval = 0;
  if ( cfg->logger != NULL && cfg->verbose > level) {
    va_list arg;
    va_start(arg,format);
    retval = vfprintf(cfg->logger, format, arg); 
    va_end(arg);
    fflush(cfg->logger);
  }
  return retval;
}


/*
 * Associate the logger with a previously opened file descriptor.
 * Provisionally closes previous logger.
 */
int jel_set_log_fd (jel_config *cfg, FILE *fd)
{
  int retval = jel_close_log (cfg);

  if (retval == 0 && fd /* != (FILE *) NULL */) {
	cfg->logger    = fd;
    cfg->jel_errno = 0;
  }

  return retval;
}


/*
 * Return the most recent error code.  Stored in the 'jel_errno' slot of
 * the config object:
 */
int    jel_error_code( jel_config * cfg ) {
  /* Returns the most recent error code. */
  return cfg->jel_errno;
}

char * jel_error_string( jel_config * cfg ) {
  /* Returns the most recent error string. */
  UNUSED (cfg);
  return "Unknown";
}



/*
 * Returns an integer capacity in bytes, minus the header overhead (4
 * bytes).  This will return the capacity of the image in MESSAGE
 * bytes.
 */

int    jel_capacity( jel_config * cfg ) {

  /* Returns the capacity in bytes of the source.  This is only
   * meaningful if we know enough about the srcinfo object, i.e., only
   * AFTER we have called 'jel_set_xxx_source'.  At present, only
   * luminance is considered.
   */
  int ijel_ecc_cap(int);
  int total, cap1, k, chan;

  
  if (!cfg->coefs)
    return 0;

  /* This measures total message capacity in terms of the number of
     bytes that can be embedded into this image.  ijel_image_capacity
     returns number of bits, but does not account for ECC or bitstream
     header info: */
  total = 0;
  for (k = 0; k < 3; k++) {
    chan = cfg->components[k];

    if (chan > -1) {
      cap1 = ijel_image_capacity(cfg, chan) >> 3;

      /* If ECC is requested, compute capacity subject to ECC overhead: */
      if (jel_getprop(cfg, JEL_PROP_ECC_METHOD) == JEL_ECC_RSCODE) {
        cap1 = ijel_capacity_ecc(cap1);
        JEL_LOG(cfg, 4, "jel_capacity assuming ECC returns %d for channel %d\n", cap1, chan);
      }

      cfg->capacity[k] = cap1 - JELBS_HDR_SIZE;
      
      total += (cap1 - JELBS_HDR_SIZE);    // 6 bytes per channel for the bitstream headers
    }
  }
  
  cfg->jel_errno = JEL_SUCCESS;

  //  printf("jel_capacity got capacity of %d, returning %d\n", cap1, cap1-4);

  return total; 
}



/*
 * Raw capacity - regardless of ECC, this is how many bytes we can
 * store in the image:
 */
int jel_raw_capacity(jel_config *cfg) {
  int ret;
  int prop = jel_getprop(cfg, JEL_PROP_ECC_METHOD);
  jel_setprop(cfg, JEL_PROP_ECC_METHOD, JEL_ECC_NONE);
  ret = jel_capacity(cfg);
  jel_setprop(cfg, JEL_PROP_ECC_METHOD, prop);
  return ret;
}

/* Return a buffer that holds anything up to the capacity of the source: */
void * jel_alloc_buffer( jel_config *cfg ) {
  int k = jel_raw_capacity( cfg );

  if (k > 0) return calloc((size_t) k, 1);
  else return NULL;
}



/*
 * Set up the message buffer for embedding:
 */
static void ijel_set_message(jel_config *cfg, unsigned char *msg, int msglen) {
  u_int64_t tot, n0, n1, n2, remaining, mytot;
  //  char *next;
  
  cfg->data = msg;
  cfg->maxlen = cfg->len = msglen;
  tot = cfg->capacity[0] + cfg->capacity[1] + cfg->capacity[2];

  if (msglen > tot) {
    JEL_LOG(cfg, 4, "ijel_set_message: msglen of %d is too big.  Truncating.\n", msglen);
    msglen = tot;
  }

  JEL_LOG(cfg, 4, "ijel_set_message: channel 0 capacity is %d bytes.\n", cfg->capacity[0]);
  JEL_LOG(cfg, 4, "ijel_set_message: channel 1 capacity is %d bytes.\n", cfg->capacity[1]);
  JEL_LOG(cfg, 4, "ijel_set_message: channel 2 capacity is %d bytes.\n", cfg->capacity[2]);

  JEL_LOG(cfg, 4, "ijel_set_message: total capacity is %d bytes, msglen=%d.\n", tot, msglen);

  remaining = msglen;

  /* Split the message into separate channels proportionally.  Using
     the total capacity tot, allocate a fraction of the message to
     each enabled channel. */

  if (cfg->components[1] == -1) {
    /* Only one channel will be used, so use the EZ form: */
    cfg->data_ptr[0] = cfg->data;
    cfg->data_lengths[0] = msglen;
    /* Just return from here: */
    return;
  }

  n0 = (cfg->capacity[0] * remaining) / tot;
  n1 = (cfg->capacity[1] * remaining) / tot;   // Length 
  n2 = (cfg->capacity[2] * remaining) / tot;
  mytot = n0+n1+n2;

  /* If the calculations are off (roundoff), then add the slack to the
     first component: */
  if (mytot < msglen) n0 += msglen - mytot;

  cfg->data_ptr[0] = cfg->data;
  cfg->data_lengths[0] = n0;
  remaining -= n0;

  JEL_LOG(cfg, 2, "ijel_set_message: channel 0 allocated n0=%lu (%lu remaining)\n", n0, remaining);
  
  /* This happens if only one channel is enabled.  That channel gets
     all the data: */
  cfg->data_ptr[1] = cfg->data_ptr[0] + n0;         // Offset into message data
  cfg->data_lengths[1] = n1;
  remaining -= n1;
  JEL_LOG(cfg, 2, "ijel_set_message: channel 1 allocated n1=%lu (%lu remaining)\n", n1, remaining);

  if (remaining > 0) {
    /* The message is split into at least three channels.  Set the
       length for the last channel: */
    cfg->data_ptr[2] = cfg->data_ptr[1] + n1;
    
    JEL_LOG(cfg, 2, "ijel_set_message: channel 2 allocated n2=%lu (%lu remaining)\n", n2, remaining);
    
    cfg->data_lengths[2] = remaining;
  }

  JEL_LOG(cfg, 2, "ijel_set_message: data_lengths[0] = %d\n", cfg->data_lengths[0]);
  JEL_LOG(cfg, 2, "ijel_set_message: data_lengths[1] = %d\n", cfg->data_lengths[1]);
  JEL_LOG(cfg, 2, "ijel_set_message: data_lengths[2] = %d\n", cfg->data_lengths[2]);
}


/*
 * Set up the message buffer for extracting:
 */
static int ijel_set_buffer(jel_config *cfg, unsigned char *buf, int maxlen) {
  int total_cap;
  
  cfg->data = buf;
  cfg->maxlen = maxlen;
  cfg->len = 0;

  total_cap = cfg->capacity[0] + cfg->capacity[1] + cfg->capacity[2];
  if (maxlen < total_cap) {
    JEL_LOG(cfg, 2, "ijel_set_buffer: Warning - supplied buffer does not seem to be big enough to hold potential message.\n");
    JEL_LOG(cfg, 2, "                 maxlen = %d, total_cap = %d\n", maxlen, total_cap);
  }

  /* Space out the components evenly, according to capacity - we will
     compact this later if needed:: */
  cfg->data_ptr[0] = cfg->data;
  cfg->data_ptr[1] = cfg->data_ptr[0] + cfg->capacity[0];
  cfg->data_ptr[2] = cfg->data_ptr[1] + cfg->capacity[1];

  /* It's unclear what's correct with respect to "raw" mode, but this
     is a band-aid: */
  if (maxlen > cfg->capacity[0])  cfg->data_lengths[0] = cfg->capacity[0];
  else cfg->data_lengths[0] = maxlen;
  cfg->data_lengths[1] = cfg->capacity[1];
  cfg->data_lengths[2] = cfg->capacity[2];

  return maxlen;
}


static size_t ijel_get_jpeg_length(jel_config * jel) {
  int k;

  if (jel->dstfp == NULL)
    k = jpeg_mem_packet_size( &(jel->dstinfo) );
  else
    k = jpeg_stdio_packet_size( &(jel->dstinfo) );

  return (size_t) k;
}


/*  
 * Embed a message in an image: 
 */
int jel_embed( jel_config * cfg, unsigned char * msg, int len) {
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
  int nwedge[3] = { 0, 0, 0 };
  int marker_count;

  cfg->jpeglen = 0;

  /* If message is NULL, shouldn't we punt? */
  if ( !msg ) {
    JEL_LOG(cfg, 1, "No message provided!  Exiting.\n" );
    jel_close_log(cfg);
    cfg->jel_errno = JEL_ERR_NOMSG;
    return cfg->jel_errno;
  }

  /* graceful-ish exit on error  */

  struct jel_error_mgr src_jerr;

  struct jel_error_mgr dst_jerr;

  cfg->srcinfo.err = jpeg_std_error(&src_jerr.mgr);
  src_jerr.mgr.error_exit = jel_error_exit;

  cfg->dstinfo.err = jpeg_std_error(&dst_jerr.mgr);
  dst_jerr.mgr.error_exit = jel_error_exit;


  if (setjmp(src_jerr.jmpbuff)) { 
    /* jpeg library has signalled an error on srcinfo */
    jel_log(cfg, "jel_embed: caught a libjpeg error in srcinfo!\n");
    return JEL_ERR_JPEG; 
  }

  if (setjmp(dst_jerr.jmpbuff)) { 
    /* jpeg library has signalled an error on destinfo */
    jel_log(cfg, "jel_embed: caught a libjpeg error in destinfo!\n");
    return JEL_ERR_JPEG; 
  }
    
  // Here's where we make the chroma mod: Calculate the proportion of
  // message to be split across the "active" channels (YUV).  It seems
  // as if this could be hidden in 'cfg' so that "ijel_set_message"
  // automatically splits the incoming 'msg' accordingly.
  // Specifically, add char pointers to jel_config that tell
  // ijel_stuff_message where to start and stop for a given channel:

  if (cfg->capacity[0] <= 0) {
    /* If we reach this point, we assume that jel_capacity hasn't been
       called yet, so do it here - this is how we determine allocation
       of message bytes across YUV: */
    JEL_LOG(cfg, 1, "jel_embed: computing capacity\n");
    jel_capacity(cfg);
  }

  /* Insert the message: */
  ijel_set_message(cfg, msg, len);
  //  JEL_LOG(cfg, 2, "jel_embed: Using components %d %d %d.\n", cfg->components[0], cfg->components[1], cfg->components[2]);
  JEL_LOG(cfg, 2, "jel_embed: Using components %d %d %d.\n", cfg->components[0], cfg->components[1], cfg->components[2]);

#if USE_PRN_CACHE
  int tot = ijel_max_mcus(cfg, 0) + ijel_max_mcus(cfg, 1) + ijel_max_mcus(cfg, 2);

  if (!cfg->prn_cache) {
    JEL_LOG(cfg, 2, "jel_embed: calling jelprn_create with size %d\n", tot);
    cfg->prn_cache = jelprn_create(tot, cfg->seed16v);
  }
#endif
  
  if (cfg->components[0] > -1) {
    JEL_LOG(cfg, 2, "\njel_embed: Using component %d.\n", cfg->components[0]);
    nwedge[0] = ijel_stuff_message(cfg, 0);
  }

  if (cfg->components[1] > -1) {
    JEL_LOG(cfg, 2, "\njel_embed: Using component %d.\n", cfg->components[1]);
    nwedge[1] = ijel_stuff_message(cfg, 1);
  }  

  if (cfg->components[2] > -1) {
    JEL_LOG(cfg, 2, "\njel_embed: Using component %d.\n", cfg->components[2]);
    nwedge[2] = ijel_stuff_message(cfg, 2);
  }
    
#if USE_PRN_CACHE
  if (cfg->prn_cache) jelprn_destroy(&(cfg->prn_cache));
#endif

  JEL_LOG(cfg, 2, "jel_embed: Return values from ijel_stuff_message = %d %d %d.\n",
          nwedge[0], nwedge[1], nwedge[2]);
  
  
  if ( cfg->quality > 0 ) {
    jpeg_set_quality( &(cfg->dstinfo), cfg->quality, FORCE_BASELINE );
    JEL_LOG(cfg, 3, "jel_embed: Reset quality to %d after jpeg_copy_critical_parameters.\n", cfg->quality);
  }

  //  iJEL_LOG_qtables(cfg);

  /* Start compressor (note no image data is actually written here) */
  jpeg_write_coefficients( &(cfg->dstinfo), cfg->coefs );

  marker_count = ijel_copy_markers(cfg);
  JEL_LOG(cfg, 3, "jel_embed: %d markers copied.", marker_count);
  // printf("jel_embed: copied %d markers.\n", marker_count);
  /* Copy to the output file any extra markers that we want to preserve */
  //  jcopy_markers_execute(&srcinfo, &dstinfo, JCOPYOPT_ALL);

  //  iJEL_LOG_qtables(cfg);

  /* Finish compression and release memory */
  jpeg_finish_compress(&cfg->dstinfo);
  cfg->needFinishCompress = FALSE;

  cfg->jpeglen = ijel_get_jpeg_length(cfg);
  JEL_LOG(cfg, 2, "jel_embed: JPEG compressed output size is %d.\n", cfg->jpeglen);

  //ian moved this to jel_free
  //jpeg_destroy_compress(&cfg->dstinfo);

  (void) jpeg_finish_decompress(&cfg->srcinfo);
  cfg->needFinishDecompress = FALSE;

  //ian moved this to jel_free
  //jpeg_destroy_decompress(&cfg->srcinfo);

  //  jel_reset (cfg);

  /* Should probably check for JPEG warnings here: */
  if (nwedge[0] < 0) return nwedge[0];
  if (nwedge[1] < 0) return nwedge[1];
  if (nwedge[2] < 0) return nwedge[2];

  return nwedge[0] + nwedge[1] + nwedge[2]; /* suppress no-return-value warnings */

}





/* 
 * Extract a message from an image. 
 */

int jel_extract( jel_config * cfg, unsigned char * msg, int maxlen) {
  /* where:
   *
   * cfg       A properly initialized jel_config object
   * msg       A region of memory to contain the extracted message
   * maxlen    The maximum size in bytes of msg
   *
   * Returns the number of bytes that were extracted, or a negative error
   * code.  Note that if 'msg' is not large enough to hold the extracted
   * message, a negative error code will be returned, but msg will still
   * contain the bytes that WERE extracted.
   */
  int msglen, clen;

  /* graceful-ish exit on error */
  struct jel_error_mgr jerr;

  JEL_LOG(cfg, 2, "in jel_extract %d\n", maxlen);
  
  cfg->srcinfo.err = jpeg_std_error(&jerr.mgr);
  jerr.mgr.error_exit = jel_error_exit;
  
  if (setjmp(jerr.jmpbuff)) { return -1; }

  if (cfg->capacity[0] <= 0) {
    /* If we reach this point, we assume that jel_capacity hasn't been
       called yet, so do it here - this is how we determine allocation
       of message bytes across YUV: */
    jel_capacity(cfg);
  }

  /* If the return value is negative, an error occurred (likely
     "overflow", meaning that maxlen wasn't big enough) */
  clen = ijel_set_buffer(cfg, msg, maxlen);
  if (clen < 0) return clen;
  
  JEL_LOG(cfg, 3, "jel_extract:  Buffer setup -----\n");
  JEL_LOG(cfg, 3, "         Component 1   start = 0x%08lx     length = %8d   capacity = %d\n",
	   cfg->data_ptr[0], cfg->data_lengths[0], cfg->capacity[0]);
  JEL_LOG(cfg, 3, "         Component 2   start = 0x%08lx     length = %8d   capacity = %d\n",
	   cfg->data_ptr[1], cfg->data_lengths[1], cfg->capacity[1]);
  JEL_LOG(cfg, 3, "         Component 3   start = 0x%08lx     length = %8d   capacity = %d\n",
	   cfg->data_ptr[2], cfg->data_lengths[2], cfg->capacity[2]);
  JEL_LOG(cfg, 3, "jel_extract:  ------------------\n");


  /* Assume here that ijel_set_buffer has set up the pointers
     correctly, so we only need run jel_unstuff_message on each component that's active:*/

#if USE_PRN_CACHE
  int tot = ijel_max_mcus(cfg, 0) + ijel_max_mcus(cfg, 1) + ijel_max_mcus(cfg, 2);

  if (!cfg->prn_cache) {
    JEL_LOG(cfg, 2, "jel_extract: calling jelprn_create with size %d\n", tot);
    cfg->prn_cache = jelprn_create(tot, cfg->seed16v);
  }
#endif
  
  cfg->len = maxlen;
  JEL_LOG(cfg, 2, "\njel_extract: Using component %d.\n", cfg->components[0]);
  clen =  ijel_unstuff_message(cfg, 0);
  msglen = clen;

  JEL_LOG(cfg, 2, "jel_extract: component %d data length = %d.\n", cfg->components[0], msglen);
  
  if (cfg->components[1] > -1) {
    JEL_LOG(cfg, 2, "\njel_extract: Using component %d.\n", cfg->components[1]);
    clen = ijel_unstuff_message(cfg, 1);
    msglen += clen;
    JEL_LOG(cfg, 2, "jel_extract: component %d data length = %d.\n", cfg->components[1], clen);
  }
  
  if (cfg->components[2] > -1) {
    JEL_LOG(cfg, 2, "\njel_extract: Using component %d.\n", cfg->components[2]);
    clen = ijel_unstuff_message(cfg, 2);
    JEL_LOG(cfg, 2, "jel_extract: component %d data length = %d.\n", cfg->components[2], clen);
    msglen += clen;
  }

#if USE_PRN_CACHE
  if (cfg->prn_cache) jelprn_destroy(&(cfg->prn_cache));
#endif
  
  /* Compoact the per-channel message data into one blob: */
  unsigned char* next = cfg->data_ptr[0] + cfg->data_lengths[0];
  if (cfg->components[1] > -1) {
    JEL_LOG(cfg, 3, "jel_extract: Copying %d bytes of component %d from 0x%lx to 0x%lx.\n",
	    cfg->data_lengths[1], cfg->components[1], (unsigned long) cfg->data_ptr[1], (unsigned long) next);
    memmove(next, cfg->data_ptr[1], cfg->data_lengths[1]);
    next += cfg->data_lengths[1];
  }

  if (cfg->components[2] > -1)
    memmove(next, cfg->data_ptr[2], cfg->data_lengths[2]);
      
  if(UNWEDGEDEBUG){
    JEL_LOG(cfg, 1, "jel_extract: %d bytes extracted\n", msglen);
  }    

  (void) jpeg_finish_decompress(&(cfg->srcinfo));
  cfg->needFinishDecompress = FALSE;

  //ian moved this to jel_free
  //jpeg_destroy_decompress(&(cfg->srcinfo));

  //  jel_reset (cfg);

  return msglen;    

}


/*
 * Called by wedge and unwedge? but not by the rest of jel - probably
 * superseded by ijel_select_freqs - think about deprecating this...
 */
int jel_init_frequencies(jel_config *cfg, int *flist, int len_flist) {
  int i, j;
  JQUANT_TBL *qtable;
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  if (cfg->freqs.init > 0) {
    j = cfg->freqs.nfreqs;
    // printf("jel_init_frequencies: freqs.init = %d, nothing done.\n", cfg->freqs.init);
  } else {
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];

    if (!flist || len_flist == 0) j = ijel_get_freq_indices(qtable, cfg->freqs.freqs, cfg->freqs.maxfreqs, cfg->freqs.nlevels);
    else {
      // printf("jel_init_frequencies: Setting frequency list.\n");
      for (i = 0; i < len_flist; i++)
        cfg->freqs.freqs[i] = flist[i];
      j = len_flist;
      cfg->freqs.maxfreqs = len_flist;  /* We should relax this at some point */
    }
    cfg->freqs.init = 1;
  }
  return j;
}


/* Used by wedge and unwedge to force frequencies. */
int jel_set_frequencies(jel_config *cfg, int *flist, int len_flist) {
  int i, j;

  //  printf("jel_set_frequencies: Setting frequency list.\n");
  for (i = 0; i < len_flist; i++) {
    cfg->freqs.freqs[i] = flist[i];
    cfg->freqs.in_use[i] = flist[i];
  }

  j = len_flist;
  cfg->freqs.maxfreqs = len_flist;  /* We should relax this at some point */

  cfg->freqs.init = 1;

  return j;
}


int ijel_get_lsbs(jel_config *cfg, int *counts);
int ijel_set_lsbs(jel_config *cfg, int *mask);

int jel_lsb_counts(jel_config *cfg, int *counts) {
  return ijel_get_lsbs(cfg, counts);
}


int jel_set_lsb(jel_config *cfg, int *mask) {
  return ijel_get_lsbs(cfg, mask);
}




void jel_perror( char *msg, int jel_errno ) {
  printf("%s ", msg);
  switch (jel_errno) {
  case JEL_ERR_JPEG:         printf("jpeg lib internal error.\n"); break;
  case JEL_ERR_NOSUCHPROP:   printf("No such jel property.\n"); break;
  case JEL_ERR_BADDIMS:      printf("Bad dimensions.\n"); break;
  case JEL_ERR_NOMSG:        printf("No message.\n"); break;
  case JEL_ERR_CANTOPENLOG:  printf("Can't open log.\n"); break;
  case JEL_ERR_CANTCLOSELOG: printf("Can't close log.\n"); break;
  case JEL_ERR_CANTOPENFILE: printf("Can't open file.\n"); break;
  case JEL_ERR_INVALIDFPTR:  printf("Invalid file pointer.\n"); break;
  case JEL_ERR_NODEST:       printf("No destination.\n"); break;
  case JEL_ERR_MSG_OVERFLOW: printf("Message too big for embedding.\n"); break;
  case JEL_ERR_CREATE_MCU:   printf("Can't create MCU map.\n"); break;
  case JEL_ERR_ECC:          printf("ECC-related error.\n"); break;
  case JEL_ERR_CHECKSUM:     printf("Invalid bitstream checksum.\n"); break;
  default:		     printf("Unknown jel error code %d\n", jel_errno); break;
  }
}




/***********************************************************************
 *                  PRN Caching
 * PRN usage is governed by a shared secret: the PRN seed.  The
 * correct PRN sequence must be preserved on both the wedge and
 * unwedge sides.  The implementation of MCU maps helps to enforce
 * this since MCU counts are fixed and clear on either side.  However,
 * the frequency selection process is also governed by a PRN, and
 * there may be cases where the number of PRN requests changes on
 * either side of the transaction.  This destroys multi-channel steg.
 *
 * One simple remedy is to precompute a fixed-size PRN cache that is
 * reset at every critical step: reset before MCU selection, and reset
 * before frequency selection, on a channel-by-channel basis.  This
 * utility could also be useful for native mpeg steg.
 */

/* See jel.h for struct definition. */

prn_cache* jelprn_create(int size, unsigned short seed[3]) {
  prn_cache *cache;

  cache = calloc(1, sizeof(prn_cache));
  cache->ncalls = 0;
  cache->k = 0;
  cache->nlist = size;
  cache->list = calloc(size, sizeof(long));
  jelprn_reload(cache, seed);

  return cache;
}


void jelprn_destroy(prn_cache **p) {
  if (*p == NULL) return;
  prn_cache *c = *p;
  free(c->list);
  free(c);
  *p = NULL;
}


void jelprn_reset(prn_cache *cache) {
  if (cache)  cache->k = 0;
}


long jelprn_next(prn_cache *cache) {
  long val, k;
  if (!cache) return 0;
  else {
    k = cache->k;
    cache->ncalls++;
    if (k >= cache->nlist) k = 0;

    val = cache->list[k];

    cache->k = k + 1;

    return val;
  }
}


void jelprn_reload(prn_cache *cache, unsigned short seed[3]) {
  int i;
  if (cache) {
    for (i = 0; i < cache->nlist; i++) cache->list[i] = nrand48 (seed);
  }
}

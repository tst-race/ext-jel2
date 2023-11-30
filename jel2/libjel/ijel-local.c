/*
 * JPEG Embedding Library - jel.c
 *
 * libjel internals - this file contains internal message embedding
 * and extraction code.  Not intended to be exposed as an API.
 *
 * -Chris Connolly
 * 
 * 1/20/2014
 *
 */

#include <math.h>
#include <stdint.h>
#include "jel/jel.h"
#include "jel/ijel-ecc.h"
#include "jel/ijel.h"

// #define BITS_TO_PRINT 1024
#define BITS_TO_PRINT 0

//#define COMP YCOMP
#define COMP YCOMP

/***********************************************************************
 *                    JEL Bitstreams
 * A word about the new model for stuffing / unstuffing data: We will
 * now shift to using only the "bits_per_freq" and "nfreqs" to define
 * the number of data elements to insert or retrieve from each MCU.
 * The core data structure for this is the "jelbs" bitstream object
 * defined below.
 *
 * jel bitstream definitions - put it in the internals file for now,
 * since it's only used here.
 */

typedef struct jelbs {
  jel_config *cfg;
  /* Ephemeral state for bit stuffing / unstuffing - bs==bitstream */
  uint32_t bit;               /* "active" bit counter */
  uint32_t nbits;             /* Total number of bits in message */
  uint32_t bufsize;           /* Buffer size = maximum number of message + length bytes */

  /* JELBS_HDR_SIZE (defined in jel.h) tells us the number of bytes in
     the following three quantities: */
  unsigned char density;      /* MCU density - can be in [1,100]  - 1 byte */
  uint32_t msgsize;           /* Message length    - 4 bytes */
  unsigned char checksum;     /* Header checksum   - 1 byte  */

  unsigned char *msg;
} jelbs;



void ijel_config_describe( jel_config *obj );
void jelbs_describe( jel_config *cfg, jelbs *obj, int level );
int jelbs_reset(jelbs *obj);
int jelbs_set_bufsize(jelbs *obj, int n);
int jelbs_set_msgsize(jelbs *obj, int n);
jelbs *jelbs_create_from_string(jel_config *obj, unsigned char *msg);
int jelbs_copy_message(jelbs *dst, char *src, int n);
void jelbs_destroy(jelbs **obj);
int jelbs_got_length(jelbs *obj);
long jelbs_get_length(jelbs *obj);
void jelbs_free(jelbs **obj);
int jelbs_get_bit(jelbs *obj, int k);
int jelbs_set_bit(jelbs *obj, int k, int val);
int jelbs_get_next_bit(jelbs *obj);
int jelbs_set_next_bit(jelbs *obj, int val);
unsigned char *ijel_maybe_init_ecc(jel_config *cfg, unsigned char *raw_msg, int *msglen, int *ecc);
int ijel_insert_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu, int channel);
int ijel_insert_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu);
int ijel_extract_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu, int channel);
int ijel_extract_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu);
int jelbs_get_density(jelbs *obj);
jelbs *jelbs_create(jel_config *obj, int size);
int jelbs_set_density(jelbs *obj, int density);



/*
 * The jelbs struct is ASSUMED to hold an integral number of 8-bit
 * bytes.  That is, we are not allowed to encode partial bytes.
 * Hence, nbits should always be 8 * nbytes.  Might be a little
 * redundant, but it's easier to inspect.
 */

void jelbs_describe( jel_config *cfg, jelbs *obj, int l ) {
  /* Print a description of the bitstream */
  JEL_LOG(cfg, l, "bit:        %8d   bits\n", obj->bit);
  JEL_LOG(cfg, l, "nbits:      %8d   bits\n", obj->nbits);
  JEL_LOG(cfg, l, "bufsize:    %8d   bytes\n", obj->bufsize);
  JEL_LOG(cfg, l, "density:    %8d   percent\n", obj->density);
  JEL_LOG(cfg, l, "msgsize:    %8d   bytes\n", obj->msgsize);
  JEL_LOG(cfg, l, "checksum:   %8x\n", obj->checksum);
  //  printf("message:    %s\n", obj->message);
}

typedef intptr_t __intptr_t;

void ijel_config_describe( jel_config *cfg ) {
  printf("-----jel_config object at 0x%lx:\n", (__intptr_t) cfg);
  printf("quality:               %d\n", cfg->quality);
  printf("extract_only:          %d\n", cfg->extract_only);
  printf("freq_spec:\n");
  printf("     nfreqs:           %d\n", cfg->freqs.nfreqs);
  printf("     maxfreqs:         %d\n", cfg->freqs.maxfreqs);
  printf("embed_length:          %d\n", cfg->embed_length);
  printf("embed_bitstream_header:%d\n", cfg->embed_bitstream_header);
  printf("jpeglen:               %d\n", cfg->jpeglen);
  printf("len:                   %d\n", cfg->len);
  printf("maxlen:                %d\n", cfg->maxlen);
  printf("data addr:             0x%lx\n", (__intptr_t) cfg->data);
  printf("ecc_method:            %d\n", cfg->ecc_method);
  printf("bits_per_freq:         %d\n", cfg->bits_per_freq);
  printf("nmcus:                 %d\n", cfg->nmcus);
  printf("mcu_density:           %d\n", cfg->mcu_density);
}

/* bitstream operations: This API supports bitwise stuffing and
 *  unstuffing into MCUs. */




int jelbs_reset(jelbs *obj) {
  /* Reset the bit stream to its initial state */
  obj->bit = 0;
  obj->nbits = (obj->msgsize + sizeof(obj->density) + sizeof(obj->msgsize) + sizeof(obj->checksum) ) * 8;
  return 0;
}



int jelbs_set_bufsize(jelbs *obj, int n) {
  /* For "empty" bit streams, set length explicitly. */
  obj->bufsize = n;
  jelbs_reset( obj );
  return obj->nbits;
}

int jelbs_set_msgsize(jelbs *obj, int n) {
  /* For "empty" bit streams, set length explicitly. */
  obj->msgsize = n;
  jelbs_reset( obj );
  return obj->nbits;
}


static void jelbs_compute_checksum(jelbs *bs) {
  unsigned char b = bs->density;
  unsigned char *len = (unsigned char*) &(bs->msgsize);
  /* msgsize should be a 32-bit long: */
  b ^= len[0];
  b ^= len[1];
  b ^= len[2];
  b ^= len[3];
  bs->checksum = b;
}


static int jelbs_validate_checksum(jel_config *cfg, jelbs *bs) {
  unsigned char b = bs->density;
  unsigned char *len = (unsigned char*) &(bs->msgsize);
  /* msgsize is uint32_t */
  b ^= len[0];
  b ^= len[1];
  b ^= len[2];
  b ^= len[3];
  if (bs->checksum == b) {
    JEL_LOG(cfg, 2, "jelbs_validate_checksum: jelbs header checksums match: %2x == %2x.\n", b, bs->checksum);
    return 1;
  } else {
    JEL_LOG(cfg, 2, "jelbs_validate_checksum: jelbs header checksum mismatch: %2x vs. %2x.\n", b, bs->checksum);
    return 0;
  }
}

/* Some confusion here, in that we depend on msg being a
 * zero-terminated string, but empty buffers can also be used.  It's
 * probably not good to depend on a null-terminated msg arg:
 */

jelbs *jelbs_create_from_string(jel_config *cfg, unsigned char *msg) {
  /* Creates and returns a bit stream object from a given string. */
  jelbs* obj = (jelbs*) calloc(1, sizeof(jelbs));
  obj->cfg = cfg;
  
  obj->msgsize = strlen( (const char*) msg);
  obj->bufsize = obj->msgsize;
  obj->msg = msg;
  jelbs_reset(obj);
  return obj;
}


int jelbs_copy_message(jelbs *dst, char *src, int n) {
  if (memmove(dst->msg, src, (unsigned int) n)) return n;
  else return 0;
}

jelbs *jelbs_create(jel_config *cfg, int size) {
  /* Creates and returns a bit stream object from a requested message size. */
  jelbs* obj = (jelbs*) calloc(1, sizeof(jelbs));
  obj->cfg = cfg;
  
  obj->msgsize = size;
  //  obj->bufsize = 2*size + 1;  // Provide ample space just in case.
  obj->bufsize = 4*size;  // Provide ample space just in case.
  obj->msg = calloc(1, (unsigned int) obj->bufsize);
  JEL_LOG(cfg, 2, "jelbs_create: size=%d, allocated %d bytes.\n", size, obj->bufsize);
  jelbs_reset(obj);
  return obj;
}


/* Companion to jelbs_create(n): Assumes that the message buffer has
 * been allocated by the jelbs API. */
void jelbs_destroy(jelbs **obj) {
  if ( *obj ) {
    if ( (*obj)->msg ) free((*obj)->msg);
    free(*obj);
    *obj = NULL;
  }
}


int jelbs_got_length(jelbs *obj) {
  unsigned int tot;
  tot = 8 * (sizeof(obj->density) + sizeof(obj->msgsize) + sizeof(obj->checksum));
  return( (unsigned int) (obj->bit) >= tot );
}

/* When length is embedded, it is the first long in the bitstream: */
long jelbs_get_length(jelbs *obj) {
  return (long) (obj->msgsize);
}


int jelbs_get_density(jelbs *obj) {
  return (int) obj->density;
}


int jelbs_set_density(jelbs *obj, int density) {
  obj->density = (unsigned char) density;
  return density;
}


void jelbs_free(jelbs **obj) {
  /* Free the bitstream object */
  free(*obj);
  *obj = NULL;
}


/* Would be good to do some sanity checking in these operations: */

int jelbs_get_bit(jelbs *obj, int k) {
  int offset = (sizeof(obj->density) + sizeof(obj->msgsize) + sizeof(obj->checksum) );
  unsigned char *len = (unsigned char*) &(obj->msgsize);
  unsigned char byte;
  /* Extract the k-th bit from the bitstream.  First compute
   * offsets: */
  int bit_in_byte = k % 8;
  int byte_in_msg = (k / 8) - offset;

  /* Mask out and shift to return either 1 or 0: */
  unsigned char mask = (1 << bit_in_byte);

  /* We are going to encode density and message length.  This logic is
     now agnostic about packing in the struct, so should work modulo
     endian-ness: */
  if (k < 8)       byte = obj->density; // This is in the first byte: density
  else if (k < 16) byte = len[0];
  else if (k < 24) byte = len[1];
  else if (k < 32) byte = len[2];
  else if (k < 40) byte = len[3];
  else if (k < 48) byte = obj->checksum;
  else             byte = obj->msg[byte_in_msg];

  int val = (mask & byte) >> bit_in_byte;

  if (k < BITS_TO_PRINT) JEL_LOG(obj->cfg, 2, "%d", val);

  return val;
}
  


int jelbs_set_bit(jelbs *obj, int k, int val) {
  int offset =  (sizeof(obj->density) + sizeof(obj->msgsize) + sizeof(obj->checksum) );
  unsigned char *len = (unsigned char*) &(obj->msgsize);
  uint32_t new;
  uint32_t byte;
  uint32_t bitval;

  if (val != 0) bitval = 1;
  else bitval = 0;

  /* Set the k-th bit from the bitstream to the value 'val'.  First
   * compute offsets: */
  int bit_in_byte = k % 8;
  int byte_in_msg = (k / 8) - offset;

  /* Then mask in the bit: */
  uint32_t vmask = (bitval << bit_in_byte);
  uint32_t mask = ~(1 << bit_in_byte);
  /* Mask is now an unsigned char with the 'val' bit in the appropriate bit position */
  
  /* Extract the appropriate byte: */
  if (k < 8)       byte = (uint32_t) obj->density; // This is in the first byte: density
  else if (k < 16) byte = (uint32_t) len[0];
  else if (k < 24) byte = (uint32_t) len[1];
  else if (k < 32) byte = (uint32_t) len[2];
  else if (k < 40) byte = (uint32_t) len[3];
  else if (k < 48) byte = (uint32_t) obj->checksum;
  else             byte = (uint32_t) obj->msg[byte_in_msg];

  //  data = (unsigned long) byte;
  /* Complement the mask, ANDing it with the byte.  Then or in the 'val' bit: */
  new =0x00FF & ( (byte & mask) | vmask );

  /* Set the altered byte: */
  if (k < 8)       obj->density = (uint8_t) new; // This is in the first byte: density
  /* The next 4 bytes contain message length: */
  else if (k < 16) len[0] = (uint8_t) new;
  else if (k < 24) len[1] = (uint8_t) new;
  else if (k < 32) len[2] = (uint8_t) new;
  else if (k < 40) len[3] = (uint8_t) new;
  /* Followed by header checksum */
  else if (k < 48) obj->checksum = (uint8_t) new;
  else             obj->msg[byte_in_msg] = (unsigned char) new;

  //  if (k == 24) printf("Length = %d\n", obj->msgsize);
  if (k < BITS_TO_PRINT) JEL_LOG(obj->cfg, 2, "%d", val);

  return val;
}




int jelbs_get_next_bit(jelbs *obj) {
  /* Get the next bit and advance the bit counter: */
  int result;
  if (obj->bit >= obj->nbits) return -1;
  else {
    result = jelbs_get_bit(obj, obj->bit);
    obj->bit++;
    return result;
  }
}


int jelbs_set_next_bit(jelbs *obj, int val) {
  /* Set the next bit to 'val' and advance the bit counter: */
  int result;
  if (obj->bit >= obj->nbits) {
    JEL_LOG(obj->cfg, 3, "jelbs_set_next_bit: obj=0x%lx\n", (unsigned long) obj);
    JEL_LOG(obj->cfg, 3, "jelbs_set_next_bit: Ran out of bits - bit pointer=%d but nbits=%d\n", obj->bit, obj->nbits);
    return -1;
  } else {
    result = jelbs_set_bit(obj, obj->bit, val);
    obj->bit++;
    return result;
  }
}



/***********************************************************************
 *              Frequency Selection
 * findFreqs: Given a quant table, find frequencies that have at
 * least 'nlevels' quanta.  Return an array of indices, starting at
 * the highest (most heavily quantized) and working toward the lowest,
 * but containing not more than nfreq component indices.  These
 * components will be used for encoding.
 *
 */

int ijel_get_freq_indices(JQUANT_TBL *q, int *i, int nfreq, int nlevels) {
  int j, quanta, m;

  m = 0;

  /* Corresponds to JDCT_ISLOW, in which the quantval[j] are shifted
   * up by 3 bits, and the corresponding range for DCT coefs is
   * [-1024,1023] ([0,2047] unsigned).
   *
   * Note that quantval[j] is a 16-bit unsigned quantity.
   */

  for (j = DCTSIZE2-1; j >= 0; j--) {
    if (m < nfreq) {
      quanta = 255 / q->quantval[j];
      if (quanta >= nlevels) {
        i[m] = j;
        m++;
      }
    }
  }

  return m;
}



#if 0
/*
 * Returns an array containing the frequency indices to use for embedding.
 */

static
int *ijel_select_freqs( jel_config *cfg ) {
  int i;
  jel_freq_spec *fspec = &(cfg->freqs);

  if(jel_verbose){
    JEL_LOG(cfg, 2, "ijel_select_freqs: returning %d frequencies: [", fspec->nfreqs);
    for (i = 0; i < fspec->nfreqs; i++) {
      JEL_LOG(cfg, 2, " %d", fspec->in_use[i]);
    }
    JEL_LOG(cfg, 2, "]\n");
  }
      
  return fspec->in_use;
}
#endif

static
int *ijel_reset_freqs( jel_config *cfg ) {
  int i;
  jel_freq_spec *fspec = &(cfg->freqs);

  //  printf("ijel_select_freqs: maxfreqs=%d\n", fspec->maxfreqs);
  /* We should not be recomputing this each time!! */
  for (i = 0; i < fspec->maxfreqs; i++)
    fspec->in_use[i] = fspec->freqs[i];

  if(jel_verbose){
    JEL_LOG(cfg, 2, "ijel_reset_freqs: selected %d frequencies: [", fspec->nfreqs);
    for (i = 0; i < fspec->nfreqs; i++) {
      JEL_LOG(cfg, 2, " %d", fspec->in_use[i]);
    }
    JEL_LOG(cfg, 2, "]\n");
  }
  return fspec->in_use;
}


/*
 * This is the pig.  It performs a frequency-list permutation for each
 * MCU.  For small maxfreqs, a table could be precomputed, but if
 * maxfreqs is large enough, there will be blood...
 */

static
int *ijel_permute_freqs( jel_config *cfg ) {
  int i, tmp;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* We should not be recomputing this each time!! */
  //  for (i = 0; i < fspec->maxfreqs; i++) fspec->in_use[i] = fspec->freqs[i];

  if (cfg->seed) {
    int j, n;
    n = fspec->maxfreqs;
    /* Fisher-Yates */
    //    printf("Freqs: ");
    for (i = 0; i < n; i++) {
#if USE_PRN_CACHE
      if (i > 0) j = jelprn_next(cfg->prn_cache) % (i+1);
#else      
      if (i > 0) j = CFG_RAND() % (i+1);
#endif
      else j = 0;

      if (j != i) {
	tmp = fspec->in_use[j];
	fspec->in_use[j] = fspec->in_use[i];
	fspec->in_use[i] = tmp;
      }
    }
    if (jel_verbose) {
      JEL_LOG(cfg, 3, "ijel_permute_freqs: [", fspec->nfreqs);
      for (i = 0; i < fspec->nfreqs; i++) {
	JEL_LOG(cfg, 3, " %d", fspec->in_use[i]);
      }
      JEL_LOG(cfg, 3, "]\n");
    }
  }
  
  return fspec->in_use;
}

/***********************************************************************
 *                   MCU Selection and maps
 *
 */
int ijel_max_mcus(jel_config *cfg, int component) {
  /* Returns the number of admissible MCUs */
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = component; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  //  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y, i, j;
  int mcu_count = 0;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  //  JCOEF *mcu;
  //JBLOCKARRAY row_ptrs;

  /* If not already specified, find a set of frequencies suitable for
     embedding 8 bits per MCU.  Use the destination object, NOT cinfo,
     which is the source: */
  JEL_LOG(cfg, 2, "ijel_max_mcus: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */

    /* Choose quant table based on compnum: */
    if (compnum == YCOMP) j = 0;
    else j = 1;

    qtable = dinfo->quant_tbl_ptrs[j];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[j];

    int ret = ijel_get_freq_indices(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    if ( fspec->nfreqs <= 0 ) fspec->nfreqs = ret;
    JEL_LOG(cfg, 2, "ijel_max_mcus: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) JEL_LOG(cfg, 2, "%d ", fspec->freqs[i]);
    JEL_LOG(cfg, 2, "]\n");
  }


  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;

  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight; blk_y += compptr->v_samp_factor) {

    /* row_ptrs = */ ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) mcu_count++;
    }
  }
  
  return mcu_count;
}



static
int ijel_create_mcu_map(jel_config *cfg, int compnum) {
  int i, max_mcus;

  JEL_LOG(cfg, 3, "ijel_create_mcu_map: Creating MCU map for compnum=%d.\n", compnum);
  max_mcus = ijel_max_mcus(cfg, compnum); // only component 0 for now
  cfg->maxmcus =  max_mcus;

  cfg->mcu_list = (unsigned int*) calloc(1, sizeof(unsigned int) * (size_t) max_mcus);
  cfg->mcu_flag = (unsigned char*) calloc(1, sizeof(unsigned char) * (size_t) max_mcus);
  cfg->dc_values = (unsigned int*) calloc(1, sizeof(unsigned int) * (size_t) max_mcus);

  
  for (i = 0; i < max_mcus; i++) {
    cfg->mcu_list[i] = (unsigned) i;
    cfg->mcu_flag[i] = 1;   /* 1 means the MCU is usable, 0 if not */
  }
  JEL_LOG(cfg, 3, "ijel_create_mcu_map: created and initialized MCU map of length %d.\n", max_mcus);
  return(max_mcus);
}

#if 1
/* 
 *Experimental speedups - this function happens to be costly:
 */
static
int ijel_select_mcus( jel_config *cfg, int compnum ) {
  int i, j, n, tmp;

  JEL_LOG(cfg, 3, "ijel_select_mcus maxmcus = %d, number of MCUs to use = %d, seed = %d\n",
	  cfg->maxmcus, cfg->nmcus, cfg->seed);

  n = cfg->maxmcus;

  if (!cfg->mcu_list) {
    i = ijel_create_mcu_map( cfg, compnum );
    if ( i != cfg->maxmcus ) return 0;
  }

  for (i = 0; i < n; i++) {
    cfg->mcu_list[i] = (unsigned) i;
    cfg->mcu_flag[i] = 1;
  }
  
  if (!cfg->seed) {
    if (cfg->mcu_density > 0 && cfg->mcu_density < 100) {
      /* Even when we're not using randomization, we spread out the
	 MCUs evenly: */
      int stride = 100 / cfg->mcu_density;
      for (i = 0; i < n; i+=stride) cfg->mcu_flag[i] = 1;
    }    
  } else {
#if USE_PRN_CACHE
    jelprn_reset(cfg->prn_cache);
    JEL_LOG(cfg, 3, "ijel_select_mcus: prn call count before = %d\n", cfg->prn_cache->ncalls);
#endif
    //    printf("MCUs: ");
    /* Fisher-Yates algorithm: */
    for (i = 1; i < n; i++) {
      cfg->mcu_flag[i] = 0;
      
#if USE_PRN_CACHE
      if (i > 1) j = jelprn_next(cfg->prn_cache) % (i+1);
#else
      if (i > 1) j = CFG_RAND() % (i+1);
#endif
      else       j = 1;

      /* Don't self-permute, and don't move if j==0 */
      if (j != i && j > 0) {
	tmp = (int) cfg->mcu_list[j];
	cfg->mcu_list[j] = cfg->mcu_list[i];
	cfg->mcu_list[i] = (unsigned) tmp;
      }
    }   

    /* Now turn on only the desired number of "active" MCUs, with
       respect to the above permutation: */
    // JEL_LOG(cfg, 3, "ijel_select_mcus mcu flags (%d): [", n);
    n = cfg->nmcus;
    for (j = 1; j < n; j++)
      cfg->mcu_flag[ cfg->mcu_list[j] ] = 1;

#if USE_PRN_CACHE
    JEL_LOG(cfg, 3, "ijel_select_mcus: prn call count after = %d\n", cfg->prn_cache->ncalls);
#endif
  }
  return cfg->nmcus;
}

#else

static
int ijel_select_mcus( jel_config *cfg, int compnum ) {
  int i, j, n, tmp;

  JEL_LOG(cfg, 3, "ijel_select_mcus maxmcus = %d, number of MCUs to use = %d, seed = %d\n",
	  cfg->maxmcus, cfg->nmcus, cfg->seed);

  n = cfg->maxmcus;

  if (!cfg->mcu_list) {
    i = ijel_create_mcu_map( cfg, compnum );
    if ( i != cfg->maxmcus ) return 0;
  }

  for (i = 0; i < n; i++) {
    cfg->mcu_list[i] = (unsigned) i;
    cfg->mcu_flag[i] = 1;
  }
  
  if (!cfg->seed) {
    if (cfg->mcu_density > 0 && cfg->mcu_density < 100) {
      /* Even when we're not using randomization, we spread out the
	 MCUs evenly: */
      // int new_n = (cfg->mcu_density * n) / 100;
      int stride = 100 / cfg->mcu_density;
      for (i = 0; i < n; i++) {
	if ((i % stride) == 0) cfg->mcu_flag[i] = 1;
	else cfg->mcu_flag[i] = 0;
      }
    }    
  } else {
#if USE_PRN_CACHE
    jelprn_reset(cfg->prn_cache);
    JEL_LOG(cfg, 3, "ijel_select_mcus: prn call count before = %d\n", cfg->prn_cache->ncalls);
#endif
    //    printf("MCUs: ");
    /* Fisher-Yates algorithm: */
    for (i = 1; i < n; i++) {
      cfg->mcu_flag[i] = 0;
      
#if USE_PRN_CACHE
      if (i > 1) j = jelprn_next(cfg->prn_cache) % (i+1);
#else
      if (i > 1) j = CFG_RAND() % (i+1);
#endif
      else       j = 1;

      /* Don't self-permute, and don't move if j==0 */
      if (j != i && j > 0) {
	tmp = (int) cfg->mcu_list[j];
	cfg->mcu_list[j] = cfg->mcu_list[i];
	cfg->mcu_list[i] = (unsigned) tmp;
      }
    }   

    /* Now turn on only the desired number of "active" MCUs, with
       respect to the above permutation: */
    // JEL_LOG(cfg, 3, "ijel_select_mcus mcu flags (%d): [", n);
    n = cfg->nmcus;
    for (j = 1; j < n; j++)
      cfg->mcu_flag[ cfg->mcu_list[j] ] = 1;

#if USE_PRN_CACHE
    JEL_LOG(cfg, 3, "ijel_select_mcus: prn call count after = %d\n", cfg->prn_cache->ncalls);
#endif
  }
  return cfg->nmcus;
}

#endif





static
int ijel_destroy_mcu_map(jel_config *cfg) {

  cfg->maxmcus =  0;

  free(cfg->mcu_list);
  cfg->mcu_list = NULL;

  free(cfg->mcu_flag);
  cfg->mcu_flag = NULL;

  free(cfg->dc_values);
  cfg->dc_values = NULL;

  return(1);
}



/***********************************************************************
 *                   Misc utility functions
 *
 */



int ac_energy( jel_config *cfg, JCOEF *mcu ) {
  int val;
  int i, j, ok;
  int e = 0;
  jel_freq_spec *fspec = &(cfg->freqs);
  struct jpeg_decompress_struct *info = &(cfg->srcinfo);
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  int compnum = COMP;
  
  compptr = &(info->comp_info[compnum]); // per-component information
  qtable = compptr->quant_table;
  

  /* We'll treat the DC component separately, so leave it out: */
  for (i = 1; i < DCTSIZE2; i++) {

    /* This version computes the energy for frequencies that are not
       in the admissible set of embedding freqs.  We could narrow this
       down to the list of in_use freqs. */

    ok = 1;
    for (j = 0; j < fspec->nfreqs && ok; j++)
      if (i == fspec->freqs[j]) ok = 0;

    if (ok) {
      val = mcu[i] * qtable->quantval[i];
      if ( val < 0 ) val = -val;
      if ( val > e ) e = val;
      //      e += (double) (val*val);
    }
  }

  //  return sqrt(e);
  return e;
}



/***********************************************************************/


/*
 * Survey of MCU energies - this is broken.
 */

int ijel_print_energies(jel_config *cfg) {

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = COMP; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;
  int energy, min_energy, max_energy;
  int j;
  
  /* If not already specified, find a set of frequencies suitable for
     embedding 8 bits per MCU.  Use the destination object, NOT cinfo,
     which is the source: */
  JEL_LOG(cfg, 2, "ijel_print_energies: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */
    if (compnum == YCOMP) j = 0;
    else j = 1;
    
    qtable = dinfo->quant_tbl_ptrs[j];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[j];

    int ret = ijel_get_freq_indices(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    if ( fspec->nfreqs <= 0 ) fspec->nfreqs = ret;
    fspec->init = 1;
  }

  if (jel_verbose) {
    JEL_LOG(cfg, 3, "ijel_get_freq_indices: ");
    for (j = 0; j < fspec->nfreqs; j++) JEL_LOG(cfg, 3, "%d ", fspec->freqs[j]);
    JEL_LOG(cfg, 3, "\n");
  }


  /* Check to see that we have at least 4 good frequencies.  This
     implicitly assumes that we are packing 8 bits per MCU.  We will
     want to change that in future versions. */ 
  if (fspec->nfreqs < 4) {
    if( debug ) {
      JEL_LOG(cfg, 4, "ijel_print_energies: Sorry - not enough good frequencies at this quality factor.\n");
    }
    return 0;
  }

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;
  min_energy = max_energy = -1.0;

  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
        /* Grab the next MCU, get the frequencies to use, and insert a
         * byte: */
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
        energy = ac_energy(cfg, mcu);
	printf("%d\n", energy);
        if (min_energy < 0 || energy < min_energy)
          min_energy = energy;
        if (max_energy < 0 || energy > max_energy)
          max_energy = energy;

      }
    }
  }
  
  JEL_LOG(cfg, 4, "# min,max energy = %d, %d\n", min_energy, max_energy);
  
  return 0;
}


/***********************************************************************
 *                   Capacity Computation
 *
 * Returns the image capacity in bits, based on number of bits per
 * frequency, frequencies per MCU, and number of MCUs:
 */

int ijel_image_capacity(jel_config *cfg, int compnum) {
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jpeg_component_info *compptr = cinfo->comp_info + compnum;
  int bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  int bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;
  int cap = ijel_capacity_iter(cfg, compnum);

  JEL_LOG(cfg, 2, "ijel_image_capacity: bwidth = %d, bheight = %d, v_samp_factor = %d, capacity = %d.\n", bwidth, bheight, compptr->v_samp_factor, cap);
  
  return cap;
}


/*
 * A reliable way to compute number of MCUs in an image.  Returns the
 * number of BITS of capacity:
 */
int ijel_capacity_iter(jel_config *cfg, int component) {
  /* Returns the number of admissible MCUs */
  // int reserved = 200;
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  JQUANT_TBL *qtable;
  jel_freq_spec *fspec = &(cfg->freqs);
  int i, j;
  int all_mcus = 0;
  int capacity = 0;

  /* If not already specified, find a set of frequencies suitable for
     embedding 8 bits per MCU.  Use the destination object, NOT cinfo,
     which is the source: */
  JEL_LOG(cfg, 3, "ijel_capacity_iter: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */
    if (component == YCOMP) j = 0;
    else j = 1;

    qtable = dinfo->quant_tbl_ptrs[j];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[j];

    int ret = ijel_get_freq_indices(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    if ( fspec->nfreqs <= 0 ) fspec->nfreqs = ret;
    JEL_LOG(cfg, 3, "ijel_capacity_iter: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) JEL_LOG(cfg, 3, "%d ", fspec->freqs[i]);
    JEL_LOG(cfg, 3, "]\n");
  }

  all_mcus = ijel_max_mcus(cfg, component);

  /* If mcu_density is positive, then use it to prorate the capacity: */
  if (cfg->mcu_density > 0 && cfg->mcu_density < 100) capacity = (cfg->mcu_density * cfg->bits_per_freq * fspec->nfreqs * all_mcus) / 100;
  else  capacity = cfg->bits_per_freq * fspec->nfreqs * all_mcus;
  
  return capacity;
}

/***********************************************************************
 * ECC initialization
 */

unsigned char *ijel_maybe_init_ecc(jel_config *cfg, unsigned char *raw_msg, int *msglen, int *ecc) {
  int i;
  int raw_msg_len = *msglen;
  unsigned char *message = NULL;

  message = raw_msg; // by default.
  
  if (jel_getprop(cfg, JEL_PROP_ECC_METHOD) == JEL_ECC_RSCODE) {

    if (ijel_ecc_sanity_check(raw_msg, raw_msg_len)){
      JEL_LOG(cfg, 3, "ijel_maybe_init_ecc: FYI, sanity check failed.\n");
      /* iam asks: why do we carry on regardless? */
    }
    
    message = ijel_encode_ecc(raw_msg,  raw_msg_len, &i);

    if (!message) {
      message = raw_msg; /* No ecc */
    }  else {

      if (cfg->verbose > 1)
        JEL_LOG(cfg, 3, "ijel_maybe_init_ecc: 1st 5 bytes of ECC data = %d %d %d %d %d\n", 
                message[0], message[1], message[2], message[3], message[4]);
      
      if(jel_verbose){
        JEL_LOG(cfg, 3, "ijel_maybe_init_ecc: ECC enabled, %d bytes of message encoded in %d bytes.\n", raw_msg_len, i);
      }
      *msglen = i;
      *ecc = 1;
    }
  }
  return message;
}



void ijel_print_mcu(jel_config *cfg, JCOEF *mcu, int quantized) {
  // Print the raw MCU as an 8x8 array:
  int i;
  JQUANT_TBL *q = cfg->qtable;
  for (i = 1; i < 65; i++) {
    if (quantized) printf("%4d ", (int) mcu[i-1]);
    else printf("%4d ", (int) mcu[i-1] * q->quantval[i]);
    if (i % 8 == 0) printf("\n");
  }
}

void ijel_print_qvalues(jel_config *cfg) {
  int i;
  JQUANT_TBL *q = cfg->qtable;
  for (i = 1; i < 65; i++) {
    printf("%4d ", (int) q->quantval[i-1]);
    if (i % 8 == 0) printf("\n");
  }
}

//#define FACTOR 2
//#define OFFSET 1
//#define FACTOR 2
//#define OFFSET -2
#define FACTOR 2
#define OFFSET -2
#define XFORM(x) (x*FACTOR+OFFSET)
#define INVXFORM(y) ((y - OFFSET) / FACTOR)


/***********************************************************************
 *                Core Steg Code
 * The jel_bitstream object is simply a convenient container for a lot
 * of ephemeral state (indices and bit pointers).

 * Insert the density byte (special case):

 */
int ijel_insert_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu, int chan) {
  int j, k, val, eom, v, nbits;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  eom = 0;
  nbits = 0;
  flist = cfg->freqs.in_use; // ijel_select_freqs(cfg);

  if (cfg->clear_ac)
    for (k = 1; k < 64; k++) mcu[k] = 0;

  JEL_LOG(cfg, 3, "ijel_insert_density: Inserting density %d\n", stream->density);
  /* for each byte in the MCU, insert it at the appropriate
     frequencies: */
  for (j = 0; j < 4; j++) {   // Always use 4 frequencies for density

    val = 0;
    v = 0;
    for ( k = 0; k < 2; k++) {  // Always use 2 bits per frequency for density:
      v = jelbs_get_next_bit(stream);
      if (v < 0) eom = 1;
      else {
	val = val << 1;
	val |= v;
	nbits++;
      }
    }
    /* After end-of-message, set all bits to zero: */
    if (eom) val = 0;
    //    mcu[ flist[j] ] = val - 1;
    mcu[ flist[j] ] = XFORM(val);
  }

  JEL_LOG(cfg, 3, "ijel_insert_density: Done inserting density (%d bits) on channel %d\n", nbits, chan);

  // Now reinitialize the active MCU map to account for the change in
  // density:
  ijel_select_mcus(cfg, cfg->components[chan]);

  if (stream->bit < BITS_TO_PRINT) JEL_LOG(cfg, 3, "|");
  return nbits;
}

/* Bits per frequency: this raises questions about the right way to
 * encode.  DCT components are SIGNED, whereas the original embedding
 * scheme inserts only positive values.
 *
 * DCT coefficient values are 11 bits, and can thus be in the
 * range [-1024,1024].  The quant coefficient f reduces this to the
 * quantized value in [ -1024/f, 1024/f ]
 *
 */

// The jel_bitstream object is simply a convenient container for a lot
// of ephemeral state (indices and bit pointers).

int ijel_insert_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, val, eom, v, nbits, idx;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  eom = 0;
  nbits = 0;

  idx = cfg->mcu_index;
  //  printf("nfreqs = %d\n", cfg->freqs.nfreqs);
  if ( cfg->mcu_flag[ idx ] ) {
    //    printf("Using MCU %d\n", cfg->mcu_index-1);
    flist = cfg->freqs.in_use; // ijel_select_freqs(cfg);
    //	  dc = (mcu[0] * dc_quant)/DCTSIZE + 128;
    if (cfg->set_dc >= 0)
      mcu[0] = ((cfg->set_dc - 128) * DCTSIZE) / cfg->dc_quant;

    if (cfg->clear_ac)
      for (k = 1; k < 64; k++) mcu[k] = 0;
    /* for each byte in the MCU, insert it at the appropriate
       frequencies: */
    for (j = 0; j < cfg->freqs.nfreqs; j++) {

      val = 0;
      v = 0;
      for ( k = 0; k < cfg->bits_per_freq; k++) {
        v = jelbs_get_next_bit(stream);
        if (v < 0) eom = 1;
        else {
          val = val << 1;
          val |= v;
	  nbits++;
        }
      }
      /* After end-of-message, set all bits to zero: */
      if (eom) val = 0;
      mcu[ flist[j] ] = XFORM(val);
      /* Turn this into a signed quantity: */
      //      mcu[ flist[j] ] = val - (1 << (k-1));
      //      printf("%d ", flist[j]);
    }
    //    printf("\n");
    if (stream->bit < BITS_TO_PRINT) printf("|");
  }
  (cfg->mcu_index)++;
  //  if (eom) printf("!\n");
  return nbits;
}


static void maybe_describe_mcu(jel_config *cfg, JCOEF *mcu, int all_mcus, int nm, char* when) {
  // If debug_mcu is -2, that indicates that we should dump all
  // MCUs.
  if (  cfg->debug_mcu == -2 || (cfg->mcu_flag[ all_mcus ] && cfg->debug_mcu == nm) ) {
    if (cfg->mcu_flag[ all_mcus ]) printf("===== %s embedding: MCU %d (ACTIVE) =====\n", when, all_mcus);
    else printf("===== %s embedding: MCU %d  =====\n", when, all_mcus);
    ijel_print_mcu(cfg, mcu, 1);
    printf("^^^^^^^^^^^^^^^^^^\n");
  }
}



/*
 * Primary embedding function:
 *
 * By default, this function will wedge the message into the
 * monochrome component, 8 bits per MCU.  Returns the number of
 * characters inserted.
 *
 * This function is WAY TOO BIG!!!
 *
 * We check for ECC here.  If present and requested, ECC will be
 * performed and written to the buffer 'message'.  Otherwise 'message'
 * simply points to the message buffer itself.  After embedding, any
 * ECC buffer must be deallocated.
 *
 * compnum:  1=Y, 2=U, 3=V.
 *
 */

int ijel_stuff_message(jel_config *cfg, int chan) {
  jelbs *bs;
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  int compnum = cfg->components[chan];
  /* Message to be embedded: */
  unsigned char *message = cfg->data_ptr[chan];
  /* Authoritative length of the message: */
  int msglen = cfg->data_lengths[chan];
  int nbits_in, msg_nbits;
  int first = TRUE;  // The next MCU we process will be the first one...
  int mcu_count = 0;
  int all_mcus = 0;
  int nb;

  /* ECC variables: */
  unsigned char *raw = cfg->data_ptr[chan];
  int ecc = 0;
  int plain_len = 0;

  /* This could use some cleanup to make sure that we really need all
   * these variables! */

  /* need to be able to know what went wrong in deployments */
  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y, i, j, k, nm;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;

  /* If we explicitly set the output quality, then this will be
   * non-NULL, but otherwise we will need to get the tables from the
   * source: */
  if (compnum == YCOMP) j = 0;
  else j = 1;
  
  qtable = dinfo->quant_tbl_ptrs[j];
  if (!qtable) qtable = cinfo->quant_tbl_ptrs[j];

  cfg->dc_quant = qtable->quantval[0];

  //size_t block_row_size = (size_t) SIZEOF(JCOEF)*DCTSIZE2*cinfo->comp_info[compnum].width_in_blocks;
  JEL_LOG(cfg, 1, "ijel_stuff_message: msglen = %d\n", msglen);
  if(jel_verbose) {
    JEL_LOG(cfg, 2, "ijel_stuff_message: 1st 5 bytes of plain text = %d %d %d %d %d\n", 
            raw[0], raw[1], raw[2], raw[3], raw[4]);
  }
  cfg->maxmcus = ijel_max_mcus(cfg, compnum); // only one component

  plain_len = msglen; /* Save the plaintext length */

  /* Check to see if we want ECC turned on - potential leakage here? */
  message = ijel_maybe_init_ecc(cfg, raw, &msglen, &ecc);

  /* If needed, message and msglen have now been updated to reflect
     ECC-related expansion. */
  
  /* Use the length of the message to create a bitstream, then copy
   * message into the bitstream */
  bs = jelbs_create(cfg, msglen);
  jelbs_copy_message(bs, (char*) message, msglen);
  jelbs_set_msgsize(bs, msglen);
  jelbs_set_density(bs, (unsigned char) cfg->mcu_density);
  JEL_LOG(cfg, 1, "ijel_stuff_message: jelbs message size = %lu, msglen = %d, nbits=%d\n", bs->msgsize, msglen, bs->nbits);

  /* If not already specified, find a set of frequencies suitable for
     embedding.  Use the destination object, NOT cinfo, which is the
     source: */
  JEL_LOG(cfg, 2, "ijel_stuff_message: cfg->bits_per_freq = %d; fspec->nfreqs = %d; fspec->maxfreqs = %d\n",
	  cfg->bits_per_freq, fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {

    /* Somewhere in here we need to check / verify that the selected
       frequencies are compatible with the quantization factor.  For
       example, with bpf = 2, we need to guarantee that the selected
       frequencies can actually HOLD 2 bits. */

    // Find frequencies with the appropriate properties:
    int ret = ijel_get_freq_indices(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    if ( fspec->nfreqs <= 0 ) fspec->nfreqs = ret;
    JEL_LOG(cfg, 2, "ijel_stuff_message: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) JEL_LOG(cfg, 2, "%d ", fspec->freqs[i]);
    JEL_LOG(cfg, 2, "]\n");
  }

  /* If requested, be verbose: */
  if ( debug && jel_verbose ) {     /* Do we want to use verbosity levels? */
    JEL_LOG(cfg, 1, "(:components #(");
    for (i = 0; i < fspec->nfreqs; i++) JEL_LOG(cfg, 1, "%d ", fspec->freqs[i]);
    JEL_LOG(cfg, 1, "))\n");
  }

  /* Need to double check the message length, to see whether it's
     compatible with the image.  Message gets truncated if it's longer
     than the number of MCU's in the luminance channel.  We will want
     to expand to the color components too:
  */
  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;
  
  k = 0;
  nbits_in = 0;
  msg_nbits = bs->nbits;

  
  JEL_LOG(cfg, 1, "ijel_stuff_message: embedded length = %lu bytes\n", bs->msgsize);
  
  //  if ( cfg->nmcus == 0 ) {
  cfg->nmcus = (int) floor( (cfg->mcu_density * cfg->maxmcus) / 100.0 );
  JEL_LOG(cfg, 2, "ijel_stuff_message: nmcus = %d = (int) floor( (%d * %d) / 100.0 )\n",
	  cfg->nmcus, cfg->mcu_density, cfg->maxmcus);
    //  }

  if ( cfg->mcu_density == -1 ) {
    //    ijel_config_describe( cfg );
    float maxbits = ijel_image_capacity(cfg, compnum);
    float bitstream_nbits = (float) ((msglen + JELBS_HDR_SIZE) << 3);
    JEL_LOG(cfg, 2, "jel_stuff_message: bitstream_nbits=%f ; maxbits = %f\n", bitstream_nbits, maxbits);
    if ( bitstream_nbits > maxbits ) {
      jelbs_destroy(&bs);
      return JEL_ERR_MSG_OVERFLOW;
    }
    
    float new_density = (100.0 * bitstream_nbits) / maxbits;
    //printf("Autocomputing density:  new density is %f (msg_bits=%d, maxbits=%f)\n", new_density, msg_nbits, maxbits);
    // Seems redundant now:
    if (new_density > 100.0) {
      jel_log(cfg, "ijel_stuff_message: Not enough space to stuff!  (New density = %f, msg_bits=%d, maxbits=%f)\n", new_density, msg_nbits, maxbits);
      jelbs_destroy(&bs);
      return JEL_ERR_MSG_OVERFLOW;
    }
    cfg->mcu_density = ceil(new_density);
    if (cfg->mcu_density < 1) cfg->mcu_density = 1;
    if (cfg->mcu_density < 100) cfg->mcu_density += 1;
    // printf("Autocomputing density:  new density is %d\n", cfg->mcu_density);
    jelbs_set_density( bs, cfg->mcu_density );
    cfg->nmcus = (int) floor( (cfg->mcu_density * cfg->maxmcus) / 100.0 );
    // printf("nmcus = %d\n", cfg->nmcus);
  }

  // This can't be done before we understand the image parameters:
  if (!ijel_create_mcu_map(cfg, compnum)) {
    jel_log(cfg, "ijel_stuff_message: Was not able to initialize the MCU map.  Density = %d\n", cfg->mcu_density);
    jelbs_destroy(&bs);
    return JEL_ERR_CREATE_MCU;
  }

  // Initialize state for bit stuffing:
  cfg->mcu_index = 0;

  if (cfg->debug_mcu >= 0) {
    printf("-----  Quant table:  -----\n");
    ijel_print_qvalues(cfg);
    printf("^^^^^  Quant table:  ^^^^^\n\n");
  }

  jelbs_compute_checksum(bs);


  if (jel_verbose) {
    JEL_LOG(cfg, 2, "ijel_stuff_message:  >>>>   bitstream before embedding:\n");
    jelbs_describe(cfg, bs, 2);
  }

  /* For embedding (stuff), I think all we'll need to do is turn off
     'first'.  This will override the embedding of the bitstream
     header, and will simply stuff all data bytes into the frame; */

  if (!cfg->embed_bitstream_header) {
    first = FALSE;
    JEL_LOG(cfg, 1, "ijel_stuff_message: Not embedding the header, just raw bits.\n");
  }

  ijel_reset_freqs(cfg);
  //  if (i > 0) j = CFG_RAND() % (i+1);
  if (cfg->seed) ijel_permute_freqs(cfg);

  nm = -1;  // Counter for the active MCUs
  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight; blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    /* This code iterates over MCUs, and is a loop over offset_y and
       blocknum.  This is a natural place to randomly select MCUs. */

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {
      /* The above loop used to have an end-of-message condition.  I
       * have removed that for now, but we should think about how to
       * handle that. */

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
#if USE_PRN_CACHE
	JEL_LOG(cfg, 4, "MCU %8d (%c): prn calls=%d \n", all_mcus, cfg->mcu_flag[all_mcus] ? '*' : 'x', cfg->prn_cache->ncalls);
else
	JEL_LOG(cfg, 4, "MCU %8d (%c): ", all_mcus, cfg->mcu_flag[all_mcus] ? '*' : 'x');
#endif
	if (cfg->seed) ijel_permute_freqs(cfg);

	if (nbits_in < msg_nbits) {
	  /* Grab the next MCU, get the frequencies to use, and insert
	   * one or more bits: */
	  mcu =(JCOEF*) row_ptrs[offset_y][blocknum];

	  if (cfg->mcu_flag[ all_mcus ]) nm++;
	
	  if (jel_verbose) maybe_describe_mcu(cfg, mcu, all_mcus, nm, "Before");

	  if (!first) nb = ijel_insert_bits(cfg, bs, mcu);
	  else {
	    // The first MCU gets an 8-bit density number:
	    if (cfg->set_dc >= 0) {
	      int v1 = cfg->set_dc - 128;
	      mcu[0] = (v1 * DCTSIZE) / cfg->dc_quant;   // Always squash if requested.
	    }

	    nb = ijel_insert_density(cfg, bs, mcu, chan);
	    first = false;
	  }

	  if (jel_verbose) maybe_describe_mcu(cfg, mcu, all_mcus, nm, "After");

	  nbits_in += nb;
	  if ( nb > 0 ) mcu_count++;
	  /* Don't use this MCU unless it's well-behaved; If random MCU
	   * selection is enabled, also test to see whether this is an
	   * MCU we want to modify. */
	}
	all_mcus++;
      }
    }
    if ( cfg->mcu_index > cfg->maxmcus ) 
      jel_log(cfg, "ijel_stuff_message: MCU map overflow!  (%d vs %d)\n", cfg->mcu_index, cfg->maxmcus);
  }
  
  if (jel_verbose) {
    JEL_LOG(cfg, 1, "ijel_stuff_message:    END OF MAIN PROCESSING LOOP\n");
    JEL_LOG(cfg, 2, "ijel_stuff_message:  <<<<<   bitstream after embedding:\n");
    jelbs_describe(cfg, bs, 2);
  }

  /* Actual size of message (not including density and length): */
  k = jelbs_get_length(bs);

  jelbs_destroy(&bs);

  // k = (nbits_in / 8) - 4;
  
  if (ecc) {
    /* ECC sets up a temporary buffer for decoding, so free it: */
    free(message);
    /* If we got here, we were successful and should set the return
     * value to be the number of bytes of plaintext that were
     * embedded: */
    k = plain_len;
  }
  
  ijel_destroy_mcu_map(cfg);
  return k;
}







// The jel_bitstream object is simply a convenient container for a lot
// of ephemeral state (indices and bit pointers).

int ijel_extract_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu, int chan) {
  int j, k, v, val, nbits, mask;
  int nfreqs = 4;
  int bits_per_freq = 2;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  nbits = 0;
  flist = cfg->freqs.in_use;  // ijel_select_freqs(cfg);

  /* for each byte in the MCU, insert it at the appropriate
     frequencies: */
  for (j = 0; j < nfreqs; j++) {
    //    val = mcu[ flist[j] ] + 1;
    val = INVXFORM(mcu[ flist[j] ]);

    mask = 1 << (bits_per_freq-1);
    for ( k = 0; k < bits_per_freq; k++) {
      v = (val & mask) ? 1: 0;
      jelbs_set_next_bit(stream, v);
      mask = mask >> 1;
      nbits++;
    }

  }
  // Now reinitialize the active MCU map to reflect the observed
  // density:
  cfg->mcu_density = (int) stream->density;
  if (cfg->mcu_density > 100) return -1;
  cfg->nmcus = (int) ( (cfg->mcu_density * cfg->maxmcus) / 100.0 );
  JEL_LOG(cfg, 2, "ijel_extract_density: Extracted density %d (%d bits) on channel %d\n", cfg->mcu_density, chan);

  ijel_select_mcus( cfg, cfg->components[chan] );

  return nbits;
}




int ijel_extract_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, v, val, nbits, mask, rc, fail;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  nbits = 0;
  fail = 0;
  if ( cfg->mcu_flag[ (cfg->mcu_index)++ ] ) {

    flist = cfg->freqs.in_use;   // ijel_select_freqs(cfg);

    /* for each byte in the MCU, insert it at the appropriate
       frequencies: */
    for (j = 0; j < cfg->freqs.nfreqs; j++) {
      val = INVXFORM( mcu[ flist[j] ] );

      mask = 1 << (cfg->bits_per_freq-1);

      for ( k = 0; k < cfg->bits_per_freq; k++) {
        v = (val & mask) ? 1: 0;
        rc = jelbs_set_next_bit(stream, v);
	if (rc == -1) fail = cfg->mcu_index - 1;
        mask = mask >> 1;
        nbits++;
      }
    }
  }
  /* Need a better failure indication here - return code should be
     negative. */
  if (fail > 0) jel_log(cfg, "ijel_extract_bits: failed at MCU %d\n", fail);

  return nbits;
}


/* Extract a message from the DCT info,
 *
 * compnum:  0=Y, 1=U, 2=V.
 */

int ijel_unstuff_message(jel_config *cfg, int chan) {
  jelbs *bs;

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);
  int compnum = cfg->components[chan];
  // Really an offset into the cfg->data buffer, but this is set up to
  // point to each "new" segment:
  unsigned char *message = cfg->data_ptr[chan];
  int mcu_count = 0;
  int all_mcus = 0;
  int msg_nbytes = 0;
  int msg_nbits = 0;
  int got_length = 0;         /* For now, we will always embed 4 bytes of message length first. */
  int nbits_out = 0;
  int nb;
  int blk_y, bheight, bwidth, offset_y, i, j, k, nm;
  JDIMENSION blocknum; // , MCU_cols;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;
  int fDoECC = jel_getprop(cfg, JEL_PROP_ECC_METHOD) == JEL_ECC_RSCODE ? 1 : 0;
  int status = 0;
  int first = true;   // The next MCU we process will be the first one.
  /* need to be able to know what went wrong in deployments */
  int debug = (cfg->logger != NULL);

  int max_nbytes;
  
  /* This uses the source quant tables, which is fine: */
  JEL_LOG(cfg, 1, "ijel_unstuff_message: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    if (compnum == YCOMP) j = 0;
    else j = 1;
    /* In "quant_tbl_ptrs[0]", is 0 a component index?? */
    int ret = ijel_get_freq_indices( cinfo->quant_tbl_ptrs[j], fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    if ( fspec->nfreqs <= 0 ) fspec->nfreqs = ret;
  }

  if (jel_verbose) {
    JEL_LOG(cfg, 3, "ijel_get_freq_indices: ");
    for (j = 0; j < fspec->nfreqs; j++) JEL_LOG(cfg, 3, "%d ", fspec->freqs[j]);
    JEL_LOG(cfg, 3, "\n");
  }

  cfg->maxmcus = ijel_max_mcus(cfg, compnum); // only one component

  bs = jelbs_create(cfg, cfg->capacity[chan]);      // points to data buffer
  /* This is the maximum message size. */
  msg_nbytes = cfg->data_lengths[chan];

  JEL_LOG(cfg, 3, "ijel_unstuff_message: Creating new bitstream object 0x%lx with a capacity of %d.\n",
	  (unsigned long) bs, msg_nbytes);
  
  /* Create the initial bitstream to be filled.  cfg->len contains the
   * max length of the data buffer:
   */ 

  if ( cinfo->err->trace_level > 0 && debug && jel_verbose) {
    JEL_LOG(cfg, 1, "(:components #(");
    for (i = 0; i < fspec->nfreqs; i++) JEL_LOG(cfg, 1, "%d ", fspec->freqs[i]);
    JEL_LOG(cfg, 1, "))\n");
  }

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;

  /* Initialize msg_nbytes to some positive value.  We will reset this
     once we get the length in: */
  
  JEL_LOG(cfg, 2, "ijel_unstuff_message: msg_nbytes=%d, cfg->len=%d\n",
	   msg_nbytes, cfg->capacity[chan]);
	  
  k = 0;
  nm = -1;

  if (cfg->debug_mcu >= 0) {
    printf("-----  Quant table:  -----\n");
    ijel_print_qvalues(cfg);
    printf("^^^^^  Quant table:  ^^^^^\n\n");
  }
  
  msg_nbits = bs->nbits;
  // This can't be done before we understand the image parameters:
  if (!ijel_create_mcu_map(cfg, compnum))
    return JEL_ERR_CREATE_MCU;

  if (!cfg->embed_bitstream_header) {
    
    first = FALSE;
    /* Let density be defined by the config - usually comes from the
       -mcudensity command line switch: */
    bs->density = cfg->mcu_density;       // bs->density = 100; /* Let's not do this */
    got_length = 1;

    /* Ultimately comes from cfg->len and should be from the -length
       command line switch in this case: */
    bs->msgsize = msg_nbytes;
    JEL_LOG(cfg, 1, "ijel_unstuff_message: Not checking for bitstream header.  Density = %d, msgsize = %d\n", bs->density, bs->msgsize);
  }

  if (jel_verbose) {
    JEL_LOG(cfg, 2, "ijel_unstuff_message:  >>>>>   bitstream 0x%lx before extraction:\n", (unsigned long) bs);
    jelbs_describe(cfg, bs, 2);
  }

  /* Don't select MCUs here, since it will invoke the PRN and screw up
     the ordering.  Since density is transmitted in the image, we will
     select MCUs only after density is discovered. */

  // Initialize state for bit stuffing:
  cfg->mcu_index = 0;

  ijel_reset_freqs(cfg);
  if (cfg->seed) ijel_permute_freqs(cfg);

  for (blk_y = 0; blk_y < bheight; blk_y += compptr->v_samp_factor) {

    row_ptrs = ((cinfo)->mem->access_virt_barray) 
      ( (j_common_ptr) cinfo, comp_array, (JDIMENSION) blk_y,
        (JDIMENSION) compptr->v_samp_factor, FALSE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {
      for (blocknum=0; blocknum < (JDIMENSION) bwidth;  blocknum++) {
#if USE_PRN_CACHE
	JEL_LOG(cfg, 4, "MCU %8d (%c): prn calls = %d \n", all_mcus, cfg->mcu_flag[all_mcus] ? '*' : 'x', cfg->prn_cache->ncalls);
#else
	JEL_LOG(cfg, 4, "MCU %8d (%c): ", all_mcus, cfg->mcu_flag[all_mcus] ? '*' : 'x');
#endif
	if (cfg->seed) ijel_permute_freqs(cfg);

	if (nbits_out < msg_nbits) {
	  mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
	  if (cfg->mcu_flag[ all_mcus ]) nm++;

	  if (!first) nb = ijel_extract_bits(cfg, bs, mcu);
	  else {
	    JEL_LOG(cfg, 2, "ijel_unstuff_message: about to extract density: \n");
	    nb = ijel_extract_density(cfg, bs, mcu, chan);
	    JEL_LOG(cfg, 2, "\nijel_unstuff_message: Density (%d) extracted (nbits_out=%d, nb=%d)\n", bs->density, nbits_out, nb);
	    if (nb < 0 || bs->density == 0) {
	      if (nb /* > 0 */)
		jel_log(cfg, "ijel_unstuff_message: invalid checksum in bitstream (%x)\n", bs->checksum);
	      jelbs_destroy(&bs);
	      ijel_destroy_mcu_map(cfg);
	      return JEL_ERR_CHECKSUM;
	    }
	    if (jel_verbose) {
	      JEL_LOG(cfg, 5, "ijel_unstuff_message:  <<<<<   bitstream after density extraction:\n");
	      jelbs_describe(cfg, bs, 5);
	    }
	    first = false;
	  }

	  /* Please think of a wrapper for this: */
	  if (jel_verbose) maybe_describe_mcu(cfg, mcu, all_mcus, nm, "After");

	  nbits_out += nb;
	  if ( nb > 0 ) mcu_count++;

	  if ( !got_length ) {
	    if (jel_verbose) {
	      JEL_LOG(cfg, 5, "ijel_unstuff_message:  =====   bitstream before got_length:\n");
	      jelbs_describe(cfg, bs, 5 );
	    }
	  }
	  if ( !got_length && jelbs_got_length( bs ) ) {
	    got_length = 1;
	    if ( jel_verbose ) {
	      JEL_LOG(cfg, 3, "ijel_unstuff_message: got_length = %d ; jelbs_got_length(bs) = %d\n", got_length, jelbs_got_length(bs));
	    }
	
	    if (cfg->embed_bitstream_header) msg_nbytes = jelbs_get_length( bs );

	    if (!cfg->embed_bitstream_header || jelbs_validate_checksum( cfg, bs ) ) {
	      JEL_LOG(cfg, 3, "ijel_unstuff_message: checksum OK in bitstream (%x)\n", bs->checksum);
	    } else {
	      jel_log(cfg, "ijel_unstuff_message: invalid checksum in bitstream (%x)\n", bs->checksum);
	      JEL_LOG(cfg, 2, "ijel_unstuff_message:  <<<<<   bitstream at invalid checksum:\n");
	      jelbs_describe(cfg, bs, 2);
	      jelbs_destroy(&bs);
	      ijel_destroy_mcu_map(cfg);
	      return JEL_ERR_CHECKSUM;
	    }

	    msg_nbits = (sizeof(bs->density) + sizeof(bs->msgsize) + sizeof(bs->checksum) + (size_t) msg_nbytes) * 8;

	    JEL_LOG(cfg, 2, "ijel_unstuff_message: Got length!  msg_nbytes = %d\n", msg_nbytes);
	    cfg->mcu_density = jelbs_get_density(bs);
	  
	    if (cfg->mcu_density <= 0 || cfg->mcu_density > 100) {
	      jel_log (cfg, "ijel_unstuff_message: bogus density %d \n", cfg->mcu_density);
	      jelbs_destroy(&bs);
	      ijel_destroy_mcu_map(cfg);
	      return JEL_ERR_NOMSG;
	    }

	    max_nbytes = (cfg->maxmcus) * (cfg->bits_per_freq) * (fspec->nfreqs) / 8;

	    if (msg_nbytes == 0) {
	      jel_log (cfg, "ijel_unstuff_message: Empty message.\n", msg_nbytes, max_nbytes);
	      jelbs_destroy(&bs);
	      ijel_destroy_mcu_map(cfg);
	      return 0;
	    }

	    if (msg_nbytes < 0 || msg_nbytes > max_nbytes) {
	      jel_log (cfg, "ijel_unstuff_message: bogus value for msg_nbytes %d (max_nbytes=%d)\n", msg_nbytes, max_nbytes);
	      jelbs_destroy(&bs);
	      ijel_destroy_mcu_map(cfg);
	      return JEL_ERR_MSG_OVERFLOW;
	    }
	  }
        
	  if (!fDoECC && cfg->nPrefilter == k && cfg->prefilter_func /* != NULL */) {
	    if ((*cfg->prefilter_func) (message, (size_t) msg_nbytes) /* != 0 */) {
	      cfg->len = msg_nbytes;
	      ijel_destroy_mcu_map(cfg);
	      return 0;   // Should this be an error code?
	    }
	  }

	}
	all_mcus++;
      }
    }
  }

  if ( jel_verbose ) {
    JEL_LOG(cfg, 1, "ijel_unstuff_message:    END OF MAIN PROCESSING LOOP\n");
    JEL_LOG(cfg, 1, "ijel_unstuff_message: Processed %d MCUs.\n", all_mcus);
    JEL_LOG(cfg, 2, "ijel_unstuff_message:  <<<<<   bitstream after extraction:\n");
    jelbs_describe(cfg, bs, 2);

    JEL_LOG(cfg, 2, "ijel_unstuff_message: embedded length = %d bytes\n", msg_nbytes);
  }

  for (int m = 0; m < msg_nbytes; m++) message[m] = bs->msg[m];
  cfg->data_lengths[chan] = msg_nbytes;


  //  k = cfg->len = msg_nbytes;
  k = msg_nbytes;
  if (fDoECC) {
    /* If we have reached here, we are using rscode for Reed-Solomon
     * error correction.  The codeword is in 'message', obtained from
     * cfg->data, but must be RS decoded to reconstruct the original
     * plaintext.  The value of k needs to be ceiling'ed up to the
     * nearest block to get all of the ECC blocks.
     */
    int truek;
    unsigned char *raw = 0;
    truek = ijel_ecc_block_length(msg_nbytes);
    JEL_LOG(cfg, 2, "ijel_unstuff_message: truek = %d, k = %d, %d\n", truek, k, msg_nbytes);
	  

    if (truek <= 0) {
      ijel_destroy_mcu_map(cfg);
      return JEL_ERR_ECC; // non-ECC input
    }

    if (jel_verbose) {
      JEL_LOG(cfg, 2, "ijel_unstuff_message: ijel_ecc_length(%d) => %d\n", k, truek);
      JEL_LOG(cfg, 2, "ijel_unstuff_message: 1st 5 bytes of ECC data = %d %d %d %d %d\n", 
	      message[0], message[1], message[2], message[3], message[4]);
    }
    
    /* If we are not embedding length, then plaintext length is a
     * shared secret and we pass it: */
    raw = ijel_decode_ecc(message,  truek, &i);

    /* 'raw' is a newly-allocated buffer.  When should it be freed?? */
    if (raw) {
      JEL_LOG(cfg, 2,  "ijel_unstuff_message: ECC enabled, %d bytes of ECC data decoded into %d bytes of message.\n", k, i);
      
      k = i;

      /*
       * Because cfg->data == message (the original buffer), do _not_ free (message)!
       * Simply copy raw into the original buffer.
       */
      (void) memcpy(message, raw, (size_t) msg_nbytes);

      if (cfg->prefilter_func /* != NULL */)
      	status = (*cfg->prefilter_func) (message, (size_t) msg_nbytes);

      JEL_LOG(cfg, 3, "ijel_unstuff_message: 1st 5 bytes of plain text = %d %d %d %d %d\n", 
	       raw[0], raw[1], raw[2], raw[3], raw[4]);
 
      /* Raw was allocated above solely because of ECC.  Free it here. */
      free(raw);    // Some parameter settings cause a crash here.  Why?
    }
    else
      ijel_destroy_mcu_map(cfg);

    return JEL_ERR_ECC;
  }


  JEL_LOG(cfg, 2, "ijel_unstuff_message: msg_nbytes=%d\n", msg_nbytes);
  jel_describe(cfg, 2);

  jelbs_destroy(&bs);
  ijel_destroy_mcu_map(cfg);
  
  return status /* != 0 */ ? 0 : k;
}

/*
 * Simply dumps the contents of each MCU (DEFLATED freq. coefficients)
 * on stdout:
 */

int ijel_print_mcus(jel_config *cfg, int active_only) {

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = COMP; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  int blk_y, bheight, bwidth, offset_y, k;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;
  //  counts[0] = counts[1] = 0;

  /* Now we walk through the MCUs of the JPEG image. */
  k = 0;
  for (blk_y = 0; blk_y < bheight;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
        /* Grab the next MCU, get the frequencies to use, and insert a
         * byte: */
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
        if (active_only) {
          if ( cfg->mcu_flag[k] ) {
            printf("===== MCU %d (ACTIVE) =====\n", k);
            ijel_print_mcu(cfg, mcu, 1);
            printf("^^^^^^^^^^^^^^^^^^\n");
          }
        } else {
          if ( cfg->mcu_flag[k] ) printf("===== MCU %d (ACTIVE) =====\n", k);
          else printf("===== MCU %d (inactive)  =====\n", k);
          ijel_print_mcu(cfg, mcu, 1);
          printf("^^^^^^^^^^^^^^^^^^\n");
        }
	k++;
      }
    }
  }
  
  return k;
}



/*
 * ijel_set_lsbs(jel_config *cfg, int *mask, JCOEF *mcu): Use the
 * specified 64-element mask array (1 element per DCT component) to
 * clear the least significant bits of the frequency components listed
 * in mask, where mask is a 64-element array of ints.  If mask[i] ==
 * 1, then freq[i]'s LSB is cleared to 0.  If mask[i] == 2, then
 * freq[i]'s LSB is set to 1.  If mask[i] == 0, then freq[i]'s LSB is
 * left untouched.
 */
#define LSB_CLEAR 1
#define LSB_SET 2
#define LSB_DONT_TOUCH 0

static void ijel_set_mcu_lsbs(int *mask, JCOEF *mcu) {
  int val;
  int j;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  for (j = 1; j < 64; j++) {
    if (mask[j] != LSB_DONT_TOUCH) {
      val = mcu[j];
      if (mask[j] == LSB_CLEAR) val = val & 0xFE;
      else if (mask[j] == LSB_SET) val = val | 1;
      mcu[j] = val;
    }
  }
}


static void ijel_get_mcu_lsbs(JCOEF *mcu, int *counts) {
  int j, val;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  for (j = 1; j < 64; j++) {
    val = mcu[j];
    if (val & 1) counts[1]++;
    else counts[0]++;
  }
}


int ijel_set_lsbs(jel_config *cfg, int *mask) {

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = COMP; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  int blk_y, bheight, bwidth, offset_y, k;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;

  k = 0;
  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
        /* Grab the next MCU, get the frequencies to use, and insert a
         * byte: */
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
	ijel_set_mcu_lsbs(mask, mcu);
	k++;
      }
    }
  }
  
  return 0;
}


int ijel_get_lsbs(jel_config *cfg, int *counts) {

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = COMP; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  int blk_y, bheight, bwidth, offset_y, k;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;
  counts[0] = counts[1] = 0;

  /* Now we walk through the MCUs of the JPEG image. */
  k = 0;
  for (blk_y = 0; blk_y < bheight;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {

      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
        /* Grab the next MCU, get the frequencies to use, and insert a
         * byte: */
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
	ijel_get_mcu_lsbs(mcu, counts);
	k++;
      }
    }
  }
  
  return k;
}



static
void ijel_print_qtable(jel_config *c, JQUANT_TBL *a) {
  int i;

  for (i = 0; i < DCTSIZE2; i++) {
    if (i % 8 == 0) jel_log(c, "\n");
    jel_log(c, "%4d ", a->quantval[i]);
  }
  jel_log(c, "\n");
}



void ijel_log_qtables(jel_config *c) {
  struct jpeg_compress_struct *comp;
  struct jpeg_decompress_struct *decomp;
  JQUANT_TBL *qptr;
  int i;

  decomp = &(c->srcinfo);

  jel_log(c, "Quant tables for source:\n");
  for (i = 0; i < NUM_QUANT_TBLS; i++) {
    qptr = decomp->quant_tbl_ptrs[i];
    if (qptr != NULL) {
      jel_log(c, "%lx\n", (long unsigned int) qptr);
      ijel_print_qtable(c, qptr);
    }
  }
  jel_log(c, "\n\n");

  comp = &(c->dstinfo);

  jel_log(c, "Quant tables for destination:\n");
  for (i = 0; i < NUM_QUANT_TBLS; i++) {
    qptr = comp->quant_tbl_ptrs[i];
    if (qptr != NULL) {
      jel_log(c, "%lx\n", (long unsigned int) qptr);
      ijel_print_qtable(c, qptr);
    }
  }
  jel_log(c, "\n");
}
















/* ================   Here be dead code:    ================   */



#if 0

void ijel_log_hufftables(jel_config *c) {
  return;
}


void ijel_buffer_dump( unsigned char *data, int nbytes) {
  int i;
  for (i = 0; i < nbytes; i++) printf(" %d ", data[i]);
  printf("\n");
}

/* Think about eliminating this - since the DC components appear in
   mcu[0] (and not the DPCM code), each MCU's DC value can be treated
   independently on the fly. */
static
int ijel_save_dc_components(jel_config *cfg) {
  /* Returns the number of admissible MCUs */
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  //  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  int component=COMP;
  jpeg_component_info *compptr;
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  JQUANT_TBL *qtable;
  int dc_quant; 
  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = component; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  //  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;
  int k = 0;  /* MCU counter */
  int dc;

  compptr = &(cinfo->comp_info[0]); // per-component information
  qtable = compptr->quant_table;
  dc_quant = qtable->quantval[0];
  cfg->dc_quant = dc_quant;         // Save this for later

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;

  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight; blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor;  offset_y++) {
      for (blocknum=0; blocknum < (JDIMENSION) bwidth; blocknum++) {
	if (cfg->set_dc >= 0 && cfg->mcu_flag[k]) cfg->dc_values[k] = cfg->set_dc;
	else {
	  mcu =(JCOEF*) row_ptrs[offset_y][blocknum];
	  dc = (mcu[0] * dc_quant)/DCTSIZE + 128;
	  cfg->dc_values[k] = dc;

	}
	// printf("%d ", cfg->dc_values[k]);
	
	k++;
      }
    }
  }
  
  return k;
}



/*
 * Returns the l2 norm of the AC energy EXCLUDING those frequencies
 * that will be used for embedding.
 */

static int dc_value( jel_config *cfg, JCOEF *mcu) {
  struct jpeg_decompress_struct *info = &(cfg->srcinfo);
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  int dc_quant;
  compptr = &(info->comp_info[0]); // per-component information
  qtable = compptr->quant_table;
  dc_quant = qtable->quantval[0];

  return (mcu[0] * dc_quant)/DCTSIZE + 128.0;
}

/***********************************************************************/

/* insert_byte and extract_byte are the atomic encoding and decoding
   operations.  This is where we have room to experiment with higher
   or lower bit dispersal.  Right now, we ALWAYS spread one byte
   across 4 frequency components.  Thus, the effect of using nfreq > 4
   is to allow better randomization of the frequency usage, but at the
   command line level, there is no control over the number of bits
   used per frequency component. 
*/


/* More information could be stored in Message structs, e.g., how many
 * bits per freq. to use.
 */

static void insert_byte( unsigned char v, int *freq, JCOEF *mcu ) {
  mcu[ freq[0] ] = (0x3 &  v      );
  mcu[ freq[1] ] = (0x3 & (v >> 2));
  mcu[ freq[2] ] = (0x3 & (v >> 4));
  mcu[ freq[3] ] = (0x3 & (v >> 6));
}


static unsigned char extract_byte( int *freq, JCOEF *mcu ) {
  return
    (  0x03 &  mcu[ freq[0] ])       |
    (  0x0C & (mcu[ freq[1] ] << 2)) |
    (  0x30 & (mcu[ freq[2] ] << 4)) |
    (  0xC0 & (mcu[ freq[3] ] << 6));
}

// https://stackoverflow.com/questions/3599160/how-to-suppress-unused-parameter-warnings-in-c

#define _UNUSED(x) (void)(x)

static
int ijel_usable_mcu(jel_config *cfg, JCOEF *mcu) {
  _UNUSED (cfg);
  _UNUSED (mcu);

  // int x = dc_value(cfg, mcu);
  //  JEL_LOG(cfg, " DC = %f\n", x);
  return ( // 1 ||
          (
           1 // x > 15 && x < 240
           //	    && ac_energy(cfg, mcu) < cfg->ethresh
           ));
}

// Old version:
int ijel_image_capacity(jel_config *cfg) {
  int compnum = COMP;  // change this when we start using chroma.
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jpeg_component_info *compptr = cinfo->comp_info + compnum;
  int bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  int bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;
  int cap = bwidth * bheight * compptr->v_samp_factor * cfg->bits_per_freq * cfg->freqs.nfreqs;

  // Is this related to YUV sampling? 
  //  cap = (2*cap) / 3;

  JEL_LOG(cfg, 1, "bwidth = %d, bheight = %d, v_samp_factor = %d, capacity = %d.\n", bwidth, bheight, compptr->v_samp_factor, cap);
  
  return cap;
  //  return (bwidth * bheight * cfg->bits_per_freq * cfg->freqs.nfreqs);

}

#endif



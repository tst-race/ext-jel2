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
#include "jel/jel.h"
#include "jel/ijel-ecc.h"
#include "jel/ijel.h"


/*
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
  /* Ephemeral state for bit stuffing / unstuffing - bs==bitstream */
  int bit;               /* "active" bit counter */
  int nbits;             /* Total number of bits in message */
  int bufsize;           /* Buffer size = maximum number of message + length bytes */
  unsigned char density; /* MCU density - can be in [1,100] */
  unsigned short msgsize;
  unsigned char *msg;
} jelbs;

/*
 * The jelbs struct is ASSUMED to hold an integral number of 8-bit
 * bytes.  That is, we are not allowed to encode partial bytes.
 * Hence, nbits should always be 8 * nbytes.  Might be a little
 * redundant, but it's easier to inspect.
 */

void jelbs_describe( jelbs *obj ) {
  /* Print a description of the bitstream */
  printf("bit:        %d\n", obj->bit);
  printf("nbits:      %d\n", obj->nbits);
  printf("bufsize:    %d\n", obj->bufsize);
  printf("density:    %d\n", obj->density);
  printf("msgsize:    %d\n", obj->msgsize);
  //  printf("message:    %s\n", obj->message);
}


/* bitstream operations: This API supports bitwise stuffing and
 *  unstuffing into MCUs. */

int jelbs_reset(jelbs *obj) {
  /* Reset the bit stream to its initial state */
  obj->bit = 0;
  obj->nbits = (obj->msgsize + sizeof(obj->density) + sizeof(obj->msgsize)) * 8;
  return 0;
}



int jelbs_set_bufsize(jelbs *obj, int n) {
  /* For "empty" bit streams, set length explicitly. */
  obj->bufsize = n;
  jelbs_reset( obj );
  return obj->nbits;
}

int jelbs_set_msgsize(jelbs *obj, unsigned short n) {
  /* For "empty" bit streams, set length explicitly. */
  obj->msgsize = n;
  jelbs_reset( obj );
  return obj->nbits;
}



/* Some confusion here, in that we depend on msg being a
 * zero-terminated string, but empty buffers can also be used.  It's
 * probably not good to depend on a null-terminated msg arg:
 */

jelbs *jelbs_create_from_string(unsigned char *msg) {
  /* Creates and returns a bit stream object from a given string. */
  jelbs* obj = (jelbs*) malloc(sizeof(jelbs));

  obj->msgsize = strlen( (const char*) msg);
  obj->bufsize = obj->msgsize;
  obj->msg = msg;
  jelbs_reset(obj);
  return obj;
}


int jelbs_copy_message(jelbs *dst, char *src, int n) {
  if (memmove(dst->msg, src, n)) return n;
  else return 0;
}

jelbs *jelbs_create(int size) {
  /* Creates and returns a bit stream object from a requested message size. */
  jelbs* obj = (jelbs*) malloc(sizeof(jelbs));

  obj->msgsize = size;
  obj->bufsize = size + 1;
  obj->msg = malloc(obj->bufsize);
  jelbs_reset(obj);
  return obj;
}


/* Companion to jelbs_create(n): Assumes that the message buffer has
 * been allocated by the jelbs API. */
void jelbs_destroy(jelbs **obj) {
  free((*obj)->msg);
  free(*obj);
  *obj = NULL;
}


int jelbs_got_length(jelbs *obj) {
  return( obj->bit >= 8 * (sizeof(obj->density) + sizeof(obj->msgsize)) );
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
  int offset = (sizeof(obj->density) + sizeof(obj->msgsize));
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
  else             byte = obj->msg[byte_in_msg];

  int val = (mask & byte) >> bit_in_byte;

  if (k < 128) printf("%d", val);

  return val;
}
  


int jelbs_set_bit(jelbs *obj, int k, int val) {
  int offset =  (sizeof(obj->density) + sizeof(obj->msgsize));
  unsigned char *len = (unsigned char*) &(obj->msgsize);
  unsigned char byte;
  /* Set the k-th bit from the bitstream to the value 'val'.  First
   * compute offsets: */
  int bit_in_byte = k % 8;
  int byte_in_msg = (k / 8) - offset;

  /* Then mask in the bit: */
  unsigned char vmask = (val << bit_in_byte);
  unsigned char mask = 0xFF & ~(1 << bit_in_byte);
  /* Mask is now an unsigned char with the 'val' bit in the appropriate bit position */
  
  /* Extract the appropriate byte: */
  if (k < 8)       byte = obj->density; // This is in the first byte: density
  else if (k < 16) byte = len[0];
  else if (k < 24) byte = len[1];
  else             byte = obj->msg[byte_in_msg];

  /* Complement the mask, ANDing it with the byte.  Then or in the 'val' bit: */
  unsigned char new = (byte & mask) | vmask;

  /* Set the altered byte: */
  if (k < 8)       obj->density = new; // This is in the first byte: density
  else if (k < 16) len[0] = new;
  else if (k < 24) len[1] = new;
  else             obj->msg[byte_in_msg] = new;

  if (k < 128) printf("%d", val);

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
  if (obj->bit >= obj->nbits) return -1;
  else {
    result = jelbs_set_bit(obj, obj->bit, val);
    obj->bit++;
    return result;
  }
}



/* Hard-coded for now, but everywhere we see a component index, we
   have the potential of using a color component.  Component 0 is
   luminance = Y: */
#define YCOMP 0


/*
 * findFreqs: Given a quant table, find frequencies that have at
 * least 'nlevels' quanta.  Return an array of indices, starting at
 * the highest (most heavily quantized) and working toward the lowest,
 * but containing not more than nfreq component indices.  These
 * components will be used for encoding.
 *
 */

int ijel_find_freqs(JQUANT_TBL *q, int *i, int nfreq, int nlevels) {
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



/*
 * Returns an array containing the frequency indices to use for embedding.
 */

static
int *ijel_select_freqs( jel_config *cfg ) {
  int i, tmp;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* We should not be recomputing this each time!! */
  for (i = 0; i < fspec->maxfreqs; i++)
    fspec->in_use[i] = fspec->freqs[i];

  if (cfg->seed) {
    int j, n;
    n = fspec->maxfreqs;
    /* Fisher-Yates */
    for (i = 0; i < n; i++) {
      if (i > 0) j = CFG_RAND() % (i+1);
      else j = 0;
      if (j != i) {
	tmp = fspec->in_use[j];
	fspec->in_use[j] = fspec->in_use[i];
	fspec->in_use[i] = tmp;
      }
    }
    if(jel_verbose){
      jel_log(cfg, "ijel_select_freqs selected %d frequencies: [", fspec->nfreqs);
      for (i = 0; i < fspec->nfreqs; i++) jel_log(cfg, " %d", fspec->in_use[i]);
      jel_log(cfg, "]\n");
    }
  }
  return fspec->in_use;
}


static
void ijel_init_mcu_map(jel_config *cfg, int max_mcus) {
  int i;
  cfg->maxmcus =  max_mcus;
  cfg->nmcus = (int) ( (cfg->mcu_density * cfg->maxmcus) / 100.0 );
  cfg->mcu_list = (unsigned int*) malloc(sizeof(unsigned int) * (size_t) max_mcus);
  cfg->mcu_flag = (unsigned char*) malloc(sizeof(unsigned char) * (size_t) max_mcus);
  jel_log(cfg, "ijel_init_mcu_map: maxmcus set to %d, nmcus set to %d.\n", cfg->maxmcus, cfg->nmcus);
  for (i = 0; i < max_mcus; i++) {
    cfg->mcu_list[i] = (unsigned) i;
    cfg->mcu_flag[i] = 1;   /* 1 means the MCU is usable, 0 if not */
  }
}


static
int ijel_select_mcus( jel_config *cfg ) {
  int i, tmp;

  jel_log(cfg, "ijel_select_mcus maxmcus = %d, number of MCUs to use = %d, seed = %d\n",
	  cfg->maxmcus, cfg->nmcus, cfg->seed);
  for (i = 1; i < cfg->maxmcus; i++) {
    cfg->mcu_list[i] = (unsigned) i;
    cfg->mcu_flag[i] = 1;
  }
  /* MCU 0 is always used for density: */
  cfg->mcu_list[0] = 0;
  cfg->mcu_flag[0] = 1;
  
  if (cfg->seed && cfg->mcu_density != 100) {
    int j, n;
    n = cfg->maxmcus;   // Permute the whole list but 1
    /* Fisher-Yates algorithm: */
    for (i = 1; i < n; i++) {
      cfg->mcu_flag[i] = 0;
      
      if (i > 1) j = CFG_RAND() % (i+1);
      else       j = 1;
      
      if (j != i) {
	tmp = (int) cfg->mcu_list[j];
	cfg->mcu_list[j] = cfg->mcu_list[i];
	cfg->mcu_list[i] = (unsigned) tmp;
      }
    }
    
    /* Now turn on only the desired number of "active" MCUs, with
       respect to the above permutation: */
    // jel_log(cfg, "ijel_select_mcus mcu flags (%d): [", n);
    n = cfg->nmcus;
    for (j = 1; j < n; j++) {
      cfg->mcu_flag[ cfg->mcu_list[j] ] = 1;
    }
    /*
    for (j = 0; j < n; j++) {
      jel_log(cfg, " %d", cfg->mcu_flag[j]);
    }
    jel_log(cfg, "]\n");
    */
  }
  return cfg->nmcus;
}

/*
 * Returns the l2 norm of the AC energy EXCLUDING those frequencies
 * that will be used for embedding.
 */

#if 0
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
#endif


int ac_energy( jel_config *cfg, JCOEF *mcu ) {
  int val;
  int i, j, ok;
  int e = 0;
  jel_freq_spec *fspec = &(cfg->freqs);
  struct jpeg_decompress_struct *info = &(cfg->srcinfo);
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;

  compptr = &(info->comp_info[0]); // per-component information
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

#if 0
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
#endif

/***********************************************************************/


/*
 * Survey of MCU energies:
 */

int ijel_print_energies(jel_config *cfg) {

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = 0; /* Component (0 = luminance, 1 = U, 2 = V) */
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
  jel_log(cfg, "ijel_print_energies: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];

    fspec->nfreqs = ijel_find_freqs(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    fspec->init = 1;
  }

  if (jel_verbose) {
    jel_log(cfg, "ijel_find_freqs: ");
    for (j = 0; j < fspec->nfreqs; j++) jel_log(cfg, "%d ", fspec->freqs[j]);
    jel_log(cfg, "\n");
  }


  /* Check to see that we have at least 4 good frequencies.  This
     implicitly assumes that we are packing 8 bits per MCU.  We will
     want to change that in future versions. */ 
  if (fspec->nfreqs < 4) {
    if( debug ) {
      jel_log(cfg, "ijel_print_energies: Sorry - not enough good frequencies at this quality factor.\n");
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
  printf("# min,max energy = %d, %d\n", min_energy, max_energy);
  
  return 0;
}

// https://stackoverflow.com/questions/3599160/how-to-suppress-unused-parameter-warnings-in-c

#define _UNUSED(x) (void)(x)

static
int ijel_usable_mcu(jel_config *cfg, JCOEF *mcu) {
  _UNUSED (cfg);
  _UNUSED (mcu);

  // int x = dc_value(cfg, mcu);
  //  jel_log(cfg, " DC = %f\n", x);
  return ( // 1 ||
          (
           1 // x > 15 && x < 240
           //	    && ac_energy(cfg, mcu) < cfg->ethresh
           ));
}



int ijel_capacity(jel_config *cfg, int component) {
  /* Returns the number of admissible MCUs */

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = component; /* Component (0 = luminance, 1 = U, 2 = V) */
  /* need to be able to know what went wrong in deployments */
  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y, i;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;
  int capacity = 0;

  /* If not already specified, find a set of frequencies suitable for
     embedding 8 bits per MCU.  Use the destination object, NOT cinfo,
     which is the source: */
  jel_log(cfg, "ijel_capacity: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];

    fspec->nfreqs = ijel_find_freqs(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    jel_log(cfg, "ijel_capacity: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) jel_log(cfg, "%d ", fspec->freqs[i]);
    jel_log(cfg, "]\n");
  }

  /* Check to see that we have at least 4 good frequencies.  This
     implicitly assumes that we are packing 8 bits per MCU.  We will
     want to change that in future versions. */ 
  if (fspec->nfreqs < 4) {
    if( debug ) {
      jel_log(cfg, "ijel_capacity: Sorry - not enough good frequencies at this quality factor.\n");
    }
    return 0;
  }

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;

  compptr = cinfo->comp_info + compnum;

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
        if ( ijel_usable_mcu(cfg, mcu) ) capacity++;
      }
    }
  }
  
  return capacity;
}



unsigned char *ijel_maybe_init_ecc(jel_config *cfg, unsigned char *raw_msg, int *msglen, int *ecc) {
  int i;
  int raw_msg_len = *msglen;
  unsigned char *message = NULL;

  message = raw_msg; // by default.
  
  if (jel_getprop(cfg, JEL_PROP_ECC_METHOD) == JEL_ECC_RSCODE) {

    if (ijel_ecc_sanity_check(raw_msg, raw_msg_len)){
      jel_log(cfg, "ijel_stuff_message: FYI, sanity check failed.\n");
      /* iam asks: why do we carry on regardless? */
    }
    
    message = ijel_encode_ecc(raw_msg,  raw_msg_len, &i);

    if (!message) {
      message = raw_msg; /* No ecc */
    }  else {

      if (cfg->verbose > 1)
        jel_log(cfg, "ijel_stuff_message: 1st 5 bytes of ECC data = %d %d %d %d %d\n", 
                message[0], message[1], message[2], message[3], message[4]);
      
      if(jel_verbose){
        jel_log(cfg, "ijel_stuff_message: ECC enabled, %d bytes of message encoded in %d bytes.\n", raw_msg_len, i);
      }
      *msglen = i;
      *ecc = 1;
    }
  }
  return message;
}



// The jel_bitstream object is simply a convenient container for a lot
// of ephemeral state (indices and bit pointers).

// Insert the density byte (special case):

int ijel_insert_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, val, eom, v, nbits;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  eom = 0;
  nbits = 0;
  if ( cfg->mcu_flag[ (cfg->mcu_index)++ ] ) {
    flist = ijel_select_freqs(cfg);

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
      mcu[ flist[j] ] = val;
    }
    //    if (stream->bit < 128) printf("|");
  }
  return nbits;
}


// The jel_bitstream object is simply a convenient container for a lot
// of ephemeral state (indices and bit pointers).

int ijel_insert_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, val, eom, v, nbits;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  eom = 0;
  nbits = 0;
  if ( cfg->mcu_flag[ (cfg->mcu_index)++ ] ) {
    flist = ijel_select_freqs(cfg);

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
      mcu[ flist[j] ] = val;
    }
    //    if (stream->bit < 128) printf("|");
  }
  return nbits;
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
 */

int ijel_stuff_message(jel_config *cfg) {
  jelbs *bs;
  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  struct jpeg_compress_struct *dinfo = &(cfg->dstinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);

  /* Message to be embedded: */
  unsigned char *message = cfg->data;
  /* Authoritative length of the message: */
  int msglen = cfg->len;
  int nbits_in, msg_nbits;
  int first = true;  // The next MCU we process will be the first one...

  /* ECC variables: */
  unsigned char *raw = cfg->data;
  int ecc = 0;
  int plain_len = 0;

  /* This could use some cleanup to make sure that we really need all
   * these variables! */
  int compnum = 0; /* Component (0 = luminance, 1 = U, 2 = V) */

  /* need to be able to know what went wrong in deployments */
  int debug = (cfg->logger != NULL);
  int blk_y, bheight, bwidth, offset_y, i, k;
  //  JDIMENSION blocknum, MCU_cols;
  JDIMENSION blocknum;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JQUANT_TBL *qtable;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;
  //size_t block_row_size = (size_t) SIZEOF(JCOEF)*DCTSIZE2*cinfo->comp_info[compnum].width_in_blocks;

  if(jel_verbose){
    jel_log(cfg, "ijel_stuff_message: 1st 5 bytes of plain text = %d %d %d %d %d\n", 
            raw[0], raw[1], raw[2], raw[3], raw[4]);
  }

  plain_len = msglen; /* Save the plaintext length */

  /* Check to see if we want ECC turned on - potential leakage here? */
  message = ijel_maybe_init_ecc(cfg, raw, &msglen, &ecc);

  /* If needed, message and msglen have now been updated to reflect
     ECC-related expansion. */
  
  /* Use the length of the message to create a bitstream, then copy
   * message into the bitstream */
  bs = jelbs_create(msglen);
  jelbs_copy_message(bs, (char*) message, msglen);
  jelbs_set_msgsize(bs, msglen);
  jelbs_set_density(bs, (unsigned char) cfg->mcu_density);

  /* If not already specified, find a set of frequencies suitable for
     embedding 8 bits per MCU.  Use the destination object, NOT cinfo,
     which is the source: */
  jel_log(cfg, "ijel_stuff_message: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init) {
    /* If we explicitly set the output quality, then this will be
     * non-NULL, but otherwise we will need to get the tables from the
     * source: */
    qtable = dinfo->quant_tbl_ptrs[0];
    if (!qtable) qtable = cinfo->quant_tbl_ptrs[0];

    // Find frequencies with the appropriate properties:
    fspec->nfreqs = ijel_find_freqs(qtable, fspec->freqs, fspec->maxfreqs, fspec->nlevels);
    jel_log(cfg, "ijel_stuff_message: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) jel_log(cfg, "%d ", fspec->freqs[i]);
    jel_log(cfg, "]\n");
  }

  /* If requested, be verbose: */
  if ( debug && jel_verbose ) {     /* Do we want to use verbosity levels? */
    jel_log(cfg, "(:components #(");
    for (i = 0; i < fspec->nfreqs; i++) jel_log(cfg, "%d ", fspec->freqs[i]);
    jel_log(cfg, "))\n");
  }

  /* Need to double check the message length, to see whether it's
     compatible with the image.  Message gets truncated if it's longer
     than the number of MCU's in the luminance channel.  We will want
     to expand to the color components too:
  */
  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;
  //  printf("In theory, we can store %d bytes\n", bheight*bwidth);

  //  MCU_cols = cinfo->image_width / (cinfo->max_h_samp_factor * DCTSIZE);
  compptr = cinfo->comp_info + compnum;
  
  k = 0;
  nbits_in = 0;
  // msg_nbits = (msglen + sizeof(short)) * 8;
  msg_nbits = bs->nbits;
  
  if(jel_verbose) {
    jel_log(cfg, "ijel_stuff_message: embedded length = %d bytes\n", bs->msgsize);
  }

  // This can't be done before we understand the image parameters:
  ijel_init_mcu_map(cfg, bwidth * bheight * compptr->v_samp_factor);
  ijel_select_mcus(cfg);

  // Initialize state for bit stuffing:
  cfg->mcu_index = 0;
  
  //  printf("ijel_stuff_message bitstream before embedding:\n");
  //  jelbs_describe(bs);

  /* Now we walk through the MCUs of the JPEG image. */
  for (blk_y = 0; blk_y < bheight && nbits_in < msg_nbits;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ( (cinfo)->mem->access_virt_barray ) 
      ((j_common_ptr) cinfo,
       comp_array,
       (JDIMENSION) blk_y,
       (JDIMENSION) compptr->v_samp_factor,
       TRUE);

    /* This code iterates over MCUs, and is a loop over offset_y and
       blocknum.  This is a natural place to randomly select MCUs. */

    for (offset_y = 0; offset_y < compptr->v_samp_factor && nbits_in < msg_nbits;  offset_y++) {
      /* The above loop used to have an end-of-message condition.  I
       * have removed that for now, but we should think about how to
       * handle that. */

      for (blocknum=0; blocknum < (JDIMENSION) bwidth && nbits_in < msg_nbits; blocknum++) {
        /* Grab the next MCU, get the frequencies to use, and insert
         * one or more bits: */
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];

	// Here's where we could replace the byte-insertion logic with
	// a call to ijel_insert_bits():

	if (!first) nbits_in += ijel_insert_bits(cfg, bs, mcu);
	else {
	  // The first MCU gets an 8-bit density number:
	  nbits_in += ijel_insert_density(cfg, bs, mcu);
	  printf("Density (%d) inserted (nbits_in=%d)\n", bs->density, nbits_in);
	  first = false;
	}

        /* Don't use this MCU unless it's well-behaved; If random MCU
	 * selection is enabled, also test to see whether this is an
	 * MCU we want to modify. */
      }
    }
    if ( cfg->mcu_index > cfg->maxmcus ) jel_log(cfg, "MCU map overflow!  (%d vs %d)\n", cfg->mcu_index, cfg->maxmcus);
  }

  //  printf("ijel_stuff_message bitstream after embedding:\n");
  //  jelbs_describe(bs);

  jelbs_destroy(&bs);
  
  if (ecc) {
    /* ECC sets up a temporary buffer for decoding, so free it: */
    free(message);
    /* If we got here, we were successful and should set the return
     * value to be the number of bytes of plaintext that were
     * embedded: */
    k = plain_len;
  }
  
  return k;
}







// The jel_bitstream object is simply a convenient container for a lot
// of ephemeral state (indices and bit pointers).

int ijel_extract_density(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, v, val, nbits, mask;
  int nfreqs = 4;
  int bits_per_freq = 2;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  nbits = 0;
  if ( ! cfg->mcu_flag[ (cfg->mcu_index)++ ] ) printf("Error: the first mcu_flag is not 1?\n");
  else {

    flist = ijel_select_freqs(cfg);

    /* for each byte in the MCU, insert it at the appropriate
       frequencies: */
    for (j = 0; j < nfreqs; j++) {
      val = mcu[ flist[j] ];

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
    ijel_init_mcu_map( cfg, cfg->maxmcus );
    ijel_select_mcus( cfg );
  }

  return nbits;
}




int ijel_extract_bits(jel_config *cfg,  jelbs *stream, JCOEF *mcu) {
  int j, k, v, val, nbits, mask;
  int *flist;

  /* Set the end-of-message flag.  For now, we will need to insert 0
   * so that trailing bytes in the message will properly terminate the
   * string. */
  nbits = 0;
  if ( cfg->mcu_flag[ (cfg->mcu_index)++ ] ) {

    flist = ijel_select_freqs(cfg);

    /* for each byte in the MCU, insert it at the appropriate
       frequencies: */
    for (j = 0; j < cfg->freqs.nfreqs; j++) {
      val = mcu[ flist[j] ];

      mask = 1 << (cfg->bits_per_freq-1);
      for ( k = 0; k < cfg->bits_per_freq; k++) {
        v = (val & mask) ? 1: 0;
        jelbs_set_next_bit(stream, v);
        mask = mask >> 1;
        nbits++;
      }

    }
    //    if (stream->bit < 128) printf("|");
  }

  return nbits;
}


/* Extract a message from the DCT info- Consider adding "compnum" to
   the argument list: */

int ijel_unstuff_message(jel_config *cfg) {
  jelbs *bs;
  static int compnum = 0;  /* static?  Really?  This is the component number, 0=luminance.  */

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;
  jel_freq_spec *fspec = &(cfg->freqs);
  unsigned char *message = cfg->data;
  //  unsigned char *newbuf;
  int capacity = 0;

  //  int plain_len = 0;
  int msg_nbytes = 0;
  int msg_nbits = 0;
  int got_length = 0;         /* For now, we will always embed 4 bytes of message length first. */
  int nbits_out = 0;
  //int echo = 0;
  int blk_y, bheight, bwidth, offset_y, i, j, k;
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
  //size_t block_row_size = (size_t) SIZEOF(JCOEF)*DCTSIZE2*cinfo->comp_info[compnum].width_in_blocks;

  /* This uses the source quant tables, which is fine: */
  jel_log(cfg, "ijel_unstuff_message: fspec->nfreqs = %d; fspec->maxfreqs = %d\n", fspec->nfreqs, fspec->maxfreqs);
  if (!fspec->init)
    /* In "quant_tbl_ptrs[0]", is 0 a component index?? */
    fspec->nfreqs = ijel_find_freqs( cinfo->quant_tbl_ptrs[0], fspec->freqs, fspec->maxfreqs, fspec->nlevels);

  if (jel_verbose) {
    jel_log(cfg, "ijel_find_freqs: ");
    for (j = 0; j < fspec->nfreqs; j++) jel_log(cfg, "%d ", fspec->freqs[j]);
    jel_log(cfg, "\n");
  }

  bs = jelbs_create(cfg->len);      // points to data buffer

  /* This is the maximum message size. */
  msg_nbytes = cfg->len;
  
  /* Create the initial bitstream to be filled.  cfg->len contains the
   * max length of the data buffer:
   */ 

  if ( cinfo->err->trace_level > 0 && debug && jel_verbose) {
    jel_log(cfg, "(:components #(");
    for (i = 0; i < fspec->nfreqs; i++) jel_log(cfg, "%d ", fspec->freqs[i]);
    jel_log(cfg, "))\n");
  }

  bheight = (int) cinfo->comp_info[compnum].height_in_blocks;
  bwidth = (int) cinfo->comp_info[compnum].width_in_blocks;
  //  printf("In theory, we can store %d bytes\n", bheight*bwidth);

  //  MCU_cols = cinfo->image_width / (cinfo->max_h_samp_factor * DCTSIZE);
  compptr = cinfo->comp_info + compnum;

  /* Initialize msg_nbytes to some positive value.  We will reset this
     once we get the length in: */
  
  if(jel_verbose){
    jel_log(cfg, "ijel_unstuff_message: msg_nbytes=%d, cfg->len=%d\n",
            msg_nbytes, cfg->len);
  }
	  
  k = 0;

  msg_nbits = bs->nbits;
  // This can't be done before we understand the image parameters:
  ijel_init_mcu_map(cfg,  bwidth * bheight * compptr->v_samp_factor);
  // Don't do this here, since it will invoke the PRN and screw up the
  // ordering.  Since density is transmitted in the image, we will
  // select MCUs only after density is discovered.

  // ijel_select_mcus(cfg);

  //  printf("\nijel_unstuff_message bitstream before extraction:\n");
  //  jelbs_describe(bs);

  for (blk_y = 0; blk_y < bheight && nbits_out < msg_nbits;
       blk_y += compptr->v_samp_factor) {

    row_ptrs = ((cinfo)->mem->access_virt_barray) 
      ( (j_common_ptr) cinfo, comp_array, (JDIMENSION) blk_y,
        (JDIMENSION) compptr->v_samp_factor, FALSE);

    for (offset_y = 0; offset_y < compptr->v_samp_factor && nbits_out < msg_nbits;
         offset_y++) {
      for (blocknum=0; blocknum < (JDIMENSION) bwidth;  blocknum++) {
        mcu =(JCOEF*) row_ptrs[offset_y][blocknum];

	if (!first) nbits_out += ijel_extract_bits(cfg, bs, mcu);
	else {
	  nbits_out += ijel_extract_density(cfg, bs, mcu);
	  printf("Density (%d) extracted (nbits_out=%d)\n", bs->density, nbits_out);
	  first = false;
	}

        if ( !got_length && jelbs_got_length( bs ) ) {
          got_length = 1;
          msg_nbytes = jelbs_get_length( bs );
	  msg_nbits = (sizeof(bs->density) + sizeof(bs->msgsize) + msg_nbytes) * 8;

	  // printf("\nijel_unstuff_message: Got length!  msg_nbytes = %d\n", msg_nbytes);
	  // cfg->mcu_density = jelbs_get_density(bs);

        }
        
        if (!fDoECC && cfg->nPrefilter == k && cfg->prefilter_func /* != NULL */) {
          if ((*cfg->prefilter_func) (message, (size_t) msg_nbytes) /* != 0 */) {
            cfg->len = msg_nbytes;
            return 0;
          }
        }
      }
    }
  }

  //  printf("\nijel_unstuff_message bitstream after extraction:\n");
  //  jelbs_describe(bs);

  //  printf("nbits_out = %d; msg_nbytes = %d; msg_nbits = %d\n", nbits_out, msg_nbytes, msg_nbits);

  if (jel_verbose) printf ( "capacity = %d\n", capacity);

  if ( jel_verbose ){
    jel_log(cfg, "ijel_unstuff_message: embedded length = %d bytes\n", msg_nbytes);
  }

  memmove((char*) cfg->data, (char*) bs->msg, msg_nbytes);
  cfg->len = msg_nbytes;


  if (fDoECC) {
    /* If we have reached here, we are using rscode for Reed-Solomon
     * error correction.  The codeword is in 'message', obtained from
     * cfg->data, but must be RS decoded to reconstruct the original
     * plaintext.  The value of k needs to be ceiling'ed up to the
     * nearest block to get all of the ECC blocks.
     */
    int truek;
    unsigned char *raw = 0;
    truek = ijel_ecc_block_length(k);

    if (truek <= 0)
      return -1;		// non-ECC input

    if(jel_verbose){
      jel_log(cfg, "ijel_unstuff_message: ijel_ecc_length(%d) => %d\n", k, truek);
      jel_log(cfg, "ijel_unstuff_message: 1st 5 bytes of ECC data = %d %d %d %d %d\n", 
              message[0], message[1], message[2], message[3], message[4]);
    }

    /* If we are not embedding length, then plaintext length is a
     * shared secret and we pass it: */
    raw = ijel_decode_ecc(message,  truek, &i);

    /* 'raw' is a newly-allocated buffer.  When should it be freed?? */
    if (raw) {
    	if(jel_verbose){
        jel_log(cfg, "ijel_unstuff_message: ECC enabled, %d bytes of ECC data decoded into %d bytes of message.\n", k, i);
      }
      
      k = i;

      /*
       * Because cfg->data == message (the original buffer), do _not_ free (message)!
       * Simply copy raw into the original buffer.
       */
      (void) memcpy(cfg->data, raw, (size_t) msg_nbytes);

      if (cfg->prefilter_func /* != NULL */)
      	status = (*cfg->prefilter_func) (message, (size_t) msg_nbytes);

      if(jel_verbose){
        jel_log(cfg, "ijel_unstuff_message: 1st 5 bytes of plain text = %d %d %d %d %d\n", 
                raw[0], raw[1], raw[2], raw[3], raw[4]);
      }
 
      /* Raw was allocated above solely because of ECC.  Free it here. */
      free(raw);    // Some parameter settings cause a crash here.  Why?
    }
    else
      return -1;
  }

  cfg->len = msg_nbytes;
  if(jel_verbose){
    jel_log(cfg, "ijel_unstuff_message: msg_nbytes=%d\n", msg_nbytes);
  }

  jelbs_destroy(&bs);
  
  return status /* != 0 */ ? 0 : k;
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



#if 0

void ijel_log_hufftables(jel_config *c) {
  return;
}


void ijel_buffer_dump( unsigned char *data, int nbytes) {
  int i;
  for (i = 0; i < nbytes; i++) printf(" %d ", data[i]);
  printf("\n");
}

#endif

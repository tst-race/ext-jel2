/*
 * C. Connolly, SRI International - 8/25/2013
 *
 * Show the quant table for a specific jpeg quality value (0,100] and
 * islow dct.  Optionally show the frequency indices that permit the
 * use of N bits (where N is presented on the command line).
 */
#include <jel/jel.h>
#include <stdlib.h>

static int nfreqs = 4;
static int nlevels = 8;
int ijel_get_freq_indices(JQUANT_TBL *, int *, int, int);

static int quality = 80;
static int verbose = 0;
static int print_quanta = 0;
//static int print_bits = 0;
static int print_huff = 0;
static int print_blockdims = 0;
static int print_qtables = 0;
static int force_baseline = 1;
static int print_colorspace = 0;

static int usage (char *progname)
/* complain about bad command line */
{
  fprintf(stderr, "usage: %s [switches] <jpeg file>\n", progname);

  fprintf(stderr, "   -h:    Print huffman tables.\n");
  fprintf(stderr, "   -v:    Print quantization tables.\n");
  fprintf(stderr, "   -c:    Print colorspace info.\n");
  fprintf(stderr, "   -n M:  Use only M frequency components (default=4).\n");
  fprintf(stderr, "   -q N:  Use quality level N.\n");
  fprintf(stderr, "   -l K:  What frequencies would\n");
  fprintf(stderr, "          allow us to pack K levels\n");
  fprintf(stderr, "          in each frequency component?\n");
  fprintf(stderr, "   -a     Print the number of quanta for each frequency component.\n");
  //  fprintf(stderr, "   -b     Print the number of bits supported by each frequency component.\n");
  fprintf(stderr, "   -bd    Print the block dimensions of this file.\n");
  fprintf(stderr, "   -qt    Print the quantization tables for this file.\n");
  exit(-1);
}


/* Parse optional switches.
 * Returns argv[] index of first file-name argument (== argc if none).
 * Any file names with indexes <= last_file_arg_seen are ignored;
 * they have presumably been processed in a previous iteration.
 * (Pass 0 for last_file_arg_seen on the first or only iteration.)
 * for_real is FALSE on the first (dummy) pass; we may skip any expensive
 * processing.
 */

static int parse_switches (int argc, char **argv)
{
  int argn;
  char * arg;

  // printf("argc = %d\n", argc);
  if (argc <= 1) usage(argv[0]);
  
  /* Scan command line options, adjust parameters */

  for (argn = 1; argn < argc; argn++) {
    arg = argv[argn];
    // printf("Arg %d = %s\n", argn, arg);
    if (arg[0] != '-') break;
    arg++;			/* advance past switch marker character */

    if (arg[0] == 'v') verbose = 1;
    else if (arg[0] == 'a') print_quanta = 1;
    else if (arg[0] == 'b' && arg[1] == 'd') print_blockdims = 1;
    else if (arg[0] == 'q' && arg[1] == 't') print_qtables = 1;
    else if (arg[0] == 'c') print_colorspace = 1;
    else if (arg[0] == 'q') {
      if (++argn >= argc) usage(argv[0]);
      quality = strtol(argv[argn], NULL, 10);
    } else if (arg[0] == 'l') {
      if (++argn >= argc) usage(argv[0]);
      nlevels = strtol(argv[argn], NULL, 10);
    } else if (arg[0] == 'h') {
      print_huff = 1;
    } else if (arg[0] == 'n') {
      if (++argn >= argc) usage(argv[0]);
      nfreqs = strtol(argv[argn], NULL, 10);
    } else {
      usage(argv[0]);			/* bogus switch */
    }
  }

  return argn;			/* return index of next arg (file name) */
}


void print_qtable(JQUANT_TBL *a) {
  int i;

  for (i = 0; i < DCTSIZE2; i++) {
    if (i % 8 == 0) printf("\n");
    printf("%4d ", a->quantval[i]);
  }
  printf("\n");
}

#if 0
void ijel_print_colorspace(jel_config *cfg, j_decompress_ptr cinfo) {
    switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE: JEL_LOG(cfg, 2, "JCS_GRAYSCALE\n"); break;
    case JCS_RGB:       JEL_LOG(cfg, 2, "JCS_RGB\n"); break;
    case JCS_YCbCr:     JEL_LOG(cfg, 2, "JCS_YCbCr (YUV)\n"); break;
    case JCS_CMYK:      JEL_LOG(cfg, 2, "JCS_CMYK\n"); break;
    case JCS_YCCK:      JEL_LOG(cfg, 2, "JCS_YCCK\n"); break;
    case JCS_UNKNOWN:   JEL_LOG(cfg, 2, "JCS_UNKNOWN\n"); break;
    default:            JEL_LOG(cfg, 2, "Unrecognized color space code.\n"); break;
    }
}
#else
void ijel_print_colorspace(jel_config *cfg, j_decompress_ptr cinfo) {
    switch (cinfo->jpeg_color_space) {
    case JCS_GRAYSCALE: printf( "JCS_GRAYSCALE\n"); break;
    case JCS_RGB:       printf( "JCS_RGB\n"); break;
    case JCS_YCbCr:     printf( "JCS_YCbCr (YUV)\n"); break;
    case JCS_CMYK:      printf( "JCS_CMYK\n"); break;
    case JCS_YCCK:      printf( "JCS_YCCK\n"); break;
    case JCS_UNKNOWN:   printf( "JCS_UNKNOWN\n"); break;
    default:            printf( "Unrecognized color space code.\n"); break;
    }
}
#endif

#if 0 /* This next is only here for reference! */

typedef struct {
  /* These two fields directly represent the contents of a JPEG DHT marker */
  UINT8 bits[17];		/* bits[k] = # of symbols with codes of */
				/* length k bits; bits[0] is unused */
  UINT8 huffval[256];		/* The symbols, in order of incr code length */
  /* This field is used only during compression.  It's initialized FALSE when
   * the table is created, and set TRUE when it's been output to the file.
   * You could suppress output of a table by setting this to TRUE.
   * (See jpeg_suppress_tables for an example.)
   */
  boolean sent_table;		/* TRUE when table has been output */
} JHUFF_TBL;
#endif

void print_huffman_table(JHUFF_TBL *table) {
  int k, sym;

  if (!table) printf("No table.\n");
  else {

    printf("  Bit lengths:\n");
    for (k = 1; k < 17; k++)
      printf("      %d symbols of length %d bits\n", table->bits[k], k);

    printf("  Symbols:\n");
    for (k = 0; k < 256; k++) {
      sym = table->huffval[k];
      printf("       %d => %d (%x)\n", k, sym, sym);
    }
  }
  printf("-------------\n");
}

void print_huffman_info( struct jpeg_compress_struct *info ) {
  //  JHUFF_TBL * dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  //  JHUFF_TBL * ac_huff_tbl_ptrs[NUM_HUFF_TBLS];
  int k;
  
  for (k = 0; k < NUM_HUFF_TBLS; k++) {
    printf("\n------Huffman DC table %d:\n", k);
    print_huffman_table( info->dc_huff_tbl_ptrs[k] );
    printf("\n------Huffman AC table %d:\n", k);
    print_huffman_table( info->dc_huff_tbl_ptrs[k] );
  }
}

int * ijel_get_quanta(JQUANT_TBL *q, int *quanta) {
  int j;

  for (j = DCTSIZE2-1; j >= 0; j--)
    quanta[j] = 255 / q->quantval[j];

  return quanta;
}


/* Maps quality in [1,100] to a quant table scale factor: */


static int jpeg_fwd_quality_scaling (int quality)
/* Convert a user-specified quality rating to a percentage scaling factor
 * for an underlying quantization table, using our recommended scaling curve.
 * The input 'quality' factor should be 0 (terrible) to 100 (very good).
 */
{
  int qtscale;
  /* Safety limit on quality factor.  Convert 0 to 1 to avoid zero divide. */
  if (quality <= 0) quality = 1;
  if (quality > 100) quality = 100;

  /* The basic table is used as-is (scaling 100) for a quality of 50.
   * Qualities 50..100 are converted to scaling percentage 200 - 2*Q;
   * note that at Q=100 the scaling is 0, which will cause jpeg_add_quant_table
   * to make all the table entries 1 (hence, minimum quantization loss).
   * Qualities 1..50 are converted to scaling percentage 5000/Q.
   */
  if (quality < 50)
    qtscale = 5000 / quality;
  else
    qtscale = 200 - quality*2;

  return qtscale;
}


/* Maps a quant table scale factor to a quality value in [1,100]: */

static int jpeg_inv_quality_scaling (int qtscale)
/* Convert a user-specified quality rating to a percentage scaling factor
 * for an underlying quantization table, using our recommended scaling curve.
 * The input 'quality' factor should be 0 (terrible) to 100 (very good).
 */
{
  int quality;

  if (qtscale > 100)
    quality = 5000 / qtscale;
  else
    quality = (200 - qtscale) / 2;

  /* Safety limit on quality factor.  Convert 0 to 1 to avoid zero divide. */
  if (quality <= 0) quality = 1;
  if (quality > 100) quality = 100;

  return quality;
}

/* Formulae derived from jcparam.c, where qtables are set up - stdval
   represents the "standard" quant table entry that is to be modified
   based on the scale_factor: 
*/

long fwd_qtable_entry(long stdval, long scale_factor) {
  long temp;
  temp = ( (long) stdval * scale_factor + 50L ) / 100L;
  if (temp <= 0L) temp = 1L;
  if (temp > 32767L) temp = 32767L; /* max quantizer needed for 12 bits */
  if (force_baseline && temp > 255L)
    temp = 255L;		/* limit to baseline range if requested */
  /* In the real world, this is converted to a UINT16, but here we return a long: */
  return temp;
}


/* Invert the computation above, modulo clipping: */

long inv_qtable_entry(long qtval, long stdval) {
  long scale_factor;
  scale_factor = ((100L * qtval) - 50L) / stdval;
  return scale_factor;
}

#if 0

/* Creating default quant tables in the forward direction: When you
   use libjpeg and set quality, this is what gets called by default.
 */
GLOBAL(void)
jpeg_fwd_linear_quality (j_compress_ptr cinfo, int scale_factor,
			 boolean force_baseline)
/* Set or change the 'quality' (quantization) setting, using default tables
 * and a straight percentage-scaling quality scale.  In most cases it's better
 * to use jpeg_set_quality (below); this entry point is provided for
 * applications that insist on a linear percentage scaling.
 */
{
  /* These are the sample quantization tables given in JPEG spec section K.1.
   * The spec says that the values given produce "good" quality, and
   * when divided by 2, "very good" quality.
   */
  static const unsigned int std_luminance_quant_tbl[DCTSIZE2] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
  };
  static const unsigned int std_chrominance_quant_tbl[DCTSIZE2] = {
    17,  18,  24,  47,  99,  99,  99,  99,
    18,  21,  26,  66,  99,  99,  99,  99,
    24,  26,  56,  99,  99,  99,  99,  99,
    47,  66,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99
  };

  /* Set up two quantization tables using the specified scaling */
  jpeg_fwd_add_quant_table(cinfo, 0, std_luminance_quant_tbl,
			   scale_factor, force_baseline);
  jpeg_fwd_add_quant_table(cinfo, 1, std_chrominance_quant_tbl,
			   scale_factor, force_baseline);
}

#endif

void print_quality_estimates(JQUANT_TBL *a) {
  int i;
  long stdval, qval, quality;
  static const unsigned int std_luminance_quant_tbl[DCTSIZE2] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
  };

  int avg = 0;
  for (i = 0; i < DCTSIZE2; i++) {
    if (i % 8 == 0) printf("\n");
    stdval = std_luminance_quant_tbl[i];
    //    printf("%4d => ", a->quantval[i]);
    qval = inv_qtable_entry((long) a->quantval[i], stdval);
    quality = jpeg_inv_quality_scaling(qval);
    //    printf("%4ld => %4ld\n", qval, quality);
    printf("%4ld", quality);
    avg += quality;
  }
  printf("\n Estimated quality: %d\n", avg/DCTSIZE2);
}



int main(int argc, char **argv) {
  j_decompress_ptr cinfo;
  int k, ret, qual, qtscale;
  int i, bwidth, bheight;
  int quanta[64];
  int good_freqs[64];
  jel_config *jel;
  // int * ijel_get_quanta(JQUANT_TBL *q, int *quanta);
  void   ijel_log_qtables(jel_config*);
  FILE *sfp;
  int file_arg = 0;
  char *progname = argv[0];

  file_arg = parse_switches(argc, argv);

  jel = jel_init(JEL_NLEVELS);
  jel->logger = stderr;

  if ( argc > 1 ) {

    k = file_arg;
    sfp = fopen(argv[k], "rb");
    if (!sfp) {
      jel_log(jel, "%s: Could not open source JPEG file %s!\n", progname, argv[k]);
      exit(EXIT_FAILURE);
    }

    ret = jel_set_fp_source(jel, sfp);
    if (ret != 0) {
      jel_log(jel, "%s: jel_set_fp_source failed and returns %d.\n", progname, ret);
      fprintf(stderr, "Error - exiting (need a diagnostic!)\n");
      exit(EXIT_FAILURE);
    }
    jel_log(jel,"%s: wedge source %s\n", progname, argv[k]);
  }

  printf("Quality check: \n");
  for (i=1; i<=100; i++) {
    qtscale = jpeg_fwd_quality_scaling(i);
    qual = jpeg_inv_quality_scaling(qtscale);
    if (i != qual) printf("%d => %d\n", i, qual);
  }
  printf("Done.\n");
    

  /*
   * Set up the compression machinery:
   */
  cinfo = &(jel->srcinfo);

  /*
   * Ask for frequencies to use for the requested quality level:
   */
  nfreqs = ijel_get_freq_indices(cinfo->quant_tbl_ptrs[0], good_freqs, nfreqs, nlevels);
  printf("Highest frequency indices:\n");
  for (i = 0; i < nfreqs; i++) {
    if (i == 0) printf("%d", good_freqs[i]);
    else printf(",%d", good_freqs[i]);
  }
  printf("\n");

  /*
   * If desired, print the quanta corresponding to the returned
   * frequencies:
   */
  if (print_quanta) {
    printf("\n");
    ijel_get_quanta(cinfo->quant_tbl_ptrs[0], quanta);
    for (i = 0; i < nfreqs; i++) {
      if (i == 0) printf("%d", quanta[good_freqs[i]]);
      else printf(",%d", quanta[good_freqs[i]]);
    }
  }
  

  if (print_blockdims) {
    bwidth = cinfo->comp_info[0].width_in_blocks;
    bheight = cinfo->comp_info[0].height_in_blocks;
    printf("Block dimensions: %d x %d\n", bwidth, bheight);
  }


  //  if (print_huff) print_huffman_info(&cinfo);

  /*
   * TMI?  Print the quant tables:
   */
  if (print_qtables) ijel_log_qtables(jel);
  if (print_colorspace) ijel_print_colorspace(jel, cinfo);
  print_quality_estimates(cinfo->quant_tbl_ptrs[0]);


  return 0;
}

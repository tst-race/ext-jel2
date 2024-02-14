/*
 * jdump: Dump the pixel values and frequency coefficients for a
 * specific MCU.
 */

#include <jel/jel.h>
#include <ctype.h>
#include <stdlib.h>

/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */


#define JDUMP_VERSION "0.1"

static const char * progname;		/* program name for error messages */
//static char * outfilename = NULL;	/* for -outfile switch */
static int symmetry = 0;

LOCAL(void)
usage (void)
/* complain about bad command line */
{
  fprintf(stderr, "usage: %s i,j <inputfile> [outputfile]\n", progname);
  fprintf(stderr, "       Prints the pixel values and DCT coefficients for the MCU that contains a given pixel.\n");
  fprintf(stderr, "       i,j are row,column pixel coordinates, and <inputfile> is a JPEG image file.\n");
  fprintf(stderr, "       At present, only prints the Y channel.\n");
  exit(EXIT_FAILURE);
}


/*
 * Switch parsing:
 */

static boolean
keymatch (char * arg, const char * keyword, int minchars)
{
  register int ca, ck;
  register int nmatched = 0;

  while ((ca = *arg++) != '\0') {
    if ((ck = *keyword++) == '\0')
      return FALSE;		/* arg longer than keyword, no good */
    if (isupper(ca))		/* force arg to lcase (assume ck is already) */
      ca = tolower(ca);
    if (ca != ck)
      return FALSE;		/* no good */
    nmatched++;			/* count matched characters */
  }
  /* reached end of argument; fail if it's too short for unique abbrev */
  if (nmatched < minchars)
    return FALSE;
  return TRUE;			/* A-OK */
}


static int
parse_switches (jel_config *cfg, int argc, char **argv)
/* Parse optional switches.
 * Returns argv[] index of first file-name argument (== argc if none).
 * Any file names with indexes <= last_file_arg_seen are ignored;
 * they have presumably been processed in a previous iteration.
 * (Pass 0 for last_file_arg_seen on the first or only iteration.)
 * for_real is FALSE on the first (dummy) pass; we may skip any expensive
 * processing.
 */
{
  int argn;
  char * arg;

  /* Scan command line options, adjust parameters */

  for (argn = 1; argn < argc; argn++) {
    arg = argv[argn];
    if (*arg != '-') break;

    arg++;			/* advance past switch marker character */

    if (keymatch(arg, "debug", 1) || keymatch(arg, "verbose", 1)) {
      /* Enable debug printouts. */
      /* On first -d, print version identification */
      //static boolean printed_version = FALSE;

    } else if (keymatch(arg, "symmetry", 1)) {
      symmetry = 1;
    } else if (keymatch(arg, "version", 7)) {
      fprintf(stderr, "jhist version %s (libjel version %s)\n",
	      JDUMP_VERSION, jel_version_string());
      exit(-1);
    } else {
      usage();			/* bogus switch */
    }
  }

  return argn;			/* return index of next arg (file name) */
}



#define HSIZE 2048

/* Accumulate DCT histograms over the image: */

int ijel_print_parent_mcu(jel_config *cfg, int mcu_i, int mcu_j) {

  /* Assumes 64 frequencies, and an 11-bit signed integer representation: */

  struct jpeg_decompress_struct *cinfo = &(cfg->srcinfo);
  jvirt_barray_ptr *coef_arrays = cfg->coefs;

  // jel_log(cfg, "ijel_print_parent_mcu: coef_arrays = %llx\n", coef_arrays);

  static int compnum = 0;  /* static?  Really?  This is the component number, 0=luminance.  */
  //  int hk;
  //  int offset_y;
  int blk_y, bheight, bwidth, i, j, k;
  JDIMENSION blocknum; //, MCU_cols;
  jvirt_barray_ptr comp_array = coef_arrays[compnum];
  jpeg_component_info *compptr;
  JCOEF *mcu;
  JBLOCKARRAY row_ptrs;

  //size_t block_row_size = (size_t) SIZEOF(JCOEF)*DCTSIZE2*cinfo->comp_info[compnum].width_in_blocks;

  //  jel_log(cfg, "ijel_print_parent_mcu: block_row_size = %d\n", block_row_size);

  bheight = cinfo->comp_info[compnum].height_in_blocks;
  bwidth = cinfo->comp_info[compnum].width_in_blocks;

  jel_log(cfg, "ijel_print_parent_mcu: bwidth x bheight = %dx%d\n", bwidth, bheight);

  //MCU_cols = cinfo->image_width / (cinfo->max_h_samp_factor * DCTSIZE);

  compptr = cinfo->comp_info + compnum;

  blk_y = compptr->v_samp_factor * mcu_i;
  row_ptrs = ((cinfo)->mem->access_virt_barray) 
    ( (j_common_ptr) cinfo, comp_array, blk_y,
      (JDIMENSION) compptr->v_samp_factor, FALSE);

  blocknum = mcu_j;
  
  mcu = (JCOEF*) row_ptrs[0][blocknum];

  k = 0;
  for (i = 0; i < 8; i++) {
    for (j = 0; j < 8; j++)
      printf("%4d ", mcu[k++]);
    printf("\n");
  }

  return k;
}


/* 
 * Print values then clean up:
 */

static int jel_print_parent_mcu( jel_config * cfg, int pix_i, int pix_j ) {
  int mcu_i, mcu_j;
  
  // Only handles the Y channel at present:
  mcu_i = pix_i / 8;
  mcu_j = pix_j / 8;
  
  ijel_print_parent_mcu(cfg, mcu_i, mcu_j);

  (void) jpeg_finish_decompress(&(cfg->srcinfo));
  jpeg_destroy_decompress(&(cfg->srcinfo));

  return 0;

}


/*
 * The main program.
 */

int
main (int argc, char **argv)
{
  jel_config *jel;
  int ret;
  int k, pix_i, pix_j;
  //int bytes_written;
  FILE *sfp, *output_file;

  
  jel = jel_init(JEL_NLEVELS);

  ret = jel_open_log(jel, "/tmp/jdump.log");
  if (ret == JEL_ERR_CANTOPENLOG) {
    fprintf(stderr, "Can't open /tmp/jdump.log!!\n");
    jel->logger = stderr;
  }

  jel_log(jel, "jdump version %s (libjel version %s)\n",
	  JDUMP_VERSION, jel_version_string());

  progname = argv[0];
  if (progname == NULL || progname[0] == 0)
    progname = "jdump";		/* in case C library doesn't provide it */

  if (argc <= 1) usage();

  k = parse_switches(jel, argc, argv);

  sscanf(argv[1], "%d,%d", &pix_i, &pix_j);

  sfp = fopen(argv[2], "rb");
  if ( !sfp ) {
    fprintf(stderr, "Error: Can't open input file %s!\n", argv[k]);
    exit(EXIT_FAILURE);
  }

  ret = jel_set_fp_source(jel, sfp);
  if (ret != 0) {
    fprintf(stderr, "Error: Can't set fp source!\n");
    exit(EXIT_FAILURE);
  }

  jel_log(jel,"%s: jdump input %s\n", progname, argv[k]);

  k++;

  output_file = stdout;

  if (ret != 0) {
    jel_log(jel,"%s: jel error %d\n", progname, ret);
    fprintf(stderr, "Error - exiting (need a diagnostic!)\n");
    exit(EXIT_FAILURE);
  }

  jel_print_parent_mcu(jel, pix_i, pix_j);

  jel_close_log(jel);

  if (sfp != NULL && sfp != stdin) fclose(sfp);
  if (output_file != NULL && output_file != stdout) fclose(output_file);

  return 0;			/* suppress no-return-value warnings */
}

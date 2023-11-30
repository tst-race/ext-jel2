/*
 * unwedge.c - extract a message that was embedded in a JPEG image
 * using 'wedge'.
 * 
 * The objective of "unwedge" is to extract a message from the image
 * using frequency space, assuming that "wedge" was used to embed the
 * message.  We do this by manipulating the quantized DCT
 * coefficients.  We choose relatively high frequencies that are
 * quantized "just enough" to carry some bits of our message.  We set
 * those frequency coefficients to desired values, then write the
 * results into a new JPEG file.  A companion program "unwedge" will
 * extract the message.
 *
 */

#include <jel/jel.h>
#include <ctype.h>		/* to declare isprint() */

#include <stdlib.h>

#define UNWEDGE_VERSION "1.0.1.4"

/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */


static const char * progname;		/* program name for error messages */
static char * outfilename = NULL;	/* for -outfile switch */
static unsigned char * message = NULL;
static int freq[64];
static int set_freq = 0;
static int maxfreq = 4;
static int nfreqs = 4;
static int mcu_density = 100;
static int msglen = -1;
static int embed_length = 1;
static int ecc = 1;
static int ecclen = 0;
static int seed = 0;


LOCAL(void)
usage (void)
/* complain about bad command line */
{
  fprintf(stderr, "usage: %s [switches] ", progname);
  fprintf(stderr, "inputfile [outputfile]\n");
  fprintf(stderr, "    where 'inputfile' is a steg image, and 'outputfile' is the extracted message.\n");


  fprintf(stderr, "Switches (names may be abbreviated):\n");
  fprintf(stderr, "  -length    N   Decode exactly N characters from the message.\n");
  fprintf(stderr, "  -ecc L          Set ECC block length to L bytes.\n");
  fprintf(stderr, "  -noecc         Do not use error correcting codes.\n");
  fprintf(stderr, "  -outfile name  Specify name for output file\n");
  fprintf(stderr, "  -quanta N      Ask for N quanta for extraction (default=8).\n");
  fprintf(stderr, "                 NOTE: The same value must used for embedding!\n");
  fprintf(stderr, "  -freq a,b,c,d  Decode message using specified freq. component indices.\n");
  fprintf(stderr, "                 If this is specified, -quanta is ignored.\n");
  fprintf(stderr, "                 NOTE: The same values must used for extraction!\n");
  fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=4).\n");
  fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding.\n");
  fprintf(stderr, "  -maxfreqs <N>   Allow N frequency components to be available for embedding.\n");
  fprintf(stderr, "                  Only the first 4 are used, but if seed is supplied, they are randomized.\n");
  fprintf(stderr, "  -mcudensity <M> Allow M percent of MCUs to be available for embedding.\n");
  fprintf(stderr, "                  M must be <= 100.\n");
  fprintf(stderr, "                  If M is -1 (the default), then all MCUs are used.\n");
  fprintf(stderr, "  -seed <n>      Seed (shared secret) for random frequency selection.\n");
  fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  exit(EXIT_FAILURE);
}


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



LOCAL(int)
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

      /* Whether to be verbose */
      jel_verbose = true;

    } else if (keymatch(arg, "outfile", 4)) {
      /* Set output file name. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      outfilename = argv[argn];	/* save it away for later use */

    } else if (keymatch(arg, "ecc", 3)) {
      /* Block length to use for error correction */
      if (++argn >= argc)
	usage();
      ecclen = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "frequencies", 4)) {
      /* freq. components */
      if (++argn >= argc)
	usage();
      if (sscanf(argv[argn], "%d,%d,%d,%d",
		 &freq[0], &freq[1], &freq[2], &freq[3]) != 4)
	usage();
      maxfreq = 4;
      set_freq = 1;
    } else if (keymatch(arg, "length", 3)) {
      /* Start block */
      if (++argn >= argc)
	usage();
      msglen = strtol(argv[argn], NULL, 10);
      embed_length = 0;
    } else if (keymatch(arg, "quanta", 5)) {
      /* Start block */
      if (++argn >= argc)
	usage();
      cfg->freqs.nlevels = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "seed", 4)) {
      /* Start block */
      if (++argn >= argc)
	usage();
      seed = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "maxfreqs", 8)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      maxfreq = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "noecc", 5)) {
      /* Whether to assume error correction */
      ecc = 0;
    } else if (keymatch(arg, "nfreqs", 5)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      nfreqs = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "bpf", 3)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      cfg->bits_per_freq = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "maxfreqs", 8)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      maxfreq = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "mcudensity", 10)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      mcu_density = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "version", 7)) {
      fprintf(stderr, "unwedge version %s (libjel version %s)\n",
	      UNWEDGE_VERSION, jel_version_string());
      exit(-1);
    } else {
      usage();			/* bogus switch */
    }
  }

  return argn;			/* return index of next arg (file name) */
}


/*
 * Marker processor for COM and interesting APPn markers.
 * This replaces the library's built-in processor, which just skips the marker.
 * We want to print out the marker as text, to the extent possible.
 * Note this code relies on a non-suspending data source.
 */


#if 0

LOCAL(unsigned int)
jpeg_getc (j_decompress_ptr cinfo)
/* Read next byte */
{
  struct jpeg_source_mgr * datasrc = cinfo->src;

  if (datasrc->bytes_in_buffer == 0) {
    if (! (*datasrc->fill_input_buffer) (cinfo))
      ERREXIT(cinfo, JERR_CANT_SUSPEND);
  }
  datasrc->bytes_in_buffer--;
  return GETJOCTET(*datasrc->next_input_byte++);
}


METHODDEF(boolean)
print_text_marker (j_decompress_ptr cinfo)
{
  boolean traceit = (cinfo->err->trace_level >= 1);
  INT32 length;
  unsigned int ch;
  unsigned int lastch = 0;

  length = jpeg_getc(cinfo) << 8;
  length += jpeg_getc(cinfo);
  length -= 2;			/* discount the length word itself */

  if (traceit) {
    if (cinfo->unread_marker == JPEG_COM)
      fprintf(stderr, "Comment, length %ld:\n", (long) length);
    else			/* assume it is an APPn otherwise */
      fprintf(stderr, "APP%d, length %ld:\n",
	      cinfo->unread_marker - JPEG_APP0, (long) length);
  }

  while (--length >= 0) {
    ch = jpeg_getc(cinfo);
    if (traceit) {
      /* Emit the character in a readable form.
       * Nonprintables are converted to \nnn form,
       * while \ is converted to \\.
       * Newlines in CR, CR/LF, or LF form will be printed as one newline.
       */
      if (ch == '\r') {
	fprintf(stderr, "\n");
      } else if (ch == '\n') {
	if (lastch != '\r')
	  fprintf(stderr, "\n");
      } else if (ch == '\\') {
	fprintf(stderr, "\\\\");
      } else if (isprint(ch)) {
	putc(ch, stderr);
      } else {
	fprintf(stderr, "\\%03o", ch);
      }
      lastch = ch;
    }
  }

  if (traceit)
    fprintf(stderr, "\n");

  return TRUE;
}

static int ijel_printf_message(jel_config *cfg) {
  return ijel_fprintf_message(cfg, stdout) ;
}

static void ijel_describe_message(jel_config *cfg) {
  jel_freq_spec *f = &(cfg->freqs);
  int i;
  printf("Message:\n");
  printf("   Frequency component indices: [");
  for (i = 0; i < f->maxfreqs; i++) printf("%d ", f->freqs[i]);
  printf("]\n   Message length: %d\n", cfg->len);
  printf("   Message max length: %d\n", cfg->maxlen);
  printf("   Message hex: ");
  for (i = 0; i < cfg->len; i++) printf("%2x ", (unsigned char) cfg->data[i] & 0xFF);
}

#endif


static int ijel_fprintf_message(jel_config *cfg, FILE* fp) {
  int i;
  int retval = 0;

  // msg->len always dictates the length:
  for (i = 0; i < cfg->len; i++){
    char c = cfg->data[i];
    fputc(c, fp);
    retval++; 
  }
  return retval;
}



/*
 * The main program.
 */

int
main (int argc, char **argv)
{
  jel_config *jel;
  jel_freq_spec *fspec;

  int i, test_iter;
  int max_bytes;
  int ret;
  //int file_index;
  int k; //, bw, bh;
  int bytes_written;
  FILE *sfp, *output_file;

  for (test_iter = 0; test_iter < 1000; test_iter++) {
    /* Unwedge */

    printf("Iteration %d\n", test_iter);
    jel = jel_init(JEL_NLEVELS);
    ret = jel_open_log(jel, "/tmp/unloop.log");
    if (ret == JEL_ERR_CANTOPENLOG) {
      fprintf(stderr, "Can't open /tmp/unloop.log!!\n");
      jel->logger = stderr;
    }

    jel_log(jel,"unwedge version %s\n", UNWEDGE_VERSION);
    progname = argv[0];
    if (progname == NULL || progname[0] == 0)
      progname = "unloop";		/* in case C library doesn't provide it */

    if (argc == 1) usage();
  
    k = parse_switches(jel, argc, argv);

    jel_log(jel, "%s: Setting maxfreqs to %d\n", progname, maxfreq);
    if ( jel_setprop( jel, JEL_PROP_MAXFREQS, maxfreq ) != maxfreq )
      jel_log(jel, "Failed to set frequency generation seed.\n");

    jel_log(jel, "%s: Setting nfreqs to %d\n", progname, nfreqs);
    if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
      jel_log(jel, "Failed to set frequency pool.\n");

    jel_log(jel, "%s: Setting MCU density to %d\n", progname, mcu_density);
    if ( jel_setprop( jel, JEL_PROP_MCU_DENSITY, mcu_density ) != mcu_density )
      jel_log(jel, "Failed to set the MCU density.\n");

    if (!ecc) {
      jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_NONE);
      jel_log(jel, "Disabling ECC.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_METHOD));
    } else if (ecclen > 0) {
      jel_setprop(jel, JEL_PROP_ECC_BLOCKLEN, ecclen);
      jel_log(jel, "ECC block length set.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_BLOCKLEN));
    }

    sfp = fopen(argv[k], "rb");
    if (!sfp) {
      jel_log(jel, "%s: Could not open source JPEG file %s!\n", progname, argv[k]);
      exit(EXIT_FAILURE);
    }

    ret = jel_set_fp_source(jel, sfp);
    if (ret != 0) {
      fprintf(stderr, "Error - could not set fp source!\n");
      exit(EXIT_FAILURE);
    }
    jel_log(jel,"%s: unwedge input %s\n", progname, argv[k]);

    k++;

    if (argc == k && !outfilename) output_file = stdout;
    else {
      if (!outfilename) outfilename = strdup(argv[k]);

      output_file = fopen(outfilename, "wb");
      jel_log(jel,"%s: wedge output %s\n", progname, outfilename);
    }

    if (ret != 0) {
      jel_log(jel,"%s: jel error %d\n", progname, ret);
      fprintf(stderr, "Error - exiting (need a diagnostic!)\n");
      exit(EXIT_FAILURE);
    }
  
    /*
     * 'raw' capacity just returns the number of bytes that can be
     * stored in the image, regardless of ECC or other forms of
     * encoding.  On this end, we just need to make sure that the
     * allocated buffer has enough space to load every byte:
     */

    max_bytes = jel_raw_capacity(jel);

    /* Set up the buffer for receiving the incoming message.  Internals
     * are handled by jel_extract: */
    message = malloc(max_bytes*2);

    if ( seed > 0 ) {
      jel_log(jel, "%s: Setting randomization seed to %d\n", progname, seed);
      if ( jel_setprop( jel, JEL_PROP_PRN_SEED, seed ) != seed )
	jel_log(jel, "Failed to set randomization seed.\n");

    }

    if (embed_length) jel_log(jel, "%s: Length is embedded.\n", progname);
    else jel_log(jel, "%s: Length is NOT embedded.\n", progname);

    jel_setprop(jel, JEL_PROP_EMBED_LENGTH, embed_length);

    fspec = &(jel->freqs);

    fspec->init = 0;
    /* If supplied, copy the selected frequency components: */
    if (set_freq) jel_init_frequencies(jel, freq, maxfreq);
    else jel_init_frequencies(jel, NULL, 0);

    if (jel_verbose) {
      jel_log(jel, "unwedge frequencies: fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
      for (i = 0; i < fspec->maxfreqs; i++) jel_log(jel, "%d ", fspec->freqs[i]);
      jel_log(jel, "]\n");
    }

    /* Forces unwedge to read 'msglen' bytes: */
    if (msglen == -1) {
      jel_log(jel, "%s: Message length unspecified.  Setting to maximum: %d\n", progname, max_bytes);
      msglen = max_bytes;
    } else if (msglen > max_bytes) {
      jel_log(jel, "%s: Specified message length %d exceeds maximum %d.  Using maximum.\n", progname, msglen, max_bytes);
      msglen = max_bytes;
    } else {
      jel_log(jel, "%s: Specified message length is %d.\n", progname, msglen);
    }

    msglen = jel_extract(jel, message, msglen);
    if (msglen < 0) jel_perror("unwedge error:", msglen);
    else
      //  printf("message = 0x%x; msglen = %d\n", (unsigned int) message, msglen);
      jel_log(jel, "%s: %d bytes extracted\n", progname, msglen);
    //  printf("message:   ");
    //  for (k = 0; k < 16; k++) printf(" %x ", message[k] );
    //  printf("\n");

    bytes_written = ijel_fprintf_message(jel, output_file);

    if( bytes_written  != msglen ){
      jel_log(jel, "jel_fprintf_message wrote %d bytes; message contained %d bytes\n", bytes_written, msglen);
    }
    if (sfp != NULL && sfp != stdin) fclose(sfp);
    if (output_file != NULL && output_file != stdout) fclose(output_file);
    jel_close_log(jel);


    free(message);

    jel_free(jel);
    // if ( outfilename ) free(outfilename);
  }

  return 0;			/* suppress no-return-value warnings */
}

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

#define UNWEDGE_VERSION "1.0.1.9"
#define LOGFILE   ".unwedge.log"

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
static int maxfreq = 6;
static int nfreqs = 1;
static int mcu_density = 100;
static int msglen = -1;
static int embed_length = 1;
static int ecc = 0;
static int ecclen = 0;
static int seed = 0;
static int comps[3];


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
  fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=1).\n");
  fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding (default=1).\n");
  fprintf(stderr, "  -maxfreqs <N>   Allow N frequency components to be available for embedding (default=6).\n");
  fprintf(stderr, "                  Only the first 4 are used, but if seed is supplied, they are randomized.\n");
  fprintf(stderr, "  -mcudensity <M> Allow M percent of MCUs to be available for embedding.\n");
  fprintf(stderr, "                  M must be <= 100.\n");
  fprintf(stderr, "                  If M is -1 (the default), then all MCUs are used.\n");
  fprintf(stderr, "  -seed <n>      Seed (shared secret) for random frequency selection.\n");
  fprintf(stderr, "  -debug_mcu <k>  Show the effect of embedding on the kth active MCU.\n");
  fprintf(stderr, "  -raw            Do not try to read a header from the image - raw data bits only.\n");
  fprintf(stderr, "  -setval         [IGNORED] Do not set the LSBs of frequency components, set the values.\n");
  fprintf(stderr, "  -normalize      Operate on true DCT coefficients, not the 'squashed' versions in quant space.\n"); 
  fprintf(stderr, "  -component <c>  Components to use in order, eg 'y', 'yu', 'uyv', etc...\n");
  fprintf(stderr, "  -verbose <level>  or  -debug   Emit debug output\n");
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
parse_frequencies(char *str) {
  char *cur, *next;
  int k, maxfreqs;

  for (k = 0; k < 64; k++) freq[k] = -1;

  k = 0;
  cur = str;
  while (cur && k < 64) {
    freq[k++] = strtol(cur, &next, 10);
    if (*next == ',') cur = next + 1;
    else cur = NULL;
  }
  maxfreqs = k;

  // for (k = 0; k < maxfreqs; k++) printf("%d ", freq[k]);
  
  return maxfreqs;
}


static int compname_index(char comp) {
  if (comp == 'y' || comp == 'Y') return 0;
  else if (comp == 'u' || comp == 'U') return 1;
  else if (comp == 'v' || comp == 'V') return 2;
  else return -1;
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
  int argn, len;
  char * arg;

  /* Scan command line options, adjust parameters */

  jel_verbose = false;
  for (argn = 1; argn < argc; argn++) {
    arg = argv[argn];
    if (*arg != '-') break;

    arg++;			/* advance past switch marker character */

    if (keymatch(arg, "debug", 1)) {
      /* Enable debug printouts. */
      /* On first -d, print version identification */
      //static boolean printed_version = FALSE;

      /* Whether to be verbose */
      jel_verbose = true;

    } else if (keymatch(arg, "verbose", 4)) {
      /* Whether to be verbose */
      if (++argn >= argc)
        usage();
      jel_verbose = true;
      cfg->verbose = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "outfile", 4)) {
      /* Set output file name. */
      if (++argn >= argc)	/* advance to next argument */
	usage();
      outfilename = strdup(argv[argn]);	/* save it away for later use */

    } else if (keymatch(arg, "ecc", 3)) {
      /* Block length to use for error correction */
      if (++argn >= argc)
	usage();
      ecc = 1;
      ecclen = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "frequencies", 4)) {
      /* freq. components */
      if (++argn >= argc)
	usage();
      maxfreq = parse_frequencies(argv[argn]);
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
    } else if (keymatch(arg, "component", 4)) {
      if (++argn >= argc)
        usage();
      char *compnames = argv[argn];
      len = strlen(compnames);
      comps[0] = comps[1] = comps[2] = -1;
      comps[0] = compname_index(compnames[0]);
      if (len > 1) comps[1] = compname_index(compnames[1]);
      if (len > 2) comps[2] = compname_index(compnames[2]);
    } else if (keymatch(arg, "debug_mcu", 7)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      cfg->debug_mcu = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "raw", 3)) {
      cfg->embed_bitstream_header = FALSE;
    } else if (keymatch(arg, "setval", 6)) {
      cfg->set_lsbs = FALSE;
    } else if (keymatch(arg, "normalize", 4)) {
      cfg->normalize = TRUE;
    } else if (keymatch(arg, "version", 7)) {
      fprintf(stderr, "unwedge version %s (libjel version %s)\n",
	      UNWEDGE_VERSION, jel_version_string());
      exit(-1);
    } else {
      usage();			/* bogus switch */
    }
  }

  if (seed == 0) fprintf(stderr, "unwedge warning: No seed provided.  Embedding will be deterministic and easily detected.\n");

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

  int i;
  int max_bytes;
  int ret;
  //int file_index;
  int k; //, bw, bh;
  int bytes_written;
  FILE *sfp, *output_file;

  
  jel = jel_init(JEL_NLEVELS);

  progname = argv[0];
  if (progname == NULL || progname[0] == 0)
    progname = "unwedge";		/* in case C library doesn't provide it */

  if (argc == 1) usage();
  
  k = parse_switches(jel, argc, argv);
  /* We will always set this false for now, at least until LSB
     insertion can be debugged properly: */
  jel->set_lsbs = FALSE;

  if (jel_verbose) {
    ret = jel_open_log(jel, LOGFILE);
    if (ret == JEL_ERR_CANTOPENLOG) {
      fprintf(stderr, "Can't open %s!!\n", LOGFILE);
      jel->logger = stderr;
    }
  }

  JEL_LOG(jel, 1,"unwedge version %s\n", UNWEDGE_VERSION);

  JEL_LOG(jel, 1, "%s: Setting maxfreqs to %d\n", progname, maxfreq);
  if ( jel_setprop( jel, JEL_PROP_MAXFREQS, maxfreq ) != maxfreq )
    JEL_LOG(jel, 1, "Failed to set frequency generation seed.\n");

  JEL_LOG(jel, 1, "%s: Setting nfreqs to %d\n", progname, nfreqs);
  if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
    JEL_LOG(jel, 1, "Failed to set frequency pool.\n");

  JEL_LOG(jel, 1, "%s: Setting MCU density to %d\n", progname, mcu_density);
  if ( jel_setprop( jel, JEL_PROP_MCU_DENSITY, mcu_density ) != mcu_density )
    JEL_LOG(jel, 1, "%s: Failed to set the MCU density.\n", progname);

  if (!ecc) {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_NONE);
    JEL_LOG(jel, 1, "%s: Disabling ECC.  getprop=%d\n", progname, jel_getprop(jel, JEL_PROP_ECC_METHOD));
  } else {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_RSCODE);
    if (ecclen > 0) {
      jel_setprop(jel, JEL_PROP_ECC_BLOCKLEN, ecclen);
      JEL_LOG(jel, 1, "%s: ECC block length set.  getprop=%d\n", progname, jel_getprop(jel, JEL_PROP_ECC_BLOCKLEN));
    }
  }


  sfp = fopen(argv[k], "rb");
  if (!sfp) {
    fprintf(stderr, "%s: Could not open source JPEG file %s!\n", progname, argv[k]);
    JEL_LOG(jel, 1, "%s: Could not open source JPEG file %s!\n", progname, argv[k]);
    exit(EXIT_FAILURE);
  }

  ret = jel_set_fp_source(jel, sfp);
  if (ret != 0) {
    fprintf(stderr, "%s: Error - exiting (need a diagnostic!)\n", progname);
    exit(EXIT_FAILURE);
  }
  JEL_LOG(jel, 1,"%s: unwedge input %s\n", progname, argv[k]);

  JEL_LOG(jel, 1, "%s: Components: %d %d %d\n", progname, comps[0], comps[1], comps[2]);
  if (comps[0] >= 0) jel_set_components(jel, comps[0], comps[1], comps[2]);

  k++;

  if (argc == k && !outfilename) output_file = stdout;
  else {
    if (!outfilename) outfilename = strdup(argv[k]);

    output_file = fopen(outfilename, "wb");
    JEL_LOG(jel, 1,"%s: wedge output %s\n", progname, outfilename);
  }

  if (ret != 0) {
    JEL_LOG(jel, 1,"%s: jel error %d\n", progname, ret);
    fprintf(stderr, "%s: Error - exiting (need a diagnostic!)\n", progname);
    exit(EXIT_FAILURE);
  }


  if ( seed > 0 ) {
    JEL_LOG(jel, 1, "%s: Setting randomization seed to %d\n", progname, seed);
    if ( jel_setprop( jel, JEL_PROP_PRN_SEED, seed ) != seed )
      JEL_LOG(jel, 1, "%s: Failed to set randomization seed.\n", progname);

  }

  if (embed_length) {
    JEL_LOG(jel, 1, "%s: Length is embedded.\n", progname);
  } else {
    JEL_LOG(jel, 1, "%s: Length is NOT embedded.\n", progname);
  }

  jel_setprop(jel, JEL_PROP_EMBED_LENGTH, embed_length);

  fspec = &(jel->freqs);

  fspec->init = 0;
  /* If supplied, copy the selected frequency components: */
#if 0
  if (set_freq) {
    printf("set_freq = 1; setting frequencies explicitly.\n");
    jel_init_frequencies(jel, freq, maxfreq);
    JEL_LOG(jel, 1, "%s: setting frequencies to [", progname);
    for (i = 0; i < maxfreq; i++) JEL_LOG(jel, 1, "%d ", freq[i]);
    JEL_LOG(jel, 1, "]\n");
  } else {
    jel_init_frequencies(jel, NULL, 0);
    JEL_LOG(jel, 1, "%s: Initializing frequencies to defaults.\n", progname);    
  }

#else
  jel_init_frequencies(jel, NULL, 0);

  if (set_freq) {
    if (nfreqs > maxfreq) {
      nfreqs = maxfreq;

      JEL_LOG(jel, 1, "%s: Setting nfreqs to %d\n", progname, nfreqs);
      if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
	JEL_LOG(jel, 1, "Failed to set number of frequencies.\n");
    }
    if (maxfreq < 4) {
      printf("wedge: You must supply at least 4 frequencies so that density can be encoded.\n");
      exit(-1);
    }
    jel_set_frequencies(jel, freq, maxfreq);
  }

  JEL_LOG(jel, 1, "%s: frequencies are [", progname);
  for (i = 0; i < maxfreq; i++) JEL_LOG(jel, 1, "%d ", freq[i]);
  JEL_LOG(jel, 1, "]\n");

#endif

  if (1 || jel_verbose) {
    JEL_LOG(jel, 1, "unwedge frequencies: after jel_init_frequencies, fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) JEL_LOG(jel, 1, "%d ", fspec->freqs[i]);
    JEL_LOG(jel, 1, "]\n");
  }

  /*
   * 'raw' capacity just returns the number of bytes that can be
   * stored in the image, regardless of ECC or other forms of
   * encoding.  On this end, we just need to make sure that the
   * allocated buffer has enough space to load every byte:
   */

  max_bytes = jel_raw_capacity(jel);
  JEL_LOG(jel, 1, "%s: capacity max_bytes = %d\n", progname, max_bytes);

  /* Set up the buffer for receiving the incoming message.  Internals
   * are handled by jel_extract: */
  message = malloc(max_bytes*2);


  /* Forces unwedge to read 'msglen' bytes: */
  if (msglen == -1) {
    JEL_LOG(jel, 1, "%s: Message length unspecified.  Setting to maximum: %d\n", progname, max_bytes);
    msglen = max_bytes;
  } else if (msglen > max_bytes) {
    JEL_LOG(jel, 1, "%s: Specified message length %d exceeds maximum %d.  Using maximum.\n", progname, msglen, max_bytes);
    msglen = max_bytes;
  } else {
    JEL_LOG(jel, 1, "%s: Specified message length is %d.\n", progname, msglen);
  }

  msglen = jel_extract(jel, message, msglen);

  if (msglen < 0) {

    // Need to do something better here.  Regular command-line usage
    // should provide the option to print an error message:

    // jel_perror("unwedge error: ", msglen);
    exit(msglen);
  } else
  //  printf("message = 0x%x; msglen = %d\n", (unsigned int) message, msglen);
    JEL_LOG(jel, 1, "%s: %d bytes extracted\n", progname, msglen);
  //  printf("message:   ");
  //  for (k = 0; k < 16; k++) printf(" %x ", message[k] );
  //  printf("\n");

  jel->len = msglen;
  bytes_written = ijel_fprintf_message(jel, output_file);
  //  printf("bytes_written = %d\n", bytes_written);

  if( bytes_written  != msglen ){
    JEL_LOG(jel, 1, "jel_fprintf_message wrote %d bytes; message contained %d bytes\n", bytes_written, msglen);
  }

  if (jel_verbose) jel_close_log(jel);

  if (sfp != NULL && sfp != stdin) fclose(sfp);
  if (output_file != NULL && output_file != stdout) fclose(output_file);

  free(message);

  jel_free(jel);
  if ( outfilename ) free(outfilename);

  return 0;			/* suppress no-return-value warnings */
}

#include <time.h>
/*
 * wedge.c - Originally cribbed from djpeg.c but substantially
 * rewritten, which contains the following:
 * 
 * Copyright (C) 1991-1997, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 *  The objective of "wedge" is to place a message into the image
 * using frequency space.  We do this by manipulating the quantized
 * DCT coefficients.  We choose relatively high frequencies that are
 * quantized "just enough" to carry some bits of our message.  We set
 * those frequency coefficients to desired values, then write the
 * results into a new JPEG file.  A companion program "unwedge" will
 * extract the message.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <jel/jel.h>

#include <ctype.h>		/* to declare isprint() */

#define WEDGE_VERSION "1.0.1.9"
#define LOGFILE   ".wedge.log"

#ifdef _WIN32
#define PriSize_t   "u"
#define PriSSize_t   "u"
#else
#define PriSize_t   "zu"
#define PriSSize_t  "zd"
#endif

/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */


static const char * progname;		/* program name for error messages */
static char * outfilename = NULL;	/* for -outfile switch */
static char * msgfilename = NULL;		/* for -infile switch */
static unsigned char * message = NULL;
static bool msg_allocated = false;
static bool set_freq = false;
static int fmax=100;
static int freq[64];
static int maxfreq = 6;
static int nfreqs = 1;
//static int mcu_density = 100;
static int mcu_density = -1;
//static int verbose = 0;
extern bool jel_verbose;
static int embed_length = 1;   /* Always embed the length */
static int message_length;     /* Could be strlen, but not if we read from a file. */
static int quality = 0;        /* If 0, do nothing.  If >0, then set the output quality factor. */
static int ecc = 0;
static int ecclen = 0;
static int seed = 0;
static int comps[3];

LOCAL(void)
usage (void)
/* complain about bad command line */
{
  fprintf(stderr, "usage: %s [switches] inputfile [outputfile]\n", progname);
  fprintf(stderr, "Embeds a message in a jpeg image.\n");
  fprintf(stderr, "Switches (names may be abbreviated):\n");
  fprintf(stderr, "  -message M      Wedge a string M into the image\n");
  fprintf(stderr, "                  If not supplied, stdin will be used.\n");
  fprintf(stderr, "  -nolength       Do not embed the message length.\n");
  fprintf(stderr, "  -ecc L          Set ECC block length to L bytes.\n");
  fprintf(stderr, "  -noecc          Do not use error correcting codes.\n");
  fprintf(stderr, "  -quanta N       Ask for N quanta for embedding (default=8).\n");
  fprintf(stderr, "                  NOTE: The same value must used for extraction!\n");
  fprintf(stderr, "  -quality Q      Ask for quality level Q for embedding (default=same as input quality).\n");
  fprintf(stderr, "  -freq a,b,c,d   Encode using frequencies corresponding to indices a,b,c,d\n");
  fprintf(stderr, "                  If this is specified, -quanta is ignored.\n");
  fprintf(stderr, "                  NOTE: The same values must used for extraction!\n");
  fprintf(stderr, "                  Overrides -maxfreqs.\n");
  fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=1).\n");
  fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding (default=1).\n");
  fprintf(stderr, "  -maxfreqs <M>   Allow M frequency components to be available for embedding (default=6).\n");
  fprintf(stderr, "                  Only the first N are used, but if seed is supplied, they are randomized.\n");
  fprintf(stderr, "  -mcudensity <M> Allow M percent of MCUs to be available for embedding.\n");
  fprintf(stderr, "                  M must be <= 100.\n");
  fprintf(stderr, "                  If M is -1 (the default), then the density is auto-computed based on the message size.\n");
  fprintf(stderr, "  -data    <file> Use the contents of the file as the message (alternative to stdin).\n");
  fprintf(stderr, "  -outfile <file> Filename for output image.\n");
  fprintf(stderr, "  -seed <n>       Seed (shared secret) for random frequency selection.\n");
  fprintf(stderr, "  -raw            Do not embed a header in the image - raw data bits only.\n");
  fprintf(stderr, "  -setval         [IGNORED] Do not set the LSBs of frequency components, set the values.\n");
  fprintf(stderr, "  -normalize      Operate on true DCT coefficients, not the 'squashed' versions in quant space.\n");
  fprintf(stderr, "  -setdc <dc>     Set the DC component to the given value for every MCU that gets touched.\n");
  fprintf(stderr, "  -debug_mcu <k>  Show the effect of embedding on the kth active MCU.\n");
  fprintf(stderr, "  -clearac        Set all AC components to 0 before embedding.\n");
  fprintf(stderr, "  -component <c>  Components to use in order, eg 'y', 'yu', 'uyv', etc...\n");
  fprintf(stderr, "  -verbose <level> or  -debug   Emit debug output\n");
  fprintf(stderr, "  -version        Print version info and exit.\n");
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

LOCAL(void)
clean_up_statics(void)
{
  if(msgfilename != NULL){ 
    free(msgfilename);
    msgfilename = NULL;
  }

  if(outfilename != NULL){
    free(outfilename);
    outfilename = NULL;
  }

  if(message != NULL){
    free(message);
    message = NULL;
  }

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
{
  int argn, len;
  char * arg;

  for (argn = 1; argn < argc; argn++) {
    arg = argv[argn];
    if (*arg != '-') break;

    arg++;			/* advance past switch marker character */

    if (keymatch(arg, "data", 4)) {
      /* Set output file name. */
      if (++argn >= argc)	/* advance to next argument */
        usage();
      /* coverity: prevent leak if multiple output files */
      if(msgfilename != NULL){ free(msgfilename); }
      msgfilename = strdup(argv[argn]);	/* save it away for later use */
      
    } else if (keymatch(arg, "outfile", 3)) {
      /* Set output file name. */
      if (++argn >= argc)	/* advance to next argument */
        usage();
      /* coverity: prevent leak if multiple output files */
      if(outfilename != NULL){ free(outfilename); }
      outfilename = strdup(argv[argn]);	/* save it away for later use */

    } else if (keymatch(arg, "message", 3)) {
      /* Message string */
      if (++argn >= argc)	/* advance to next argument */
        usage();
      /* coverity:  prevent leak if multiple messages */
      if(message != NULL){ free(message); }
      message = (unsigned char *) strdup(argv[argn]);

    } else if (keymatch(arg, "nolength", 5)) {
      /* Whether to embed length */
      embed_length = 0;
    } else if (keymatch(arg, "verbose", 4)) {
      /* Whether to be verbose */
      if (++argn >= argc)
        usage();
      jel_verbose = true;
      cfg->verbose = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "noecc", 5)) {
      /* Whether to use error correction */
      ecc = 0;
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
      set_freq = true;
    } else if (keymatch(arg, "fmax", 4)) {
      /* Cap the freq. components */
      if (++argn >= argc)
        usage();
      fmax = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "quanta", 4)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      cfg->freqs.nlevels = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "quality", 4)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      quality = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "seed", 4)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      seed = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "nfreqs", 5)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      nfreqs = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "bpf", 3)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      /* jel_init defaults this to 1: */
      cfg->bits_per_freq = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "maxfreqs", 5)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      maxfreq = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "mcudensity", 6)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      mcu_density = strtol(argv[argn], NULL, 10);
    } else if (keymatch(arg, "raw", 3)) {
      cfg->embed_bitstream_header = FALSE;
    } else if (keymatch(arg, "setval", 6)) {
      cfg->set_lsbs = FALSE;
    } else if (keymatch(arg, "normalize", 4)) {
      cfg->normalize = TRUE;
    } else if (keymatch(arg, "clearac", 5)) {
      cfg->clear_ac = TRUE;
    } else if (keymatch(arg, "setdc", 4)) {
      /* Start block */
      if (++argn >= argc)
        usage();
      cfg->set_dc = strtol(argv[argn], NULL, 10);
      if (cfg->set_dc > 255) {
	fprintf(stderr, "Invalid dc value (must be [0,255] or negative): %d\n", cfg->set_dc);
	exit(-1);
      }
    } else if (keymatch(arg, "component", 4)) {
      if (++argn >= argc)
        usage();
      char* compnames = argv[argn];
      // fprintf(stderr, "Component string: %s\n", compnames);
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
    } else if (keymatch(arg, "version", 7)) {
      fprintf(stderr, "wedge version %s (libjel version %s)\n",
              WEDGE_VERSION, jel_version_string());
      exit(-1);
    } else {
      usage();			/* bogus switch */
    }
  }

  if (seed == 0) fprintf(stderr, "unwedge warning: No seed provided.  Embedding will be deterministic and easily detected.\n");
  return argn;			/* return index of next arg (file name) */
}


#if 0
/*
 * Not really needed, but kept in case this is useful later:
 * Marker processor for COM and interesting APPn markers.
 * This replaces the library's built-in processor, which just skips the marker.
 * We want to print out the marker as text, to the extent possible.
 * Note this code relies on a non-suspending data source.
 */

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
#endif

static size_t read_message(char *filename, unsigned char *message, int maxlen, int abort_on_overflow) {
  long length;
  FILE * fp = fopen (filename, "rb");
  size_t retval = 0;
  if(fp == NULL){
    fprintf(stderr, "Opening %s failed\n", filename);
  } else {
    
    fseek (fp, 0, SEEK_END);
    length = ftell (fp);
    fseek (fp, 0, SEEK_SET);
    
    if (length > maxlen)
      fprintf(stderr, "read_message: message of length %ld is too long (maxlen=%d)!\n", length, maxlen);
    if (length < 0) {
      fprintf(stderr, "ftell failed: %s\n", strerror(errno));
    } else {
      size_t bytes = fread(message, 1, length, fp);
      if(bytes < length){
        fprintf(stderr, "Read failed to fetch all the bytes,  only read %" PriSize_t " out of %ld\n", bytes, length);
      } else {
        retval = bytes;
      }
    }
    fclose (fp);
  }
  return retval;
}




int
main (int argc, char **argv)
{
  jel_config *jel;
  jel_freq_spec *fspec;
  int abort_on_overflow = 1;
  //int file_index;
  int i, k, ret;
  int max_bytes;
  FILE *sfp, *dfp;
  time_t x = time(NULL);
  unsigned char * junk = malloc(x % 65535);

  jel = jel_init(JEL_NLEVELS);

  progname = argv[0];
  if (progname == NULL || progname[0] == 0)
    progname = "wedge";		/* in case C library doesn't provide it */

  k = parse_switches(jel, argc, argv);
  /* We will always set this false for now, at least until LSB
     insertion can be debugged properly: */
  jel->set_lsbs = FALSE;

  if (argc == k) {
    usage();
    exit(-1);
  }

  if (jel_verbose) {
    ret = jel_open_log(jel, LOGFILE);
    if (ret == JEL_ERR_CANTOPENLOG) {
      fprintf(stderr, "Can't open %s!!\n", LOGFILE);
      jel->logger = stderr;
    }
  }

  JEL_LOG(jel, 1,"wedge version %s\n", WEDGE_VERSION);

  JEL_LOG(jel, 1, "%s: Setting maxfreqs to %d\n", progname, maxfreq);
  if ( jel_setprop( jel, JEL_PROP_MAXFREQS, maxfreq ) != maxfreq )
    JEL_LOG(jel, 1, "Failed to set frequency pool.\n");

  JEL_LOG(jel, 1, "%s: Setting nfreqs to %d\n", progname, nfreqs);
  if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
    JEL_LOG(jel, 1, "Failed to set number of frequencies.\n");

  JEL_LOG(jel, 1, "%s: Setting MCU density to %d\n", progname, mcu_density);
  if ( jel_setprop( jel, JEL_PROP_MCU_DENSITY, mcu_density ) != mcu_density )
    JEL_LOG(jel, 1, "Failed to set the MCU density.\n");

  if (!ecc) {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_NONE);
    JEL_LOG(jel, 1, "Disabling ECC.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_METHOD));
  } else {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_RSCODE);
    if (ecclen > 0) {
      jel_setprop(jel, JEL_PROP_ECC_BLOCKLEN, ecclen);
      JEL_LOG(jel, 1, "ECC block length set.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_BLOCKLEN));
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
    JEL_LOG(jel, 1, "%s: jel_set_fp_source failed and returns %d.\n", progname, ret);
    fprintf(stderr, "Error - exiting (need a diagnostic!)\n");
    exit(EXIT_FAILURE);
  }
  JEL_LOG(jel, 1,"%s: wedge source %s\n", progname, argv[k]);

  k++;
  /* Risky - this makes the -outfile switch optional and opens the
     door to confusion and mayhem:  */
  if (argc == k && !outfilename) {
    dfp = stdout;
    ret = jel_set_fp_dest(jel, stdout);
  } else {
    if (!outfilename) outfilename = strdup(argv[k]);

    dfp = fopen(outfilename, "wb");
    ret = jel_set_fp_dest(jel, dfp);
    JEL_LOG(jel, 1,"%s: wedge output %s\n", progname, outfilename);

  }

  if (ret != 0) {
    JEL_LOG(jel, 1,"%s: jel error %d\n", progname, ret);
    fprintf(stderr, "Error - exiting (need a diagnostic!)\n");
    exit(EXIT_FAILURE);
  }
  
  JEL_LOG(jel, 1, "%s: Components: %d %d %d\n", progname, comps[0], comps[1], comps[2]);
  if (comps[0] >= 0) jel_set_components(jel, comps[0], comps[1], comps[2]);

  if ( quality > 0 ) {
    JEL_LOG(jel, 1, "%s: Setting output quality to %d\n", progname, quality);
    if ( jel_setprop( jel, JEL_PROP_QUALITY, quality ) != quality )
      JEL_LOG(jel, 1, "Failed to set output quality.\n");
  }

  if ( seed > 0 ) {
    JEL_LOG(jel, 1, "%s: Setting randomization seed to %d\n", progname, seed);
    if ( jel_setprop( jel, JEL_PROP_PRN_SEED, seed ) != seed )
      JEL_LOG(jel, 1, "Failed to set randomization seed.\n");
  }
  
  jel_setprop(jel, JEL_PROP_EMBED_LENGTH, embed_length);

  /* Frequency assignment is sensitive to the order in which things
     are initialized and must come after quality and seed are set.
     Calls to capacity depend on all of frequency, quality, seed, so
     any call to capacity has to come last. */

  fspec = &(jel->freqs);

  fspec->init = 0;
#if 0 
  if (set_freq) {
    printf("set_freq = 1; setting frequencies explicitly.\n");
    jel_init_frequencies(jel, freq, maxfreq);
    JEL_LOG(jel, 1, "wedge: setting frequencies to [");
    for (i = 0; i < maxfreq; i++) JEL_LOG(jel, 1, "%d ", freq[i]);
    JEL_LOG(jel, 1, "]\n");
  } else {
    jel_init_frequencies(jel, NULL, 0);
    JEL_LOG(jel, 1, "wedge: Initializing frequencies to defaults.");    
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
    JEL_LOG(jel, 1, "wedge frequencies: after jel_init_frequencies, fspec->nfreqs is now %d; freqs = [", fspec->nfreqs);
    for (i = 0; i < fspec->maxfreqs; i++) JEL_LOG(jel, 1, "%d ", fspec->freqs[i]);
    JEL_LOG(jel, 1, "]\n");
  }

  if (message) message_length = strlen( (char *) message);
  else {
    max_bytes = jel_capacity(jel);
    message = (unsigned char*)calloc(max_bytes+1, sizeof(unsigned char));
    msg_allocated = true;
    
    JEL_LOG(jel, 1,"%s: wedge data %s\n", progname, msgfilename);
    JEL_LOG(jel, 1, "wedge message address: 0x%lx\n", (unsigned long) message);
    
    message_length = (int) read_message(msgfilename, message, max_bytes, abort_on_overflow);
    JEL_LOG(jel, 1, "%s: Message length to be used is: %d\n", progname, message_length);

  }

  /* If message is NULL, shouldn't we punt? */
  if (!message) {
    JEL_LOG(jel, 1,"No message provided!  Exiting.\n");
    exit(-2);
  }


  //  if (verbose) describeMessage(msg);

  /* Insert the message: */
  JEL_LOG(jel, 1,"Message length is %d.\n", message_length);
  jel_describe(jel, 0);
  ret = jel_embed(jel, message, message_length);
  //  printf("return code = %d\n", ret);
  if (ret < 0) jel_perror("wedge error: ", ret);

  //max_bytes = jel_capacity(jel);

  JEL_LOG(jel, 1, "%s: JPEG compressed to %d bytes.\n", progname, jel->jpeglen);
  if (jel_verbose) jel_close_log(jel);

  if (sfp != NULL && sfp != stdin) fclose(sfp);
  if (dfp != NULL && dfp != stdout) fclose(dfp);

  jel_free(jel);

  clean_up_statics();
  if (msg_allocated) free(message);
  free(junk);

  return 0;			/* suppress no-return-value warnings */
}

/*
 * wcap.c - Report the capacity of a JPEG file.
 */

#include <jel/jel.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>		/* to declare isprint() */

/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */

#define WCAP_VERSION "1.0.1.8"
#define LOGFILE   ".wcap.log"

/* This is an empirically derived fudge factor to account for an
   overestimation of capacity.  */
#define MAGIC_NUMBER 100

static const char * progname;		/* program name for error messages */
static char * outfilename = NULL;	/* for -outfile switch */
static char * msgfilename = NULL;		/* for -infile switch */
static unsigned char * message = NULL;
static bool set_freq = false;
static int freq[64];
static int maxfreq = 6;
static int nfreqs = 1;
//static int mcu_density = 100;
static int mcu_density = -1;
//static int verbose = 0;
extern bool jel_verbose;
static int embed_length = 1;   /* Always embed the length */
//static int message_length;     /* Could be strlen, but not if we read from a file. */
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
  fprintf(stderr, "Checks the steganographic capacity of a JPEG file.\n");
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
  fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=4).\n");
  fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding.\n");
  fprintf(stderr, "  -maxfreqs <M>   Allow M frequency components to be available for embedding.\n");
  fprintf(stderr, "                  Only the first N are used, but if seed is supplied, they are randomized.\n");
  fprintf(stderr, "  -mcudensity <M> Allow M percent of MCUs to be available for embedding.\n");
  fprintf(stderr, "                  M must be <= 100.\n");
  fprintf(stderr, "                  If M is -1 (the default), then all MCUs are used.\n");
  fprintf(stderr, "  -data    <file> Use the contents of the file as the message (alternative to stdin).\n");
  fprintf(stderr, "  -outfile <file> Filename for output image.\n");
  fprintf(stderr, "  -seed <n>       Seed (shared secret) for random frequency selection.\n");
  fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  fprintf(stderr, "  -version        Print version info and exit.\n");
  fprintf(stderr, "  -component <c>  Components to use in order, eg 'y', 'yu', 'uyv', etc...\n");
  fprintf(stderr, "  -dump_mcus      Dump the contents (quantized coefficients) of all MCUs.\n");
  exit(EXIT_FAILURE);
}

static int compname_index(char comp) {
  if (comp == 'y' || comp == 'Y') return 0;
  else if (comp == 'u' || comp == 'U') return 1;
  else if (comp == 'v' || comp == 'V') return 2;
  else return -1;
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
      jel_verbose = true;
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
      if (sscanf(argv[argn], "%d,%d,%d,%d",
                 &freq[0], &freq[1], &freq[2], &freq[3]) != 4)
        usage();
      maxfreq = 4;
      set_freq = true;
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
    } else if (keymatch(arg, "component", 4)) {
      if (++argn >= argc)
        usage();
      char *compnames = argv[argn];
      len = strlen(compnames);
      comps[0] = comps[1] = comps[2] = -1;
      if (len > 0) {
        comps[0] = compname_index(compnames[0]);
        if (len <= 1) {
          comps[1] = -1;
          comps[2] = -1;
        } else {
          comps[1] = compname_index(compnames[1]);
          if (len > 2) comps[2] = compname_index(compnames[2]);
          else comps[2] = -1;
        } 
      }
    } else if (keymatch(arg, "version", 7)) {
      fprintf(stderr, "wedge version %s (libjel version %s)\n",
              WCAP_VERSION, jel_version_string());
      exit(-1);
    } else {
      usage();			/* bogus switch */
    }
  }

  return argn;			/* return index of next arg (file name) */
}

/*
 * The main program.
 */

int
main (int argc, char **argv)
{
  jel_config *jel;
  FILE * input_file;
  int max_bytes, ret, k;
  //  int ecc_method;
  //  int mcudensity;

  jel = jel_init(JEL_NLEVELS);

  ret = jel_open_log(jel, LOGFILE);
  if(ret != 0){
    fprintf(stderr, "Error - exiting (can't open log file)\n");
    exit(EXIT_FAILURE);
  }

  progname = argv[0];
  k = parse_switches(jel, argc, argv);
  if (argc == k) {
    usage();
    exit(-1);
  }

  jel_log(jel, "%s: Setting maxfreqs to %d\n", progname, maxfreq);
  if ( jel_setprop( jel, JEL_PROP_MAXFREQS, maxfreq ) != maxfreq )
    jel_log(jel, "Failed to set frequency pool.\n");

  jel_log(jel, "%s: Setting nfreqs to %d\n", progname, nfreqs);
  if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
    jel_log(jel, "Failed to set frequency pool.\n");

  jel_log(jel, "%s: Setting MCU density to %d\n", progname, mcu_density);
  if ( jel_setprop( jel, JEL_PROP_MCU_DENSITY, mcu_density ) != mcu_density )
    jel_log(jel, "Failed to set the MCU density.\n");

  if (!ecc) {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_NONE);
    jel_log(jel, "Disabling ECC.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_METHOD));
  } else {
    jel_setprop(jel, JEL_PROP_ECC_METHOD, JEL_ECC_RSCODE);
    if (ecclen > 0) {
      jel_setprop(jel, JEL_PROP_ECC_BLOCKLEN, ecclen);
      jel_log(jel, "ECC block length set.  getprop=%d\n", jel_getprop(jel, JEL_PROP_ECC_BLOCKLEN));
    }
  }


  input_file = fopen(argv[k], "rb");
  ret = jel_set_fp_source(jel, input_file);

  if (ret != 0) {
    fprintf(stderr, "Error - exiting - jel_set_fp_source failed to open %s!\n", argv[k]);
    exit(EXIT_FAILURE);
  }

  
  if ( quality > 0 ) {
    jel_log(jel, "%s: Setting output quality to %d\n", progname, quality);
    if ( jel_setprop( jel, JEL_PROP_QUALITY, quality ) != quality )
      jel_log(jel, "Failed to set output quality.\n");
  }
  if ( seed > 0 ) {
    jel_log(jel, "%s: Setting randomization seed to %d\n", progname, seed);
    if ( jel_setprop( jel, JEL_PROP_PRN_SEED, seed ) != seed )
      jel_log(jel, "%s: Failed to set randomization seed.\n", progname);
  }

  if (comps[0] >= 0) jel_set_components(jel, comps[0], comps[1], comps[2]);
  jel_log(jel, "%s: Using components: %d %d %d\n", progname, comps[0], comps[1], comps[2]);
  
  jel_setprop(jel, JEL_PROP_EMBED_LENGTH, embed_length);
  max_bytes = jel_capacity(jel);

  /* this is a real hack.  For unknown reasons, iteratively counting
     the MCUs gives us a number that is too high.  We've empirically
     found that the excess is somewhere around 86.  MAGIC_NUMBER is
     set to 100. */

  //  max_bytes -= MAGIC_NUMBER;
  
  jel_close_log(jel);
  if (input_file != NULL && input_file != stdin) fclose(input_file);

  // jel_capacity now returns image message capacity in bytes,
  // accounting for overhead:
  printf("%d\n", max_bytes);

  exit(0);
}

/*
 * energy.c - Report the frequency domain energies of each MCU in a JPEG file.
 */

#include <jel/jel.h>
#include <stdlib.h>

/*
 * Argument-parsing code.
 * The switch parser is designed to be useful with DOS-style command line
 * syntax, ie, intermixed switches and file names, where only the switches
 * to the left of a given file name affect processing of that file.
 * The main program in this file doesn't actually use this capability...
 */

/*
 * The main program.
 */

int
main (int argc, char **argv)
{
  double* ijel_spectrum(jel_config*);
  int ijel_max_mcus(jel_config*, int);
  jel_config *jel;
  FILE * input_file;
  int  ret;
  // int i, N;
  int i;
  double* out;

  if (argc < 2) {
    fprintf(stderr, "usage: wcap <file> [-noecc]\n");
    exit(-1);
  }

  jel = jel_init(JEL_NLEVELS);

  input_file = fopen(argv[1], "rb");
  if (!input_file) {
    fprintf(stderr, "fopen returned %ld \n", (long) input_file);
    perror("Toast");
    exit(-1);
  }
  
  ret = jel_set_fp_source(jel, input_file);

  if (ret != 0) {
    if (ret == JEL_ERR_INVALIDFPTR) fprintf(stderr, "Invalid file pointer (why?)\n");
    else{
      fprintf(stderr, "Error - exiting (need a diagnostic!)  ret = %d\n", ret);
      exit(EXIT_FAILURE);
    }
  }

  out = ijel_spectrum(jel);
  ijel_max_mcus(jel, 0);

  if (input_file != NULL && input_file != stdin) fclose(input_file);

  for (i = 0; i < DCTSIZE2; i++)
    printf("%f ", out[i] );
  printf("\n");
  exit(0);
}

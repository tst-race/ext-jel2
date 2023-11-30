#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct jel_bs {
  /* Ephemeral state for bit stuffing / unstuffing - bs==bitstream */
  int bit;           /* "active" bit counter */
  int nbits;         /* Total number of bits in message */
  int length_in;     /* Length in bytes of the message */
  int compnum;       /* We might want to throw this away.  Component number. */
  unsigned char *message;
} jel_bs;


void jel_bs_describe( jel_bs *obj ) {
  /* Print a description of the bitstream */
  printf("bit:        %d\n", obj->bit);
  printf("nbits:      %d\n", obj->nbits);
  printf("length_in:  %d\n", obj->length_in);
  printf("compnum:    %d\n", obj->compnum);
  printf("message:    %s\n", obj->message);
}


/* bitstream operations: This API supports bitwise stuffing and
 *  unstuffing into MCUs. */

int jel_bs_reset(jel_bs *obj) {
  /* Reset the bit stream to its initial state */
  obj->bit = 0;
  obj->nbits = obj->length_in * 8;
  obj->compnum = 0;
  return 0;
}



int jel_bs_set_length(jel_bs *obj, int n) {
  /* For "empty" bit streams, set length explicitly. */
  obj->length_in = n;
  jel_bs_reset( obj );
  return obj->nbits;
}



/* Some confusion here, in that we depend on msg being a
 * zero-terminated string, but empty buffers can also be used.  Should
 * we be passing length here?
 */

jel_bs *jel_make_bs(unsigned char *msg) {
  /* Creates and returns a bit stream object from a given string. */
  jel_bs* obj = (jel_bs*) malloc(sizeof(jel_bs));

  obj->length_in = strlen(msg);
  obj->message = msg;
  jel_bs_reset(obj);
  return obj;
}


void jel_bs_free(jel_bs **obj) {
  /* Free the bitstream object */
  free(*obj);
  *obj = NULL;
}


int jel_bs_get_bit(jel_bs *obj, int k) {
  /* Extract the k-th bit from the bitstream.  First compute
   * offsets: */
  int bit_in_byte = k % 8;
  int byte_in_msg = k / 8;

  /* Mask out and shift to return either 1 or 0: */
  unsigned char mask = (1 << bit_in_byte);
  unsigned char byte = obj->message[byte_in_msg];
  int val = (mask & byte) >> bit_in_byte;

  return val;
}
  


int jel_bs_set_bit(jel_bs *obj, int k, int val) {
  /* Set the k-th bit from the bitstream to the value 'val'.  First
   * compute offsets: */
  int bit_in_byte = k % 8;
  int byte_in_msg = k / 8;

  /* Then mask in the bit: */
  unsigned char mask = (val << bit_in_byte);
  /* Extract the appropriate byte: */
  unsigned char byte = obj->message[byte_in_msg];
  unsigned char new = (byte & ~mask) | mask;

  /* Set the altered byte: */
  obj->message[byte_in_msg] = new;

  return val;
}




int jel_bs_get_next_bit(jel_bs *obj) {
  /* Get the next bit and advance the bit counter: */
  int result;
  if (obj->bit >= obj->nbits) return -1;
  else {
    result = jel_bs_get_bit(obj, obj->bit);
    obj->bit++;
    return result;
  }
}


int jel_bs_set_next_bit(jel_bs *obj, int val) {
  /* Set the next bit to 'val' and advance the bit counter: */
  int result;
  if (obj->bit >= obj->nbits) return -1;
  else {
    result = jel_bs_set_bit(obj, obj->bit, val);
    obj->bit++;
    return result;
  }
}






int main (int argc, char **argv) {
  int k;
  jel_bs *in, *out;
  unsigned char *msgcopy, *buf;
  
  if (argc != 2) printf("Usage: %s <message>\n", argv[0]);
  else {
    msgcopy = strdup( argv[1] );

    in = jel_make_bs( msgcopy );
    jel_bs_describe( in );

    buf = calloc( strlen(msgcopy) + 1, 1 );
    out = jel_make_bs( buf );
    jel_bs_set_length( out, strlen(msgcopy) );
    jel_bs_describe( out );

    in->bit = 0;
    while (in->bit < in->nbits) {
      k = jel_bs_get_next_bit( in );
      jel_bs_set_next_bit( out, k );
      printf("%d", k);
    }
    printf("\n");
    printf("Result: %s\n", out->message);
  }
}
 

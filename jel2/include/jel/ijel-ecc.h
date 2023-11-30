#ifndef __IJEL_ECC_H__

#ifdef __cplusplus
extern "C" {
#endif

int ijel_set_ecc_blocklen(int new_len);
int ijel_get_ecc_blocklen();
int ijel_ecc_block_length(int nbytes);
int ijel_ecc_sanity_check(unsigned char *msg, int msglen);
int ijel_capacity_ecc(int nbytes);
int ijel_message_ecc_length(int msglen, int embed_len);
unsigned char *ijel_decode_ecc(unsigned char *ecc, int ecclen, int *msglen);
unsigned char *ijel_encode_ecc(unsigned char *msg, int msglen, int *outlen);
unsigned char *ijel_encode_ecc_nolength(unsigned char *msg, int msglen, int *outlen);
unsigned char *ijel_decode_ecc_nolength(unsigned char *ecc, int ecclen, int length);
  
#ifdef __cplusplus
}
#endif

#define __IJEL_ECC_H__
#endif

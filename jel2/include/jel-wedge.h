/*
 * jel-wedge header.  Defines jel_wedge and jel_unwedge API.
 */

#ifndef __JEL_WEDGE_H__

#include <cstdint>
#include <string>
#include <jel/jel.h>


uint32_t ipv4FromHost (std::string hostname);

int  jel_wedge_init (const char *dirpath);
void jel_wedge_cleanup ();

int jel_wedge   (void *pMsgIn, size_t nMsgIn, jel_config *pJelConfig, void **pMsgOut, size_t *nMsgOut, void *pImgIn = nullptr, size_t nImgIn = 0);
int jel_unwedge (void *pMsgIn, size_t nMsgIn, jel_config *pJelConfig, void **pMsgOut, size_t *nMsgOut);

uint32_t jel_make_key (uint32_t ip1, uint32_t ip2);
int      jel_set_key  (jel_config *pJelConfig, std::string host1, std::string host2);
int      jel_set_key  (jel_config *pJelConfig, uint32_t ip1, uint32_t ip2);
int      jel_set_key  (jel_config *pJelConfig, uint32_t seed);


#define __JEL_WEDGE_H__
#endif

/*
 * JELWedge header.  Encapsulates jel and jel-wedge functionality.
 */

#ifndef __JELWEDGE_H__

#include <RCObj.h>
#include <jel-wedge.h>

class JELWedge : public RCObj {

private:
    jel_config _jel_config;

public:
    static int Init (const char *dirpath);

    JELWedge (int nlevels = 0);
    ~JELWedge ();

    /* Get and set jel_config properties.
     *
     * Examples of properties:
     *   frequency components
     *   quality
     *   source
     *   destination
     */

    int getprop (jel_property prop);
    int setprop (jel_property prop, int value);

    int encode  (void *message, size_t message_length, void **pMsgOut, size_t *nMsgOut, void *pImgIn = nullptr, size_t nImgIn = 0);
    int decode  (void *message, size_t message_length, void **pMsgOut, size_t *nMsgOut);

};

#define __JELWEDGE_H__
#endif

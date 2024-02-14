/*
 * RACEJel header.  Defines JELConfig and RACEJel API.
 */

#ifndef __RACEJEL_H__

#include <cstdint>
#include <cstdio>
#include <functional>
#include "jel-wedge-priv.h"


enum {
  RJEL_WHOLE_MSG   =  1,
  RJEL_PARTIAL_MSG =  0,
  RJEL_OUT_OF_MEM  = -1,
  RJEL_NOT_SEGMENT = -2,
  RJEL_DUP_SEGMENT = -3,
  RJEL_BAD_SEG_IDX = -4,
  RJEL_BAD_NUM_SEG = -5,
  RJEL_EXPIRED_SEG = -6,
  RJEL_NO_SENDER   = -7,
};

typedef enum RJEL_msg_type
{
  RJEL_CT_ORDERED = -1,
  RJEL_CT_GENERAL,
  RJEL_CT_AVIDEO,
  RJEL_CT_D_SVR,

  RJEL_MT_GENERAL = RJEL_CT_GENERAL,
  RJEL_MT_AVIDEO  = RJEL_CT_AVIDEO,
  RJEL_MT_D_SVR   = RJEL_CT_D_SVR
} RJEL_msg_type;

typedef jel_config *jel_cfg_ptr;


class JELConfig
{
  jel_config  _config;
  jel_cfg_ptr _pCfg;

  JELConfig  (int seed);
  JELConfig  (uint32_t ip1, uint32_t ip2);
  JELConfig  (JELConfig *jCfgIn);
  ~JELConfig ();

  jel_cfg_ptr  config ();
  int          capacity (ImagePtr pImage);
  int          capacity (void *pImg, size_t nImg);

  int          set_fp_source  (FILE *dfp);
  int          set_fp_dest    (FILE *sfp);
  int          set_mem_source (void *pImg, size_t nImg);
  int          embed (uint8_t *pMsg, size_t nMsg);

  friend class RACEJel;
  friend class _JELEncoder;
  friend class _ClearConfigMap;

 public:
  int getProp (jel_property prop);
  int setProp (jel_property prop, int value);
};


class MessageWrapper
{
  void *_pMsgWrapperPriv;       // has-a (cf. is-a, which requires advertising _MessageWrapper)

 public:
  MessageWrapper  ();
  ~MessageWrapper ();

  static size_t WrappedSize (size_t nData);

  void   wrap  (void  *pMsg,  size_t  nMsg, RJEL_msg_type mType, std::string toHost);
  void   wrap  (void  *pMsg,  size_t  nMsg, RJEL_msg_type mType, uint32_t    toIP);
#if defined (SWIG)
  int    close (void **pData = nullptr, size_t *nData = nullptr);
#else
  int    close (void **pData, size_t *nData);
#endif
};


typedef void (*RecvMsgCB) (                void *refcon, uint8_t *, size_t);
typedef int  (*SendMsgCB) (uint32_t dstIP, void *refcon, uint8_t *, size_t, RJEL_msg_type);

typedef std::function <void (          void *, uint8_t *, size_t)>                RecvMsgFn;
typedef std::function <int  (uint32_t, void *, uint8_t *, size_t, RJEL_msg_type)> SendMsgFn;

class RACEJel
{
  static uint32_t _hostIP;
  static uint32_t _duration;    // discard segment when epoch_secs + _duration < current time iff _duration > 0

  static int        ProcessJelSegments    (void *pMsgOut, size_t nMsgOut, uint32_t fromIP, void *refcon);

  static JELConfig *GetFromConfig         (std::string fromHost, bool doCreate = false);
  static JELConfig *GetFromConfig         (uint32_t    fromIP,   bool doCreate = false);
  static JELConfig *GetFromConfigTemplate (uint32_t    fromIP,   bool doCreate = true);

  static JELConfig *GetToConfig           (std::string toHost);
  static JELConfig *GetToConfig           (uint32_t    toIP);
  static JELConfig *GetToConfigTemplate   (uint32_t    toIP);

  static void       SetBroadcastConfigs   ();

  friend class _MsgTracker;

 public:

  // Required initialization
                                // Defines template JPEG image directory
  static int  SetImageDirectory (std::string imgPath);

                                // Sets current host IP address
  static int  SetHostIP         (std::string hostname);
  static int  SetHostIP         (uint32_t hostIP = 0UL);

                                // Examine () callback function
  static void SetProcessMsg     (RecvMsgCB processMsg, RJEL_msg_type mType = RJEL_MT_GENERAL);
  static void SetProcessMsg     (RecvMsgFn processMsg, RJEL_msg_type mType = RJEL_MT_GENERAL);
                                // Send () callback function
  static void SetSendMsg        (SendMsgCB sendMsg,    RJEL_msg_type cType = RJEL_CT_ORDERED);
  static void SetSendMsg        (SendMsgFn sendMsg,    RJEL_msg_type cType = RJEL_CT_ORDERED);

                                // Defines inbound and outbound links
  static JELConfig *GetFromConfigTemplate (std::string fromHost);
  static JELConfig *GetToConfigTemplate   (std::string toHost);

  static uint32_t   SetBroadcastHost (std::string broadcastHost, uint32_t broadcastSeed = 0);
  static uint32_t   SetBroadcastIP (uint32_t    broadcastIP,   uint32_t broadcastSeed = 0);

  // Process incoming or send outgoing messages

  static int  Examine   (void *pMsgIn,  size_t nMsgIn,         uint32_t fromIP = 0,  void *refcon = nullptr);

  static int  Send      (void *message, size_t message_length, std::string hostname,
                         void *refcon = nullptr,
                         RJEL_msg_type mType = RJEL_MT_GENERAL,
                         RJEL_msg_type cType = RJEL_CT_ORDERED,
                         void *pImgIn = nullptr, size_t nImgIn = 0);
  static int  Send      (void *message, size_t message_length, uint32_t toIP,
                         void *refcon = nullptr,
                         RJEL_msg_type mType = RJEL_MT_GENERAL,
                         RJEL_msg_type cType = RJEL_CT_ORDERED,
                         void *pImgIn = nullptr, size_t nImgIn = 0);
  static int  Broadcast (void *message, size_t message_length,
                         void *refcon = nullptr,
                         RJEL_msg_type mType = RJEL_MT_GENERAL,
                         RJEL_msg_type cType = RJEL_CT_ORDERED,
                         void *pImgIn = nullptr, size_t nImgIn = 0);

  // Optional configuration

  // discard segment when epoch_secs + _duration < current time iff _duration > 0
  static void SetDuration (uint32_t duration) { _duration = duration; }

  // Optional clean up on shutdown

  static void CleanUp ();

  // For testing

  static int RandomImage (uint8_t **pImage, size_t *nImage);
};


#define __RACEJEL_H__
#endif /* !defined  __RACEJEL_H__ */

#ifndef VIDEO_PREPROCESS_REGS_V3_H
#define VIDEO_PREPROCESS_REGS_V3_H
/* Compatibility spelling for the v3 hardware contract; keep definitions single. */
#include "video_preprocess_regs.h"
#if VPP_PROTOCOL_VERSION != 3u
#error "This header requires the protocol 3 register contract"
#endif
#endif

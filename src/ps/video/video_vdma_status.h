#ifndef VIDEO_VDMA_STATUS_H
#define VIDEO_VDMA_STATUS_H
#include <stdint.h>

/* PG020: bit15 is S2MM EOLLateErr, not a reserved MM2S error bit.
 * IRQ summary/count bits are deliberately not treated as transfer errors. */
#define VIDEO_VDMA_HALTED 0x00000001u
#define VIDEO_VDMA_MM2S_ERRORS 0x000000f0u
#define VIDEO_VDMA_S2MM_ERRORS 0x000089f0u
#define VIDEO_VDMA_S2MM_LINE_ERRORS 0x00008100u
#define VIDEO_VDMA_S2MM_FRAME_ERRORS 0x00000880u
#define VIDEO_VDMA_IRQ_STATUS 0x00007000u
static inline uint32_t video_vdma_errors(uint32_t status, unsigned s2mm) {
    return status & (s2mm ? VIDEO_VDMA_S2MM_ERRORS : VIDEO_VDMA_MM2S_ERRORS);
}
#endif

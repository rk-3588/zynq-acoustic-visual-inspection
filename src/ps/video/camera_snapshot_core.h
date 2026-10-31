#ifndef CAMERA_SNAPSHOT_CORE_H
#define CAMERA_SNAPSHOT_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "video_vdma_status.h"

#define SNAP_VERSION "20261026-camera-snapshot-v3"
#ifndef SNAP_ROOT
#define SNAP_ROOT "/var/lib/acoustic-camera/captures"
#endif
#define SNAP_WIDTH 1280u
#define SNAP_HEIGHT 720u
#define SNAP_STRIDE (SNAP_WIDTH*3u)
#define SNAP_BYTES (SNAP_STRIDE*SNAP_HEIGHT)
#define SNAP_BASE 0x02100000u
#define SNAP_MAP_BYTES (3u*SNAP_BYTES)
#define SNAP_CHUNK 4096u
/* Current design: 64-bit S2MM AXI at FCLK0=100 MHz. Deliberately much
 * tighter than a full-frame write, even at four times that AXI clock:
 * 2,764,800 / (8 * 400 MHz) = 864 us. This is NOT a DMA ownership fence.
 * Reject scheduling gaps instead of trusting before/after pointers (ABA).
 * Reassess this bound if bus width, clock, geometry or buffering changes. */
#define SNAP_MAX_GAP_NS 250000ull
#define SNAP_CHECK_PROGRESS_NS 200000000ull
struct snap_observation {unsigned writer; uint32_t status,control; uint64_t ns;};
enum snap_reject {SNAP_ACCEPT, SNAP_BAD_WRITER, SNAP_DMA_ERROR, SNAP_BAD_CONTROL,
    SNAP_CLOCK_REVERSED, SNAP_GAP, SNAP_OVERWRITE};
struct snap_guard {unsigned target; uint64_t last_ns,max_gap_ns; unsigned polls; enum snap_reject reject;};
static inline int snap_guard_step(struct snap_guard *g,struct snap_observation o) {
    g->reject=SNAP_ACCEPT;
    if(o.writer>2)g->reject=SNAP_BAD_WRITER;
    else if(o.status&(VIDEO_VDMA_S2MM_ERRORS|VIDEO_VDMA_HALTED))g->reject=SNAP_DMA_ERROR;
    else if((o.control&7u)!=3u)g->reject=SNAP_BAD_CONTROL;
    else if(o.ns<g->last_ns)g->reject=SNAP_CLOCK_REVERSED;
    if(g->reject)return 0;
    uint64_t gap=o.ns-g->last_ns;
    if(gap>g->max_gap_ns)g->max_gap_ns=gap;
    if(gap>=SNAP_MAX_GAP_NS){g->reject=SNAP_GAP;return 0;}
    if(o.writer==g->target){g->reject=SNAP_OVERWRITE;return 0;}
    g->last_ns=o.ns;g->polls++;return 1;
}
static inline uint32_t snap_crc32(const unsigned char *p,size_t n) {
    uint32_t table[256],v=~0u;
    for(unsigned i=0;i<256;i++) {
        uint32_t c=i;for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320u:0);
        table[i]=c;
    }
    for(size_t i=0;i<n;i++)v=(v>>8)^table[(v^p[i])&255];
    return ~v;
}
static inline void snap_le32(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));
}
static inline void snap_bmp_header(unsigned char h[54]) {
    memset(h,0,54);h[0]='B';h[1]='M';snap_le32(h+2,54+SNAP_BYTES);
    snap_le32(h+10,54);snap_le32(h+14,40);snap_le32(h+18,SNAP_WIDTH);
    snap_le32(h+22,(uint32_t)-(int32_t)SNAP_HEIGHT);h[26]=1;h[28]=24;
    snap_le32(h+34,SNAP_BYTES);
}
static inline void snap_json_string(FILE *f,const char *s) {
    fputc('"',f);
    for(const unsigned char *p=(const unsigned char *)s;*p;p++) {
        if(*p=='"' || *p=='\\'){fputc('\\',f);fputc(*p,f);}
        else if(*p<32 || *p>=127)fprintf(f,"\\u%04x",*p);
        else fputc(*p,f);
    }
    fputc('"',f);
}
#endif

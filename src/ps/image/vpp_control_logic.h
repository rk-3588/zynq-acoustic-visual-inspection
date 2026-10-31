#ifndef VPP_CONTROL_LOGIC_H
#define VPP_CONTROL_LOGIC_H
#include <stdint.h>
#include "video_preprocess_regs.h"
#define VPP_WIDTH 1280u
#define VPP_HEIGHT 720u
struct vpp_io {
    void *ctx;
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    uint64_t (*milliseconds)(void *);
    void (*pause)(void *);
    int (*stopped)(void *);
};
struct vpp_config {
    uint32_t value, xy0, xy1, enabled;
    int32_t brightness,gain_shift;
};
struct vpp_state {
    struct vpp_config active, pending;
    uint32_t status, frame, pixels, apply, published;
};
struct vpp_stats {
    uint32_t status, frame, apply, area, bright, total;
    struct vpp_config config;
};
enum vpp_result {
    VPP_OK=0, VPP_BAD_HW, VPP_BUSY, VPP_CHANGED, VPP_TIMEOUT, VPP_INTERRUPTED,
    VPP_INVALID_FRAME
};
static inline int vpp_config_valid(const struct vpp_config *c) {
    return !(c->value & ~0xff07u) && (c->value & VPP_MODE_MASK)<=VPP_MODE_GAIN && c->enabled<=1 &&
        c->brightness>=-255 && c->brightness<=255 && c->gain_shift>=-2 && c->gain_shift<=2 &&
        (c->xy0 & 65535u)<(c->xy1 & 65535u) && (c->xy1 & 65535u)<=VPP_WIDTH &&
        (c->xy0>>16)<(c->xy1>>16) && (c->xy1>>16)<=VPP_HEIGHT;
}
static inline int vpp_config_equal(const struct vpp_config *a,const struct vpp_config *b) {
    return a->value==b->value && a->xy0==b->xy0 && a->xy1==b->xy1 && a->enabled==b->enabled &&
        a->brightness==b->brightness && a->gain_shift==b->gain_shift;
}
static inline int vpp_seq_after(uint32_t a,uint32_t b) {
    uint32_t delta=a-b;
    return delta && delta<0x80000000u;
}
static inline enum vpp_result vpp_identity(struct vpp_io *io) {
    if(io->read(io->ctx,VPP_REG_ID)!=VPP_ID_VALUE ||
       io->read(io->ctx,VPP_REG_VERSION)!=VPP_PROTOCOL_VERSION ||
       io->read(io->ctx,VPP_REG_GEOMETRY)!=((VPP_HEIGHT<<16)|VPP_WIDTH) ||
       io->read(io->ctx,VPP_REG_CAPABILITIES)!=VPP_CAPABILITIES_VALUE) return VPP_BAD_HW;
    return VPP_OK;
}
/* Configuration is coherency checked; counters are sequential observations. */
static inline enum vpp_result vpp_observe(struct vpp_io *io,struct vpp_state *s) {
    for(unsigned retry=0;retry<8;++retry) {
        uint32_t before=io->read(io->ctx,VPP_REG_APPLY_SEQ);
        uint32_t status=io->read(io->ctx,VPP_REG_STATUS);
        s->active.value=io->read(io->ctx,VPP_REG_ACTIVE);
        s->active.xy0=io->read(io->ctx,VPP_REG_ACTIVE_ROI_XY0);
        s->active.xy1=io->read(io->ctx,VPP_REG_ACTIVE_ROI_XY1);
        s->active.enabled=io->read(io->ctx,VPP_REG_ACTIVE_ROI_ENABLE);
        s->active.brightness=(int32_t)io->read(io->ctx,VPP_REG_ACTIVE_BRIGHTNESS);
        s->active.gain_shift=(int32_t)io->read(io->ctx,VPP_REG_ACTIVE_GAIN_SHIFT);
        s->pending.value=io->read(io->ctx,VPP_REG_PENDING);
        s->pending.xy0=io->read(io->ctx,VPP_REG_PENDING_ROI_XY0);
        s->pending.xy1=io->read(io->ctx,VPP_REG_PENDING_ROI_XY1);
        s->pending.enabled=io->read(io->ctx,VPP_REG_PENDING_ROI_ENABLE);
        s->pending.brightness=(int32_t)io->read(io->ctx,VPP_REG_PENDING_BRIGHTNESS);
        s->pending.gain_shift=(int32_t)io->read(io->ctx,VPP_REG_PENDING_GAIN_SHIFT);
        s->frame=io->read(io->ctx,VPP_REG_FRAME_SEQ);
        s->pixels=io->read(io->ctx,VPP_REG_PIXELS);
        s->published=io->read(io->ctx,VPP_REG_STATS_PUBLISHED);
        s->status=io->read(io->ctx,VPP_REG_STATUS);
        s->apply=io->read(io->ctx,VPP_REG_APPLY_SEQ);
        if(before!=s->apply || ((status^s->status)&VPP_STATUS_PENDING))continue;
        if((s->status&~7u) || (s->published&~3u) || s->published==2 ||
           !vpp_config_valid(&s->active) ||
           ((s->status&VPP_STATUS_PENDING) && !vpp_config_valid(&s->pending)))return VPP_BAD_HW;
        return VPP_OK;
    }
    return VPP_CHANGED;
}
static inline enum vpp_result vpp_apply(struct vpp_io *io,const struct vpp_config *request,
                                      unsigned timeout_ms,struct vpp_state *last,int *submitted) {
    *submitted=0;
    if(!vpp_config_valid(request) || !timeout_ms)return VPP_BAD_HW;
    enum vpp_result rc=vpp_identity(io);
    if(rc!=VPP_OK)return rc;
    rc=vpp_observe(io,last);if(rc!=VPP_OK)return rc;
    if(last->status&VPP_STATUS_PENDING)return VPP_BUSY;
    if(io->stopped(io->ctx))return VPP_INTERRUPTED;
    uint32_t previous=last->apply;
    const unsigned regs[]={VPP_REG_MODE,VPP_REG_THRESHOLD,VPP_REG_ROI_XY0,VPP_REG_ROI_XY1,
        VPP_REG_ROI_ENABLE,VPP_REG_BRIGHTNESS,VPP_REG_GAIN_SHIFT};
    const uint32_t values[]={request->value&VPP_MODE_MASK,(request->value>>8)&255u,request->xy0,
        request->xy1,request->enabled,(uint32_t)request->brightness,(uint32_t)request->gain_shift};
    for(unsigned i=0;i<7;++i)io->write(io->ctx,regs[i],values[i]);
    for(unsigned i=0;i<7;++i)if(io->read(io->ctx,regs[i])!=values[i])return VPP_CHANGED;
    rc=vpp_observe(io,last);if(rc!=VPP_OK)return rc;
    if((last->status&VPP_STATUS_PENDING) || last->apply!=previous)return VPP_CHANGED;
    if(io->stopped(io->ctx))return VPP_INTERRUPTED;
    io->write(io->ctx,VPP_REG_COMMIT,1);*submitted=1;
    uint64_t begin=io->milliseconds(io->ctx);
    for(;;) {
        rc=vpp_identity(io);if(rc!=VPP_OK)return rc;
        rc=vpp_observe(io,last);if(rc!=VPP_OK)return rc;
        if(last->apply==previous+1u && !(last->status&VPP_STATUS_PENDING))
            return vpp_config_equal(&last->active,request)?VPP_OK:VPP_CHANGED;
        if(last->apply!=previous)return VPP_CHANGED;
        if((last->status&VPP_STATUS_PENDING) && !vpp_config_equal(&last->pending,request))return VPP_CHANGED;
        if(io->stopped(io->ctx))return VPP_INTERRUPTED;
        if(io->milliseconds(io->ctx)-begin>=timeout_ms)return VPP_TIMEOUT;
        io->pause(io->ctx);
    }
}
static inline enum vpp_result vpp_snapshot(struct vpp_io *io,struct vpp_stats *s) {
    /* Caller polls published bit0. One cooperative owner holds the lock. */
    io->write(io->ctx,VPP_REG_STATS_SNAPSHOT,1);
    uint32_t first=io->read(io->ctx,VPP_REG_STATS_FRAME);
    uint32_t epoch=io->read(io->ctx,VPP_REG_STATS_APPLY_SEQ);
    s->status=io->read(io->ctx,VPP_REG_STATS_STATUS);
    s->frame=first;s->apply=epoch;
    s->area=io->read(io->ctx,VPP_REG_STATS_AREA);
    s->bright=io->read(io->ctx,VPP_REG_STATS_BRIGHT);
    s->total=io->read(io->ctx,VPP_REG_STATS_TOTAL);
    uint32_t packed=io->read(io->ctx,VPP_REG_STATS_CONFIG);
    s->config.value=packed&0xffffu;s->config.enabled=(packed>>16)&1u;
    s->config.xy0=io->read(io->ctx,VPP_REG_STATS_ROI_XY0);
    s->config.xy1=io->read(io->ctx,VPP_REG_STATS_ROI_XY1);
    s->config.brightness=(int32_t)io->read(io->ctx,VPP_REG_STATS_BRIGHTNESS);
    s->config.gain_shift=(int32_t)io->read(io->ctx,VPP_REG_STATS_GAIN_SHIFT);
    if(first!=io->read(io->ctx,VPP_REG_STATS_FRAME) || epoch!=io->read(io->ctx,VPP_REG_STATS_APPLY_SEQ))return VPP_CHANGED;
    if((s->status&~15u) || !(s->status&VPP_STATS_AVAILABLE) || (packed&~0x1ff07u) ||
       !vpp_config_valid(&s->config) || !!(s->status&VPP_STATS_ROI_ENABLED)!=s->config.enabled ||
       !!(s->status&VPP_STATS_VALID)==!!(s->status&VPP_STATS_MALFORMED) ||
       s->bright>s->area || s->area>s->total)return VPP_BAD_HW;
    if(s->status&VPP_STATS_VALID) {
        uint32_t area=s->config.enabled?
            ((s->config.xy1&65535u)-(s->config.xy0&65535u))*((s->config.xy1>>16)-(s->config.xy0>>16)):
            VPP_WIDTH*VPP_HEIGHT;
        if(s->total!=VPP_WIDTH*VPP_HEIGHT || s->area!=area)return VPP_BAD_HW;
    }
    return VPP_OK;
}
/* Always wait for a newly published completed frame after entry. Optional
 * after-frame adds a wrap-aware constraint; it never bypasses this freshness. */
static inline enum vpp_result vpp_collect(struct vpp_io *io,unsigned timeout_ms,
        int have_after,uint32_t after,struct vpp_stats *out,int *captured,int *fresh) {
    *captured=0;*fresh=0;
    enum vpp_result rc=vpp_identity(io);if(rc!=VPP_OK)return rc;
    struct vpp_state initial;
    rc=vpp_observe(io,&initial);if(rc!=VPP_OK)return rc;
    if(initial.status&VPP_STATUS_PENDING)return VPP_BUSY;
    if(io->stopped(io->ctx))return VPP_INTERRUPTED;
    int baseline_set=0;uint32_t baseline=0;
    if(initial.published&1u) {
        rc=vpp_snapshot(io,out);if(rc!=VPP_OK)return rc;
        *captured=1;baseline=out->frame;baseline_set=1;
    }
    uint64_t begin=io->milliseconds(io->ctx);
    for(;;) {
        if(io->stopped(io->ctx))return VPP_INTERRUPTED;
        rc=vpp_identity(io);if(rc!=VPP_OK)return rc;
        struct vpp_state current;
        rc=vpp_observe(io,&current);if(rc!=VPP_OK)return rc;
        if(current.apply!=initial.apply || (current.status&VPP_STATUS_PENDING) ||
           !vpp_config_equal(&current.active,&initial.active))return VPP_CHANGED;
        if(current.published&1u) {
            rc=vpp_snapshot(io,out);if(rc!=VPP_OK)return rc;
            *captured=1;
            if((!baseline_set || vpp_seq_after(out->frame,baseline)) &&
               (!have_after || vpp_seq_after(out->frame,after)) &&
               out->apply==initial.apply && vpp_config_equal(&out->config,&initial.active)) {
                *fresh=1;
                return out->status&VPP_STATS_VALID?VPP_OK:VPP_INVALID_FRAME;
            }
        }
        if(io->milliseconds(io->ctx)-begin>=timeout_ms)return VPP_TIMEOUT;
        io->pause(io->ctx);
    }
}
#endif

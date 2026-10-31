#ifndef VIDEO_STATUS_PERF_H
#define VIDEO_STATUS_PERF_H
#include "video_readonly_probe.h"
struct panel_perf {
    struct video_ro_probe vpp;
    struct video_probe_rate rate;
    uint64_t render_us,write_us,verify_us,retire_us,loop_us,max_loop_us;
    uint64_t next_probe_ms;
    int vpp_known;
};
static inline void panel_perf_init(struct panel_perf *p) {
    *p=(struct panel_perf){.vpp={.fd=-1}};
    if(!video_probe_open(&p->vpp,"video_preprocess@40040000",0x40040000u)) {
        p->vpp_known=video_probe_read(&p->vpp,0)==0x56505031u;
        if(!p->vpp_known)video_probe_close(&p->vpp);
    }
}
static inline void panel_perf_tick(struct panel_perf *p,uint64_t now) {
    if(p->loop_us>p->max_loop_us)p->max_loop_us=p->loop_us;
    if(!p->vpp_known||now<p->next_probe_ms)return;
    uint32_t sequence=video_probe_read(&p->vpp,0x1c);
    video_probe_rate_step(&p->rate,sequence,video_probe_ns());
    p->next_probe_ms=now+1000;
}
static inline void panel_perf_close(struct panel_perf *p) {video_probe_close(&p->vpp);}
#endif

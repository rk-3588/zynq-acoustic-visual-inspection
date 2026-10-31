#ifndef VIDEO_UI_WATCHDOG_CORE_H
#define VIDEO_UI_WATCHDOG_CORE_H
#include <stdint.h>
#include "video_vdma_status.h"

#define UI_WATCHDOG_VERSION "20261031-ui-watchdog-v2"
#define UI_WATCHDOG_COOLDOWN_NS 10000000000ull
struct ui_watch_sample {
    uint64_t ns;
    uint32_t sequence, pid;
    int ui_on, camera_ok;
};
struct ui_watch_policy {
    struct ui_watch_sample previous;
    uint64_t cooldown_until;
    uint32_t millihz;
    unsigned hits;
    int rate_valid;
};
static inline int ui_watch_camera_ok(uint32_t read_control,uint32_t read_status,
                                     uint32_t write_control,uint32_t write_status) {
    return (read_control&1) && (write_control&1) &&
        !(read_status&VIDEO_VDMA_HALTED) && !(write_status&VIDEO_VDMA_HALTED) &&
        !video_vdma_errors(read_status,0) && !video_vdma_errors(write_status,1);
}
/* One baseline plus three consecutive healthy ~1-second intervals. Never
 * count through UI restarts, missing samples, camera errors or counter resets. */
static inline int ui_watch_step(struct ui_watch_policy *p,struct ui_watch_sample s) {
    struct ui_watch_sample old=p->previous;
    p->previous=s;p->rate_valid=0;p->millihz=0;
    if(!s.ui_on || !s.camera_ok || !old.ui_on || !old.camera_ok ||
       !s.pid || s.pid!=old.pid || !old.ns || s.ns<=old.ns) {p->hits=0;return 0;}
    uint64_t elapsed=s.ns-old.ns;
    uint32_t delta=s.sequence-old.sequence;
    if(elapsed<800000000ull || elapsed>2000000000ull || delta>10000u) {p->hits=0;return 0;}
    p->millihz=(uint32_t)((uint64_t)delta*1000000000000ull/elapsed);p->rate_valid=1;
    if(old.ns<p->cooldown_until || s.ns<p->cooldown_until ||
       p->millihz<72000 || p->millihz>90000) {p->hits=0;return 0;}
    if(++p->hits<3)return 0;
    p->hits=0;return 1;
}
/* Cooldown starts after the service command finishes, including a rejected or
 * failed attempt. No rolling-window or lifetime recovery-count restriction. */
static inline void ui_watch_attempt_finished(struct ui_watch_policy *p,uint64_t ns) {
    p->cooldown_until=ns+UI_WATCHDOG_COOLDOWN_NS;p->hits=0;
    p->previous=(struct ui_watch_sample){0};p->rate_valid=0;
}
#endif

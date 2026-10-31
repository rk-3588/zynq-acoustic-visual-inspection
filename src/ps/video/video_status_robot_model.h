#ifndef VIDEO_STATUS_ROBOT_MODEL_H
#define VIDEO_STATUS_ROBOT_MODEL_H
#include "video_robot_core.h"
#include "video_robot_modes_core.h"
struct panel_robot_view {
    int fresh,uart_ready,fault,feedback_seen;
    unsigned id,phase,target,modal_region,region_mask;
    int modal,request,error,submitting;
    unsigned revision;
    uint64_t modal_until,status_ms,target_ms,error_until;
    int window,tab,confirm_kind,operation;
    unsigned operation_value,mode,transaction_pending,track_armed,track_region,track_known,track_mask;
    unsigned cycle_known,cycle_ready,cycle_active,cycle_stage,cycle_reason,cycle_limit,cycle_completed,cycle_stopping;
    unsigned mode_error,lease_valid,reply_age_ms,track_target,cycle_draft,cycle_continuous;
    unsigned auto_mode,auto_waiting,auto_latched;
    int grab_auto_choice;
    int col,row;
};
static inline const char *panel_robot_region_label(unsigned region) {
    return region==1?"左侧":region==2?"中间":region==3?"右侧":"--";
}
static inline int panel_robot_ready(const struct panel_robot_view *r,uint64_t now) {
    return !r->auto_mode&&r->fresh&&r->uart_ready&&!r->fault&&r->phase!=ROBOT_WAITING&&r->phase!=ROBOT_UNKNOWN&&
        r->phase!=ROBOT_ACCEPTED&&r->phase!=ROBOT_EXECUTING&&r->target>=1&&r->target<=3&&
        (r->region_mask&(1u<<(r->target-1)))&&
        r->target&&now>=r->target_ms&&now-r->target_ms<=300&&
        now>=r->status_ms&&now-r->status_ms<=1500&&!r->request&&!r->operation&&!r->submitting&&!r->transaction_pending;
}
static inline int panel_robot_idle(const struct panel_robot_view *r,uint64_t now) {
    return !r->auto_mode&&r->fresh&&r->uart_ready&&!r->fault&&r->phase!=ROBOT_WAITING&&r->phase!=ROBOT_UNKNOWN&&
        r->phase!=ROBOT_ACCEPTED&&r->phase!=ROBOT_EXECUTING&&now>=r->status_ms&&now-r->status_ms<=1500&&
        !r->request&&!r->operation&&!r->submitting&&!r->transaction_pending;
}
static inline int panel_robot_auto_toggle(const struct panel_robot_view *r,unsigned mode,uint64_t now){
    return r->fresh&&r->uart_ready&&!r->operation&&!r->submitting&&
        (r->auto_mode==mode||(!r->auto_mode&&panel_robot_idle(r,now)&&
          (mode==1?(r->track_known&&r->track_mask):r->region_mask)));
}
static inline int panel_robot_track_ready(const struct panel_robot_view *r,uint64_t now) {
    return panel_robot_idle(r,now)&&r->track_known&&r->track_target>=1&&r->track_target<=12&&
        (r->track_mask&(1u<<(r->track_target-1)))&&now>=r->target_ms&&now-r->target_ms<=300;
}
static inline int panel_robot_confirmable(const struct panel_robot_view *r,uint64_t now) {
    if(!r->modal||now>=r->modal_until)return 0;
    if(r->confirm_kind==0)return panel_robot_ready(r,now);
    if(r->confirm_kind==1)return r->modal_region==r->track_target&&panel_robot_track_ready(r,now);
    return r->confirm_kind==2&&r->modal_region<=255&&r->cycle_known&&r->cycle_ready&&panel_robot_idle(r,now);
}
#endif

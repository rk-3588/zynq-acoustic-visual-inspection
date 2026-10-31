#ifndef VIDEO_STATUS_FOLLOW_H
#define VIDEO_STATUS_FOLLOW_H
#include "video_status_panel.h"
#include "video_status_image_model.h"
#define PANEL_FOLLOW_PERIOD_MS 200u
#define PANEL_FOLLOW_HOLD_MS 400u
#define PANEL_FOLLOW_DEADZONE 10
#define PANEL_FOLLOW_MAX_STEP 60
static inline void panel_follow_rectangle(int cx,int cy,unsigned w,unsigned h,unsigned *x,unsigned *y) {
    *x=(unsigned)panel_image_bound(cx-(int)w/2,0,1280-(int)w);
    *y=(unsigned)panel_image_bound(cy-(int)h/2,0,720-(int)h);
}
/* Only stable daemon coordinates drive the box; no second stability detector.
 * Quiet/OFF/unknown bypass immediately. An ACTIVE transient may hold <=400ms.
 * This is an approximate UI guide, not synchronous sensor/actuator control. */
static inline void panel_follow_observe(struct panel_image_view *i,const struct panel_status *s,uint64_t now) {
    if(!i->follow)return;
    if(i->follow_last_step_ms&&now-i->follow_last_step_ms<PANEL_FOLLOW_PERIOD_MS)return;
    i->follow_last_step_ms=now;
    int active=s->online&&s->age_ms<=150&&!strcmp(s->sound,"active")&&s->acoustic_known&&
        s->acoustic_enabled&&s->enable_applied;
    int good=active&&s->valid&&s->col>=0&&s->col<64&&s->row>=0&&s->row<36;
    i->follow_hold=0;
    if(good) {
        int cx=20*s->col+10,cy=20*s->row+10;
        if(!i->follow_have){i->follow_cx=cx;i->follow_cy=cy;i->follow_have=1;}
        else {
            int dx=cx-i->follow_cx,dy=cy-i->follow_cy;
            if(abs(dx)>PANEL_FOLLOW_DEADZONE||abs(dy)>PANEL_FOLLOW_DEADZONE) {
                i->follow_cx+=panel_image_bound(dx/2,-PANEL_FOLLOW_MAX_STEP,PANEL_FOLLOW_MAX_STEP);
                i->follow_cy+=panel_image_bound(dy/2,-PANEL_FOLLOW_MAX_STEP,PANEL_FOLLOW_MAX_STEP);
            }
        }
        i->follow_last_good_ms=now;i->follow_visible=1;
    } else if(active&&i->follow_have&&now>=i->follow_last_good_ms&&now-i->follow_last_good_ms<=PANEL_FOLLOW_HOLD_MS) {
        i->follow_visible=1;i->follow_hold=1;
    } else {i->follow_visible=0;i->follow_have=0;i->follow_applied=0;}
    panel_follow_rectangle(i->follow_have?i->follow_cx:640,i->follow_have?i->follow_cy:360,
        panel_follow_width(i),panel_follow_height(i),&i->follow_x,&i->follow_y);
}
static inline void panel_follow_schedule(struct panel_image_view *i,uint64_t now) {
    if(!i->follow||i->follow_fault||!panel_image_available(i)||now<i->follow_next_ms)return;
    if(i->follow_visible&&(i->editing||i->settings||i->popup||i->region_popup))return;
    unsigned mode=i->follow_visible?i->follow_mode:0,w=panel_follow_width(i),h=panel_follow_height(i);
    /* Hidden/pause keeps the last hardware rectangle; it must NOT expand a
     * non-bypass mode to fullscreen. Silence does not keep moving an old box. */
    int same=i->active.roi&&i->active.mode==mode&&(!i->follow_visible||
        (i->active.x==i->follow_x&&i->active.y==i->follow_y&&i->active.w==w&&i->active.h==h));
    if(same){if(i->follow_visible)i->follow_applied=1;return;}
    i->request=PIMG_FOLLOW;i->follow_next_ms=now+PANEL_FOLLOW_PERIOD_MS;
}
#endif

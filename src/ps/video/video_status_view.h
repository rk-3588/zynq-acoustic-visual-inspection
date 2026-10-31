#ifndef VIDEO_STATUS_VIEW_H
#define VIDEO_STATUS_VIEW_H
#include <stdint.h>
#include <stdlib.h>
#include "video_status_image_model.h"
#include "video_status_robot_model.h"
#include "video_status_power_model.h"

#define PANEL_SIDEBAR_ALPHA 180u
enum panel_robot_state {PANEL_ROBOT_DISCONNECTED,PANEL_ROBOT_IDLE,PANEL_ROBOT_EXECUTING,
    PANEL_ROBOT_WAITING,PANEL_ROBOT_UNKNOWN,PANEL_ROBOT_FAILED,PANEL_ROBOT_BUSY,PANEL_ROBOT_INVALID,
    PANEL_ROBOT_COMPLETE,PANEL_ROBOT_TRACKING,PANEL_ROBOT_TESTING,PANEL_ROBOT_HOLD};
static inline const char *panel_robot_label(enum panel_robot_state state) {
    return state==PANEL_ROBOT_IDLE?"待机中":state==PANEL_ROBOT_EXECUTING?"执行中":
        state==PANEL_ROBOT_WAITING?"待回报":state==PANEL_ROBOT_UNKNOWN?"结果未知":
        state==PANEL_ROBOT_FAILED?"执行失败":state==PANEL_ROBOT_BUSY?"设备忙":
        state==PANEL_ROBOT_INVALID?"区域无效":state==PANEL_ROBOT_COMPLETE?"执行完成":
        state==PANEL_ROBOT_TRACKING?"跟踪中":state==PANEL_ROBOT_TESTING?"测试中":state==PANEL_ROBOT_HOLD?"保持":"未连接";
}
static inline const char *panel_robot_name(enum panel_robot_state state) {
    return state==PANEL_ROBOT_IDLE?"idle":state==PANEL_ROBOT_EXECUTING?"executing":
        state==PANEL_ROBOT_WAITING?"waiting_result":state==PANEL_ROBOT_UNKNOWN?"unknown":
        state==PANEL_ROBOT_FAILED?"failed":state==PANEL_ROBOT_BUSY?"busy":
        state==PANEL_ROBOT_INVALID?"invalid_region":state==PANEL_ROBOT_COMPLETE?"complete":
        state==PANEL_ROBOT_TRACKING?"tracking":state==PANEL_ROBOT_TESTING?"testing":state==PANEL_ROBOT_HOLD?"holding":"disconnected";
}
/* Completed is the last persisted workflow result, not a live link heartbeat. */
static inline enum panel_robot_state panel_robot_state_of(const struct panel_robot_view *r) {
    return !r->fresh?PANEL_ROBOT_DISCONNECTED:r->fault||r->phase==ROBOT_UNKNOWN?PANEL_ROBOT_UNKNOWN:
        r->phase==ROBOT_EXECUTING?(r->mode==ROBOT_MODE_TRACK?PANEL_ROBOT_TRACKING:
            r->mode==ROBOT_MODE_CYCLE?PANEL_ROBOT_TESTING:PANEL_ROBOT_EXECUTING):
        r->phase==ROBOT_WAITING||r->phase==ROBOT_ACCEPTED?PANEL_ROBOT_WAITING:
        r->phase==ROBOT_FAILED?PANEL_ROBOT_FAILED:r->phase==ROBOT_INVALID?PANEL_ROBOT_INVALID:
        r->phase==ROBOT_BUSY?PANEL_ROBOT_BUSY:!r->uart_ready?PANEL_ROBOT_DISCONNECTED:
        r->phase==ROBOT_COMPLETE?(r->mode==ROBOT_MODE_TRACK?PANEL_ROBOT_HOLD:PANEL_ROBOT_COMPLETE):
        r->feedback_seen?PANEL_ROBOT_IDLE:PANEL_ROBOT_DISCONNECTED;
}
struct album_packet;
struct panel_view {
    struct panel_image_view image;
    struct panel_robot_view robot;
    struct panel_power_view power;
    enum panel_robot_state robot_state;
    int sidebar_visible, touch_connected, pressed;
    int touch_x, touch_y;
    uint64_t taps;
    int pressed_action,acoustic_known,acoustic_on,acoustic_pending;
    int acoustic_request,acoustic_target;
    uint64_t acoustic_deadline_ms,acoustic_error_until_ms;
    int capture_ready,capture_busy,capture_request,capture_feedback,capture_count_known;
    unsigned capture_count;
    uint64_t capture_feedback_until_ms;
    uint64_t capture_checked_ms;
    char capture_reason[40];
    int capture_status_known;
    uint32_t capture_s2mm_status,capture_error_bits;
    int gallery_ready,gallery_open,gallery_busy,gallery_error,gallery_detail,gallery_json;
    unsigned gallery_page,gallery_pages,gallery_items,gallery_selected,gallery_request;
    unsigned gallery_revision,gallery_json_page,gallery_json_pages;
    const struct album_packet *gallery_data;
};
struct panel_hitbox {int x,y,w,h;};
static inline struct panel_hitbox panel_robot_box(int action) {
    if(action==70)return (struct panel_hitbox){824,634,80,56};
    if(action==71)return (struct panel_hitbox){408,394,220,48};
    if(action==72)return (struct panel_hitbox){652,394,220,48};
    if(action==90)return (struct panel_hitbox){968,108,48,48};
    if(action>=91&&action<=93)return (struct panel_hitbox){264+256*(action-91),184,240,48};
    if(action==95)return (struct panel_hitbox){824,112,128,40};
    if(action==94||action==98||action==105)return (struct panel_hitbox){264,532,360,52};
    if(action==96)return (struct panel_hitbox){712,252,304,48};
    if(action==97||action==99)return (struct panel_hitbox){656,532,360,52};
    if(action==100)return (struct panel_hitbox){280,324,64,56};
    if(action==101)return (struct panel_hitbox){536,324,64,56};
    if(action==102)return (struct panel_hitbox){280,408,320,48};
    if(action==103||action==104)return (struct panel_hitbox){264+180*(action-103),248,172,44};
    return (struct panel_hitbox){0,0,0,0};
}
static inline void panel_robot_action(struct panel_view *v,int action,uint64_t now) {
    struct panel_robot_view *r=&v->robot;
    if(action==70){r->window=1;if(!r->cycle_draft)r->cycle_draft=1;r->revision++;
    }else if(action==90){r->window=0;r->revision++;
    }else if(action>=91&&action<=93){r->tab=action-91;r->revision++;
    }else if(action==95&&panel_robot_idle(r,now)){r->operation=1;r->revision++;
    }else if(action==96&&panel_robot_auto_toggle(r,1,now)){
        r->operation=r->auto_mode==1?9:7;r->revision++;
    }else if(action==105&&panel_robot_auto_toggle(r,2,now)){
        r->operation=r->auto_mode==2?9:8;r->revision++;
    }else if(action==103||action==104){
        r->grab_auto_choice=action==104;
        if(action==103&&r->auto_mode==2&&!r->operation)r->operation=9;
        r->revision++;
    }else if((action==94&&!r->grab_auto_choice&&panel_robot_ready(r,now))||
             (action==98&&panel_robot_idle(r,now)&&r->cycle_known&&r->cycle_ready)){
        r->confirm_kind=action==94?0:2;r->modal=1;
        r->modal_region=r->confirm_kind==0?r->target:r->confirm_kind==1?r->track_target:r->cycle_continuous?0:r->cycle_draft;
        r->modal_until=now+10000;r->revision++;
    }else if(action==71){r->modal=0;r->revision++;}
    else if(action==72&&panel_robot_confirmable(r,now)){
        if(r->confirm_kind==0)r->request=(int)r->target;
        else {r->operation=r->confirm_kind==1?2:4;r->operation_value=r->modal_region;}
        r->modal=0;r->revision++;
    }else if(action==97||action==99){r->operation=action==97?3:5;r->revision++;
    }else if(action>=100&&action<=102&&panel_robot_idle(r,now)){
        if(action==100&&r->cycle_draft>1)r->cycle_draft--;
        if(action==101&&r->cycle_draft<255)r->cycle_draft++;
        if(action==102)r->cycle_continuous=!r->cycle_continuous;
        r->revision++;
    }
}
static inline struct panel_hitbox panel_acoustic_box(void) {
    return (struct panel_hitbox){32,628,160,64};
}
static inline struct panel_hitbox panel_capture_box(void) {
    return (struct panel_hitbox){600,622,160,80};
}
static inline struct panel_hitbox panel_gallery_box(void){return (struct panel_hitbox){916,634,174,56};}
static inline struct panel_hitbox panel_gallery_target(const struct panel_view *v,int action) {
    if(action==5)return (struct panel_hitbox){1216,12,48,48};
    if(action==6||action==15)return (struct panel_hitbox){776,648,152,52};
    if(action==7||action==16)return (struct panel_hitbox){1096,648,152,52};
    if(action==8)return (struct panel_hitbox){1104,12,96,48};
    if(action==9)return (struct panel_hitbox){776,84,160,48};
    if(action==14)return (struct panel_hitbox){1088,84,160,48};
    if(action>=10&&action<=13)return (struct panel_hitbox){776,112+128*(action-10),472,116};
    (void)v;return (struct panel_hitbox){0,0,0,0};
}
static inline int panel_gallery_action(struct panel_view *v,int action) {
    if(action==4){v->gallery_open=1;v->gallery_page=0;v->gallery_detail=v->gallery_json=0;v->gallery_request=1;}
    else if(action==5){v->gallery_open=0;v->gallery_request=0;v->gallery_busy=0;}
    else if(action==6||action==7){v->gallery_page+=action==6?(unsigned)-1:1;v->gallery_request=1;}
    else if(action==8){v->gallery_request=1;}
    else if(action==9){v->gallery_detail=v->gallery_json=0;v->gallery_request=1;}
    else if(action>=10&&action<=13){v->gallery_selected=(unsigned)(action-10);v->gallery_detail=1;v->gallery_json=0;v->gallery_request=2;}
    else if(action==14){v->gallery_json=!v->gallery_json;v->gallery_json_page=0;return action;}
    else if(action==15||action==16){v->gallery_json_page+=action==15?(unsigned)-1:1;return action;}
    else return 0;
    v->gallery_revision++;v->gallery_error=0;
    if(v->gallery_open)v->gallery_busy=1;
    return action;
}
static inline struct panel_hitbox panel_toggle_box(int visible) {
    return (struct panel_hitbox){visible?812:1216,224,48,64};
}
static inline struct panel_hitbox panel_toggle_visual_box(int visible) {
    struct panel_hitbox box=panel_toggle_box(visible);
    box.x+=box.w-28;box.w=28;return box;
}
static inline int panel_inside(struct panel_hitbox b,int x,int y) {
    return x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h;
}
static inline int panel_robot_hit(const struct panel_robot_view *r,int x,int y,uint64_t now){
    const int actions[]={90,91,92,93,95,94,96,98,99,100,101,102,103,104,105};
    for(unsigned n=0;n<sizeof(actions)/sizeof(*actions);n++){
        int a=actions[n],enabled=a<=93;
        if(a==95)enabled=panel_robot_idle(r,now);
        if(a==94)enabled=r->tab==0&&!r->grab_auto_choice&&panel_robot_ready(r,now);
        if(a==96)enabled=r->tab==1&&panel_robot_auto_toggle(r,1,now);
        if(a==98)enabled=r->tab==2&&panel_robot_idle(r,now)&&r->cycle_known&&r->cycle_ready;
        if(a==97||a==99)enabled=r->tab==(a==97?1:2)&&r->fresh&&r->uart_ready&&
            r->mode==(a==97?ROBOT_MODE_TRACK:ROBOT_MODE_CYCLE)&&
            (r->phase==ROBOT_WAITING||r->phase==ROBOT_UNKNOWN||r->phase==ROBOT_EXECUTING)&&!r->operation;
        if(a>=100&&a<=102)enabled=r->tab==2&&panel_robot_idle(r,now)&&(!r->cycle_continuous||a==102);
        if(a==100)enabled=enabled&&r->cycle_draft>1;
        if(a==101)enabled=enabled&&r->cycle_draft<255;
        if(a==103||a==104)enabled=r->tab==0&&!r->operation&&!r->submitting;
        if(a==105)enabled=r->tab==0&&r->grab_auto_choice&&panel_robot_auto_toggle(r,2,now);
        if(enabled&&panel_inside(panel_robot_box(a),x,y))return a;
    }
    return 0;
}
#include "video_status_image_view.h"
static inline struct panel_hitbox panel_power_box(int action) {
    if(action==80)return (struct panel_hitbox){776,668,224,48};
    if(action==81)return (struct panel_hitbox){408,394,220,48};
    if(action==82)return (struct panel_hitbox){652,394,220,48};
    return (struct panel_hitbox){0,0,0,0};
}
static inline int panel_power_blocked(const struct panel_view *v) {
    return v->capture_busy||v->capture_request||v->gallery_busy||v->image.request||v->image.pending||
        v->robot.auto_mode||v->robot.request||v->robot.operation||v->robot.submitting||v->robot.transaction_pending||v->robot.phase==ROBOT_WAITING||
        v->robot.phase==ROBOT_ACCEPTED||v->robot.phase==ROBOT_EXECUTING||
        v->robot.phase==ROBOT_UNKNOWN;
}
static inline void panel_power_action(struct panel_view *v,int action,uint64_t now) {
    struct panel_power_view *p=&v->power;
    if(action==80&&v->image.settings&&!p->phase&&!panel_power_blocked(v)) {
        p->phase=PANEL_POWER_CONFIRM;p->confirm_until=now+30000;p->error=0;p->revision++;
    } else if(action==81&&(p->phase==PANEL_POWER_CONFIRM||p->phase==PANEL_POWER_FAILED)) {
        p->phase=PANEL_POWER_IDLE;p->request=p->error=p->slow=0;p->revision++;
    } else if(action==82&&p->phase==PANEL_POWER_CONFIRM&&now<p->confirm_until&&!panel_power_blocked(v)) {
        p->phase=PANEL_POWER_PENDING;p->request=1;p->revision++;
    }
}
static inline void panel_enable_view_update(struct panel_view *v,int fresh,int enabled,int applied,uint64_t now) {
    if(v->acoustic_error_until_ms && now>=v->acoustic_error_until_ms)v->acoustic_error_until_ms=0;
    if(v->acoustic_deadline_ms && fresh && applied && enabled==v->acoustic_target) {
        v->acoustic_deadline_ms=0;v->acoustic_error_until_ms=0;
    }
    if(v->acoustic_deadline_ms && now>=v->acoustic_deadline_ms) {
        v->acoustic_deadline_ms=0;v->acoustic_error_until_ms=now+3000;
    }
    if(fresh && applied && enabled==v->acoustic_target)v->acoustic_error_until_ms=0;
    v->acoustic_pending=v->acoustic_deadline_ms!=0 || (fresh && !applied);
    v->acoustic_known=fresh && applied && !v->acoustic_pending;
    if(v->acoustic_known)v->acoustic_on=enabled;
}
struct panel_tap {
    int was_down, armed, blocked, start_x, start_y;
    int action;
    int gallery_context,swipe_armed;
    unsigned revision;
    unsigned image_revision;
    int image_context,roi_gesture,slider_gesture;
    int robot_context;unsigned robot_revision;
    unsigned power_context,power_revision;
    struct panel_hitbox target;
    uint64_t start_ms, last_tap_ms;
};
static inline void panel_tap_cancel(struct panel_tap *t) {
    t->armed=0;t->swipe_armed=0;t->roi_gesture=0;t->slider_gesture=0;t->blocked=1;
}
static inline void panel_tap_cancel_view(struct panel_tap *t,struct panel_view *v) {
    if(t->slider_gesture)v->image.slider_dragging=0;
    panel_tap_cancel(t);
}
/* Commit one complete SYN_REPORT, never act on a partial event packet. */
static inline int panel_tap_report(struct panel_tap *t,struct panel_view *v,
                                   int down,int contacts,int x,int y,int valid,uint64_t now) {
    v->touch_x=valid?x:-1; v->touch_y=valid?y:-1;
    if(contacts>1 || (down && !valid)) {
        if(t->roi_gesture){v->image.dragging=0;v->image.preview_valid=0;}
        panel_tap_cancel_view(t,v);
    }
    if(t->blocked) {
        if(!down) {t->blocked=0;t->was_down=0;}
        else t->was_down=1;
        v->pressed=0; return 0;
    }
    if(down && !t->was_down) {
        t->power_context=v->power.phase;t->power_revision=v->power.revision;
        t->robot_context=v->robot.modal;t->robot_revision=v->robot.revision;
        t->image_context=panel_image_context(v);t->image_revision=v->image.revision;
        t->roi_gesture=t->slider_gesture=0;
        t->gallery_context=v->gallery_open;t->revision=v->gallery_revision;t->swipe_armed=0;
        t->target=(struct panel_hitbox){0,0,0,0};t->action=0;
        if(v->power.phase) {
            if(v->power.phase==PANEL_POWER_CONFIRM||v->power.phase==PANEL_POWER_FAILED) {
                if(panel_inside(panel_power_box(81),x,y)) {t->action=81;t->target=panel_power_box(81);}
                if(v->power.phase==PANEL_POWER_CONFIRM&&now<v->power.confirm_until&&!panel_power_blocked(v)&&
                   panel_inside(panel_power_box(82),x,y)) {t->action=82;t->target=panel_power_box(82);}
            }
        } else if(v->robot.modal) {
            for(int a=71;a<=72;a++)if((a==71||panel_robot_confirmable(&v->robot,now))&&panel_inside(panel_robot_box(a),x,y)){
                t->action=a;t->target=panel_robot_box(a);
            }
        } else if(v->robot.window) {
            t->action=panel_robot_hit(&v->robot,x,y,now);t->target=panel_robot_box(t->action);
        } else if(v->gallery_open) {
            if(!v->gallery_detail&&x<752){t->action=5;t->target=(struct panel_hitbox){0,0,752,720};}
            for(int action=5;action<=16;action++) {
                int enabled=action==5;
                if(!v->gallery_busy) {
                    enabled|=(!v->gallery_detail&&action==6&&v->gallery_page>0)||
                        (!v->gallery_detail&&action==7&&v->gallery_page+1<v->gallery_pages)||
                        (!v->gallery_detail&&action==8)||
                        (v->gallery_detail&&action==9)||
                        (!v->gallery_detail&&action>=10&&action<=13&&(unsigned)(action-10)<v->gallery_items)||
                        (v->gallery_detail&&!v->gallery_error&&action==14)||
                        (v->gallery_detail&&v->gallery_json&&action==15&&v->gallery_json_page>0)||
                        (v->gallery_detail&&v->gallery_json&&action==16&&v->gallery_json_page+1<v->gallery_json_pages);
                }
                struct panel_hitbox box=panel_gallery_target(v,action);
                if(enabled&&panel_inside(box,x,y)){t->action=action;t->target=box;}
            }
            t->swipe_armed=!v->gallery_busy&&!v->gallery_detail&&panel_inside((struct panel_hitbox){776,112,472,500},x,y);
        } else if(v->image.editing && y>=64 && y<616 && valid) {
            t->roi_gesture=1;t->start_x=x;t->start_y=y;t->start_ms=now;
            v->image.dragging=1;v->image.preview_valid=0;
        } else if(panel_image_context(v)) {
            t->action=panel_image_hit(v,x,y);t->target=panel_image_target(t->action);
            if(v->image.settings&&!panel_power_blocked(v)&&panel_inside(panel_power_box(80),x,y)) {
                t->action=80;t->target=panel_power_box(80);
            }
            if(t->action==39||t->action==33)t->target=(struct panel_hitbox){0,64,1280,552};
        } else {
            t->target=panel_toggle_box(v->sidebar_visible);t->action=1;
            if(v->acoustic_known && !v->acoustic_pending && panel_inside(panel_acoustic_box(),x,y)) {
                t->target=panel_acoustic_box();t->action=2;
            }
            if(v->capture_ready && !v->capture_busy && panel_inside(panel_capture_box(),x,y)) {
                t->target=panel_capture_box();t->action=3;
            }
            if(v->gallery_ready&&panel_inside(panel_gallery_box(),x,y)){t->target=panel_gallery_box();t->action=4;}
            if(panel_inside(panel_robot_box(70),x,y)){
                t->target=panel_robot_box(70);t->action=70;
            }
            int image_action=panel_image_hit(v,x,y);
            if(image_action){t->action=image_action;t->target=panel_image_target(image_action);}
        }
        t->start_x=x; t->start_y=y; t->start_ms=now;
        t->armed=valid && panel_inside(t->target,x,y) &&
            (!t->last_tap_ms || now-t->last_tap_ms>=200);
        if(t->armed&&(t->action==60||t->action==61)) {
            t->slider_gesture=1;v->image.slider_dragging=1;
        }
    }
    if(t->slider_gesture) {
        struct panel_image_view *i=&v->image;
        unsigned mode=t->action==61?3:2;
        int same=!v->power.phase&&!v->robot.window&&!v->gallery_open&&i->settings&&i->ready&&i->known&&!i->pending&&!i->request&&
            i->slider_dragging&&t->image_revision==i->revision&&panel_image_selected_mode(i)==mode;
        /* Read-only background polls may be busy during a gesture. They do
         * not change the draft; a real write/config change cancels below. */
        if(!same||!valid||now<t->start_ms||now-t->start_ms>5000||
           y<t->target.y-32||y>=t->target.y+t->target.h+32) {
            panel_tap_cancel_view(t,v);t->blocked=down;t->was_down=down;v->pressed=0;return 0;
        }
        i->slider_value=panel_image_slider_value(x,mode);
        int action=0;
        if(!down) {
            if(mode==2)i->draft_threshold=i->slider_value;else i->draft_brightness=i->slider_value;
            i->slider_dragging=0;i->revision++;action=t->action;
            t->slider_gesture=t->armed=0;t->last_tap_ms=now;v->taps++;
        }
        t->was_down=down;v->pressed=down;v->pressed_action=down?t->action:0;
        return action;
    }
    if(t->roi_gesture) {
        if(valid)panel_image_preview(&v->image,t->start_x,t->start_y,x,y);
        if(!down){v->image.dragging=0;t->roi_gesture=0;if(!valid)v->image.preview_valid=0;}
        t->was_down=down;v->pressed=0;return 0;
    }
    if(down && t->armed) {
        int dx=x-t->start_x,dy=y-t->start_y;
        if(dx*dx+dy*dy>18*18 || !panel_inside(t->target,x,y) || now-t->start_ms>1500)
            t->armed=0;
    }
    int toggled=0;
    if(!down && t->was_down) {
        int same=t->power_context==v->power.phase&&t->power_revision==v->power.revision&&
            t->robot_context==v->robot.modal&&t->robot_revision==v->robot.revision&&
            t->gallery_context==v->gallery_open&&(t->revision==v->gallery_revision||t->action==5)&&
            t->image_context==panel_image_context(v)&&(t->image_revision==v->image.revision||
                t->action==20||t->action==24||t->action==26||t->action==31||t->action==33||t->action==39);
        int dy=y-t->start_y,dx=x-t->start_x;
        if(t->swipe_armed&&same&&valid&&!v->gallery_busy&&now-t->start_ms<=1500&&
           (dy>=60||dy<=-60)&&abs(dy)>=2*abs(dx)&&(!t->last_tap_ms||now-t->last_tap_ms>=200)) {
            int action=dy<0?7:6;
            if((action==7&&v->gallery_page+1<v->gallery_pages)||(action==6&&v->gallery_page>0))
                toggled=panel_gallery_action(v,action);
            t->armed=0;
        }
        if(!toggled&&same&&t->armed && valid && panel_inside(t->target,x,y) && now-t->start_ms<=1500) {
            if(t->action==1)v->sidebar_visible=!v->sidebar_visible;
            else if(t->action==2){v->acoustic_request=v->acoustic_on?-1:1;v->acoustic_pending=1;}
            else if(t->action==3){v->capture_request=1;v->capture_busy=1;}
            else if(t->action>=80&&t->action<=82)panel_power_action(v,t->action,now);
            else if((t->action>=70&&t->action<=72)||(t->action>=90&&t->action<=105))panel_robot_action(v,t->action,now);
            else if(t->action>=20)panel_image_action(v,t->action);
            else panel_gallery_action(v,t->action);
            toggled=t->action;
        }
        if(toggled){v->taps++;t->last_tap_ms=now;}
        t->armed=0;
    }
    t->was_down=down; v->pressed=down && t->armed;v->pressed_action=v->pressed?t->action:0;
    return toggled;
}
#endif

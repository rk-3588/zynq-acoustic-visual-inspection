#ifndef VIDEO_STATUS_ROBOT_RENDER_H
#define VIDEO_STATUS_ROBOT_RENDER_H
static inline void panel_robot_button(uint32_t *p,const struct panel_view *v,int action,const char *label,int enabled,int warning){
    struct panel_hitbox b=panel_robot_box(action);
    uint32_t cyan=panel_color(42,213,239,255),amber=panel_color(240,185,90,255);
    uint32_t edge=panel_color(35,70,90,235),muted=panel_color(130,146,164,255);
    uint32_t color=enabled?(warning?amber:cyan):muted;
    int pressed=v->pressed&&v->pressed_action==action;
    panel_roundrect_set(p,b.x,b.y,b.w,b.h,12,enabled?color:edge);
    panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,11,
        pressed?panel_color(25,77,90,255):warning?panel_color(47,38,24,250):panel_color(16,26,42,250));
    unsigned font=action==95?PANEL_F_SMALL:PANEL_F_TITLE;
    panel_toolbar_center(p,b,b.y+(b.h-(int)panel_fonts[font].height)/2,label,font,color);
}
static inline void panel_robot_switch(uint32_t *p,const struct panel_view *v,int action,const char *label,int on,int enabled){
    struct panel_hitbox b=panel_robot_box(action);
    uint32_t cyan=panel_color(42,213,239,255),muted=panel_color(130,146,164,255),edge=panel_color(35,70,90,235);
    panel_roundrect_set(p,b.x,b.y,b.w,b.h,12,on?cyan:edge);
    panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,11,v->pressed&&v->pressed_action==action?
        panel_color(25,77,90,255):panel_color(16,26,42,250));
    panel_text(p,b.x+16,b.y+(b.h-(int)panel_fonts[PANEL_F_TITLE].height)/2,label,PANEL_F_TITLE,enabled?cyan:muted);
    int x=b.x+b.w-80,y=b.y+(b.h-28)/2;
    panel_roundrect_set(p,x,y,64,28,14,on?panel_color(18,118,132,255):panel_color(51,66,82,255));
    panel_roundrect_set(p,x+(on?38:4),y+4,20,20,10,on?cyan:muted);
}
static inline void panel_robot_window(uint32_t *p,const struct panel_view *v){
    const struct panel_robot_view *r=&v->robot;
    uint64_t now=r->target_ms;
    uint32_t ink=panel_color(226,235,243,255),muted=panel_color(163,178,195,255);
    uint32_t cyan=panel_color(42,213,239,255),amber=panel_color(240,185,90,255),edge=panel_color(35,70,90,235);
    panel_rect(p,0,64,1280,552,panel_color(4,8,15,105));
    panel_roundrect_set(p,240,96,800,520,20,edge);
    panel_roundrect_set(p,241,97,798,518,19,panel_color(12,21,34,250));
    panel_text(p,264,112,"机械臂",PANEL_F_TITLE,ink);
    panel_text(p,632,122,"UART1 / 9600",PANEL_F_SMALL,muted);
    panel_robot_button(p,v,95,"读取标定",panel_robot_idle(r,now),0);
    panel_robot_button(p,v,90,"X",1,0);
    panel_rect(p,264,168,752,1,edge);
    const char *tabs[]={"夹取","跟踪","测试"};
    for(int n=0;n<3;n++){
        struct panel_hitbox b=panel_robot_box(91+n);int on=r->tab==n;
        panel_roundrect_set(p,b.x,b.y,b.w,b.h,12,on?cyan:edge);
        panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,11,on?panel_color(14,60,74,255):panel_color(16,26,42,245));
        panel_toolbar_center(p,b,194,tabs[n],PANEL_F_TITLE,on?cyan:muted);
    }
    char text[128];
    if(r->tab==0){
        for(int a=103;a<=104;a++){
            struct panel_hitbox b=panel_robot_box(a);int on=(a==104)==r->grab_auto_choice;
            panel_roundrect_set(p,b.x,b.y,b.w,b.h,10,on?cyan:edge);
            panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,9,on?panel_color(14,60,74,255):panel_color(16,26,42,245));
            panel_toolbar_center(p,b,b.y+7,a==103?"手动夹取":"自动夹取",PANEL_F_LABEL,on?cyan:muted);
        }
        panel_card(p,264,304,352,196);panel_card(p,632,256,384,244);
        panel_text(p,284,314,"声源目标",PANEL_F_LABEL,muted);
        panel_text(p,284,352,panel_robot_region_label(r->target),PANEL_F_TITLE,cyan);
        if(r->target)snprintf(text,sizeof(text),"COL %02d / ROW %02d",r->col,r->row);else strcpy(text,"COL -- / ROW --");
        panel_text(p,284,390,text,PANEL_F_MONO,ink);
        panel_text(p,284,430,r->auto_mode==2?(r->auto_latched?"本次目标已触发":"等待稳定目标"):
            r->target?"实时网格坐标":"等待声源",PANEL_F_LABEL,r->target?cyan:amber);
        panel_text(p,284,468,r->grab_auto_choice?"静音后才等待下一次":"夹取按左、中、右三分区",PANEL_F_SMALL,muted);
        panel_text(p,652,274,"机械臂状态",PANEL_F_LABEL,muted);
        panel_text(p,652,314,panel_robot_label(panel_robot_state_of(r)),PANEL_F_TITLE,ink);
        snprintf(text,sizeof(text),"当前任务  #%u",r->id);panel_text(p,652,354,text,PANEL_F_LABEL,muted);
        if(r->grab_auto_choice)panel_robot_switch(p,v,105,"自动夹取",r->auto_mode==2,panel_robot_auto_toggle(r,2,now));
        else panel_robot_button(p,v,94,"夹取当前目标",panel_robot_ready(r,now),1);
    }else if(r->tab==1){
        panel_text(p,264,252,"固定相机 / 12 区朝向",PANEL_F_SMALL,muted);
        for(unsigned n=0;n<12;n++){
            int x=264+104*(int)(n%4),y=288+72*(int)(n/4);
            int calibrated=r->track_known&&(r->track_mask&(1u<<n));
            int target=r->track_target==n+1,accepted=r->track_armed&&r->track_region==n+1;
            panel_roundrect_set(p,x,y,96,64,10,accepted?amber:target?cyan:edge);
            panel_roundrect_set(p,x+1,y+1,94,62,9,accepted?panel_color(54,41,24,255):panel_color(16,26,42,250));
            snprintf(text,sizeof(text),"%02u",n+1);
            panel_toolbar_center(p,(struct panel_hitbox){x,y,96,64},y+5,text,PANEL_F_MONO,calibrated?ink:muted);
            panel_toolbar_center(p,(struct panel_hitbox){x,y,96,64},y+36,
                !calibrated?"未标定":accepted?"已接受":target?"声源区":"已标定",PANEL_F_SMALL,calibrated?(accepted?amber:cyan):muted);
        }
        panel_robot_switch(p,v,96,"自动跟踪",r->auto_mode==1,panel_robot_auto_toggle(r,1,now));
        panel_text(p,712,328,r->auto_mode==1?(r->auto_waiting?"等待稳定声源":"实时跟踪中"):
            r->mode==ROBOT_MODE_TRACK?panel_robot_label(panel_robot_state_of(r)):"未开启",PANEL_F_TITLE,ink);
        if(r->track_target&&r->track_region)snprintf(text,sizeof(text),"声源区 %02u / 目标区 %02u",r->track_target,r->track_region);
        else if(r->track_target)snprintf(text,sizeof(text),"声源区 %02u / 目标区 --",r->track_target);
        else strcpy(text,"声源区 -- / 目标区 --");
        panel_text(p,712,374,text,PANEL_F_SMALL,cyan);
        panel_text(p,712,408,!r->track_known?"请先读取朝向标定":r->track_target&&
            !(r->track_mask&(1u<<(r->track_target-1)))?"该声源区尚未标定":r->track_target?"声源位置有效":"等待稳定声源",PANEL_F_SMALL,muted);
    }else{
        panel_card(p,264,256,352,244);panel_card(p,632,256,384,244);
        panel_text(p,284,274,"A / B 循环次数",PANEL_F_LABEL,muted);
        int edit=panel_robot_idle(r,now);
        panel_robot_button(p,v,100,"-",edit&&!r->cycle_continuous&&r->cycle_draft>1,0);
        panel_robot_button(p,v,101,"+",edit&&!r->cycle_continuous&&r->cycle_draft<255,0);
        snprintf(text,sizeof(text),"%u",r->cycle_draft?r->cycle_draft:1);
        panel_toolbar_center(p,(struct panel_hitbox){352,324,176,56},334,r->cycle_continuous?"持续":text,PANEL_F_TITLE,cyan);
        panel_robot_button(p,v,102,r->cycle_continuous?"持续循环：开":"持续循环：关",edit,0);
        panel_text(p,652,274,"循环进度",PANEL_F_LABEL,muted);
        if(r->mode==ROBOT_MODE_CYCLE){
            if(r->cycle_limit)snprintf(text,sizeof(text),"%u / %u",r->cycle_completed,r->cycle_limit);
            else snprintf(text,sizeof(text),"%u / CONT",r->cycle_completed);
        }else strcpy(text,"-- / --");
        panel_text(p,652,314,text,PANEL_F_MONO,cyan);
        const char *stage=r->cycle_stopping?"停止请求已提交":r->mode==ROBOT_MODE_CYCLE&&r->phase==ROBOT_EXECUTING&&!r->cycle_reason?
            "启动已接受，等待状态":!r->cycle_active?
            (r->cycle_reason==2?"指定次数完成":r->cycle_reason==3?"后续动作已停止":r->cycle_reason==4?"心跳超时":"尚未开始"):
            r->cycle_stage==0?"前往 A":r->cycle_stage==1?"前往 B":"返回 A";
        panel_text(p,652,366,stage,PANEL_F_LABEL,ink);
        panel_text(p,652,412,!r->cycle_known?"请先读取 A/B 标定":r->cycle_ready?
            "A/B 标定可用":"A/B 缺失或位置相同",PANEL_F_SMALL,r->cycle_ready?muted:amber);
        panel_robot_button(p,v,98,"开始测试",edit&&r->cycle_known&&r->cycle_ready,1);
        int stop=r->fresh&&r->uart_ready&&r->mode==ROBOT_MODE_CYCLE&&
            (r->phase==ROBOT_EXECUTING||r->phase==ROBOT_WAITING||r->phase==ROBOT_UNKNOWN)&&!r->operation;
        panel_robot_button(p,v,99,"停止后续动作",stop,0);
    }
    const char *note=r->error?(r->error==2?"目标或状态已变化，请重新确认":"请求未确认，请检查机械臂状态"):
        r->fault?"状态异常，请先现场核查":r->mode_error==RM_LEASE_LOST?"控制连接已失效，不自动恢复运动":
        r->mode_error==RM_NO_TARGET?"声源无效，已进入保持":r->mode_error==RM_TIMEOUT?"回包超时，请检查串口连接":
        r->mode_error==RM_UNCALIBRATED?"目标尚未标定":r->mode_error?"控制器请求失败，请检查状态":
        "";
    if(*note)panel_text(p,264,590,note,PANEL_F_SMALL,r->error||r->fault||r->mode_error?amber:muted);
}
static inline void panel_robot_draw(uint32_t *p,const struct panel_view *v){
    const struct panel_robot_view *r=&v->robot;
    uint32_t ink=panel_color(226,235,243,255),muted=panel_color(163,178,195,255);
    uint32_t amber=panel_color(240,185,90,255),edge=panel_color(35,70,90,235);
    /* Keep the existing Execute label/geometry; opening is not an action. */
    panel_robot_button(p,v,70,"执行",1,1);
    if(r->window)panel_robot_window(p,v);
    if(!r->modal)return;
    panel_rect(p,0,64,1280,552,panel_color(4,8,15,140));
    panel_roundrect_set(p,384,204,512,252,20,edge);
    panel_roundrect_set(p,385,205,510,250,19,panel_color(12,21,34,255));
    panel_text(p,408,220,r->confirm_kind==0?"确认夹取":r->confirm_kind==1?"确认跟踪":"确认测试",PANEL_F_TITLE,ink);
    panel_text(p,748,224,"UART1 / 9600",PANEL_F_SMALL,muted);
    panel_rect(p,408,260,464,1,edge);
    panel_text(p,408,280,r->confirm_kind==2?"循环次数":"目标区域",PANEL_F_LABEL,muted);
    char text[80];
    if(!r->confirm_kind)snprintf(text,sizeof(text),"%s",panel_robot_region_label(r->target));
    else if(r->confirm_kind==1)snprintf(text,sizeof(text),"第 %02u 区",r->modal_region);
    else if(r->modal_region)snprintf(text,sizeof(text),"%u 次",r->modal_region);
    else strcpy(text,"持续循环");
    panel_text(p,536,277,text,PANEL_F_TITLE,amber);
    panel_text(p,408,328,r->confirm_kind==0?"确认后发送夹取命令，请确保周围安全。":
        r->confirm_kind==1?"确认后机械臂随声源切换朝向。":"确认后开始 A/B 动作，请确保周围安全。",PANEL_F_LABEL,ink);
    for(int a=71;a<=72;a++)panel_robot_button(p,v,a,a==72?"确认发送":"取消",
        a==71||panel_robot_confirmable(r,r->target_ms),a==72);
}
#endif

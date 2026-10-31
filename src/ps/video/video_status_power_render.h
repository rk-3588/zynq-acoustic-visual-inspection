#ifndef VIDEO_STATUS_POWER_RENDER_H
#define VIDEO_STATUS_POWER_RENDER_H
static inline void panel_power_button(uint32_t *pixels,const struct panel_view *v,int action,
                                      const char *label,int enabled,int warning) {
    struct panel_hitbox box=panel_power_box(action);
    uint32_t edge=panel_color(35,70,90,255),ink=panel_color(226,235,243,255);
    uint32_t red=panel_color(255,132,116,255),muted=panel_color(130,146,164,255);
    int pressed=v->pressed&&v->pressed_action==action;
    panel_roundrect_set(pixels,box.x,box.y,box.w,box.h,12,enabled&&warning?red:edge);
    panel_roundrect_set(pixels,box.x+1,box.y+1,box.w-2,box.h-2,11,
        pressed?panel_color(76,39,39,255):warning?panel_color(52,29,35,255):panel_color(20,32,47,255));
    panel_toolbar_center(pixels,box,box.y+(box.h-27)/2,label,PANEL_F_TITLE,
        !enabled?muted:warning?red:ink);
}
static inline void panel_power_settings_draw(uint32_t *pixels,const struct panel_view *v) {
    int blocked=panel_power_blocked(v);
    panel_power_button(pixels,v,80,"关机",!blocked,1);
    panel_text(pixels,1024,680,blocked?"任务进行中":"安全关闭设备",PANEL_F_SMALL,
        panel_color(163,178,195,255));
}
static inline void panel_power_draw(uint32_t *pixels,const struct panel_view *v) {
    unsigned phase=v->power.phase;if(!phase)return;
    uint32_t edge=panel_color(35,70,90,235),ink=panel_color(226,235,243,255);
    uint32_t muted=panel_color(163,178,195,255),red=panel_color(255,132,116,255);
    panel_rect(pixels,0,64,1280,656,panel_color(4,8,15,150));
    panel_roundrect_set(pixels,384,204,512,252,20,edge);
    panel_roundrect_set(pixels,385,205,510,250,19,panel_color(12,21,34,255));
    panel_text(pixels,408,220,phase==PANEL_POWER_CONFIRM?"确认关机":
        phase==PANEL_POWER_FAILED?"关机请求失败":"正在关机",PANEL_F_TITLE,
        phase==PANEL_POWER_FAILED?red:ink);
    panel_rect(pixels,408,260,464,1,edge);
    if(phase==PANEL_POWER_CONFIRM) {
        panel_text(pixels,408,280,"系统将停止运行，请先完成保存。",PANEL_F_LABEL,ink);
        panel_text(pixels,408,322,panel_power_blocked(v)?"任务进行中，请稍后再试。":
            "等待系统关闭后，再断开电源。",PANEL_F_LABEL,muted);
        panel_power_button(pixels,v,81,"取消",1,0);
        panel_power_button(pixels,v,82,"确认关机",!panel_power_blocked(v),1);
    } else if(phase==PANEL_POWER_FAILED) {
        panel_text(pixels,408,280,"未能发送关机请求，设备仍在运行。",PANEL_F_LABEL,ink);
        panel_text(pixels,408,322,"请返回设置后重试，或检查系统日志。",PANEL_F_LABEL,muted);
        panel_power_button(pixels,v,81,"返回设置",1,0);
    } else {
        panel_text(pixels,408,280,phase==PANEL_POWER_ACCEPTED?"已发送关机请求。":
            v->power.slow?"等待系统响应，请勿重复操作。":"正在发送关机请求。",PANEL_F_LABEL,ink);
        panel_text(pixels,408,322,"等待系统关闭后，再断开电源。",PANEL_F_LABEL,muted);
    }
}
#endif

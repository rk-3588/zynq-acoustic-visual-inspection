#ifndef VIDEO_STATUS_IMAGE_VIEW_H
#define VIDEO_STATUS_IMAGE_VIEW_H
static inline int panel_image_context(const struct panel_view *v) {
    return v->image.editing?3:v->image.settings?2:v->image.region_popup?4:v->image.popup?1:0;
}
static inline struct panel_hitbox panel_image_drawer_box(void) {
    return (struct panel_hitbox){208,430,448,172};
}
static inline struct panel_hitbox panel_region_drawer_box(void) {
    return (struct panel_hitbox){408,418,208,184};
}
static inline struct panel_hitbox panel_image_target(int action) {
    switch(action) {
    case 20:return (struct panel_hitbox){208,634,184,56};
    case 21:return (struct panel_hitbox){420,430,184,48};
    case 22:return (struct panel_hitbox){420,486,184,48};
    case 23:return (struct panel_hitbox){672,634,176,56};
    case 24:return (struct panel_hitbox){432,634,176,56};
    case 25:return (struct panel_hitbox){1110,634,138,56};
    case 26:return (struct panel_hitbox){1216,76,48,48};
    case 27:return (struct panel_hitbox){776,530,224,56};
    case 28:return (struct panel_hitbox){776,602,224,56};
    case 29:return (struct panel_hitbox){1024,602,224,56};
    case 30:return (struct panel_hitbox){540,430,104,44};
    case 31:return (struct panel_hitbox){408,634,176,56};
    case 32:return (struct panel_hitbox){420,542,184,48};
    case 56:case 58:return (struct panel_hitbox){776,action==56?410:470,64,44};
    case 57:case 59:return (struct panel_hitbox){1184,action==57?410:470,64,44};
    case 50:return (struct panel_hitbox){776,258,64,56};
    case 51:return (struct panel_hitbox){1184,258,64,56};
    case 52:case 54:return (struct panel_hitbox){776,258,64,56};
    case 53:case 55:return (struct panel_hitbox){1184,258,64,56};
    case 60:case 61:return (struct panel_hitbox){776,316,472,52};
    default:if(action>=40&&action<=45)return (struct panel_hitbox){224+142*((action-40)%3),474+60*((action-40)/3),132,52};
    }
    return (struct panel_hitbox){0,0,0,0};
}
/* Track endpoints are centers, not the generous 52px-high touch target. */
static inline int panel_image_slider_x(int value,unsigned mode) {
    int low=mode==3?-255:0,range=mode==3?510:255;
    value=panel_image_bound(value,low,255);
    return 796+((value-low)*432+range/2)/range;
}
static inline int panel_image_slider_value(int x,unsigned mode) {
    int low=mode==3?-255:0,range=mode==3?510:255;
    x=panel_image_bound(x,796,1228);
    return low+((x-796)*range+216)/432;
}
static inline int panel_image_slider_display(const struct panel_image_view *i) {
    return i->slider_dragging?i->slider_value:
        panel_image_selected_mode(i)==3?i->draft_brightness:i->draft_threshold;
}
static inline int panel_image_hit(const struct panel_view *v,int x,int y) {
    const struct panel_image_view *i=&v->image;int ready=panel_image_available(i);
    if(i->editing) {
        if(panel_inside(panel_image_target(24),x,y))return 24;
        if(ready&&i->preview_valid&&!i->dragging&&panel_inside(panel_image_target(23),x,y))return 23;
        return 0;
    }
    if(i->settings) {
        if(panel_inside(panel_image_target(26),x,y))return 26;
        if(!ready)return 0;
        const int actions[]={27,28,29,50,51,52,53,54,55,56,57,58,59,60,61};
        unsigned mode=panel_image_selected_mode(i);
        for(unsigned n=0;n<sizeof(actions)/sizeof(actions[0]);n++) {
            int a=actions[n];if((a==50||a==51)&&mode!=2)continue;
            if(a==27&&!i->follow&&mode!=2&&mode!=3&&mode!=5)continue;
            if((a==52||a==53)&&mode!=3)continue;
            if((a==54||a==55)&&mode!=5)continue;
            if(a>=56&&a<=59&&!i->follow)continue;
            if((a==60&&mode!=2)||(a==61&&mode!=3))continue;
            if(panel_inside(panel_image_target(a),x,y))return a;
        }
        return 0;
    }
    if(panel_inside(panel_image_target(20),x,y))return 20;
    if(panel_inside(panel_image_target(25),x,y))return 25;
    if(panel_inside(panel_image_target(31),x,y))return 31;
    if(i->region_popup) {
        const int actions[]={21,22,32};
        if(ready)for(unsigned k=0;k<3;k++)if(panel_inside(panel_image_target(actions[k]),x,y))return actions[k];
        if(!panel_inside(panel_region_drawer_box(),x,y))return 33;
        return 0;
    }
    if(i->popup) {
        if(panel_inside(panel_image_target(30),x,y))return 30;
        if(ready)for(int a=40;a<=45;a++)if(panel_inside(panel_image_target(a),x,y))return a;
        if(!panel_inside(panel_image_drawer_box(),x,y))return 39;
    }
    return 0;
}
static inline void panel_image_preview(struct panel_image_view *i,int x0,int y0,int x1,int y1) {
    if(x1<0)x1=0;else if(x1>1279)x1=1279;
    if(y1<64)y1=64;else if(y1>615)y1=615;
    i->x=(unsigned)(x0<x1?x0:x1);i->y=(unsigned)(y0<y1?y0:y1);
    i->w=(unsigned)abs(x1-x0)+1;i->h=(unsigned)abs(y1-y0)+1;
    i->preview_valid=i->w>=8&&i->h>=8;
}
static inline void panel_image_action(struct panel_view *v,int action) {
    struct panel_image_view *i=&v->image;
    if(action!=60&&action!=61)i->slider_dragging=0;
    if(action==20){i->popup=!i->popup;i->region_popup=0;}
    else if(action==31){i->region_popup=!i->region_popup;i->popup=0;}
    else if(action==33)i->region_popup=0;
    else if(action==25||action==30) {
        i->popup=i->region_popup=0;i->settings=1;i->draft_threshold=(int)i->active.threshold;
        i->draft_brightness=i->active.brightness;i->draft_gain=i->active.gain;
        i->draft_follow_w=panel_follow_width(i);i->draft_follow_h=panel_follow_height(i);
    } else if(action==26)i->settings=0;
    else if(action==39)i->popup=0;
    else if(action==24){i->editing=0;i->dragging=0;i->preview_valid=0;}
    else if(panel_image_available(i)) {
        if(action>=40&&action<=45){i->request=PIMG_MODE;i->request_mode=(unsigned)(action-40);}
        else if(action==21){i->request=PIMG_FULL;i->popup=i->region_popup=0;}
        else if(action==22){i->editing=1;i->popup=i->region_popup=0;i->preview_valid=0;}
        else if(action==32){
            if(!i->follow)i->follow_mode=i->active.mode;
            i->follow=1;i->follow_have=i->follow_visible=i->follow_hold=i->follow_applied=0;
            i->follow_fault=i->error=0;i->follow_last_step_ms=i->follow_next_ms=0;
            i->follow_w=panel_follow_width(i);i->follow_h=panel_follow_height(i);
            i->region_popup=0;
        }
        else if(action==23&&i->preview_valid&&!i->dragging)i->request=PIMG_ROI;
        else if(action==27)i->request=PIMG_PARAMS;
        else if(action==28)i->request=PIMG_ORIGINAL;
        else if(action==29)i->request=PIMG_DEFAULTS;
        else if(action==50&&i->draft_threshold>0)i->draft_threshold--;
        else if(action==51&&i->draft_threshold<255)i->draft_threshold++;
        else if(action==52)i->draft_brightness=panel_image_bound(i->draft_brightness-8,-255,255);
        else if(action==53)i->draft_brightness=panel_image_bound(i->draft_brightness+8,-255,255);
        else if(action==54&&i->draft_gain>-2)i->draft_gain--;
        else if(action==55&&i->draft_gain<2)i->draft_gain++;
        else if(action==56)i->draft_follow_w=(unsigned)panel_image_bound((int)i->draft_follow_w-20,80,1280);
        else if(action==57)i->draft_follow_w=(unsigned)panel_image_bound((int)i->draft_follow_w+20,80,1280);
        else if(action==58)i->draft_follow_h=(unsigned)panel_image_bound((int)i->draft_follow_h-20,60,720);
        else if(action==59)i->draft_follow_h=(unsigned)panel_image_bound((int)i->draft_follow_h+20,60,720);
    }
    i->revision++;
}
#endif

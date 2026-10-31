#ifndef VIDEO_STATUS_IMAGE_RENDER_H
#define VIDEO_STATUS_IMAGE_RENDER_H
static inline void panel_image_button(uint32_t *p,const struct panel_view *v,int action,const char *label,int enabled) {
    gallery_button(p,panel_image_target(action),label,enabled,v->pressed&&v->pressed_action==action);
}
static inline void panel_image_outline(uint32_t *p,unsigned x,unsigned y,unsigned w,unsigned h,uint32_t ink) {
    if(!w||!h||x+w>1280||y+h>720)return;
    int top=y<64?64:(int)y,bottom=y+h>616?615:(int)(y+h)-1;
    if(top>bottom)return;
    panel_rect(p,(int)x,top,(int)w,2,ink);panel_rect(p,(int)x,bottom-1,(int)w,2,ink);
    panel_rect(p,(int)x,top,2,bottom-top+1,ink);panel_rect(p,(int)(x+w)-2,top,2,bottom-top+1,ink);
}
static inline void panel_image_slider_draw(uint32_t *p,const struct panel_image_view *i,int enabled) {
    unsigned mode=panel_image_selected_mode(i);int value=panel_image_slider_display(i);
    int x=panel_image_slider_x(value,mode),zero=panel_image_slider_x(0,mode);
    uint32_t cyan=panel_color(42,213,239,255),muted=panel_color(163,178,195,255);
    uint32_t edge=panel_color(35,70,90,255),bg=panel_color(16,26,42,255);
    uint32_t active=enabled||i->slider_dragging?cyan:muted;
    panel_roundrect_set(p,796,337,433,6,3,edge);
    int start=mode==3&&x<zero?x:zero,stop=mode==3&&x<zero?zero:x;
    if(stop>start)panel_roundrect_set(p,start,337,stop-start+1,6,3,active);
    if(mode==3)panel_rect(p,zero-1,332,2,16,muted);
    if(i->slider_dragging)panel_roundrect_set(p,x-15,325,30,30,15,edge);
    panel_roundrect_set(p,x-12,328,24,24,12,active);
    panel_roundrect_set(p,x-8,332,16,16,8,bg);
    panel_roundrect_set(p,x-3,337,6,6,3,active);
    const char *low=mode==3?"-255":"0",*high=mode==3?"+255":"255";
    panel_text(p,796,355,low,PANEL_F_SMALL,muted);
    panel_text(p,1228-panel_text_width(high,PANEL_F_SMALL),355,high,PANEL_F_SMALL,muted);
    if(mode==3)panel_text(p,1012-panel_text_width("0",PANEL_F_SMALL)/2,355,"0",PANEL_F_SMALL,muted);
}
static inline void panel_image_draw(uint32_t *p,const struct panel_view *v) {
    const struct panel_image_view *i=&v->image;
    uint32_t cyan=panel_color(42,213,239,255),ink=panel_color(226,235,243,255),muted=panel_color(163,178,195,255);
    uint32_t edge=panel_color(35,70,90,245),card=panel_color(16,26,42,245),amber=panel_color(240,185,90,255);
    int ready=panel_image_available(i);char text[160];unsigned mode=panel_image_selected_mode(i);
    if(i->known&&i->active.roi&&!i->editing&&(!i->follow||(i->follow_visible&&i->follow_applied&&!i->follow_fault)))
        panel_image_outline(p,i->active.x,i->active.y,i->active.w,i->active.h,i->follow_hold?amber:cyan);
    if(i->editing) {
        if(i->preview_valid||i->dragging)panel_image_outline(p,i->x,i->y,i->w,i->h,i->dragging?amber:cyan);
        panel_roundrect_set(p,264,78,752,66,16,card);
        panel_text(p,288,87,"拖动框选，松手预览；确认后生效",PANEL_F_TITLE,ink);
        if(i->preview_valid)snprintf(text,sizeof(text),"X %u  Y %u   %u x %u",i->x,i->y,i->w,i->h);
        else strcpy(text,"请在画面中拖动，框选至少 8 x 8 像素");
        panel_text(p,288,118,text,PANEL_F_SMALL,muted);
        panel_roundrect_set(p,16,616,1248,88,20,card);
        panel_image_button(p,v,24,"取消框选",1);
        panel_image_button(p,v,23,i->busy||i->request?"提交中":"应用区域",ready&&i->preview_valid&&!i->dragging);
        if(i->error)panel_text(p,32,648,"提交失败，请重新确认",PANEL_F_SMALL,amber);
        return;
    }
    if(i->settings) {
        panel_rect(p,752,64,528,656,panel_color(12,20,29,255));
        panel_rect(p,752,64,1,656,edge);
        panel_text(p,776,84,"设置",PANEL_F_TITLE,ink);
        panel_image_button(p,v,26,"X",1);
        panel_text(p,776,144,"当前模式",PANEL_F_LABEL,muted);
        panel_text(p,1016,142,i->known?panel_image_mode_name(mode):"未连接",PANEL_F_TITLE,cyan);
        const char *state=i->busy||i->request?"处理中":i->pending?"等待帧边界":i->error?"上次操作失败":
            !i->known?"等待图像控制接口":i->follow&&!i->follow_visible?
                (i->active.mode==0?"跟随暂停，图像处理已旁路":"等待旁路确认"):"参数修改后点击应用";
        panel_text(p,776,180,state,PANEL_F_SMALL,i->error?amber:muted);
        if(i->known&&mode==2) {
            panel_text(p,776,226,"二值化阈值",PANEL_F_TITLE,ink);
            panel_image_button(p,v,50,"-",ready);panel_image_button(p,v,51,"+",ready);
            snprintf(text,sizeof(text),"%d",panel_image_slider_display(i));
            panel_toolbar_center(p,(struct panel_hitbox){856,258,312,56},268,text,PANEL_F_NUMBER,cyan);
            panel_image_slider_draw(p,i,ready);
        }
        if(i->known&&(mode==3||mode==5)) {
            int gain=mode==5;
            panel_text(p,776,226,gain?"移位增益":"亮度偏移",PANEL_F_TITLE,ink);
            panel_image_button(p,v,gain?54:52,"-",ready);panel_image_button(p,v,gain?55:53,"+",ready);
            if(gain) {
                static const char *gains[]={"x0.25","x0.5","x1","x2","x4"};
                strcpy(text,i->draft_gain>=-2&&i->draft_gain<=2?gains[i->draft_gain+2]:"--");
            } else snprintf(text,sizeof(text),"%d",panel_image_slider_display(i));
            panel_toolbar_center(p,(struct panel_hitbox){856,258,312,56},268,text,PANEL_F_NUMBER,cyan);
            if(gain)panel_text(p,776,336,"五档增益，确认后生效",PANEL_F_SMALL,muted);
            else panel_image_slider_draw(p,i,ready);
        } else if(i->known&&mode!=2)panel_text(p,776,226,"此模式无需额外参数",PANEL_F_TITLE,muted);
        if(i->follow) {
            panel_text(p,776,376,"跟随框尺寸",PANEL_F_SMALL,muted);
            panel_image_button(p,v,56,"-",ready);panel_image_button(p,v,57,"+",ready);
            panel_image_button(p,v,58,"-",ready);panel_image_button(p,v,59,"+",ready);
            snprintf(text,sizeof(text),"宽 %u px",i->draft_follow_w);
            panel_toolbar_center(p,(struct panel_hitbox){848,410,328,44},418,text,PANEL_F_LABEL,cyan);
            snprintf(text,sizeof(text),"高 %u px",i->draft_follow_h);
            panel_toolbar_center(p,(struct panel_hitbox){848,470,328,44},478,text,PANEL_F_LABEL,cyan);
        }
        panel_image_button(p,v,27,"应用设置",ready&&(i->follow||mode==2||mode==3||mode==5));
        panel_image_button(p,v,28,"恢复原图",ready);panel_image_button(p,v,29,"恢复默认",ready);
        panel_power_settings_draw(p,v);return;
    }
    if(i->region_popup) {
        struct panel_hitbox box=panel_region_drawer_box();
        panel_roundrect_set(p,box.x,box.y,box.w,box.h,14,edge);
        panel_roundrect_set(p,box.x+1,box.y+1,box.w-2,box.h-2,13,panel_color(12,22,34,245));
        const int actions[]={21,22,32};int selected=panel_region_selected(i);
        for(int k=0;k<3;k++) {
            struct panel_hitbox b=panel_image_target(actions[k]);int on=selected==k;
            panel_roundrect_set(p,b.x,b.y,b.w,b.h,10,on?cyan:edge);
            panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,9,on?panel_color(14,60,74,250):card);
            panel_text(p,b.x+16,b.y+11,panel_region_name(k),PANEL_F_SMALL,ready?(on?cyan:ink):muted);
            if(on)panel_roundrect_set(p,b.x+b.w-24,b.y+20,8,8,4,cyan);
        }
        return;
    }
    if(!i->popup)return;
    struct panel_hitbox drawer=panel_image_drawer_box(),settings=panel_image_target(30);
    panel_roundrect_set(p,drawer.x,drawer.y,drawer.w,drawer.h,14,edge);
    panel_roundrect_set(p,drawer.x+1,drawer.y+1,drawer.w-2,drawer.h-2,13,panel_color(12,22,34,245));
    panel_text(p,224,442,"图像模式",PANEL_F_CAPTION,muted);
    /* Compact paint inside a full-height touch target; never shrink the hitbox. */
    struct panel_hitbox link={settings.x+4,settings.y+8,settings.w-8,28};
    panel_roundrect_set(p,link.x,link.y,link.w,link.h,8,edge);
    panel_roundrect_set(p,link.x+1,link.y+1,link.w-2,link.h-2,7,card);
    panel_toolbar_center(p,link,link.y+2,"参数设置 >",PANEL_F_CAPTION,cyan);
    for(unsigned mode=0;mode<6;mode++) {
        struct panel_hitbox b=panel_image_target(40+(int)mode);
        int selected=i->known&&panel_image_selected_mode(i)==mode;
        panel_roundrect_set(p,b.x,b.y,b.w,b.h,10,selected?cyan:edge);
        panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,9,selected?panel_color(14,60,74,250):card);
        panel_toolbar_center(p,b,b.y+4,panel_image_mode_name(mode),PANEL_F_SMALL,selected?cyan:ink);
        if(mode==0)strcpy(text,"RGB 直通");else if(mode==1)strcpy(text,"Luma");
        else if(mode==2){if(i->known)snprintf(text,sizeof(text),"阈值 %u",i->active.threshold);else strcpy(text,"阈值 --");}
        else if(mode==3){if(i->known)snprintf(text,sizeof(text),"偏移 %d",i->active.brightness);else strcpy(text,"偏移 --");}
        else if(mode==4)strcpy(text,"255 - RGB");
        else {static const char *gains[]={"x0.25","x0.5","x1","x2","x4"};
            strcpy(text,i->known&&i->active.gain>=-2&&i->active.gain<=2?gains[i->active.gain+2]:"--");}
        panel_toolbar_center(p,b,b.y+28,text,PANEL_F_CAPTION,muted);
        if(panel_image_warning(i))panel_rect(p,b.x+5,b.y+b.h-4,b.w-10,1,amber);
    }
}
#endif

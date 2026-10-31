#ifndef VIDEO_STATUS_TOOLBAR_H
#define VIDEO_STATUS_TOOLBAR_H
/* Acoustic, image control, camera capture and gallery have real backends.
 * Include after drawing helpers. A 720-line canvas is required to draw it.
 * confirmed/enabled flags must be populated by real backends before deployment. */
enum panel_toolbar_target {
    PANEL_TOOL_NONE, PANEL_TOOL_ACOUSTIC, PANEL_TOOL_NORMAL, PANEL_TOOL_GRAY,
    PANEL_TOOL_BINARY, PANEL_TOOL_SOBEL, PANEL_TOOL_CAPTURE, PANEL_TOOL_GALLERY,
    PANEL_TOOL_SETTINGS
};
struct panel_toolbar_state {
    const struct panel_image_view *image;
    int acoustic_known, acoustic_on, mode_known;
    unsigned mode, available; /* mask: 1u << panel_toolbar_target */
    int count_known; unsigned screenshots;
    enum panel_toolbar_target pressed;
    int acoustic_pending,acoustic_error;
    int capture_busy,capture_feedback;
};
static inline struct panel_hitbox panel_toolbar_box(enum panel_toolbar_target target) {
    if(target==PANEL_TOOL_ACOUSTIC)return panel_acoustic_box();
    if(target>=PANEL_TOOL_NORMAL && target<=PANEL_TOOL_SOBEL)
        return (struct panel_hitbox){216+84*(target-PANEL_TOOL_NORMAL),634,84,56};
    if(target==PANEL_TOOL_CAPTURE)return panel_capture_box();
    if(target==PANEL_TOOL_GALLERY)return panel_gallery_box();
    if(target==PANEL_TOOL_SETTINGS)return (struct panel_hitbox){1110,634,138,56};
    return (struct panel_hitbox){0,0,0,0};
}
static inline enum panel_toolbar_target panel_toolbar_hit(int x,int y) {
    for(unsigned i=PANEL_TOOL_ACOUSTIC;i<=PANEL_TOOL_SETTINGS;i++)
        if(panel_inside(panel_toolbar_box((enum panel_toolbar_target)i),x,y))return (enum panel_toolbar_target)i;
    return PANEL_TOOL_NONE;
}
static inline int panel_toolbar_available(const struct panel_toolbar_state *s,enum panel_toolbar_target t) {
    return t>PANEL_TOOL_NONE && t<=PANEL_TOOL_SETTINGS && (s->available&(1u<<t))!=0;
}
static inline void panel_toolbar_line(uint32_t *p,int x0,int y0,int x1,int y1,int width,uint32_t color) {
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,err=dx+dy;
    for(;;) {
        panel_rect(p,x0-width/2,y0-width/2,width,width,color);
        if(x0==x1 && y0==y1)break;
        int twice=2*err;
        if(twice>=dy){err+=dy;x0+=sx;}
        if(twice<=dx){err+=dx;y0+=sy;}
    }
}
static inline void panel_toolbar_ring(uint32_t *p,int cx,int cy,int radius,int thickness,uint32_t color) {
    for(int y=-radius-1;y<=radius+1;y++)for(int x=-radius-1;x<=radius+1;x++) {
        unsigned inside=0;
        for(int a=1;a<=3;a+=2)for(int b=1;b<=3;b+=2) {
            int dx=4*x+a-2,dy=4*y+b-2,d=dx*dx+dy*dy;
            inside+=d<=16*radius*radius && d>=16*(radius-thickness)*(radius-thickness);
        }
        if(inside)panel_blend(p,cx+x,cy+y,color,inside*255/4);
    }
}
static inline void panel_toolbar_center(uint32_t *p,struct panel_hitbox b,int y,const char *text,unsigned font,uint32_t color) {
    panel_text(p,b.x+(b.w-panel_text_width(text,font))/2,y,text,font,color);
}
static inline void panel_toolbar_draw(uint32_t *p,const struct panel_toolbar_state *s) {
    if(PANEL_HEIGHT<720u)return;
    uint32_t edge=panel_color(35,70,90,235),bg=panel_color(10,16,28,220);
    uint32_t ink=panel_color(226,235,243,255),muted=panel_color(163,178,195,255);
    uint32_t cyan=panel_color(42,213,239,255),amber=panel_color(240,185,90,255);
    panel_roundrect_set(p,16,616,1248,88,23,edge);
    panel_roundrect_set(p,17,617,1246,86,22,bg);
    struct panel_hitbox acoustic=panel_toolbar_box(PANEL_TOOL_ACOUSTIC);
    int active=s->acoustic_known && s->acoustic_on;
    panel_roundrect_set(p,acoustic.x,acoustic.y,acoustic.w,acoustic.h,17,active?cyan:edge);
    panel_roundrect_set(p,acoustic.x+1,acoustic.y+1,acoustic.w-2,acoustic.h-2,16,
        panel_color(active?12:16,active?48:26,active?61:42,235));
    panel_roundrect(p,46,654,12,12,6,active?cyan:muted);
    panel_text(p,68,634,"声学识别",PANEL_F_TITLE,active?ink:muted);
    const char *acoustic_label=s->acoustic_error?"ERROR":s->acoustic_pending?"WAIT":
        !s->acoustic_known?"待接入":active?"ON / 开启":"OFF / 关闭";
    panel_text(p,68,663,acoustic_label,PANEL_F_SMALL,s->acoustic_error || s->acoustic_pending?amber:active?cyan:muted);
    const struct panel_image_view unknown={0};const struct panel_image_view *image=s->image?s->image:&unknown;
    struct panel_hitbox mode=panel_image_target(20);
    panel_roundrect_set(p,mode.x,mode.y,mode.w,mode.h,16,image->popup?cyan:edge);
    panel_roundrect_set(p,mode.x+1,mode.y+1,mode.w-2,mode.h-2,15,panel_color(16,35,46,235));
    panel_text(p,224,639,"图像模式",PANEL_F_SMALL,muted);
    panel_text(p,224,657,image->known?panel_image_mode_name(panel_image_selected_mode(image)):"未连接",PANEL_F_TITLE,image->known?cyan:muted);
    panel_chevron(p,mode.x+mode.w-20,mode.y+mode.h/2,
        image->popup?PANEL_CHEVRON_DOWN:PANEL_CHEVRON_UP,7,7,2,cyan);
    struct panel_hitbox region=panel_image_target(31);
    panel_roundrect_set(p,region.x,region.y,region.w,region.h,16,image->region_popup?cyan:edge);
    panel_roundrect_set(p,region.x+1,region.y+1,region.w-2,region.h-2,15,panel_color(16,35,46,235));
    panel_text(p,424,639,"区域选择",PANEL_F_SMALL,muted);
    panel_text(p,424,657,panel_region_name(panel_region_selected(image)),PANEL_F_TITLE,image->known?cyan:muted);
    panel_chevron(p,region.x+region.w-20,region.y+region.h/2,
        image->region_popup?PANEL_CHEVRON_DOWN:PANEL_CHEVRON_UP,7,7,2,cyan);
    int ready=panel_toolbar_available(s,PANEL_TOOL_CAPTURE);
    panel_roundrect_set(p,601,623,78,78,39,panel_color(10,16,28,255));
    uint32_t capture_color=s->capture_feedback<0?panel_color(255,107,102,255):
        s->capture_feedback>0?panel_color(67,214,170,255):s->capture_busy?amber:ready?cyan:muted;
    panel_toolbar_ring(p,640,662,38,3,s->pressed==PANEL_TOOL_CAPTURE?amber:capture_color);
    panel_toolbar_ring(p,640,662,33,1,edge);
    panel_roundrect(p,623,651,34,24,5,ink);
    panel_roundrect_set(p,626,654,28,18,3,panel_color(10,16,28,255));
    panel_rect(p,630,647,15,5,ink);
    panel_toolbar_ring(p,640,663,7,2,ink);
    panel_text(p,704,648,"抓拍",PANEL_F_TITLE,ready?ink:muted);
    const char *capture_label=s->capture_busy?"SAVING":s->capture_feedback>0?"SAVED":s->capture_feedback<0?"FAILED":ready?"BMP + JSON":"NOT READY";
    panel_text(p,704,677,capture_label,PANEL_F_SMALL,capture_color);
    struct panel_hitbox gallery=panel_toolbar_box(PANEL_TOOL_GALLERY);
    panel_roundrect_set(p,gallery.x,gallery.y,gallery.w,gallery.h,16,edge);
    panel_roundrect_set(p,gallery.x+1,gallery.y+1,gallery.w-2,gallery.h-2,15,panel_color(16,26,42,235));
    uint32_t gallery_color=panel_toolbar_available(s,PANEL_TOOL_GALLERY)?cyan:muted;
    panel_roundrect(p,931,651,24,21,3,gallery_color);
    panel_rect(p,933,653,20,17,panel_color(16,26,42,255));
    panel_toolbar_line(p,935,667,941,661,2,gallery_color);
    panel_toolbar_line(p,941,661,948,667,2,gallery_color);
    panel_roundrect(p,947,656,3,3,1,gallery_color);
    panel_text(p,968,648,"相册",PANEL_F_TITLE,panel_toolbar_available(s,PANEL_TOOL_GALLERY)?ink:muted);
    char count[16];
    if(!s->count_known)strcpy(count,"--");
    else if(s->screenshots>999)strcpy(count,"999+");
    else snprintf(count,sizeof(count),"%u",s->screenshots);
    panel_roundrect_set(p,1030,647,46,30,15,panel_color(14,77,94,255));
    panel_toolbar_center(p,(struct panel_hitbox){1030,647,46,30},650,count,PANEL_F_SMALL,cyan);
    struct panel_hitbox settings=panel_toolbar_box(PANEL_TOOL_SETTINGS);
    panel_roundrect_set(p,settings.x,settings.y,settings.w,settings.h,16,edge);
    panel_roundrect_set(p,settings.x+1,settings.y+1,settings.w-2,settings.h-2,15,panel_color(16,26,42,235));
    /* Eight gear teeth, explicitly drawn rather than requiring an icon font. */
    int cx=1138,cy=662;
    panel_toolbar_ring(p,cx,cy,9,3,muted);panel_toolbar_ring(p,cx,cy,3,2,muted);
    const int tooth[8][2]={{0,-12},{8,-8},{12,0},{8,8},{0,12},{-8,8},{-12,0},{-8,-8}};
    for(unsigned i=0;i<8;i++)panel_rect(p,cx+tooth[i][0]-2,cy+tooth[i][1]-2,4,4,muted);
    panel_text(p,1162,648,"设置",PANEL_F_TITLE,ink);
}
#endif

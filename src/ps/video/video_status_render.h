#ifndef VIDEO_STATUS_RENDER_H
#define VIDEO_STATUS_RENDER_H
#include "video_status_panel.h"
#include "video_status_temp.h"
#include "video_status_font.h"
#include "video_status_view.h"

static inline uint32_t panel_color(unsigned r,unsigned g,unsigned b,unsigned a) {
    return r|(g<<8)|(b<<16)|(a<<24);
}
static inline void panel_blend(uint32_t *p,int x,int y,uint32_t color,unsigned coverage) {
    if(x<0 || y<0 || x>=(int)PANEL_WIDTH || y>=(int)PANEL_HEIGHT || !coverage) return;
    unsigned sa=(color>>24)*coverage/255;
    if(!sa) return;
    uint32_t *pixel=&p[(unsigned)y*PANEL_WIDTH+(unsigned)x];
    if(sa==255) {*pixel=color; return;}
    unsigned da=*pixel>>24,oa=sa+(da*(255-sa)+127)/255;
    uint32_t result=oa<<24;
    for(unsigned c=0;c<3;c++) {
        unsigned sc=(color>>(8*c))&255,dc=(*pixel>>(8*c))&255;
        unsigned premult=sc*sa*255+dc*da*(255-sa);
        result|=((premult+oa*127)/(oa*255))<<(8*c);
    }
    *pixel=result;
}
static inline void panel_rect(uint32_t *p,int x,int y,int w,int h,uint32_t color) {
    for(int row=y;row<y+h;row++) for(int col=x;col<x+w;col++)
        if(row>=0 && row<(int)PANEL_HEIGHT && col>=0 && col<(int)PANEL_WIDTH)
            p[(unsigned)row*PANEL_WIDTH+(unsigned)col]=color;
}
static inline void panel_roundrect_impl(uint32_t *p,int x,int y,int w,int h,int radius,uint32_t color,int replace) {
    for(int row=0;row<h;row++) for(int col=0;col<w;col++) {
        unsigned inside=0;
        for(int sy=1;sy<=3;sy+=2) for(int sx=1;sx<=3;sx+=2) {
            int px=4*col+sx,py=4*row+sy;
            int cx=px<4*radius?4*radius:px>4*(w-radius)?4*(w-radius):px;
            int cy=py<4*radius?4*radius:py>4*(h-radius)?4*(h-radius):py;
            int dx=px-cx,dy=py-cy;
            inside+=(dx*dx+dy*dy<=16*radius*radius);
        }
        if(replace && inside==4) panel_rect(p,x+col,y+row,1,1,color);
        else if(inside) panel_blend(p,x+col,y+row,color,inside*255/4);
    }
}
static inline void panel_roundrect(uint32_t *p,int x,int y,int w,int h,int radius,uint32_t color) {
    panel_roundrect_impl(p,x,y,w,h,radius,color,0);
}
static inline void panel_roundrect_set(uint32_t *p,int x,int y,int w,int h,int radius,uint32_t color) {
    panel_roundrect_impl(p,x,y,w,h,radius,color,1);
}
static inline const struct panel_glyph *panel_glyph_find(unsigned font,uint32_t code) {
    if(font>=PANEL_FONT_COUNT) return NULL;
    const struct panel_font_metrics *m=&panel_fonts[font];
    for(unsigned attempt=0;attempt<2;attempt++,code='?') {
        unsigned lo=m->first,hi=lo+m->count;
        while(lo<hi) {
            unsigned mid=lo+(hi-lo)/2;
            if(panel_glyphs[mid].code<code) lo=mid+1; else hi=mid;
        }
        if(lo<m->first+m->count && panel_glyphs[lo].code==code) return &panel_glyphs[lo];
    }
    return NULL;
}
static inline uint32_t panel_utf8(const char **cursor) {
    const unsigned char *p=(const unsigned char *)*cursor;
    unsigned n; uint32_t code,min;
    if(*p<128) {(*cursor)++;return *p;}
    if(*p>=0xc2 && *p<=0xdf) {n=2;code=*p&31;min=0x80;}
    else if(*p>=0xe0 && *p<=0xef) {n=3;code=*p&15;min=0x800;}
    else if(*p>=0xf0 && *p<=0xf4) {n=4;code=*p&7;min=0x10000;}
    else {(*cursor)++;return '?';}
    for(unsigned i=1;i<n;i++) {
        if(!p[i] || (p[i]&0xc0)!=0x80) {(*cursor)++;return '?';}
        code=(code<<6)|(p[i]&63);
    }
    if(code<min || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) {(*cursor)++;return '?';}
    *cursor+=n; return code;
}
static inline int panel_text_width(const char *text,unsigned font) {
    int width=0;
    while(*text) {
        const struct panel_glyph *g=panel_glyph_find(font,panel_utf8(&text));
        if(g) width+=g->advance;
    }
    return width;
}
/* Top-left text coordinates, baseline-aware original-size 8-bit glyph masks. */
static inline void panel_text(uint32_t *p,int x,int y,const char *text,unsigned font,uint32_t color) {
    if(font>=PANEL_FONT_COUNT) return;
    int baseline=y+(int)panel_fonts[font].ascent;
    while(*text) {
        const struct panel_glyph *g=panel_glyph_find(font,panel_utf8(&text));
        if(!g) continue;
        for(unsigned row=0;row<g->h;row++) for(unsigned col=0;col<g->w;col++)
            panel_blend(p,x+g->dx+(int)col,baseline+g->dy+(int)row,color,
                panel_font_coverage[g->offset+row*g->w+col]);
        x+=g->advance;
    }
}
static inline void panel_badge(uint32_t *p,int x,int y,int w,const char *text,uint32_t color) {
    panel_roundrect(p,x,y,w,30,15,panel_color(20,44,53,255));
    panel_text(p,x+(w-panel_text_width(text,PANEL_F_SMALL))/2,y+3,text,PANEL_F_SMALL,color);
}
static inline const char *panel_position_label(const struct panel_status *s) {
    if(!s->online) return "无数据";
    if(!strcmp(s->position,"disabled"))return "已关闭";
    if(!strcmp(s->position,"enable_pending"))return "切换中";
    if(!strcmp(s->position,"quiet")) return "静音";
    if(!strcmp(s->position,"stable")) return "稳定";
    if(!strcmp(s->position,"moving")) return "移动中";
    if(!strcmp(s->position,"acquiring")) return "获取中";
    if(!strcmp(s->position,"read_unstable")) return "不可靠";
    return "未稳定";
}
static inline void panel_card(uint32_t *p,int x,int y,int w,int h) {
    /* Replace fills rather than alpha-compositing nested translucent cards. */
    panel_roundrect_set(p,x,y,w,h,14,panel_color(41,57,77,210));
    panel_roundrect_set(p,x+1,y+1,w-2,h-2,13,panel_color(16,26,42,192));
}
enum panel_chevron_direction {PANEL_CHEVRON_UP,PANEL_CHEVRON_DOWN,PANEL_CHEVRON_LEFT,PANEL_CHEVRON_RIGHT};
/* Quarter-pixel capsule test: symmetric round strokes, without font baselines
 * or Bresenham tie-breaking. Coordinates and radius are in quarter pixels. */
static inline int panel_chevron_stroke(int x,int y,int ax,int ay,int bx,int by,int radius) {
    int dx=bx-ax,dy=by-ay,px=x-ax,py=y-ay;
    int length=dx*dx+dy*dy,dot=px*dx+py*dy;
    if(dot<=0)return px*px+py*py<=radius*radius;
    if(dot>=length){px=x-bx;py=y-by;return px*px+py*py<=radius*radius;}
    int cross=px*dy-py*dx;
    return (int64_t)cross*cross<=(int64_t)radius*radius*length;
}
static inline void panel_chevron(uint32_t *p,int cx,int cy,enum panel_chevron_direction direction,
                                 int span,int depth,int stroke,uint32_t color) {
    if(direction<PANEL_CHEVRON_UP||direction>PANEL_CHEVRON_RIGHT||span<1||span>32||
       depth<1||depth>32||stroke<1||stroke>8)return;
    int bound=(span>(depth+1)/2?span:(depth+1)/2)+stroke+1;
    for(int y=-bound;y<bound;y++)for(int x=-bound;x<bound;x++) {
        unsigned inside=0;
        for(int sy=1;sy<=3;sy+=2)for(int sx=1;sx<=3;sx+=2) {
            int px=4*x+sx,py=4*y+sy;
            int u=direction==PANEL_CHEVRON_UP?-py:direction==PANEL_CHEVRON_DOWN?py:
                direction==PANEL_CHEVRON_LEFT?-px:px;
            int v=direction==PANEL_CHEVRON_UP||direction==PANEL_CHEVRON_DOWN?px:py;
            inside+=panel_chevron_stroke(u,v,-2*depth,-4*span,2*depth,0,2*stroke)||
                panel_chevron_stroke(u,v,-2*depth,4*span,2*depth,0,2*stroke);
        }
        if(inside)panel_blend(p,cx+x,cy+y,color,inside*255/4);
    }
}
static inline void panel_toggle_draw(uint32_t *p,const struct panel_view *v) {
    struct panel_hitbox b=panel_toggle_visual_box(v->sidebar_visible);
    uint32_t fg=v->touch_connected?panel_color(42,213,239,255):panel_color(163,178,195,255);
    panel_roundrect_set(p,b.x,b.y,b.w,b.h,12,panel_color(35,70,90,220));
    panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,11,
        v->pressed && v->pressed_action==1?panel_color(25,77,90,230):panel_color(10,16,28,200));
    panel_chevron(p,b.x+b.w/2,b.y+b.h/2,
        v->sidebar_visible?PANEL_CHEVRON_RIGHT:PANEL_CHEVRON_LEFT,8,8,2,fg);
}
#include "video_status_toolbar.h"
#include "video_status_gallery_render.h"
#include "video_status_power_render.h"
#include "video_status_image_render.h"
#include "video_status_robot_render.h"
static inline void panel_render_view(uint32_t *p,const struct panel_status *s,
                                  const struct panel_temperature *t,
                                  uint64_t seq,unsigned slot,uint64_t uptime_s,const struct panel_view *view) {
    (void)seq; (void)slot;
    uint32_t bg=panel_color(10,16,28,255),ink=panel_color(226,235,243,255);
    uint32_t cyan=panel_color(42,213,239,255),muted=panel_color(163,178,195,255);
    uint32_t amber=panel_color(240,185,90,255),bad=panel_color(255,105,95,255);
    memset(p,0,PANEL_BYTES);
    if(view->gallery_open){panel_gallery_draw(p,view,t);panel_power_draw(p,view);return;}
    panel_rect(p,0,0,PANEL_WIDTH,64,bg);
    panel_rect(p,0,63,PANEL_WIDTH,1,panel_color(35,83,102,255));
    const int dividers[]={216,480,742,1098};
    for(unsigned i=0;i<sizeof(dividers)/sizeof(dividers[0]);i++)
        panel_rect(p,dividers[i],15,1,34,panel_color(41,57,77,255));
    panel_roundrect(p,16,28,9,9,4,s->online?cyan:amber);
    panel_text(p,36,21,"AC-16 ARRAY",PANEL_F_EN,cyan);
    panel_text(p,248,5,"MODE",PANEL_F_SMALL,muted);
    panel_text(p,248,30,"DELAY-SUM",PANEL_F_EN,ink);
    panel_text(p,512,5,"SOUND",PANEL_F_SMALL,muted);
    panel_text(p,512,30,!s->online?"NO DATA":!strcmp(s->sound,"active")?"ACTIVE":"QUIET",PANEL_F_EN,
        s->online?cyan:amber);
    int level=panel_temp_level(t);
    uint32_t thermal=level<0?muted:level==2?bad:level==1?amber:cyan;
    char line[80];
    panel_text(p,776,5,"XADC TEMP",PANEL_F_SMALL,muted);
    if(t->valid) snprintf(line,sizeof(line),"%.1f°C",t->millidegrees/1000.0);
    else strcpy(line,"--.-°C");
    panel_text(p,776,27,line,PANEL_F_NUMBER,thermal);
    panel_badge(p,930,27,82,level<0?"N/A":level==2?"HOT":level==1?"WARM":"OK",thermal);
    panel_text(p,1120,5,"UPTIME",PANEL_F_SMALL,muted);
    snprintf(line,sizeof(line),"%02llu:%02llu:%02llu",(unsigned long long)((uptime_s/3600)%100),
        (unsigned long long)((uptime_s/60)%60),(unsigned long long)(uptime_s%60));
    panel_text(p,1120,30,line,PANEL_F_MONO,ink);
    panel_image_draw(p,view);
    if(view->image.editing||view->image.settings){panel_power_draw(p,view);return;}
    panel_toggle_draw(p,view);
    struct panel_toolbar_state toolbar={.image=&view->image,.acoustic_known=view->acoustic_known,.acoustic_on=view->acoustic_on,
        .acoustic_pending=view->acoustic_pending,.acoustic_error=view->acoustic_error_until_ms!=0,
        .pressed=!view->pressed?PANEL_TOOL_NONE:view->pressed_action==2?PANEL_TOOL_ACOUSTIC:
            view->pressed_action==3?PANEL_TOOL_CAPTURE:PANEL_TOOL_NONE,
        .capture_busy=view->capture_busy,.capture_feedback=view->capture_feedback,
        .count_known=view->capture_count_known,.screenshots=view->capture_count};
    if(view->acoustic_known && !view->acoustic_pending)toolbar.available=1u<<PANEL_TOOL_ACOUSTIC;
    if(view->capture_ready && !view->capture_busy)toolbar.available|=1u<<PANEL_TOOL_CAPTURE;
    if(view->gallery_ready)toolbar.available|=1u<<PANEL_TOOL_GALLERY;
    panel_toolbar_draw(p,&toolbar);
    if(!view->sidebar_visible){panel_robot_draw(p,view);panel_power_draw(p,view);return;}
    /* Rounded measurement card, following the user's reference hierarchy. */
    panel_roundrect_set(p,864,80,400,352,18,panel_color(35,70,90,210));
    panel_roundrect_set(p,865,81,398,350,17,panel_color(10,16,28,PANEL_SIDEBAR_ALPHA));
    panel_text(p,884,96,"声源遥测 [ACOUSTIC]",PANEL_F_TITLE,ink);
    const char *lock=!s->online?"NO DATA":s->acoustic_known && !s->acoustic_enabled?"OFF":s->valid?"LOCKED":
        !strcmp(s->sound,"quiet")?"QUIET":"TRACKING";
    panel_badge(p,1150,95,94,lock,s->valid?cyan:muted);
    panel_rect(p,884,135,360,1,panel_color(35,70,90,255));
    panel_card(p,884,151,176,116);
    panel_card(p,1076,151,168,116);
    panel_text(p,896,161,"实时坐标(X,Y)",PANEL_F_LABEL,muted);
    if(s->raw_valid) snprintf(line,sizeof(line),"%02d, %02d",s->raw_col,s->raw_row);
    else strcpy(line,"--, --");
    panel_text(p,896,198,line,PANEL_F_COORD,cyan);
    if(s->valid) snprintf(line,sizeof(line),"稳定 %02d,%02d",s->col,s->row);
    else strcpy(line,"稳定 --,--");
    panel_text(p,896,241,line,PANEL_F_SMALL,muted);
    panel_text(p,1088,161,"网格能量峰值",PANEL_F_LABEL,muted);
    if(s->online) snprintf(line,sizeof(line),"%u",s->peak); else strcpy(line,"--");
    panel_text(p,1088,200,line,PANEL_F_NUMBER,amber);
    panel_text(p,1088,241,"原始单位",PANEL_F_SMALL,muted);
    panel_card(p,884,281,360,74);
    panel_text(p,896,290,"主峰频率",PANEL_F_LABEL,muted);
    if(s->frequency.valid)snprintf(line,sizeof(line),"%.0f Hz",s->frequency.hz);
    else strcpy(line,"-- Hz");
    panel_text(p,1232-panel_text_width(line,PANEL_F_NUMBER),286,line,PANEL_F_NUMBER,cyan);
    const char *frequency_state=s->frequency.valid?"LIVE":
        !strcmp(s->sound,"quiet") && s->online?"QUIET":
        s->frequency.fresh?"NO PEAK":"NO DATA";
    snprintf(line,sizeof(line),"SINGLE MIC CH%u / %s",s->frequency.channel,frequency_state);
    panel_text(p,896,323,line,PANEL_F_SMALL,muted);
    panel_text(p,884,370,"机械臂状态",PANEL_F_LABEL,muted);
    uint32_t robot_color=view->robot_state==PANEL_ROBOT_IDLE||view->robot_state==PANEL_ROBOT_COMPLETE?panel_color(67,214,170,255):
        view->robot_state==PANEL_ROBOT_EXECUTING||view->robot_state==PANEL_ROBOT_WAITING||
        view->robot_state==PANEL_ROBOT_UNKNOWN||view->robot_state==PANEL_ROBOT_TRACKING||
        view->robot_state==PANEL_ROBOT_TESTING?amber:muted;
    panel_roundrect_set(p,1116,366,128,32,16,panel_color(20,44,53,255));
    panel_roundrect_set(p,1130,378,8,8,4,robot_color);
    const char *robot_label=panel_robot_label(view->robot_state);
    panel_text(p,1148+(84-panel_text_width(robot_label,PANEL_F_SMALL))/2,369,robot_label,PANEL_F_SMALL,robot_color);
    snprintf(line,sizeof(line),"定位状态: %s",panel_position_label(s));
    panel_text(p,884,400,line,PANEL_F_SMALL,s->valid?cyan:muted);
    panel_text(p,1120,400,"64×36 网格",PANEL_F_SMALL,muted);
    panel_robot_draw(p,view);
    panel_power_draw(p,view);
}
static inline void panel_render(uint32_t *p,const struct panel_status *s,
                                const struct panel_temperature *t,uint64_t seq,unsigned slot,uint64_t uptime_s) {
    const struct panel_view view={.sidebar_visible=1};
    panel_render_view(p,s,t,seq,slot,uptime_s,&view);
}
#endif

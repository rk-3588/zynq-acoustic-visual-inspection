#ifndef VIDEO_STATUS_GALLERY_RENDER_H
#define VIDEO_STATUS_GALLERY_RENDER_H
#include "camera_album_core.h"
#include "video_status_gallery_text.h"
static inline void gallery_text(uint32_t *p,int x,int y,const char *s,unsigned font,uint32_t ink,int width) {
    char fit[256];unsigned used=0;int pixels=0;
    while(*s&&used+5<sizeof(fit)) {
        const char *begin=s;uint32_t code=panel_utf8(&s);const struct panel_glyph *g=panel_glyph_find(font,code);
        if(g&&pixels+g->advance>width)break;
        size_t n=(size_t)(s-begin);if(used+n>=sizeof(fit))break;
        memcpy(fit+used,begin,n);used+=(unsigned)n;if(g)pixels+=g->advance;
    }
    fit[used]=0;panel_text(p,x,y,fit,font,ink);
}
static inline void gallery_image(uint32_t *p,int x,int y,unsigned w,unsigned h,const uint32_t *source) {
    if(x<0||y<0||(unsigned)x+w>PANEL_WIDTH||(unsigned)y+h>PANEL_HEIGHT)return;
    for(unsigned row=0;row<h;row++)memcpy(p+(y+(int)row)*PANEL_WIDTH+x,source+row*w,w*4u);
}
static inline void gallery_button(uint32_t *p,struct panel_hitbox b,const char *label,int enabled,int pressed) {
    uint32_t edge=panel_color(35,70,90,255),ink=enabled?panel_color(42,213,239,255):panel_color(95,112,128,255);
    panel_roundrect_set(p,b.x,b.y,b.w,b.h,12,edge);
    panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,11,pressed?panel_color(21,67,82,255):panel_color(16,30,42,255));
    panel_toolbar_center(p,b,b.y+12,label,PANEL_F_TITLE,ink);
}
static inline struct panel_hitbox panel_gallery_pager_box(const struct panel_view *v,int previous,int next) {
    struct panel_hitbox left=panel_gallery_target(v,previous),right=panel_gallery_target(v,next);
    return (struct panel_hitbox){left.x+left.w,left.y,right.x-left.x-left.w,left.h};
}
static inline const char *gallery_state(const struct album_meta *m) {
    if(!strcmp(m->position,"disabled"))return "关闭";
    if(!strcmp(m->sound,"quiet"))return "静音";
    if(m->position_valid)return "稳定";
    if(!strcmp(m->sound,"active"))return "活动";
    return "未知";
}
static inline void gallery_coordinate(char *s,size_t n,int col,int row) {
    if(col>=0&&row>=0)snprintf(s,n,"(%02d, %02d)",col,row);else snprintf(s,n,"(--, --)");
}
static inline void gallery_time(char *s,size_t n,uint64_t trigger) {
    uint64_t t=trigger/1000000000ull;
    snprintf(s,n,"+%02llu:%02llu:%02llu",(unsigned long long)(t/3600),
        (unsigned long long)(t/60%60),(unsigned long long)(t%60));
}
static inline void gallery_record_line(char *s,size_t n,const struct album_meta *m) {
    char time[48];gallery_time(time,sizeof(time),m->trigger_ns);
    snprintf(s,n,"#%03llu  %s",(unsigned long long)m->sequence,time);
}
static inline void panel_gallery_draw(uint32_t *p,const struct panel_view *v,const struct panel_temperature *temperature) {
    uint32_t ink=panel_color(226,235,243,255),muted=panel_color(157,176,194,255);
    uint32_t cyan=panel_color(42,213,239,255),amber=panel_color(240,185,90,255),edge=panel_color(35,59,76,255);
    const struct album_packet *data=v->gallery_data;
    panel_rect(p,0,0,752,720,panel_color(0,0,0,v->gallery_detail?196:76));
    panel_rect(p,752,0,528,720,panel_color(12,20,29,255));panel_rect(p,752,0,1,720,edge);
    panel_rect(p,752,0,528,78,panel_color(17,27,37,255));panel_rect(p,752,77,528,1,edge);
    panel_text(p,32,26,v->gallery_detail?"CAPTURE PREVIEW":"LIVE CAMERA",PANEL_F_EN,ink);
    char line[160];
    if(temperature->valid)snprintf(line,sizeof(line),"XADC %.1f°C",temperature->millidegrees/1000.0);
    else strcpy(line,"XADC --°C");
    int thermal=panel_temp_level(temperature);
    panel_text(p,456,26,line,PANEL_F_EN,thermal==2?panel_color(255,105,95,255):thermal==1?amber:cyan);
    panel_text(p,776,16,v->gallery_detail?"抓拍详情":"抓拍相册记录",PANEL_F_TITLE,ink);
    if(!v->gallery_detail) {
        snprintf(line,sizeof(line),"%u",data&&data->kind==1?data->total:0);
        panel_badge(p,962,15,64,line,cyan);
        if(data&&data->kind==1)snprintf(line,sizeof(line),"BMP + JSON | SD剩余 %.1f GiB",data->free_bytes/1073741824.0);
        else strcpy(line,"BMP + JSON | SD剩余 -- GiB");
        panel_text(p,776,50,line,PANEL_F_SMALL,muted);
        gallery_button(p,panel_gallery_target(v,8),"刷新",!v->gallery_busy,v->pressed&&v->pressed_action==8);
    }
    struct panel_hitbox close=panel_gallery_target(v,5);
    panel_roundrect_set(p,close.x,close.y,close.w,close.h,12,panel_color(30,43,53,255));
    panel_toolbar_line(p,1232,28,1248,44,2,ink);panel_toolbar_line(p,1248,28,1232,44,2,ink);
    if(v->gallery_detail) {
        gallery_button(p,panel_gallery_target(v,9),"返回列表",!v->gallery_busy,v->pressed&&v->pressed_action==9);
        gallery_button(p,panel_gallery_target(v,14),v->gallery_json?"测量摘要":"详细数据",!v->gallery_busy&&!v->gallery_error,v->pressed&&v->pressed_action==14);
    } else panel_text(p,776,84,"点击条目查看原图与测量",PANEL_F_SMALL,muted);
    if(v->gallery_busy||v->gallery_error||!data) {
        panel_text(p,816,260,v->gallery_error?"无法读取截图":"加载中",PANEL_F_TITLE,v->gallery_error?amber:cyan);
        if(v->gallery_error)panel_text(p,816,302,"请返回列表或刷新重试",PANEL_F_SMALL,muted);
        return;
    }
    if(v->gallery_detail&&data->kind==2) {
        const struct album_meta *m=&data->entries[0].meta;
        panel_roundrect_set(p,40,132,672,392,16,panel_color(23,34,44,255));
        gallery_image(p,56,148,ALBUM_PW,ALBUM_PH,data->preview);
        if(v->gallery_json) {
            char lines[256][49];unsigned count=panel_gallery_text_lines(data->json,lines,256);
            for(unsigned i=0;i<20;i++) {
                unsigned row=v->gallery_json_page*20+i;
                if(row<count)panel_text(p,776,144+(int)i*24,lines[row],PANEL_F_MONO,ink);
            }
            gallery_button(p,panel_gallery_target(v,15),"上一页",v->gallery_json_page>0,v->pressed&&v->pressed_action==15);
            gallery_button(p,panel_gallery_target(v,16),"下一页",v->gallery_json_page+1<v->gallery_json_pages,v->pressed&&v->pressed_action==16);
            snprintf(line,sizeof(line),"%u / %u",v->gallery_json_page+1,v->gallery_json_pages?v->gallery_json_pages:1);
            panel_toolbar_center(p,panel_gallery_pager_box(v,15,16),664,line,PANEL_F_SMALL,muted);return;
        }
        const char *labels[]={"定位状态","实时网格坐标","稳定网格坐标","网格能量峰值","主峰频率","XADC 温度"};
        for(unsigned i=0;i<6;i++) {
            if(i==4) {
                if(m->frequency_channel<16)snprintf(line,sizeof(line),"主峰频率 / CH%u",m->frequency_channel);
                else strcpy(line,"主峰频率 / --");
                panel_text(p,776,156+(int)i*62,line,PANEL_F_LABEL,muted);
            } else panel_text(p,776,156+(int)i*62,labels[i],PANEL_F_LABEL,muted);
        }
        panel_text(p,1072,154,gallery_state(m),PANEL_F_TITLE,m->position_valid?cyan:muted);
        gallery_coordinate(line,sizeof(line),m->raw_col,m->raw_row);panel_text(p,1072,216,line,PANEL_F_MONO,cyan);
        gallery_coordinate(line,sizeof(line),m->position_valid?m->stable_col:-1,m->stable_row);panel_text(p,1072,278,line,PANEL_F_MONO,cyan);
        if(m->energy_valid)snprintf(line,sizeof(line),"%u",m->energy);else strcpy(line,"--");panel_text(p,1072,340,line,PANEL_F_MONO,amber);
        if(m->frequency_valid)snprintf(line,sizeof(line),"%.0f Hz",m->frequency);else strcpy(line,"-- Hz");panel_text(p,1072,402,line,PANEL_F_MONO,cyan);
        if(m->temp_valid)snprintf(line,sizeof(line),"%.1f°C",m->temp_mc/1000.0);else strcpy(line,"--°C");panel_text(p,1072,464,line,PANEL_F_MONO,ink);
        return;
    }
    if(data->kind!=1)return;
    if(!data->total)panel_text(p,816,260,"暂无截图",PANEL_F_TITLE,muted);
    for(unsigned i=0;i<data->n;i++) {
        const struct album_entry *e=&data->entries[i];const struct album_meta *m=&e->meta;
        struct panel_hitbox b=panel_gallery_target(v,10+(int)i);
        panel_roundrect_set(p,b.x,b.y,b.w,b.h,16,edge);
        panel_roundrect_set(p,b.x+1,b.y+1,b.w-2,b.h-2,15,v->pressed&&v->pressed_action==10+(int)i?panel_color(24,55,67,255):panel_color(25,38,49,255));
        if(m->thumbnail_ok)gallery_image(p,784,b.y+13,ALBUM_TW,ALBUM_TH,e->pixels);
        else {panel_rect(p,784,b.y+13,160,90,panel_color(15,24,34,255));panel_text(p,804,b.y+44,"预览不可用",PANEL_F_SMALL,muted);}
        panel_badge(p,884,b.y+76,60,gallery_state(m),m->position_valid?cyan:muted);
        gallery_record_line(line,sizeof(line),m);gallery_text(p,960,b.y+14,line,PANEL_F_EN,ink,276);
        char coordinate[40];gallery_coordinate(coordinate,sizeof(coordinate),m->raw_col,m->raw_row);
        if(m->frequency_valid)snprintf(line,sizeof(line),"%s  %.0f Hz / CH%u",coordinate,m->frequency,m->frequency_channel);
        else snprintf(line,sizeof(line),"%s  -- Hz",coordinate);
        gallery_text(p,960,b.y+47,line,PANEL_F_SMALL,cyan,276);
        if(m->energy_valid)snprintf(line,sizeof(line),"峰值 %u",m->energy);else strcpy(line,"峰值 --");
        gallery_text(p,960,b.y+82,line,PANEL_F_SMALL,muted,190);panel_text(p,1190,b.y+82,"详情 >",PANEL_F_SMALL,cyan);
    }
    gallery_button(p,panel_gallery_target(v,6),"上一页",v->gallery_page>0,v->pressed&&v->pressed_action==6);
    gallery_button(p,panel_gallery_target(v,7),"下一页",v->gallery_page+1<v->gallery_pages,v->pressed&&v->pressed_action==7);
    struct panel_hitbox pager=panel_gallery_pager_box(v,6,7);
    snprintf(line,sizeof(line),"%u / %u",v->gallery_page+1,v->gallery_pages?v->gallery_pages:1);
    panel_toolbar_center(p,pager,654,line,PANEL_F_SMALL,muted);
    snprintf(line,sizeof(line),data->bad?"损坏 %u":"上滑翻页",data->bad);
    panel_toolbar_center(p,pager,680,line,PANEL_F_SMALL,data->bad?amber:muted);
}
#endif

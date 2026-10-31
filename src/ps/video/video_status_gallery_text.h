#ifndef VIDEO_STATUS_GALLERY_TEXT_H
#define VIDEO_STATUS_GALLERY_TEXT_H
#include "camera_album_core.h"
struct gallery_text_output {char (*lines)[49];unsigned cap,row,col;};
static inline void gallery_text_char(struct gallery_text_output *o,unsigned char c) {
    if(o->row>=o->cap)return;
    if(c=='\r')return;
    if(c=='\n'){o->lines[o->row][o->col]=0;o->row++;o->col=0;return;}
    o->lines[o->row][o->col++]=c>=32&&c<127?(char)c:'?';
    if(o->col==36){o->lines[o->row][o->col]=0;o->row++;o->col=0;}
}
static inline void gallery_text_span(struct gallery_text_output *o,const char *s,size_t n) {
    for(size_t i=0;i<n&&o->row<o->cap;i++)gallery_text_char(o,(unsigned char)s[i]);
}
static inline size_t gallery_text_unescape(const char *s,size_t n,char *out) {
    size_t used=0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)s[i];
        if(c=='\\'&&i+1<n) {
            c=(unsigned char)s[++i];
            if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c=' ';
            else if(c=='b'||c=='f')c=' ';
            else if(c=='u') {
                unsigned value=0;
                for(unsigned k=0;k<4&&i+1<n;k++) {unsigned char h=(unsigned char)s[++i];value=value*16+(h<='9'?h-'0':(h|32)-'a'+10);}
                c=value>=32&&value<127?(unsigned char)value:'?';
            }
        }
        out[used++]=(char)c;
    }
    out[used]=0;return used;
}
static inline void gallery_text_indent(struct gallery_text_output *o,unsigned depth) {
    for(unsigned i=0;i<depth*2&&i<12;i++)gallery_text_char(o,' ');
}
static inline int gallery_text_value(struct pl_json *j,struct gallery_text_output *o,unsigned depth) {
    pl_json_space(j);if(j->p==j->end||depth>16)return 0;
    if(*j->p=='{'||*j->p=='[') {
        int object=*j->p=='{';char close=object?'}':']';j->p++;pl_json_space(j);
        if(j->p<j->end&&*j->p==close){j->p++;return 1;}
        while(j->p<j->end&&o->row<o->cap) {
            gallery_text_indent(o,depth);
            if(object) {
                const char *key;size_t n;
                if(!pl_json_string(j,&key,&n))return 0;
                char *decoded=malloc(n+1);if(!decoded)return 0;
                size_t length=gallery_text_unescape(key,n,decoded);gallery_text_span(o,decoded,length);free(decoded);
                pl_json_space(j);if(j->p==j->end||*j->p++!=':')return 0;
                gallery_text_char(o,':');gallery_text_char(o,' ');
            } else {gallery_text_char(o,'-');gallery_text_char(o,' ');}
            pl_json_space(j);
            if(j->p<j->end&&(*j->p=='{'||*j->p=='['))gallery_text_char(o,'\n');
            if(!gallery_text_value(j,o,depth+1))return 0;
            if(o->row>=o->cap)return 1;
            if(o->col)gallery_text_char(o,'\n');
            pl_json_space(j);if(j->p==j->end)return 0;
            if(*j->p==close){j->p++;return 1;}
            if(*j->p++!=',')return 0;
            pl_json_space(j);
        }
        return o->row>=o->cap;
    }
    if(*j->p=='"') {
        const char *s;size_t n;if(!pl_json_string(j,&s,&n))return 0;
        char *decoded=malloc(n+1);if(!decoded)return 0;
        size_t length=gallery_text_unescape(s,n,decoded);
        struct pl_json nested={decoded,decoded+length,0};pl_json_space(&nested);
        int container=nested.p<nested.end&&(*nested.p=='{'||*nested.p=='[');
        struct pl_json check=nested;int valid=container&&pl_json_value(&check);pl_json_space(&check);
        int ok=1;
        if(valid&&check.p==check.end&&depth<16) {
            gallery_text_char(o,'\n');ok=gallery_text_value(&nested,o,depth);
        } else gallery_text_span(o,decoded,length);
        free(decoded);return ok;
    }
    const char *start=j->p;if(!pl_json_value(j))return 0;
    gallery_text_span(o,start,(size_t)(j->p-start));return 1;
}
static inline unsigned panel_gallery_text_lines(const char *text,char lines[][49],unsigned capacity) {
    if(!capacity)return 0;
    struct pl_json j={text,text+strnlen(text,ALBUM_JSON),0},check=j;
    int valid=pl_json_value(&check);pl_json_space(&check);
    struct gallery_text_output output={lines,capacity,0,0};
    if(!valid||check.p!=check.end||!gallery_text_value(&j,&output,0)) {
        strcpy(lines[0],"Text unavailable");return 1;
    }
    if(output.col)gallery_text_char(&output,'\n');
    if(output.row==capacity)strcpy(lines[capacity-1],"... (text display limit)");
    return output.row;
}
#endif

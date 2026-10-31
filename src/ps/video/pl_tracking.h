#ifndef PL_TRACKING_H
#define PL_TRACKING_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define PL_PERIOD_MS 50u
#define PL_WINDOW_MS 500u
#define PL_SOURCE_TIMEOUT_MS 1000u
#define PL_MIN_SAMPLES 8u
#define PL_RADIUS_CELLS 3
#define PL_DWELL_MS 300u
#define PL_WINDOW_CAPACITY 10u
enum pl_gate { PL_GATE_UNKNOWN, PL_GATE_QUIET, PL_GATE_ACTIVE };
struct pl_point { int col, row; uint64_t ms; };
struct pl_tracker {
    struct pl_point points[PL_WINDOW_CAPACITY];
    unsigned count, inliers;
    int col, row, have_position, valid, candidate_ready;
    int anchor_col, anchor_row;
    uint64_t candidate_since, stable_ms;
    const char *state;
};
static inline int pl_coordinate_valid(uint32_t x,uint32_t y) {
    return x>=1 && x<=127 && y>=1 && y<=71 && (x&1) && (y&1);
}
static inline int pl_near(int x,int y,int cx,int cy) {
    int dx=x-cx,dy=y-cy;
    return dx*dx+dy*dy<=PL_RADIUS_CELLS*PL_RADIUS_CELLS;
}
static inline int pl_median(int *values,unsigned count) {
    for(unsigned i=1;i<count;i++) {
        int v=values[i];unsigned j=i;
        while(j && values[j-1]>v) {values[j]=values[j-1];j--;}
        values[j]=v;
    }
    return values[count/2];
}
static inline void pl_tracker_clear(struct pl_tracker *t,const char *state) {
    memset(t,0,sizeof(*t));t->state=state;
}
static inline void pl_tracker_step(struct pl_tracker *t,uint64_t now,
    enum pl_gate gate,int good,uint32_t x,uint32_t y) {
    if(gate!=PL_GATE_ACTIVE) {
        pl_tracker_clear(t,gate==PL_GATE_QUIET?"quiet":"unavailable");return;
    }
    unsigned kept=0;
    for(unsigned i=0;i<t->count;i++)
        if(now>=t->points[i].ms && now-t->points[i].ms<=PL_WINDOW_MS)
            t->points[kept++]=t->points[i];
    t->count=kept;
    if(!good || !pl_coordinate_valid(x,y)) {
        t->valid=0;t->candidate_ready=0;t->stable_ms=0;t->state="read_unstable";
        if(!kept)t->have_position=0;
        return;
    }
    int col=(int)((x-1)/2),row=(int)((y-1)/2);
    if(t->count==PL_WINDOW_CAPACITY) {
        memmove(t->points,t->points+1,(PL_WINDOW_CAPACITY-1)*sizeof(t->points[0]));t->count--;
    }
    t->points[t->count++]=(struct pl_point){col,row,now};
    int cols[PL_WINDOW_CAPACITY]={0},rows[PL_WINDOW_CAPACITY]={0};
    for(unsigned i=0;i<t->count;i++) {cols[i]=t->points[i].col;rows[i]=t->points[i].row;}
    t->col=pl_median(cols,t->count);t->row=pl_median(rows,t->count);t->have_position=1;t->inliers=0;
    for(unsigned i=0;i<t->count;i++)
        if(pl_near(t->points[i].col,t->points[i].row,t->col,t->row))t->inliers++;
    if(!t->candidate_ready || !pl_near(t->col,t->row,t->anchor_col,t->anchor_row)) {
        t->candidate_ready=1;t->anchor_col=t->col;t->anchor_row=t->row;t->candidate_since=now;
    }
    int spatial=t->inliers*5>=t->count*4 && pl_near(col,row,t->col,t->row);
    if(!spatial) {
        t->candidate_ready=0;t->stable_ms=0;t->valid=0;t->state="moving";return;
    }
    t->stable_ms=now-t->candidate_since;
    t->valid=t->count>=PL_MIN_SAMPLES &&
        now-t->points[0].ms>=(PL_MIN_SAMPLES-1)*PL_PERIOD_MS && t->stable_ms>=PL_DWELL_MS;
    t->state=t->valid?"stable":"acquiring";
}
/* Strict, bounded JSON reader for the display-state document. No duplicate gate algorithm. */
struct pl_json { const char *p,*end;unsigned depth; };
static inline void pl_json_space(struct pl_json *j) {
    while(j->p<j->end && (*j->p==' ' || *j->p=='\n' || *j->p=='\r' || *j->p=='\t'))j->p++;
}
static inline int pl_json_string(struct pl_json *j,const char **start,size_t *length) {
    if(j->p==j->end || *j->p++!='"')return 0;
    const char *s=j->p;
    while(j->p<j->end) {
        unsigned char c=(unsigned char)*j->p++;
        if(c=='"') {*start=s;*length=(size_t)(j->p-s-1);return 1;}
        if(c<32)return 0;
        if(c=='\\') {
            if(j->p==j->end)return 0;
            c=(unsigned char)*j->p++;
            if(c=='u') {
                for(unsigned i=0;i<4;i++) {
                    if(j->p==j->end)return 0;
                    c=(unsigned char)*j->p++;
                    if(!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))return 0;
                }
            } else if(!c || !strchr("\"\\/bfnrt",c))return 0;
        }
    }
    return 0;
}
static inline int pl_json_value(struct pl_json *j);
static inline int pl_json_container(struct pl_json *j,char opening,char closing) {
    if(j->depth++>=8)return 0;
    j->p++;pl_json_space(j);
    if(j->p<j->end && *j->p==closing) {j->p++;j->depth--;return 1;}
    while(j->p<j->end) {
        if(opening=='{') {
            const char *key;size_t length;
            if(!pl_json_string(j,&key,&length))return 0;
            pl_json_space(j);if(j->p==j->end || *j->p++!=':')return 0;
        }
        if(!pl_json_value(j))return 0;
        pl_json_space(j);if(j->p==j->end)return 0;
        if(*j->p==closing) {j->p++;j->depth--;return 1;}
        if(*j->p++!=',')return 0;
        pl_json_space(j);
    }
    return 0;
}
static inline int pl_json_value(struct pl_json *j) {
    pl_json_space(j);if(j->p==j->end)return 0;
    if(*j->p=='{' || *j->p=='[')return pl_json_container(j,*j->p,*j->p=='{'?'}':']');
    if(*j->p=='"') {const char *s;size_t n;return pl_json_string(j,&s,&n);}
    const char *literals[]={"true","false","null"};
    for(unsigned i=0;i<3;i++) {
        size_t n=strlen(literals[i]);
        if((size_t)(j->end-j->p)>=n && !memcmp(j->p,literals[i],n)) {j->p+=n;return 1;}
    }
    if(*j->p=='-')j->p++;
    if(j->p==j->end)return 0;
    if(*j->p=='0')j->p++;
    else {
        if(*j->p<'1' || *j->p>'9')return 0;
        while(j->p<j->end && *j->p>='0' && *j->p<='9')j->p++;
    }
    if(j->p<j->end && *j->p=='.') {
        j->p++;const char *s=j->p;
        while(j->p<j->end && *j->p>='0' && *j->p<='9')j->p++;
        if(j->p==s)return 0;
    }
    if(j->p<j->end && (*j->p=='e' || *j->p=='E')) {
        j->p++;if(j->p<j->end && (*j->p=='+' || *j->p=='-'))j->p++;
        const char *s=j->p;
        while(j->p<j->end && *j->p>='0' && *j->p<='9')j->p++;
        if(j->p==s)return 0;
    }
    return 1;
}
static inline int pl_parse_gate(const char *text,size_t length,enum pl_gate *gate) {
    struct pl_json j={text,text+length,0};int found=0;
    pl_json_space(&j);if(j.p==j.end || *j.p++!='{')return 0;
    for(;;) {
        const char *key;size_t size;
        pl_json_space(&j);if(!pl_json_string(&j,&key,&size))return 0;
        pl_json_space(&j);if(j.p==j.end || *j.p++!=':')return 0;
        if(size==4 && !memcmp(key,"gate",4)) {
            const char *value;size_t n;
            pl_json_space(&j);if(found++ || !pl_json_string(&j,&value,&n))return 0;
            if(n==6 && !memcmp(value,"active",6))*gate=PL_GATE_ACTIVE;
            else if(n==5 && !memcmp(value,"quiet",5))*gate=PL_GATE_QUIET;
            else if(n==7 && !memcmp(value,"stopped",7))*gate=PL_GATE_UNKNOWN;
            else return 0;
        } else if(!pl_json_value(&j))return 0;
        pl_json_space(&j);if(j.p==j.end)return 0;
        if(*j.p=='}') {j.p++;break;}
        if(*j.p++!=',')return 0;
    }
    pl_json_space(&j);return found==1 && j.p==j.end;
}
#endif

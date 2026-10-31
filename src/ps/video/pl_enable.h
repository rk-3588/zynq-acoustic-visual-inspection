#ifndef PL_ENABLE_H
#define PL_ENABLE_H
#include "pl_tracking.h"
#define PL_ENABLE_DISPLAY_VERSION "20261013-display-enable-v7"
struct pl_display_ack {
    int enabled,alpha_ok;
    unsigned alpha;
    uint64_t observed_ms;
};
static inline int pl_enable_uint(const char *p,const char *end,uint64_t *out) {
    uint64_t v=0;if(p==end)return 0;
    for(;p<end;p++) {
        if(*p<'0' || *p>'9' || v>(UINT64_MAX-(unsigned)(*p-'0'))/10)return 0;
        v=v*10+(unsigned)(*p-'0');
    }
    *out=v;return 1;
}
static inline int pl_enable_bool(const char *p,const char *end,int *out) {
    if(end-p==4 && !memcmp(p,"true",4)){*out=1;return 1;}
    if(end-p==5 && !memcmp(p,"false",5)){*out=0;return 1;}
    return 0;
}
static inline int pl_enable_parse(const char *text,size_t len,struct pl_display_ack *out) {
    struct pl_json j={text,text+len,0};struct pl_display_ack a={0};unsigned seen=0;
    const char *keys[]={"version","acoustic_enabled","enable_observed_ms","alpha_readback_ok","effective_alpha"};
    pl_json_space(&j);if(j.p==j.end || *j.p++!='{')return 0;
    for(;;) {
        const char *key;size_t n;pl_json_space(&j);if(!pl_json_string(&j,&key,&n))return 0;
        pl_json_space(&j);if(j.p==j.end || *j.p++!=':')return 0;
        pl_json_space(&j);const char *p=j.p;if(!pl_json_value(&j))return 0;
        const char *end=j.p;unsigned k;
        for(k=0;k<sizeof(keys)/sizeof(keys[0]);k++)if(strlen(keys[k])==n && !memcmp(keys[k],key,n))break;
        if(k<sizeof(keys)/sizeof(keys[0])) {
            if(seen&(1u<<k))return 0;
            seen|=1u<<k;
            uint64_t v;
            switch(k) {
            case 0: {
                struct pl_json str={p,end,0};const char *s;size_t size;
                if(!pl_json_string(&str,&s,&size) || str.p!=end || size!=strlen(PL_ENABLE_DISPLAY_VERSION) || memcmp(s,PL_ENABLE_DISPLAY_VERSION,size))return 0;
                break;
            }
            case 1: if(!pl_enable_bool(p,end,&a.enabled))return 0;break;
            case 2: if(!pl_enable_uint(p,end,&a.observed_ms))return 0;break;
            case 3: if(!pl_enable_bool(p,end,&a.alpha_ok))return 0;break;
            case 4: if(!pl_enable_uint(p,end,&v) || v>256)return 0;a.alpha=(unsigned)v;break;
            }
        }
        pl_json_space(&j);if(j.p==j.end)return 0;
        if(*j.p=='}'){j.p++;break;}
        if(*j.p++!=',')return 0;
    }
    pl_json_space(&j);if(j.p!=j.end || seen!=31 || (!a.enabled && a.alpha))return 0;
    *out=a;return 1;
}
static inline int pl_enable_confirmed(int requested,uint64_t changed_ms,uint64_t now,
                                      int fresh,const struct pl_display_ack *a) {
    return fresh && a->alpha_ok && a->enabled==requested &&
        a->observed_ms>changed_ms && a->observed_ms<=now && now-a->observed_ms<PL_SOURCE_TIMEOUT_MS &&
        (requested || a->alpha==0);
}
static inline void pl_enable_tracker_step(struct pl_tracker *t,uint64_t now,
    int enabled,int applied,enum pl_gate gate,int good,uint32_t x,uint32_t y) {
    if(!enabled)pl_tracker_clear(t,"disabled");
    else if(!applied)pl_tracker_clear(t,"enable_pending");
    else pl_tracker_step(t,now,gate,good,x,y);
}
#endif

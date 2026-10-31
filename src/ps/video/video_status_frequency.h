#ifndef VIDEO_STATUS_FREQUENCY_H
#define VIDEO_STATUS_FREQUENCY_H
#include "pl_tracking.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PANEL_FREQUENCY_MAX_AGE_MS 1000u
struct panel_frequency {
    int fresh, candidate_valid, valid;
    unsigned channel, sequence;
    double hz;
    uint64_t sampled_ms, age_ms;
    const char *reason;
};
static inline int panel_freq_uint(const char *p,const char *end,uint64_t *out) {
    uint64_t n=0;
    if(p==end)return 0;
    for(;p<end;p++) {
        if(*p<'0' || *p>'9' || n>(UINT64_MAX-(unsigned)(*p-'0'))/10)return 0;
        n=n*10+(unsigned)(*p-'0');
    }
    *out=n; return 1;
}
static inline int panel_freq_equal(const char *p,const char *end,const char *value) {
    return (size_t)(end-p)==strlen(value) && !memcmp(p,value,(size_t)(end-p));
}
static inline int panel_freq_double(const char *p,const char *end,double *out) {
    char text[64],*tail;size_t n=(size_t)(end-p);
    if(!n || n>=sizeof(text))return 0;
    memcpy(text,p,n);text[n]=0;errno=0;
    double value=strtod(text,&tail);
    if(errno || tail!=text+n || !isfinite(value))return 0;
    *out=value;return 1;
}
/* A separate, best-effort observation. Never claim this belongs to a hotspot. */
static inline int panel_frequency_parse(const char *text,size_t len,uint64_t now,
                                        struct panel_frequency *out) {
    *out=(struct panel_frequency){.reason="unavailable"};
    if(!len || len>=2048)return 0;
    struct pl_json j={text,text+len,0};struct panel_frequency f={0};
    const char *keys[]={"version","channel","sample_rate_hz","samples",
        "spectral_candidate_valid","peak_frequency_hz","clipped_samples","monotonic_ms",
        "frequency_source","hotspot_associated","quiet_gate_fused","sequence"};
    unsigned seen=0;int hz_null=0;
    pl_json_space(&j);if(j.p==j.end || *j.p++!='{')return 0;
    for(;;) {
        const char *key;size_t n;pl_json_space(&j);
        if(!pl_json_string(&j,&key,&n))return 0;
        pl_json_space(&j);if(j.p==j.end || *j.p++!=':')return 0;
        pl_json_space(&j);const char *start=j.p;
        if(!pl_json_value(&j))return 0;
        const char *end=j.p;unsigned k;uint64_t v;
        for(k=0;k<sizeof(keys)/sizeof(keys[0]);k++)
            if(strlen(keys[k])==n && !memcmp(keys[k],key,n))break;
        if(k<sizeof(keys)/sizeof(keys[0])) {
            if(seen&(1u<<k))return 0;
            seen|=1u<<k;
            switch(k) {
            case 0: if(!panel_freq_equal(start,end,"\"20261013-acoustic-frequency-v2\""))return 0;break;
            case 1: if(!panel_freq_uint(start,end,&v) || v>15)return 0;f.channel=(unsigned)v;break;
            case 2: if(!panel_freq_uint(start,end,&v) || v!=46875)return 0;break;
            case 3: if(!panel_freq_uint(start,end,&v) || v!=2048)return 0;break;
            case 4:
                if(panel_freq_equal(start,end,"true"))f.candidate_valid=1;
                else if(!panel_freq_equal(start,end,"false"))return 0;
                break;
            case 5:
                hz_null=panel_freq_equal(start,end,"null");
                if(!hz_null && (!panel_freq_double(start,end,&f.hz) || f.hz<300 || f.hz>10000))return 0;
                break;
            case 6: if(!panel_freq_uint(start,end,&v) || v>2048)return 0;if(v)f.reason="clipped";break;
            case 7: if(!panel_freq_uint(start,end,&f.sampled_ms))return 0;break;
            case 8: if(!panel_freq_equal(start,end,"\"single_mic_hpf_output\""))return 0;break;
            case 9: case 10: if(!panel_freq_equal(start,end,"false"))return 0;break;
            case 11: if(!panel_freq_uint(start,end,&v) || v>UINT32_MAX)return 0;f.sequence=(unsigned)v;break;
            }
        }
        pl_json_space(&j);if(j.p==j.end)return 0;
        if(*j.p=='}'){j.p++;break;}
        if(*j.p++!=',')return 0;
    }
    pl_json_space(&j);
    if(j.p!=j.end || seen!=0xfff || f.candidate_valid==hz_null)return 0;
    if(now<f.sampled_ms || now-f.sampled_ms>PANEL_FREQUENCY_MAX_AGE_MS) {
        out->reason="stale";return 0;
    }
    f.fresh=1;f.age_ms=now-f.sampled_ms;
    if(f.reason)f.candidate_valid=0;
    else f.reason=f.candidate_valid?"candidate":"low_prominence";
    *out=f;return 1;
}
static inline void panel_frequency_gate(struct panel_frequency *f,int online,const char *sound) {
    f->valid=0;
    if(!online){f->reason="gate_unavailable";return;}
    if(!strcmp(sound,"quiet")){f->reason="quiet";return;}
    if(strcmp(sound,"active")){f->reason="gate_unavailable";return;}
    if(f->fresh && f->candidate_valid){f->valid=1;f->reason="live";}
}
static inline void panel_frequency_read(struct panel_frequency *f,const char *path,
                                        uint64_t now,int online,const char *sound) {
    *f=(struct panel_frequency){.reason="unavailable"};
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd>=0) {
        struct stat st;char text[2048];
        if(!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_size>0 && st.st_size<(off_t)sizeof(text)) {
            ssize_t n=read(fd,text,sizeof(text));
            if(n==st.st_size)panel_frequency_parse(text,(size_t)n,now,f);
        }
        close(fd);
    }
    panel_frequency_gate(f,online,sound);
}
#endif

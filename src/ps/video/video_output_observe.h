#ifndef VIDEO_OUTPUT_OBSERVE_H
#define VIDEO_OUTPUT_OBSERVE_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define VO_UIO_NAME "video_output_observer@44a40000"
#define VO_BASE 0x44a40000ull
#define VO_ID 0x564f4d31u
#define VO_WORDS 18u
struct vo_snapshot { int valid,error,vpp_valid; uint32_t seq,word[VO_WORDS],vpp_sofs; uint64_t begin_ns,end_ns; };
static inline uint64_t vo_now_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static inline uint32_t vo_read(volatile uint32_t *r,unsigned off) {
    uint32_t value=r[off/4];__sync_synchronize();return value;
}
static inline int vo_text(const char *path,char *value,size_t capacity) {
    FILE *f=fopen(path,"r");if(!f)return -1;
    int ok=fgets(value,(int)capacity,f)!=NULL;fclose(f);
    if(ok)value[strcspn(value,"\r\n")]=0;
    return ok?0:-1;
}
/* Exact name+physical range+unique match. No hardcoded /dev/uioN or /dev/mem. */
static inline int vo_find(const char *sysroot,const char *devroot,const char *wanted,
                          uint64_t base,char *result,size_t capacity) {
    DIR *d=opendir(sysroot);if(!d)return -1;
    unsigned count=0;char chosen[512]={0};struct dirent *entry;
    while((entry=readdir(d))) {
        const char *suffix=entry->d_name;
        if(strncmp(suffix,"uio",3) || !suffix[3])continue;
        int numeric=1;for(suffix+=3;*suffix;suffix++)if(*suffix<'0'||*suffix>'9')numeric=0;
        if(!numeric)continue;
        char path[512],text[160],*end;unsigned long long addr,size;
        int n=snprintf(path,sizeof(path),"%s/%s/name",sysroot,entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||vo_text(path,text,sizeof(text))||strcmp(text,wanted))continue;
        n=snprintf(path,sizeof(path),"%s/%s/maps/map0/addr",sysroot,entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||vo_text(path,text,sizeof(text)))continue;
        errno=0;addr=strtoull(text,&end,0);if(errno||end==text||*end||*text=='-'||addr!=base)continue;
        n=snprintf(path,sizeof(path),"%s/%s/maps/map0/size",sysroot,entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||vo_text(path,text,sizeof(text)))continue;
        errno=0;size=strtoull(text,&end,0);if(errno||end==text||*end||*text=='-'||size<0x10000)continue;
        n=snprintf(chosen,sizeof(chosen),"%s/%s",devroot,entry->d_name);
        if(n<0||(size_t)n>=sizeof(chosen))continue;
        count++;
    }
    closedir(d);
    if(count!=1){errno=count?EEXIST:ENODEV;return -1;}
    if(strlen(chosen)>=capacity){errno=ENAMETOOLONG;return -1;}
    strcpy(result,chosen);return 0;
}
static inline void vo_sample(struct vo_snapshot *s) {
    memset(s,0,sizeof(*s));s->begin_ns=vo_now_ns();
    char path[512];int fd=-1,lock=-1;volatile uint32_t *r=MAP_FAILED;
    /* Existing VPP frame counter is always a read-only fallback, NOT lock evidence. */
    if(!vo_find("/sys/class/uio","/dev","video_preprocess@40040000",0x40040000,path,sizeof(path))) {
        int vfd=open(path,O_RDONLY|O_SYNC|O_CLOEXEC);
        if(vfd>=0) {
            volatile uint32_t *v=mmap(NULL,0x10000,PROT_READ,MAP_SHARED,vfd,0);
            if(v!=MAP_FAILED) {
                if(vo_read(v,0)==0x56505031u && vo_read(v,0x24)==3) {s->vpp_sofs=vo_read(v,0x1c);s->vpp_valid=1;}
                munmap((void*)v,0x10000);
            }
            close(vfd);
        }
    }
    if(vo_find("/sys/class/uio","/dev",VO_UIO_NAME,VO_BASE,path,sizeof(path))){s->error=errno;goto out;}
    lock=open("/run/video-output-observer.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)){s->error=errno;goto out;}
    fd=open(path,O_RDWR|O_SYNC|O_CLOEXEC);if(fd<0){s->error=errno;goto out;}
    r=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(r==MAP_FAILED){s->error=errno;goto out;}
    if(vo_read(r,0)!=VO_ID || vo_read(r,4)!=1){s->error=EPROTO;goto out;}
    if(vo_read(r,12)&1){s->error=EBUSY;goto out;} // No reset/cancel, preserve pending CDC request.
    uint32_t before=vo_read(r,16);
    r[8/4]=1;__sync_synchronize(); // Only write is this passive monitor's SNAP command.
    uint64_t until=vo_now_ns()+20000000ull;
    while(vo_now_ns()<until) {
        if(vo_read(r,12)==2 && vo_read(r,16)!=before) {
            s->seq=vo_read(r,16);
            for(unsigned i=0;i<VO_WORDS;i++)s->word[i]=vo_read(r,0x20+4*i);
            if(vo_read(r,12)==2 && vo_read(r,16)==s->seq)s->valid=1;
            else s->error=EAGAIN;
            goto out;
        }
        usleep(100);
    }
    s->error=ETIMEDOUT;
out:
    if(r!=MAP_FAILED)munmap((void*)r,0x10000);
    if(fd>=0)close(fd);
    if(lock>=0)close(lock);
    s->end_ns=vo_now_ns();
}
static inline uint32_t vo_delta(uint32_t after,uint32_t before){return after-before;}
static inline void vo_print(FILE *f,const char *phase,const struct vo_snapshot *s) {
    const uint32_t *w=s->word;
    fprintf(f,"VIDEO_OBS {\"phase\":\"%s\",\"begin_ns\":%llu,\"end_ns\":%llu,"
        "\"hardware\":%s,\"error\":%d,\"vpp_valid\":%s,\"vpp_sofs\":%u",
        phase,(unsigned long long)s->begin_ns,(unsigned long long)s->end_ns,
        s->valid?"true":"false",s->error,s->vpp_valid?"true":"false",s->vpp_sofs);
    if(s->valid) {
        fprintf(f,",\"sequence\":%u,\"flags\":%u,\"locked\":%s,\"ever_locked\":%s,"
            "\"bridge_status\":%u,\"pixel_error\":%s,\"eol_error\":%s,"
            "\"vid_cycles\":%u,\"vid_vsyncs\":%u,\"vtg_vsyncs\":%u,\"active_cycles\":%u,"
            "\"lock_rises\":%u,\"lock_losses\":%u,\"underflow_rises\":%u,\"vtg_stall_cycles\":%u,"
            "\"axi_cycles\":%u,\"mixer_in_sofs\":%u,\"mixer_out_sofs\":%u,"
            "\"mixer_in_eols\":%u,\"mixer_out_eols\":%u,\"mixer_in_stalls\":%u,"
            "\"mixer_out_stalls\":%u,\"overflow_rises\":%u",
            s->seq,w[0],w[0]&1?"true":"false",w[0]&64?"true":"false",w[1],
            w[1]&(1u<<13)?"true":"false",w[1]&(1u<<14)?"true":"false",
            w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11],w[12],w[13],w[14],w[15],w[16],w[17]);
    }
    fputs("}\n",f);fflush(f);
}
static inline void vo_log_phase(const char *phase) {
    struct vo_snapshot s;vo_sample(&s);vo_print(stdout,phase,&s);
}
#endif

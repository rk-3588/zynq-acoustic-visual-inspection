#ifndef VIDEO_READONLY_PROBE_H
#define VIDEO_READONLY_PROBE_H
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "video_mixer.h"

struct video_ro_probe {int fd;const volatile uint32_t *regs;size_t bytes;};
static inline uint64_t video_probe_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static inline int video_probe_find(const char *sysfs,const char *devices,const char *wanted,
                                   uint64_t base,char *result,size_t capacity) {
    DIR *directory=opendir(sysfs);if(!directory)return -1;
    unsigned found=0;struct dirent *entry;char chosen[512]={0};
    while((entry=readdir(directory))) {
        if(strncmp(entry->d_name,"uio",3)||!entry->d_name[3])continue;
        int numeric=1;for(const char *p=entry->d_name+3;*p;p++)if(*p<'0'||*p>'9')numeric=0;
        if(!numeric)continue;
        char path[512],text[128];unsigned long long value;
        int n=snprintf(path,sizeof(path),"%s/%s/name",sysfs,entry->d_name);
        if(n<0||(size_t)n>=sizeof(path)||mix_read_text(path,text,sizeof(text))||strcmp(text,wanted))continue;
        const char *properties[]={"addr","size","offset"};int valid=1;
        for(unsigned i=0;i<3&&valid;i++) {
            n=snprintf(path,sizeof(path),"%s/%s/maps/map0/%s",sysfs,entry->d_name,properties[i]);
            if(n<0||(size_t)n>=sizeof(path)||mix_read_text(path,text,sizeof(text))||!mix_parse_integer(text,&value))valid=0;
            else if((i==0&&value!=base)||(i==1&&value<0x1000u)||(i==2&&value!=0))valid=0;
        }
        if(!valid)continue;
        n=snprintf(chosen,sizeof(chosen),"%s/%s",devices,entry->d_name);
        if(n<0||(size_t)n>=sizeof(chosen))continue;
        found++;
    }
    closedir(directory);
    if(found!=1||strlen(chosen)>=capacity){errno=found>1?EEXIST:ENODEV;return -1;}
    strcpy(result,chosen);return 0;
}
static inline int video_probe_open(struct video_ro_probe *probe,const char *name,uint64_t base) {
    char path[512];*probe=(struct video_ro_probe){.fd=-1};
    if(video_probe_find("/sys/class/uio","/dev",name,base,path,sizeof(path)))return -1;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    void *regs=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0);
    if(regs==MAP_FAILED){close(fd);return -1;}
    *probe=(struct video_ro_probe){.fd=fd,.regs=regs,.bytes=0x1000};return 0;
}
static inline uint32_t video_probe_read(const struct video_ro_probe *probe,unsigned offset) {
    if(!probe->regs||(offset&3u)||offset>=probe->bytes)return 0;
    uint32_t value=probe->regs[offset/4];__sync_synchronize();return value;
}
static inline void video_probe_close(struct video_ro_probe *probe) {
    if(probe->regs)munmap((void *)probe->regs,probe->bytes);
    if(probe->fd>=0)close(probe->fd);
    *probe=(struct video_ro_probe){.fd=-1};
}
struct video_probe_rate {uint64_t ns;uint32_t sequence,millihz;int valid,discontinuity;};
static inline void video_probe_rate_step(struct video_probe_rate *rate,uint32_t sequence,uint64_t ns) {
    if(rate->ns&&ns>rate->ns) {
        uint32_t delta=sequence-rate->sequence;
        uint64_t elapsed=ns-rate->ns;
        /* Reject resets/discontinuities instead of reporting a bogus huge rate.
         * Natural 32-bit wrap still produces a small unsigned difference. */
        rate->discontinuity=delta>10000u||elapsed>10000000000ull;
        rate->valid=!rate->discontinuity;
        rate->millihz=rate->valid?(uint32_t)((uint64_t)delta*1000000000000ull/elapsed):0;
    } else {rate->valid=0;rate->discontinuity=rate->ns!=0;}
    rate->sequence=sequence;rate->ns=ns;
}
#endif

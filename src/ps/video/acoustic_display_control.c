#define _GNU_SOURCE
#include "acoustic_display_logic.h"
#include "pl_enable.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define VERSION PL_ENABLE_DISPLAY_VERSION
#define CONFIG_PATH "/etc/acoustic-display.conf"
#ifndef STATE_PATH
#define STATE_PATH "/run/acoustic-display.state"
#endif
#define SIZE 0x10000
struct mapping {int fd;volatile uint8_t *p;};
static volatile sig_atomic_t live=1,reload_config=0;
static uint64_t display_now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static void signal_handler(int sig) {if(sig==SIGHUP)reload_config=1;else live=0;}
static uint32_t read_reg(struct mapping *m,unsigned o) {
    uint32_t v=*(volatile uint32_t *)(m->p+o);__sync_synchronize();return v;
}
static void write_reg(struct mapping *m,unsigned o,uint32_t v) {
    __sync_synchronize();*(volatile uint32_t *)(m->p+o)=v;__sync_synchronize();
}
static unsigned apply_enable_alpha(struct mapping *add,int enabled,unsigned target,int *ok) {
    unsigned effective=enabled?target:0;
    if(read_reg(add,0x10)!=effective)write_reg(add,0x10,effective);
    *ok=read_reg(add,0x10)==effective;return effective;
}
static uint32_t float_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
static int text_file(const char *path,char *s,size_t n) {
    FILE *f=fopen(path,"r");if(!f)return -1;
    int rc=fgets(s,(int)n,f) ? 0 : -1;fclose(f);
    if(!rc)s[strcspn(s,"\r\n")]=0;
    return rc;
}
static int map_uio(struct mapping *m,const char *expected,uint64_t address,int writable) {
    DIR *d=opendir("/sys/class/uio");if(!d)return -1;
    struct dirent *e;int rc=-1;
    while((e=readdir(d))) {
        if(strncmp(e->d_name,"uio",3))continue;
        char path[256],name[128],addr[64],size[64];
        snprintf(path,sizeof(path),"/sys/class/uio/%.32s/name",e->d_name);
        if(text_file(path,name,sizeof(name)) || strcmp(name,expected))continue;
        snprintf(path,sizeof(path),"/sys/class/uio/%.32s/maps/map0/addr",e->d_name);
        if(text_file(path,addr,sizeof(addr)) || strtoull(addr,NULL,0)!=address)continue;
        snprintf(path,sizeof(path),"/sys/class/uio/%.32s/maps/map0/size",e->d_name);
        if(text_file(path,size,sizeof(size)) || strtoull(size,NULL,0)<SIZE)continue;
        snprintf(path,sizeof(path),"/dev/%.32s",e->d_name);
        m->fd=open(path,(writable?O_RDWR:O_RDONLY)|O_SYNC|O_CLOEXEC);
        if(m->fd<0)break;
        m->p=mmap(NULL,SIZE,PROT_READ|(writable?PROT_WRITE:0),MAP_SHARED,m->fd,0);
        if(m->p==MAP_FAILED) {close(m->fd);m->fd=-1;break;}
        fprintf(stderr,"UIO %s %s address=0x%llx\n",path,name,(unsigned long long)address);
        rc=0;break;
    }
    closedir(d);return rc;
}
static int map_extrema(struct mapping *m) {
    // Verified BD address; this legacy IP has no UIO node. Read-only mapping.
    m->fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
    if(m->fd<0)return -1;
    m->p=mmap(NULL,SIZE,PROT_READ,MAP_SHARED,m->fd,0x44a10000);
    if(m->p==MAP_FAILED) {close(m->fd);m->fd=-1;return -1;}
    return 0;
}
static void close_map(struct mapping *m) {
    if(m->p && m->p!=MAP_FAILED)munmap((void *)m->p,SIZE);
    if(m->fd>=0)close(m->fd);
}
static int stable_stats(struct mapping *m,uint32_t *lo,uint32_t *hi) {
    for(int i=0;i<8;i++) {
        uint32_t a=read_reg(m,0),b=read_reg(m,4);
        uint32_t c=read_reg(m,0),d=read_reg(m,4);
        if(a==c && b==d && a<=0xffffff && b<a) {*lo=b;*hi=a;return 1;}
    }
    return 0;
}
static int config_valid(const struct display_config *c,int require_noise) {
    return isfinite(c->noise_peak) && c->noise_peak>=0 && c->noise_peak<0xffffff &&
        (!require_noise || c->noise_peak>0) && isfinite(c->on_ratio) && isfinite(c->off_ratio) &&
        c->on_ratio>c->off_ratio && c->off_ratio>=1 && c->on_ratio<=100 &&
        c->on_count>=1 && c->on_count<=200 && c->off_count>=1 && c->off_count<=200 &&
        c->period_ms>=20 && c->period_ms<=1000 && c->alpha<=256 &&
        (c->adaptive==0 || c->adaptive==1) && isfinite(c->contrast_min) &&
        (c->low_contrast_stabilize==0 || c->low_contrast_stabilize==1) &&
        c->contrast_min>=0 && c->contrast_min<1 &&
        isfinite(c->hotspot_floor_ratio) && c->hotspot_floor_ratio>=0 && c->hotspot_floor_ratio<=.95 &&
        isfinite(c->red_core_ratio) && c->red_core_ratio>=.65 && c->red_core_ratio<=.99 &&
        isfinite(c->attack_seconds) && c->attack_seconds>=.02 && c->attack_seconds<=20 &&
        isfinite(c->release_seconds) && c->release_seconds>=.02 && c->release_seconds<=20 &&
        isfinite(c->slew_bits_per_second) && c->slew_bits_per_second>0 && c->slew_bits_per_second<=100;
}
static int load_config(const char *path,struct display_config *c,int require_noise) {
    FILE *f=fopen(path,"r");if(!f)return -1;
    *c=display_defaults();char line[256];int rc=0;
    while(fgets(line,sizeof(line),f)) {
        char key[64],value[96],tail;
        if(line[0]=='#' || line[0]=='\n')continue;
        if(sscanf(line," %63[^=]=%95s %c",key,value,&tail)!=2){rc=-1;break;}
        char *end;errno=0;double v=strtod(value,&end);
        if(errno || *end || !isfinite(v)){rc=-1;break;}
        #define FIELD(name) if(!strcmp(key,#name))c->name=v;
        #define UINT_FIELD(name) if(!strcmp(key,#name)) {if(v<0 || v>100000 || floor(v)!=v){rc=-1;break;}c->name=(unsigned)v;}
        FIELD(noise_peak) else FIELD(on_ratio) else FIELD(off_ratio) else FIELD(contrast_min)
        else FIELD(hotspot_floor_ratio)
        else FIELD(red_core_ratio)
        else UINT_FIELD(low_contrast_stabilize)
        else FIELD(attack_seconds) else FIELD(release_seconds) else FIELD(slew_bits_per_second)
        else UINT_FIELD(on_count) else UINT_FIELD(off_count) else UINT_FIELD(period_ms)
        else UINT_FIELD(alpha) else UINT_FIELD(adaptive)
        else {rc=-1;break;}
        #undef FIELD
        #undef UINT_FIELD
    }
    if(ferror(f))rc=-1;
    fclose(f);return !rc && config_valid(c,require_noise) ? 0 : -1;
}
static int save_config(const char *path,const struct display_config *c) {
    char temp[512];if(snprintf(temp,sizeof(temp),"%s.new.%ld",path,(long)getpid())>=(int)sizeof(temp))return -1;
    FILE *f=fopen(temp,"wx");if(!f)return -1;
    int n=fprintf(f,"# Quiet baseline; change adaptive=0 for gate-only validation.\n"
        "noise_peak=%.0f\non_ratio=%.6g\noff_ratio=%.6g\non_count=%u\noff_count=%u\n"
        "period_ms=%u\nalpha=%u\nadaptive=%d\ncontrast_min=%.6g\nhotspot_floor_ratio=%.6g\nred_core_ratio=%.6g\n"
        "attack_seconds=%.6g\nrelease_seconds=%.6g\nslew_bits_per_second=%.6g\nlow_contrast_stabilize=%d\n",
        c->noise_peak,c->on_ratio,c->off_ratio,c->on_count,c->off_count,c->period_ms,
        c->alpha,c->adaptive,c->contrast_min,c->hotspot_floor_ratio,
        c->red_core_ratio,c->attack_seconds,c->release_seconds,c->slew_bits_per_second,c->low_contrast_stabilize);
    int rc=n<0 || fflush(f) || fsync(fileno(f));if(fclose(f))rc=1;
    if(rc){unlink(temp);return -1;}
    if(rename(temp,path)){unlink(temp);return -1;}
    return 0;
}
static int compare_double(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
static void delay_ms(unsigned ms) {
    struct timespec t={ms/1000,(long)(ms%1000)*1000000};
    while(live && nanosleep(&t,&t)<0 && errno==EINTR){}
}
static int calibrate(struct mapping *ctrl,const char *path,struct display_config *c) {
    double samples[100];unsigned n=0;
    fprintf(stderr,"CALIBRATE: keep test source OFF for 5 seconds; read-only capture\n");
    for(unsigned i=0;i<100 && live;i++) {
        uint32_t p=read_reg(ctrl,0x10),status=read_reg(ctrl,4);
        if((status&1) && p>0 && p<=0xffffff)samples[n++]=p;
        delay_ms(50);
    }
    if(!live || n<80){fprintf(stderr,"Calibration failed: %u valid samples\n",n);return -1;}
    qsort(samples,n,sizeof(double),compare_double);
    c->noise_peak=samples[(unsigned)ceil(.95*n)-1];
    fprintf(stderr,"CALIBRATED count=%u min=%.0f median=%.0f p95=%.0f max=%.0f enter=%.0f leave=%.0f\n",
        n,samples[0],samples[n/2],c->noise_peak,samples[n-1],c->noise_peak*c->on_ratio,c->noise_peak*c->off_ratio);
    return save_config(path,c);
}
static void state_file(const struct display_config *c,const struct display_logic *s,
    uint32_t peak,uint32_t lo,uint32_t hi,int stable,unsigned invalid,int stopped,
    int enabled,uint64_t observed_ms,unsigned effective_alpha,int alpha_ok) {
    FILE *f=fopen(STATE_PATH ".new","w");if(!f)return;
    fprintf(f,"{\"version\":\"%s\",\"stage\":\"%s\",\"gate\":\"%s\",\"peak\":%u,"
        "\"noise_peak\":%.0f,\"enter\":%.0f,\"leave\":%.0f,\"hotspot_floor_ratio\":%.6g,\"red_core_ratio\":%.6g,\"filtered_peak\":%.0f,"
        "\"alpha\":%u,\"display_min\":%u,\"display_max\":%u,\"stats_stable_read\":%s,"
        "\"invalid_samples\":%u,\"palette_mode\":\"%s\",\"colors\":[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g],"
        "\"acoustic_enabled\":%s,\"enable_observed_ms\":%llu,\"effective_alpha\":%u,"
        "\"alpha_readback_ok\":%s,\"target_alpha\":%u}\n",
        VERSION,c->adaptive?"adaptive":"gate-only",stopped?"stopped":(s->gate?"active":"quiet"),
        peak,c->noise_peak,c->noise_peak*c->on_ratio,c->noise_peak*c->off_ratio,c->hotspot_floor_ratio,
        c->red_core_ratio,s->filter_ready?exp2(s->filtered_log):0,effective_alpha,lo,hi,stable?"true":"false",invalid,
        s->palette_low_contrast?"low-contrast-stable":"legacy-adaptive",
        s->colors[0],s->colors[1],s->colors[2],s->colors[3],s->colors[4],s->colors[5],
        enabled?"true":"false",(unsigned long long)observed_ms,effective_alpha,
        alpha_ok?"true":"false",s->alpha);
    if(!fclose(f))rename(STATE_PATH ".new",STATE_PATH);
}
int main(int argc,char **argv) {
    const char *path=CONFIG_PATH;int calibration=0,gate_only=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--version")){puts(VERSION);return 0;}
        if(!strcmp(argv[i],"--calibrate"))calibration=1;
        else if(!strcmp(argv[i],"--gate-only"))gate_only=1;
        else if(!strcmp(argv[i],"--config") && i+1<argc)path=argv[++i];
        else {fprintf(stderr,"Usage: %s [--calibrate] [--gate-only] [--config FILE]\n",argv[0]);return 2;}
    }
    setvbuf(stderr,NULL,_IOLBF,0);
    signal(SIGINT,signal_handler);signal(SIGTERM,signal_handler);signal(SIGHUP,signal_handler);
    int lock=open("/run/acoustic-display.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)){perror("display controller lock");return 2;}
    struct display_config c;
    if(load_config(path,&c,!calibration)){fprintf(stderr,"Invalid/uncalibrated config: %s\n",path);close(lock);return 2;}
    struct mapping ctrl={.fd=-1},color={.fd=-1},add={.fd=-1},ext={.fd=-1};int rc=1;
    if(map_uio(&ctrl,"acoustic_ctrl@44a20000",0x44a20000,0)) {perror("acoustic UIO");goto done;}
    if(calibration){rc=calibrate(&ctrl,path,&c)?1:0;goto done;}
    if(gate_only)c.adaptive=0;
    if(map_uio(&color,"color_mapping@40010000",0x40010000,1) ||
       map_uio(&add,"add_image@40000000",0x40000000,1) || map_extrema(&ext)) {
        perror("display mapping");goto done;
    }
    uint32_t saved[6],old_alpha=read_reg(&add,0x10);
    for(unsigned i=0;i<6;i++)saved[i]=read_reg(&color,0x10+i*8);
    write_reg(&add,0x10,0);
    struct display_logic s={0};unsigned invalid=0,ticks=0;
    int prior_enabled=-1;
    fprintf(stderr,"START %s stage=%s baseline=%.0f enter=%.0f leave=%.0f hotspot_floor_ratio=%.3f red_core_ratio=%.3f\n",VERSION,
        c.adaptive?"adaptive":"gate-only",c.noise_peak,c.noise_peak*c.on_ratio,c.noise_peak*c.off_ratio,
        c.hotspot_floor_ratio,c.red_core_ratio);
    while(live) {
        uint64_t observed_ms=display_now_ms(); /* Taken BEFORE reading the request. */
        int enabled=(read_reg(&ctrl,0)&1u)!=0;
        int enable_changed=enabled!=prior_enabled;
        if(enable_changed) {
            s.alpha=0;
            fprintf(stderr,"ACOUSTIC_ENABLE requested=%d\n",enabled);prior_enabled=enabled;
        }
        if(reload_config) {
            reload_config=0;struct display_config next;
            if(!load_config(path,&next,1)) {
                if(gate_only)next.adaptive=0;
                c=next;memset(&s,0,sizeof(s));write_reg(&add,0x10,0);
                fprintf(stderr,"CONFIG reloaded stage=%s hotspot_floor_ratio=%.3f red_core_ratio=%.3f\n",
                    c.adaptive?"adaptive":"gate-only",c.hotspot_floor_ratio,c.red_core_ratio);
            } else fprintf(stderr,"CONFIG rejected; retaining previous settings\n");
        }
        uint32_t peak=read_reg(&ctrl,0x10),status=read_reg(&ctrl,4),lo=0,hi=0;
        int valid=(status&1) && peak<=0xffffff;
        int stable=stable_stats(&ext,&lo,&hi);
        if(!valid)invalid++;
        int previous=s.gate;
        display_step(&c,&s,peak,valid,lo,hi,stable,c.period_ms/1000.0);
        if(s.gate!=previous)fprintf(stderr,"GATE %s peak=%u filtered=%.0f min=%u max=%u\n",
            s.gate?"active":"quiet",peak,exp2(s.filtered_log),lo,hi);
        if(!c.adaptive || s.palette_ready) {
            for(unsigned i=0;i<6;i++) {
                uint32_t b=float_bits(s.colors[i]);
                if(read_reg(&color,0x10+i*8)!=b)write_reg(&color,0x10+i*8,b);
            }
        }
        int alpha_ok=0;
        unsigned effective_alpha=apply_enable_alpha(&add,enabled,s.alpha,&alpha_ok);
        if(ticks++%4==0 || enable_changed || !alpha_ok)
            state_file(&c,&s,peak,lo,hi,stable,invalid,0,enabled,observed_ms,effective_alpha,alpha_ok);
        delay_ms(c.period_ms);
    }
    // Restore the exact pre-controller values on normal stop; no DMA resets.
    write_reg(&add,0x10,0);
    for(unsigned i=0;i<6;i++)write_reg(&color,0x10+i*8,saved[i]);
    int enabled=(read_reg(&ctrl,0)&1u)!=0;
    unsigned restored_alpha=enabled?old_alpha:0;
    write_reg(&add,0x10,restored_alpha);
    state_file(&c,&s,0,0,0,0,invalid,1,enabled,display_now_ms(),restored_alpha,
        read_reg(&add,0x10)==restored_alpha);
    fprintf(stderr,"STOP restored original color/alpha registers\n");rc=0;
done:
    close_map(&ext);close_map(&add);close_map(&color);close_map(&ctrl);close(lock);return rc;
}

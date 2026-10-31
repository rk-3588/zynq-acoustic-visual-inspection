#define _GNU_SOURCE
#include "audio_fft.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#define VERSION "20261013-acoustic-frequency-v2"
#define MAP_BYTES 0x10000u
#define CAPTURE_BASE 0x40030000ull
#define CAPTURE_NAME "acoustic_capture@40030000"
#ifndef CAPTURE_UIO_CLASS
#define CAPTURE_UIO_CLASS "/sys/class/uio"
#endif
static volatile sig_atomic_t stopping;
static void stop_handler(int sig) {(void)sig;stopping=1;}
static uint64_t now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static uint32_t rd(volatile uint32_t *p,unsigned off) {uint32_t v=p[off/4];__sync_synchronize();return v;}
static void wr(volatile uint32_t *p,unsigned off,uint32_t v) {__sync_synchronize();p[off/4]=v;__sync_synchronize();}
static int text_file(const char *p,char *out,size_t n) {
    FILE *f=fopen(p,"r");if(!f)return -1;
    int rc=fgets(out,(int)n,f)?0:-1;fclose(f);if(!rc)out[strcspn(out,"\r\n")]=0;return rc;
}
static int number(const char *s,unsigned long long *v) {char *end;if(*s<'0' || *s>'9')return 0;errno=0;*v=strtoull(s,&end,0);return !errno && end!=s && !*end;}
static int find_device(char *out,size_t n) {
    DIR *dir=opendir(CAPTURE_UIO_CLASS);if(!dir)return -1;
    struct dirent *e;unsigned count=0;
    while((e=readdir(dir))) {
        if(strncmp(e->d_name,"uio",3) || !e->d_name[3])continue;
        const char *digit=e->d_name+3;while(*digit>='0' && *digit<='9')digit++;if(*digit)continue;
        char path[512],value[128];unsigned long long addr,size,offset;
        snprintf(path,sizeof(path),CAPTURE_UIO_CLASS "/%s/name",e->d_name);
        if(text_file(path,value,sizeof(value)) || strcmp(value,CAPTURE_NAME))continue;
        snprintf(path,sizeof(path),CAPTURE_UIO_CLASS "/%s/maps/map0/addr",e->d_name);
        if(text_file(path,value,sizeof(value)) || !number(value,&addr) || addr!=CAPTURE_BASE)continue;
        snprintf(path,sizeof(path),CAPTURE_UIO_CLASS "/%s/maps/map0/size",e->d_name);
        if(text_file(path,value,sizeof(value)) || !number(value,&size) || size<MAP_BYTES)continue;
        snprintf(path,sizeof(path),CAPTURE_UIO_CLASS "/%s/maps/map0/offset",e->d_name);
        if(text_file(path,value,sizeof(value)) || !number(value,&offset) || offset)continue;
        int written=snprintf(out,n,"/dev/%s",e->d_name);
        if(written<0 || (size_t)written>=n)continue;
        count++;
    }
    closedir(dir);if(count!=1){errno=count?EEXIST:ENODEV;return -1;}return 0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")){puts(VERSION);return 0;}
    if(argc==2 && !strcmp(argv[1],"--help")) {
        printf("%s --check | --inspect | --capture [--channel 0..15] [--blocks 1..100] | --release-ready | --version\n"
               "--check reads sysfs only; --inspect reads metadata without arming.\n"
               "Capture is bounded; 2048 points, declared Fs=46875, search=300..10000 Hz.\n"
               "The result is a spectral peak, not a hotspot-associated fundamental.\n",argv[0]);return 0;
    }
    int capture=0,check=0,inspect=0,release_only=0,parameters=0;unsigned channel=0,blocks=1;
    for(int i=1;i<argc;i++) {
        unsigned long long value;
        if(!strcmp(argv[i],"--capture"))capture=1;
        else if(!strcmp(argv[i],"--check"))check=1;
        else if(!strcmp(argv[i],"--inspect"))inspect=1;
        else if(!strcmp(argv[i],"--release-ready"))release_only=1;
        else if(!strcmp(argv[i],"--channel") && i+1<argc && number(argv[i+1],&value) && value<16){channel=(unsigned)value;parameters=1;i++;}
        else if(!strcmp(argv[i],"--blocks") && i+1<argc && number(argv[i+1],&value) && value>=1 && value<=100){blocks=(unsigned)value;parameters=1;i++;}
        else {fprintf(stderr,"Invalid option\n");return 2;}
    }
    if(capture+check+inspect+release_only!=1 || (parameters && !capture)) {
        fprintf(stderr,"Usage: %s --check | --inspect | --capture [--channel 0..15] [--blocks 1..100] | --release-ready | --version\n",argv[0]);return 2;
    }
    char device[128];if(find_device(device,sizeof(device))){fprintf(stderr,"Expected UIO %s base=0x%08llx size>=0x%x offset=0: %s\n",CAPTURE_NAME,CAPTURE_BASE,MAP_BYTES,strerror(errno));return 3;}
    if(check){printf("%s %s name=%s base=0x%08llx identity-sysfs-only=pass\n",VERSION,device,CAPTURE_NAME,CAPTURE_BASE);return 0;}
    int lock=open("/run/acoustic-frequency.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)){perror("capture ownership lock");if(lock>=0)close(lock);return 3;}
    int fd=open(device,O_RDWR|O_CLOEXEC);if(fd<0){perror(device);close(lock);return 3;}
    volatile uint32_t *regs=mmap(NULL,MAP_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    int rc=4,owned=0;
    if(regs==MAP_FAILED){perror("mmap");goto finish;}
    if(rd(regs,0)!=0x41434631 || rd(regs,0x24)!=1 || rd(regs,0x14)!=AUDIO_N ||
       rd(regs,0x20)!=0x00180020 || rd(regs,0x10)!=46875) {
        fprintf(stderr,"Capture hardware ID/protocol/format/sample-rate mismatch\n");goto finish;
    }
    if(inspect) {
        printf("{\"version\":\"%s\",\"device\":\"%s\",\"base\":\"0x40030000\",\"hardware_id\":\"0x41434631\","
               "\"protocol\":1,\"sample_rate_hz\":46875,\"samples\":2048,\"status\":%u,\"sequence\":%u,"
               "\"configured_channel\":%u,\"captured_channel\":%u,\"available_samples\":%u,\"arm_command_written\":false}\n",
               VERSION,device,rd(regs,8),rd(regs,0x18),rd(regs,0x0c),rd(regs,0x1c),rd(regs,0x28));
        rc=0;goto finish;
    }
    if(release_only) {
        if(rd(regs,8)!=2){fprintf(stderr,"Release allowed only for a complete frozen block\n");goto finish;}
        wr(regs,4,2);if(rd(regs,8)!=0)goto finish;rc=0;goto finish;
    }
    if(rd(regs,8)!=0){fprintf(stderr,"Previous capture busy/frozen; refusing overwrite (ready block: --release-ready)\n");goto finish;}
    struct sigaction action={0};action.sa_handler=stop_handler;sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);sigaction(SIGHUP,&action,NULL);
    wr(regs,0x0c,channel);if(rd(regs,0x0c)!=channel)goto finish;
    uint64_t next=now_ms();
    for(unsigned block=0;block<blocks && !stopping;block++) {
        uint64_t begin=now_ms();
        uint32_t seq_before=rd(regs,0x18);wr(regs,4,1);owned=1;
        uint64_t deadline=now_ms()+2000;
        while(rd(regs,8)!=2 && now_ms()<deadline)usleep(1000);
        if(rd(regs,8)!=2){fprintf(stderr,"Capture timeout: no release while hardware busy; check clocks/reset/sample_valid\n");goto finish;}
        uint64_t capture_wait_ms=now_ms()-begin;
        uint32_t seq=rd(regs,0x18);int32_t samples[AUDIO_N];
        if(seq!=seq_before+1 || rd(regs,0x28)!=AUDIO_N || rd(regs,0x1c)!=channel){fprintf(stderr,"Completed block sequence/count/channel mismatch\n");goto finish;}
        for(unsigned i=0;i<AUDIO_N;i++) {
            uint32_t raw=rd(regs,0x1000+4*i);
            if((raw>>24)!=((raw&0x800000)?255u:0u)){fprintf(stderr,"Invalid signed24 extension at %u\n",i);goto finish;}
            samples[i]=(int32_t)raw;
        }
        if(rd(regs,8)!=2 || rd(regs,0x18)!=seq){fprintf(stderr,"Frozen block changed during read\n");goto finish;}
        wr(regs,4,2);owned=0;if(rd(regs,8)!=0)goto finish;
        if(stopping)break;
        struct audio_result r=audio_analyse(samples,46875,300,10000);
        if(r.clipped_samples)r.valid=0;
        printf("{\"version\":\"%s\",\"sequence\":%u,\"channel\":%u,\"sample_rate_hz\":46875,\"samples\":2048,"
            "\"raw_signal_peak\":%u,\"ac_rms_counts\":%.3f,\"spectral_prominence_db\":%.2f,"
            "\"spectral_candidate_valid\":%s,\"peak_frequency_hz\":",VERSION,seq,channel,r.peak,r.rms,r.prominence_db,r.valid?"true":"false");
        if(r.valid)printf("%.2f",r.frequency);else printf("null");
        printf(",\"bin_hz\":%.6f,\"capture_wait_ms\":%llu,\"monotonic_ms\":%llu,"
               "\"sample_min\":%d,\"sample_max\":%d,\"mean_counts\":%.3f,\"clipped_samples\":%u,"
               "\"candidate_reason\":\"%s\",\"frequency_source\":\"single_mic_hpf_output\","
               "\"hotspot_associated\":false,\"quiet_gate_fused\":false}\n",
               46875.0/AUDIO_N,(unsigned long long)capture_wait_ms,(unsigned long long)now_ms(),
               r.minimum,r.maximum,r.mean,r.clipped_samples,
               r.clipped_samples?"clipped":r.rms==0?"constant_signal":r.valid?"spectral_candidate":"low_spectral_prominence");fflush(stdout);
        next+=200;while(block+1<blocks && !stopping && now_ms()<next)usleep(1000);
        if(next<now_ms())next=now_ms();
    }
    rc=0;
finish:
    if(regs!=MAP_FAILED) {
        if(owned && rd(regs,8)==2)wr(regs,4,2);
        munmap((void *)regs,MAP_BYTES);
    }
    close(fd);close(lock);return rc;
}

#define _GNU_SOURCE
#include <signal.h>
#include <sys/stat.h>
#include "video_readonly_probe.h"
#include "camera_snapshot_core.h"
#define DISPLAY_DIAG_VERSION "20261026-video-display-diag-v1"
static volatile sig_atomic_t stopped;
static void stop_diag(int signal_number){(void)signal_number;stopped=1;}
static void dma(const char *name,const struct video_ro_probe *p) {
    printf(",\"%s\":",name);
    if(!p->regs){fputs("null",stdout);return;}
    uint32_t read_status=video_probe_read(p,0x04),write_status=video_probe_read(p,0x34);
    printf("{\"mm2s_control\":%u,\"mm2s_status\":%u,\"mm2s_errors\":%u,"
        "\"s2mm_control\":%u,\"s2mm_status\":%u,\"s2mm_errors\":%u,\"park\":%u,\"hsize\":%u,\"vsize\":%u}",
        video_probe_read(p,0),read_status,video_vdma_errors(read_status,0),video_probe_read(p,0x30),
        write_status,video_vdma_errors(write_status,1),video_probe_read(p,0x28),video_probe_read(p,0xa4),video_probe_read(p,0xa0));
}
static void sample(const struct video_ro_probe *p,struct video_probe_rate *rate) {
    uint64_t ns=video_probe_ns();
    printf("{\"version\":\"%s\",\"monotonic_ns\":%llu",DISPLAY_DIAG_VERSION,(unsigned long long)ns);
    dma("camera",&p[0]);dma("acoustic",&p[1]);
    fputs(",\"mixer\":",stdout);
    if(p[2].regs) {
        /* Do NOT read ap_ctrl or ISR: those reads/toggles belong to the UI's
         * completion protocol. These configuration registers have no read clear. */
        printf("{\"layers\":%u,\"width\":%u,\"height\":%u,\"ui_alpha\":%u,\"ui_stride\":%u,\"ui_buffer\":%u}",
            video_probe_read(&p[2],0x40),video_probe_read(&p[2],0x10),video_probe_read(&p[2],0x18),
            video_probe_read(&p[2],0x200),video_probe_read(&p[2],0x220),video_probe_read(&p[2],0x240));
    } else fputs("null",stdout);
    fputs(",\"vtc_control\":",stdout);
    if(p[3].regs)printf("%u",video_probe_read(&p[3],0));else fputs("null",stdout);
    fputs(",\"vpp\":",stdout);
    if(p[4].regs&&video_probe_read(&p[4],0)==0x56505031u) {
        uint32_t sequence=video_probe_read(&p[4],0x1c);
        video_probe_rate_step(rate,sequence,video_probe_ns());
        printf("{\"accepted_sof\":%u,\"rate_millihz\":",sequence);
        if(rate->valid)printf("%u",rate->millihz);else fputs("null",stdout);
        printf(",\"counter_discontinuity\":%s}",rate->discontinuity?"true":"false");
    } else fputs("null",stdout);
    /* No register exposes Video Out lock/underflow in this deployed design. */
    puts(",\"video_out_locked\":null,\"video_out_underflow\":null}");fflush(stdout);
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts(DISPLAY_DIAG_VERSION);return 0;}
    unsigned long long samples=10,interval=1000;
    for(int i=1;i<argc;i+=2) {
        if(i+1>=argc)return 2;
        if(!strcmp(argv[i],"--samples")){if(!mix_parse_integer(argv[i+1],&samples)||samples<1||samples>120)return 2;}
        else if(!strcmp(argv[i],"--interval-ms")){if(!mix_parse_integer(argv[i+1],&interval)||interval<100||interval>5000)return 2;}
        else return 2;
    }
    struct video_ro_probe p[5];
    const char *names[]={"dma@44a00000","dma@43000000",VIDEO_MIXER_UIO_NAME,"v_tc@43c10000","video_preprocess@40040000"};
    const uint64_t bases[]={0x44a00000u,0x43000000u,VIDEO_MIXER_BASE,0x43c10000u,0x40040000u};
    unsigned available=0;
    for(unsigned i=0;i<5;i++)if(!video_probe_open(&p[i],names[i],bases[i]))available++;
    if(!available){fprintf(stderr,"No uniquely identified video UIO devices; no registers accessed\n");return 3;}
    signal(SIGTERM,stop_diag);signal(SIGINT,stop_diag);
    struct video_probe_rate rate={0};
    for(unsigned i=0;i<samples&&!stopped;i++) {
        sample(p,&rate);
        if(i+1<samples)usleep((useconds_t)(interval*1000));
    }
    for(unsigned i=0;i<5;i++)video_probe_close(&p[i]);
    return 0;
}

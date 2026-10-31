#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ov5640_720p_regs.h"
#include "video_mixer.h"
#include "video_vdma_status.h"

#define WIDTH 1280u
#define HEIGHT 720u
#define BPP 3u
#define STRIDE (WIDTH * BPP)
/* Match the known-good bare-metal buffer addresses: DDR base 0x00100000
 * plus the application's +0x01000000/+0x02000000 offsets. */
#define FB0 0x01100000u
#define FB1 0x02100000u
#define FRAMES 3u

#define VDMA_PARKPTR_OFFSET 0x28u
#define VDMA_STATUS_ERROR_MASK VIDEO_VDMA_S2MM_ERRORS
#define VDMA_STATUS_W1C_MASK (VIDEO_VDMA_IRQ_STATUS | VIDEO_VDMA_S2MM_ERRORS)

struct uio_map { int fd; volatile uint32_t *r; size_t len; char name[96]; };
static void wr(volatile uint32_t *r, unsigned off, uint32_t v) { r[off/4] = v; __sync_synchronize(); }
static uint32_t rd(volatile uint32_t *r, unsigned off) { uint32_t v=r[off/4]; __sync_synchronize(); return v; }

static int open_uio(const char *wanted, struct uio_map *m) {
    char p[128], n[128], name[128]; FILE *f; int i;
    for (i=0;i<32;i++) {
        snprintf(p,sizeof p,"/sys/class/uio/uio%d/name",i); f=fopen(p,"r"); if(!f) continue;
        if(!fgets(name,sizeof name,f)){fclose(f);continue;} fclose(f); name[strcspn(name,"\n")]=0;
        if(strcmp(name,wanted)) continue;
        snprintf(p,sizeof p,"/dev/uio%d",i); m->fd=open(p,O_RDWR|O_SYNC); if(m->fd<0) return -1;
        snprintf(n,sizeof n,"/sys/class/uio/uio%d/maps/map0/size",i); f=fopen(n,"r");
        unsigned long len=0; if(f){if(fscanf(f,"%lx",&len)!=1)len=0;fclose(f);} if(!len) len=0x10000;
        m->len=len; m->r=mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,m->fd,0); if(m->r==MAP_FAILED){close(m->fd);return -1;}
        snprintf(m->name,sizeof(m->name),"%.*s",(int)sizeof(m->name)-1,name); return 0;
    }
    errno=ENOENT; return -1;
}
static void close_uio(struct uio_map *m){if(m->r && m->r!=MAP_FAILED)munmap((void*)m->r,m->len);if(m->fd>=0)close(m->fd);}
static uint32_t fbits(float x){union{float f;uint32_t u;}v={x};return v.u;}

static uint32_t mixer_read(void *context, unsigned off){return rd(context,off);}
static void mixer_write(void *context, unsigned off, uint32_t value){wr(context,off,value);}
static void mixer_pause(void *context, unsigned usec){(void)context;usleep(usec);}

static int mixer_start_primary(void){
    char path[512];
    if(video_mixer_find_uio("/sys/class/uio","/dev",path,sizeof(path))<0){
        perror("Mixer UIO identity (name/address/size)");
        return -1;
    }
    struct uio_map m={.fd=-1};
    m.fd=open(path,O_RDWR|O_SYNC);
    if(m.fd<0){perror("Mixer UIO open");return -1;}
    m.len=VIDEO_MIXER_MAP_SIZE;
    m.r=mmap(NULL,m.len,PROT_READ|PROT_WRITE,MAP_SHARED,m.fd,0);
    if(m.r==MAP_FAILED){perror("Mixer UIO mmap");close_uio(&m);return -1;}
    const struct video_mixer_io io={.context=(void *)m.r,.read=mixer_read,
        .write=mixer_write,.pause_us=mixer_pause};
    int rc=video_mixer_configure_primary(&io,WIDTH,HEIGHT);
    printf("mixer version=%s uio=%s base=44a30000 ctrl=%08x size=%ux%u "
           "format=%u layers=%08x primary-only ui=off rc=%d\n",
           VIDEO_MIXER_VERSION,path,rd(m.r,MIX_CTRL),rd(m.r,MIX_WIDTH),
           rd(m.r,MIX_HEIGHT),rd(m.r,MIX_FORMAT),rd(m.r,MIX_LAYERS),rc);
    if(rc<0) fprintf(stderr,"Mixer initialization failed; not starting upstream readers. "
                           "rc=%d (-1=idle timeout, -2=readback, -3=control, -5=flush needs reset)\n",rc);
    close_uio(&m);
    return rc;
}

/* OV5640 SCCB on PS GPIO EMIO 54/55. This is intentionally small: reset, ID and
 * the 1280x720 timing registers are done here; the remaining vendor table can
 * be enabled later without changing the pipeline state machine. */
static volatile uint32_t *gpio;
static int gpiofd;
static int sccb_trace;
static int ps_gpio_clock_init(void){
    int fd=open("/dev/mem",O_RDWR|O_SYNC);
    if(fd<0) return -1;
    volatile uint32_t *slcr=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0xF8000000);
    if(slcr==MAP_FAILED){close(fd);return -1;}
    uint32_t v=slcr[0x12c/4];
    if(!(v & (1u<<22))){
        slcr[0x12c/4]=v | (1u<<22);
        __sync_synchronize();
    }
    uint32_t verify=slcr[0x12c/4];
    printf("ps gpio clock=%s aper_clk_ctrl=%08x\n",
           (verify & (1u<<22)) ? "on" : "off", verify);
    munmap((void*)slcr,0x1000); close(fd);
    return (verify & (1u<<22)) ? 0 : -1;
}
static void gpio_bank_bit(unsigned pin, unsigned *bank, unsigned *bit){
    /* Zynq PS GPIO numbering is MIO 0..53 followed by EMIO 54..117.
     * EMIO 54/55 are Bank2 bits 0/1, not Bank1 bits 22/23. */
    if(pin >= 54){ *bank=2; *bit=pin-54; }
    else { *bank=pin/32; *bit=pin%32; }
}
static void sdbg(const char *tag){
    if(!sccb_trace) return;
    uint32_t d=gpio[0x48/4], in=gpio[0x68/4], dir=gpio[0x284/4], oen=gpio[0x288/4];
    printf("sccb %-12s scl=%u sda=%u in2=%08x dir2=%08x oen2=%08x\n",
           tag, (d>>0)&1u, (d>>1)&1u, in, dir, oen);
}
static void gpw(unsigned pin,int v){unsigned bank,bit;gpio_bank_bit(pin,&bank,&bit); unsigned d=0x40+bank*4; uint32_t x=gpio[d/4]; x=v?(x|(1u<<bit)):(x&~(1u<<bit)); gpio[d/4]=x; __sync_synchronize();}
static int gpr(unsigned pin){unsigned bank,bit;gpio_bank_bit(pin,&bank,&bit);return (gpio[(0x60+bank*4)/4]>>bit)&1;}
static void gdir(unsigned pin,int out){unsigned bank,bit;gpio_bank_bit(pin,&bank,&bit); unsigned o=0x204+bank*0x40,e=0x208+bank*0x40;uint32_t x=gpio[o/4];gpio[o/4]=out?(x|(1u<<bit)):(x&~(1u<<bit));x=gpio[e/4];gpio[e/4]=out?(x|(1u<<bit)):(x&~(1u<<bit));}
static void tick(void){usleep(4);} static void sstart(void){sdbg("start-bef");gpw(54,1);gpw(55,1);tick();gpw(55,0);tick();gpw(54,0);sdbg("start-aft");}
static void sstop(void){gpw(54,0);gpw(55,0);tick();gpw(54,1);tick();gpw(55,1);sdbg("stop");}
static void sbyte(uint8_t x){
    uint8_t orig=x;
    gpw(54,0);
    for(int i=0;i<8;i++){
        gpw(55,(x&0x80)!=0); tick(); gpw(54,1); tick();
        if(sccb_trace)
            printf("sccb bit tx=%02x[%d]=%u scl=%d sda=%d\n",
                   orig,i,(orig>>(7-i))&1,gpr(54),gpr(55));
        gpw(54,0); tick(); x<<=1;
    }
}

static int sack(const char *tag){
    gdir(55,0); gpw(54,1); tick();
    int ack=(gpr(55)==0);
    if(sccb_trace || !ack)
        printf("sccb ack %-8s value=%d scl=%d sda=%d\n",tag,ack,gpr(54),gpr(55));
    gpw(54,0); tick(); gdir(55,1);
    return ack;
}

static void snack(void){
    /* Single-byte reads end with a master NACK, then STOP. */
    gdir(55,0); gpw(54,1); tick();
    if(sccb_trace)
        printf("sccb nack read     scl=%d sda=%d\n",gpr(54),gpr(55));
    gpw(54,0); tick(); gdir(55,1); gpw(55,1);
}

static uint8_t sread(void){
    uint8_t x=0;
    gdir(55,0); gpw(54,0); tick();
    for(int i=0;i<8;i++){
        gpw(54,1); tick();
        int bit=gpr(55); x=(uint8_t)((x<<1)|bit);
        if(sccb_trace)
            printf("sccb bit rx[%d]=%d scl=%d sda=%d\n",i,bit,gpr(54),gpr(55));
        gpw(54,0); tick();
    }
    gdir(55,1);
    return x;
}

static int cam_wr(uint16_t a,uint8_t d){
    int ok=1;
    sstart();
    sbyte(0x78); ok &= sack("dev-w");
    sbyte((uint8_t)(a>>8)); ok &= sack("addr-hi");
    sbyte((uint8_t)a); ok &= sack("addr-lo");
    sbyte(d); ok &= sack("data");
    sstop();
    if(!ok) fprintf(stderr,"camera write failed reg=%04x value=%02x\n",a,d);
    return ok ? 0 : -1;
}

static int cam_rd(uint16_t a,uint8_t *d){
    int ok=1;
    sstart();
    sbyte(0x78); ok &= sack("dev-w");
    sbyte((uint8_t)(a>>8)); ok &= sack("addr-hi");
    sbyte((uint8_t)a); ok &= sack("addr-lo");
    sstop();
    sstart();
    sbyte(0x79); ok &= sack("dev-r");
    *d=sread(); snack(); sstop();
    if(!ok) fprintf(stderr,"camera read failed reg=%04x\n",a);
    return ok ? 0 : -1;
}

static int camera_init(void){
    uint8_t hi=0,lo=0,mode=0,fmt=0,w_hi=0,w_lo=0,h_hi=0,h_lo=0;
    const char *trace=getenv("VIDEO_SCCB_TRACE");
    sccb_trace=trace && strcmp(trace,"0")!=0;

    if(ps_gpio_clock_init()<0) return -1;
    gpiofd=open("/dev/mem",O_RDWR|O_SYNC);
    if(gpiofd<0) return -1;
    gpio=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,gpiofd,0xE000A000);
    if(gpio==MAP_FAILED) return -1;

    gdir(54,1); gdir(55,1); gpw(54,1); gpw(55,1);
    usleep(20000);
    if(cam_rd(0x300a,&hi)<0 || cam_rd(0x300b,&lo)<0) return -1;
    printf("camera id=%02x%02x\n",hi,lo);
    if(hi!=0x56 || lo!=0x40) return -2;

    /* Linux SCCB configuration takes seconds. Disable DVP pads throughout
     * configuration so Video In cannot consume partially configured frames. */
    if(cam_wr(0x3008,0x82)<0) return -3;
    usleep(1000);
    if(cam_wr(0x3017,0x00)<0 || cam_wr(0x3018,0x00)<0) return -3;
    for(size_t i=0;i<OV5640_720P_REG_COUNT;i++){
        if(ov5640_720p_regs[i].addr==0x3017 ||
           ov5640_720p_regs[i].addr==0x3018) continue;
        if(cam_wr(ov5640_720p_regs[i].addr,ov5640_720p_regs[i].value)<0)
            return -3;
    }

    if(cam_rd(0x3008,&mode)<0 || cam_rd(0x4300,&fmt)<0 ||
       cam_rd(0x3808,&w_hi)<0 || cam_rd(0x3809,&w_lo)<0 ||
       cam_rd(0x380a,&h_hi)<0 || cam_rd(0x380b,&h_lo)<0)
        return -4;
    unsigned width=((unsigned)w_hi<<8)|w_lo;
    unsigned height=((unsigned)h_hi<<8)|h_lo;
    printf("camera configured regs=%zu mode=%02x fmt=%02x size=%ux%u\n",
           OV5640_720P_REG_COUNT,mode,fmt,width,height);
    if((mode&0x40) || fmt!=0x61 || width!=WIDTH || height!=HEIGHT) return -5;
    return 0;
}

static int camera_stream_on(void){
    uint8_t stream=0;
    if(cam_wr(0x4202,0x00)<0) return -1;
    if(cam_wr(0x3017,0xff)<0 || cam_wr(0x3018,0xff)<0) return -1;
    usleep(1000);
    if(cam_rd(0x4202,&stream)<0) return -1;
    printf("camera stream=%s ctrl4202=%02x\n", stream==0x00 ? "on" : "unknown", stream);
    return stream==0x00 ? 0 : -1;
}

static int hls_defaults(void){
    struct uio_map c={.fd=-1},a={.fd=-1};
    if(open_uio("color_mapping@40010000",&c)<0||open_uio("add_image@40000000",&a)<0){perror("HLS UIO");return -1;}
    float v[]={24,21,18,15,12,6};
    for(int i=0;i<6;i++) wr(c.r,0x10+i*8,fbits(v[i]));
    wr(a.r,0x10,100);
    /* Both HLS kernels use ap_ctrl_none; only parameter registers exist. */
    close_uio(&c);close_uio(&a);return 0;
}

static int hls_start_add_image(void){
    struct uio_map a={.fd=-1};
    if(open_uio("add_image@40000000",&a)<0){perror("add_image UIO");return -1;}
    wr(a.r,0x00,1);
    close_uio(&a);
    return 0;
}

struct vdma_ctx { struct uio_map d; const char *name; uint32_t base; };

static int vdma_prepare(struct vdma_ctx *v,const char *name,uint32_t base){
    memset(v,0,sizeof(*v)); v->d.fd=-1; v->name=name; v->base=base;
    if(open_uio(name,&v->d)<0){perror(name);return -1;}

    /*
     * Reset both channels, retaining the hardware's frame-sync/genlock
     * source selection. The working BSP modifies CR with read/modify/write;
     * writing zero here discards the reset value's internal-genlock bit.
     */
    for(unsigned ch=0;ch<2;ch++){
        unsigned o=ch?0x30:0;
        wr(v->d.r,o+0x00,0x00000004); /* channel reset */
        for(unsigned t=0;t<1000 && (rd(v->d.r,o+0x00)&0x00000004);t++)
            usleep(10);
        if(rd(v->d.r,o+0x00)&0x00000004){
            fprintf(stderr,"%s channel%u reset timeout\n",v->name,ch);
            close_uio(&v->d);
            return -2;
        }
        uint32_t control=rd(v->d.r,o+0x00);
        /*
         * Match the verified bare-metal register image exactly. Both channels
         * use the default same-channel FSYNC source; selecting 0x40 on S2MM
         * produced Linux-only 0x100CB and left the camera writer at PARKPTR=0.
         * Preserve the generated frame-count field and enable the same
         * error/frame-count interrupt bits, yielding post-start CR 0x1408B.
         */
        control=(control & ~0x00000ff1u) | 0x0000408au;
        wr(v->d.r,o+0x00,control); /* circular + internal genlock, halted during setup */
        printf("%s %s reset cr=%08x sr=%08x\n",v->name,
               ch?"s2mm":"mm2s",rd(v->d.r,o+0x00),rd(v->d.r,o+0x04));
    }

    for(unsigned ch=0;ch<2;ch++){
        unsigned sa=ch?0xac:0x5c;
        unsigned hs=ch?0xa4:0x54;
        unsigned st=ch?0xa8:0x58;
        for(unsigned i=0;i<FRAMES;i++)
            wr(v->d.r,sa+4*i,base+i*STRIDE*HEIGHT);
        wr(v->d.r,hs,STRIDE);
        wr(v->d.r,st,STRIDE);
        /* Program the complete frame geometry while the channel is halted.
         * VSIZE=0 is not a safe placeholder on AXI VDMA: it is itself
         * treated as a VDMA internal error at the next frame boundary.
         * The start path resubmits VSIZE after RUNSTOP to trigger transfer. */
        wr(v->d.r,ch?0xa0:0x50,HEIGHT);
    }

    return 0;
}

static int vdma_start_channel(struct vdma_ctx *v,unsigned ch){
    unsigned control=ch?0x30:0;
    unsigned vsize=ch?0xa0:0x50;
    uint32_t c=rd(v->d.r,control);
    wr(v->d.r,control,c|0x00000001u); /* RUNSTOP first */
    /* DMASR W1C errors from a prior run may persist through channel reset.
     * They become clearable only after the direct-mode channel is started. */
    uint32_t stale_status=rd(v->d.r,ch?0x34:0x04);
    wr(v->d.r,ch?0x34:0x04,stale_status|VDMA_STATUS_W1C_MASK);
    /* Direct mode starts transfers on a VSIZE write after RUNSTOP, as in
     * XAxiVdma_ChannelStart(). RUNSTOP alone only arms the channel. */
    wr(v->d.r,vsize,HEIGHT);
    printf("%s %s armed cr=%08x status=%08x\n",v->name,ch?"s2mm":"mm2s",
           rd(v->d.r,control),rd(v->d.r,ch?0x34:0x04));
    usleep(1000);
    uint32_t status=rd(v->d.r,ch?0x34:0x04);
    printf("%s %s cr=%08x status=%08x errors=%08x park=%08x hsize=%u stride=%u vsize=%u\n",
           v->name,ch?"s2mm":"mm2s",rd(v->d.r,control),status,
           video_vdma_errors(status,ch),rd(v->d.r,VDMA_PARKPTR_OFFSET),
           rd(v->d.r,ch?0xa4:0x54),rd(v->d.r,ch?0xa8:0x58),
           rd(v->d.r,vsize));
    /*
     * The DMASR error bits are sticky, per-channel and self-recovering: a
     * channel that latches ERR_INTERNAL / ERR_FSZ_* while its source stream
     * is still settling keeps transferring normally afterwards.  The working
     * bare-metal application never inspects DMASR at all, which is why it is
     * immune to this.  Treating these bits as fatal here used to abort the
     * whole pipeline before the MM2S readers and the VTC were ever started,
     * producing "no HDMI output", "black screen with white bars" and frozen
     * frames depending on exactly when the camera stream was sampled.
     */
    if(video_vdma_errors(status,ch))
        printf("%s %s sticky status %08x cleared; channel keeps running\n",
               v->name,ch?"s2mm":"mm2s",video_vdma_errors(status,ch));
    /* Only a channel that never left the halted state is a real failure. */
    if(status & 0x00000001u){
        usleep(2000);
        uint32_t again=rd(v->d.r,ch?0x34:0x04);
        if(again & 0x00000001u){
            fprintf(stderr,"%s %s failed to start status=%08x\n",
                    v->name,ch?"s2mm":"mm2s",again);
            return -1;
        }
    }
    return 0;
}

static void vdma_close(struct vdma_ctx *v){ close_uio(&v->d); }

static void vdma_stop_channel(struct vdma_ctx *v,unsigned ch){
    unsigned control=ch?0x30:0x00;
    uint32_t c=rd(v->d.r,control);
    wr(v->d.r,control,c & ~0x00000001u);
    usleep(1000);
}

/* The camera writer is mandatory.  The acoustic writer is optional because
 * its source is a free-running synthetic AXI4-Stream and may be absent or
 * mid-frame when Linux releases reset. */
static int vdma_start_s2mm(struct vdma_ctx *v0,struct vdma_ctx *v1,
                           int *acoustic_ok){
    int rc0=vdma_start_channel(v0,1);
    if(rc0<0){
        fprintf(stderr,"acoustic s2mm unavailable; camera path will continue\n");
        *acoustic_ok=0;
        vdma_stop_channel(v0,1);
    } else {
        *acoustic_ok=1;
    }
    /* Do not let an acoustic startup error prevent the camera writer from
     * being armed.  The camera stream is held off until this succeeds. */
    if(vdma_start_channel(v1,1)<0){
        if(*acoustic_ok) vdma_stop_channel(v0,1);
        return -1;
    }
    return 0;
}

static int vdma_start_mm2s(struct vdma_ctx *v0,struct vdma_ctx *v1,
                           int acoustic_ok){
    /*
     * Both readers are started; the second argument is kept for call-site
     * clarity but is always 1 now.  add_image is free-running and reads both
     * streams, so neither reader may be left halted.
     *
     * Note on liveness detection: PARK_PTR.WrFrmPntrRef is NOT a reliable
     * "frames are landing" signal on this design - measured runs show it
     * pinned at 0 while the frame stores demonstrably hold different, valid
     * images.  Do not gate anything on it.
     */
    if(vdma_start_channel(v1,0)<0) return -1;
    if(acoustic_ok && vdma_start_channel(v0,0)<0){
        fprintf(stderr,"acoustic mm2s unavailable; continuing camera-only\n");
        vdma_stop_channel(v0,0);
    }
    return 0;
}

static int vdma_wait_frames(struct vdma_ctx *v, unsigned count, int require_clean){
    unsigned completed=0;
    for(unsigned polls=0;polls<3000;polls++){
        uint32_t status=rd(v->d.r,0x34);
        if((status&1u) || (require_clean && (status&VDMA_STATUS_ERROR_MASK))){
            fprintf(stderr,"%s capture failed status=%08x frames=%u/%u park=%08x\n",
                    v->name,status,completed,count,rd(v->d.r,VDMA_PARKPTR_OFFSET));
            return -1;
        }
        if(status&0x1000u){
            completed++;
            wr(v->d.r,0x34,0x1000u);
            if(completed==count){
                printf("%s observed %u new frame events status=%08x park=%08x%s\n",
                       v->name,completed,status,rd(v->d.r,VDMA_PARKPTR_OFFSET),
                       require_clean?" clean":" (acoustic errors reported separately)");
                return 0;
            }
        }
        usleep(1000);
    }
    fprintf(stderr,"%s capture timeout frames=%u/%u status=%08x park=%08x\n",
            v->name,completed,count,rd(v->d.r,0x34),rd(v->d.r,VDMA_PARKPTR_OFFSET));
    return -1;
}

static int vtc_start(void){
    struct uio_map v={.fd=-1};
    if(open_uio("v_tc@43c10000",&v)<0){perror("VTC UIO");return -1;}

    /*
     * Exact register image MEASURED from the working bare-metal application
     * on this board (JTAG snapshot of 0x43C10000..0x43C1009C while
     * acoustic_app was running and the camera picture was on screen).
     *
     * The previous revision used a hand-assembled 0x03FDEF07
     * (= ALLSS | GE | RU | SW) and GFENC = 0.  Both are wrong:
     *   0x03FDEF07 XOR 0x03F7EF06 = 0x000A0001
     *     bit 19 = XVTC_CTL_INTERLACE_MASK  -> interlaced mode was ENABLED
     *     bit 17 = XVTC_CTL_VASS_MASK
     *     bit  0 = XVTC_CTL_SW_MASK         -> core enable (bare metal leaves it 0)
     * A 1280x720 INTERLACED timing is rejected by a progressive monitor, which
     * is exactly the reported "no signal" / "no HDMI output".
     */
    static const struct { unsigned off; uint32_t value; const char *name; } cfg[]={
        {0x60,0x02D00500,"GASIZE"},
        {0x68,0x00000002,"GFENC"},
        {0x6c,0x0000007F,"GPOL"},
        {0x70,0x00000672,"GHSIZE"},
        {0x74,0x02EE02EE,"GVSIZE"},
        {0x78,0x0596056E,"GHSYNC"},
        {0x7c,0x05000500,"GVBHOFF"},
        {0x80,0x02D902D4,"GVSYNC"},
        {0x84,0x056E056E,"GVSHOFF"},
        {0x88,0x05000500,"GVBHOFF_F1"},
        {0x8c,0x02D902D4,"GVSYNC_F1"},
        {0x90,0x056E056E,"GVSHOFF_F1"},
        {0x94,0x02D00000,"GASIZE_F1"},
    };
    const uint32_t CTL_BARE_METAL = 0x03F7EF06u; /* progressive, no SW enable */

    wr(v.r,0x00,0x00000002); /* register-update enable, generator stopped */
    for(size_t i=0;i<sizeof(cfg)/sizeof(cfg[0]);i++) wr(v.r,cfg[i].off,cfg[i].value);
    wr(v.r,0x00,CTL_BARE_METAL);
    usleep(2000);

    int bad=0;
    for(size_t i=0;i<sizeof(cfg)/sizeof(cfg[0]);i++){
        uint32_t got=rd(v.r,cfg[i].off);
        printf("vtc %-11s off=%03x value=%08x%s\n",cfg[i].name,cfg[i].off,got,
               got==cfg[i].value ? "" : " MISMATCH");
        if(got!=cfg[i].value) bad=1;
    }
    uint32_t ctl=rd(v.r,0x00),status=rd(v.r,0x64);
    printf("vtc control=%08x (bare-metal %08x) timing_status=%08x\n",
           ctl,CTL_BARE_METAL,status);
    if((ctl&CTL_BARE_METAL)!=CTL_BARE_METAL) bad=1;
    if(ctl&0x00080000u){
        printf("vtc WARNING interlace bit is set (0x%08x); forcing progressive\n",ctl);
        wr(v.r,0x00,ctl & ~0x00080000u);
        usleep(2000);
        ctl=rd(v.r,0x00);
        printf("vtc control now=%08x\n",ctl);
        if(ctl&0x00080000u) bad=1;
    }
    close_uio(&v);
    return bad ? -2 : 0;
}

int main(int argc,char **argv){
    if(argc==2 && !strcmp(argv[1],"--version")){
        puts(VIDEO_MIXER_VERSION);
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--check-mixer")){
        char path[512];
        if(video_mixer_find_uio("/sys/class/uio","/dev",path,sizeof(path))<0){
            perror("Mixer UIO identity");return 1;
        }
        printf("mixer uio=%s name=%s base=44a30000 size>=00010000\n",path,VIDEO_MIXER_UIO_NAME);
        return 0; /* sysfs only: no mapping, camera or register writes */
    }
    if(argc!=1){fprintf(stderr,"usage: %s [--version|--check-mixer]\n",argv[0]);return 2;}
    const char *state="/run/video-pipeline.state";
    FILE *f=fopen(state,"w");
    if(f){fputs("camera: starting\n",f);fclose(f);}

    /*
     * Display timing is programmed LAST, in the same order as the bare-metal
     * application: camera -> run_vdma_frame_buffer(vdma0, BOTH) ->
     * run_vdma_frame_buffer(vdma1, BOTH) -> DisplayInitialize/SetMode/Start.
     *
     * It must not be gated on anything (the old code returned early and left
     * 0x43c10000 reading 0x00000000, i.e. the generator disabled, which is
     * the "no HDMI output" case), but it also must not be brought up before
     * the DMA engines: v_axi4s_vid_out latches its VTG/stream alignment when
     * the generator starts, and starting the VTG while no stream data exists
     * leaves it stuck.  vtc_ok is filled in below.
     */
    int vtc_ok=0;

    int rc=0, pipeline_ok=0;

    /* New hardware inserts Mixer between add_image and Video Out. Prepare
     * that downstream consumer before either MM2S starts; VTC remains last.
     * Missing/misidentified UIO is a real configuration error, not a bypass. */
    rc=mixer_start_primary();
    f=fopen(state,"a");
    if(f){fprintf(f,"mixer: %s version=%s\n",rc==0?"primary-only armed, ui off":"initialization-error",VIDEO_MIXER_VERSION);fclose(f);}
    if(rc<0) goto finish;

    struct vdma_ctx v0,v1;
    int v0_ok=0, v1_ok=0;

    if(rc==0){
        /* The free-running HLS kernels need parameters and both readers. */
        if(hls_defaults()<0)
            fprintf(stderr,"HLS parameter setup failed; continuing\n");
        f=fopen(state,"a"); if(f){fputs("hls: parameters ready, free-running\n",f);fclose(f);}

        v0_ok=(vdma_prepare(&v0,"dma@43000000",FB0)==0);
        v1_ok=(vdma_prepare(&v1,"dma@44a00000",FB1)==0);

        if(v0_ok && v1_ok){
            /*
             * Bring both VDMA instances up unconditionally, exactly like the
             * bare-metal application (run_vdma_frame_buffer(..., BOTH) on
             * each instance, no cross-gating).
             *
             * This is not just stylistic.  add_image is free-running: its
             * control register has no ap_start (offset 0x00 is reserved, only
             * 0x10 = alpha exists), so the kernel never stops and it consumes
             * BOTH MM2S streams.  Gating the acoustic reader on an
             * "is it healthy" test - as an earlier revision did - leaves
             * add_image's SRC_STREAM with no producer, the kernel blocks on
             * that read, and nothing ever reaches v_axi4s_vid_out / the DVI
             * transmitter even though the camera path is perfect.
             *
             * DMASR sticky bits (ERR_INTERNAL / ERR_FSZ_EARLY / ERR_FSZ_LATE)
             * are cleared and reported inside vdma_start_channel(); they are
             * documented as "VDMA does not halt" and bare metal never reads
             * them at all, so they must not influence the control flow.
             */
            int acoustic_ok=1;
            if(vdma_start_s2mm(&v0,&v1,&acoustic_ok)<0 || !acoustic_ok){
                fprintf(stderr,"camera s2mm did not leave the halted state\n");
                rc=-6;
                goto capture_failed;
            }
            f=fopen(state,"a");
            if(f){fprintf(f,"acoustic: s2mm %s\n",
                          acoustic_ok?"armed":"arm-failed, left running");fclose(f);}

            /* Arm both writers before the slow sensor register sequence. */
            rc=camera_init();
            f=fopen(state,"a");
            if(f){fprintf(f,"camera: %s\n",rc==0?"ready-output-off":"configuration-error");fclose(f);}
            if(rc<0) goto capture_failed;
            wr(v0.d.r,0x34,VDMA_STATUS_W1C_MASK);
            wr(v1.d.r,0x34,VDMA_STATUS_W1C_MASK);
            if(camera_stream_on()<0){
                rc=-7;
                goto capture_failed;
            }
            f=fopen(state,"a"); if(f){fputs("camera: streaming\n",f);fclose(f);}

            /* Camera events must be clean. The acoustic source also reports
             * 0x810 on the working bare-metal baseline; keep it running. */
            if(vdma_wait_frames(&v1,FRAMES,1)<0 ||
               vdma_wait_frames(&v0,FRAMES,0)<0){
                rc=-8;
                goto capture_failed;
            }
            f=fopen(state,"a"); if(f){fputs("camera: clean frames captured\n",f);fclose(f);}

            /* Both readers, always - see the note above. */
            if(vdma_start_mm2s(&v0,&v1,1)<0){
                fprintf(stderr,"an mm2s reader did not start; add_image will stall\n");
                f=fopen(state,"a");
                if(f){fputs("vdma: mm2s start failed\n",f);fclose(f);}
                rc=-9;
            } else {
                f=fopen(state,"a");
                if(f){fputs("vdma: both s2mm+mm2s running\n",f);fclose(f);}
            }

            /* Display timing last, exactly where bare metal puts it.  Always
             * runs - no early return may leave the generator unprogrammed. */
            vtc_ok=(vtc_start()==0);
            f=fopen(state,"a");
            if(f){fprintf(f,"vtc: %s\n",vtc_ok?"ready":"program-error");fclose(f);}
            if(!vtc_ok)
                fprintf(stderr,"VTC read-back mismatch; display timing may be wrong\n");

            /* Writes to a reserved register on this build (ap_ctrl_none);
             * kept only so the alpha/parameter state is explicit. */
            (void)hls_start_add_image();
            f=fopen(state,"a"); if(f){fputs("hls: add_image free-running\n",f);fclose(f);}
            vdma_close(&v0); vdma_close(&v1);
            pipeline_ok=vtc_ok && rc==0;
            goto finish;

capture_failed:
            fprintf(stderr,"video startup failed rc=%d; readers not started\n",rc);
            if(gpio && gpio!=MAP_FAILED){
                (void)cam_wr(0x3017,0);
                (void)cam_wr(0x3018,0);
            }
            vdma_close(&v0); vdma_close(&v1);
            goto finish;
        } else {
            fprintf(stderr,"VDMA UIO open failed v0_ok=%d v1_ok=%d\n",v0_ok,v1_ok);
            f=fopen(state,"a");
            if(f){fputs("vdma: uio open failed\n",f);fclose(f);}
            if(v0_ok) vdma_close(&v0);
            if(v1_ok) vdma_close(&v1);
            /* Bring the display up anyway. */
            vtc_ok=(vtc_start()==0);
            f=fopen(state,"a");
            if(f){fprintf(f,"vtc: %s\n",vtc_ok?"ready":"program-error");fclose(f);}
        }
    } else {
        fprintf(stderr,"camera initialization failed: %d\n",rc);
        f=fopen(state,"a");
        if(f){fprintf(f,"camera initialization failed rc=%d\n",rc);fclose(f);}
        /* Keep the display path alive so a dead sensor and a dead HDMI link
         * can be told apart on the bench. */
        if(vdma_prepare(&v0,"dma@43000000",FB0)==0) vdma_close(&v0);
        if(vdma_prepare(&v1,"dma@44a00000",FB1)==0){
            vdma_start_channel(&v1,1);
            vdma_start_channel(&v1,0);
            vdma_close(&v1);
        }
        vtc_ok=(vtc_start()==0);
        f=fopen(state,"a");
        if(f){fprintf(f,"vtc: %s\n",vtc_ok?"ready":"program-error");fclose(f);}
    }

finish:
    f=fopen(state,"a");
    if(f){
        fprintf(f,"video: %s rc=%d\n",pipeline_ok?"running":"degraded",rc);
        fclose(f);
    }
    if(gpio && gpio!=MAP_FAILED){munmap((void*)gpio,0x1000);close(gpiofd);}
    return pipeline_ok ? 0 : 1;
}

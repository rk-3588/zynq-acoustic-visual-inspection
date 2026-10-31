#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "video_status_render.h"
#include "video_status_touch.h"
#include "video_status_capture.h"
#include "video_status_gallery.h"
#include "video_status_image.h"
#include "video_status_robot.h"
#include "video_status_power.h"
#include "video_status_perf.h"

static volatile sig_atomic_t stopping;
static void stop_handler(int sig) { (void)sig; stopping=1; }
static uint64_t now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static uint32_t rd(void *p,unsigned off) {
    uint32_t v=((volatile uint32_t *)p)[off/4]; __sync_synchronize(); return v;
}
static void wr(void *p,unsigned off,uint32_t v) {
    ((volatile uint32_t *)p)[off/4]=v; __sync_synchronize();
}
static void pause_io(void *p,unsigned us) { (void)p; usleep(us); }
static void publish_pixels(void) {
    __sync_synchronize();
#if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("dsb sy" ::: "memory");
#endif
}
static int property(const char *path,void *data,size_t size) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW); if(fd<0) return -1;
    ssize_t n=read(fd,data,size); close(fd); return (int)n;
}
static uint32_t be32(const unsigned char *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static int preflight(char *uio,size_t size) {
    if(video_mixer_find_uio("/sys/class/uio","/dev",uio,size)) return -1;
    const char *root="/sys/firmware/devicetree/base/reserved-memory/";
    char path[256],state[2048]; unsigned char data[16];
    const char *cells[]={"#address-cells","#size-cells"};
    for(unsigned i=0;i<2;i++) {
        snprintf(path,sizeof(path),"%s%s",root,cells[i]);
        if(property(path,data,sizeof(data))!=4 || be32(data)!=1) return -1;
    }
    snprintf(path,sizeof(path),"%svideo-framebuffer@1000000/reg",root);
    if(property(path,data,sizeof(data))!=8 || be32(data)!=0x01000000 || be32(data+4)!=0x03000000) return -1;
    snprintf(path,sizeof(path),"%svideo-framebuffer@1000000/no-map",root);
    if(property(path,data,sizeof(data))!=0 || !mix_ui_memory_safe(MIX_UI_BASE,PANEL_MAP_BYTES)) return -1;
    int n=property("/run/video-pipeline.state",state,sizeof(state)-1);
    if(n<0) return -1;
    state[n]=0;
    return strstr(state,"video: running rc=0") && strstr(state,"mixer: primary-only armed") ? 0 : -1;
}
static int query_status(struct panel_status *s) {
    panel_unavailable(s);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0) return -1;
    struct sockaddr_un address={0}; address.sun_family=AF_UNIX;
    strcpy(address.sun_path,"/run/pl-control.sock");
    if(connect(fd,(struct sockaddr *)&address,sizeof(address)) && errno!=EINPROGRESS) {close(fd); return -1;}
    uint64_t deadline=now_ms()+60; struct pollfd poller={fd,POLLOUT,0};
    if(poll(&poller,1,20)<=0) {close(fd); return -1;}
    int error=0; socklen_t length=sizeof(error);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length) || error ||
        send(fd,"status\n",7,MSG_NOSIGNAL)!=7) {close(fd); return -1;}
    char data[4096]; size_t used=0;
    while(now_ms()<deadline && used<sizeof(data)) {
        poller.events=POLLIN;
        int remaining=(int)(deadline-now_ms());
        if(remaining<=0 || poll(&poller,1,remaining)<=0) break;
        ssize_t n=recv(fd,data+used,sizeof(data)-used,0);
        if(n<=0) break;
        used+=(size_t)n;
        if(memchr(data,'\n',used)) break;
    }
    close(fd);
    return panel_parse_status(data,used,s)?0:-1;
}
/* Bounded IPC: no MMIO/control writes from the UI and no waiting for application. */
static int request_enable(int enabled) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,"/run/pl-control.sock");
    if(connect(fd,(struct sockaddr *)&address,sizeof(address)) && errno!=EINPROGRESS){close(fd);return -1;}
    struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t length=sizeof(error);
    if(poll(&p,1,20)<=0 || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length) || error){close(fd);return -1;}
    const char *command=enabled?"enable\n":"disable\n";size_t size=strlen(command);
    if(send(fd,command,size,MSG_NOSIGNAL)!=(ssize_t)size){close(fd);return -1;}
    p.events=POLLIN;char reply[128];size_t used=0;uint64_t deadline=now_ms()+40;
    while(now_ms()<deadline && used<sizeof(reply)) {
        int remaining=(int)(deadline-now_ms());
        if(remaining<=0 || poll(&p,1,remaining)<=0)break;
        ssize_t n=recv(fd,reply+used,sizeof(reply)-used,0);
        if(n<=0)break;
        used+=(size_t)n;if(memchr(reply,'\n',used))break;
    }
    close(fd);
    const char *accepted="OK requested; query status enable_applied\n";
    return used==strlen(accepted) && !memcmp(reply,accepted,used)?0:-1;
}
static void enable_view(struct panel_view *v,const struct panel_status *s) {
    uint64_t now=now_ms();
    if(v->acoustic_request) {
        int enabled=v->acoustic_request>0;v->acoustic_request=0;v->acoustic_target=enabled;
        if(request_enable(enabled)) {
            v->acoustic_deadline_ms=0;v->acoustic_error_until_ms=now+3000;
            fprintf(stderr,"ACOUSTIC_REQUEST_FAILED target=%d\n",enabled);
        } else {
            v->acoustic_deadline_ms=now+2000;
            printf("ACOUSTIC_REQUEST_ACCEPTED target=%d (not yet applied)\n",enabled);fflush(stdout);
        }
    }
    panel_enable_view_update(v,s->online && s->acoustic_known,s->acoustic_enabled,s->enable_applied,now);
}
static void status_file(const struct panel_swap *swap,const struct panel_status *s,
                        const struct panel_temperature *t,const struct panel_view *view,
                        const struct panel_touch_reader *touch,const struct panel_perf *perf,int running) {
    FILE *file=fopen("/run/video-status-panel.state.new","w"); if(!file) return;
    fprintf(file,"{\"version\":\"%s\",\"running\":%s,\"flips\":%llu,\"completion_events\":%llu,"
        "\"front\":%u,\"front_address\":\"0x%08x\",\"pending\":%s,\"data_usable\":%s,"
        "\"sound_state\":\"%s\",\"position_state\":\"%s\",\"position_valid\":%s,"
        "\"monotonic_ms\":%llu,\"target_refresh_hz\":5,\"width\":%u,\"height\":%u,"
        "\"temperature_valid\":%s,\"temperature_millicelsius\":",PANEL_VERSION,running?"true":"false",
        (unsigned long long)swap->flips,(unsigned long long)swap->completions,
        swap->front,panel_address(swap->front),swap->pending?"true":"false",s->online?"true":"false",
        s->sound,s->position,s->valid?"true":"false",(unsigned long long)now_ms(),
        PANEL_WIDTH,PANEL_HEIGHT,t->valid?"true":"false");
    if(t->valid) fprintf(file,"%d",t->millidegrees); else fputs("null",file);
    fprintf(file,",\"temperature_level\":%d,\"temperature_sampled_ms\":%llu,\"grid_energy_peak\":",
        panel_temp_level(t),(unsigned long long)t->sampled_ms);
    if(s->online) fprintf(file,"%u",s->peak); else fputs("null",file);
    fputs(",\"filtered_col\":",file);
    if(s->valid) fprintf(file,"%d",s->col); else fputs("null",file);
    fputs(",\"filtered_row\":",file);
    if(s->valid) fprintf(file,"%d",s->row); else fputs("null",file);
    fprintf(file,",\"realtime_grid_valid\":%s,\"realtime_col\":",s->raw_valid?"true":"false");
    if(s->raw_valid) fprintf(file,"%d",s->raw_col); else fputs("null",file);
    fputs(",\"realtime_row\":",file);
    if(s->raw_valid) fprintf(file,"%d",s->raw_row); else fputs("null",file);
    fprintf(file,",\"robot_state\":\"%s\",\"robot_state_source\":\"uart1_service\",\"robot_state_live\":%s,"
        "\"robot_uart_ready\":%s,\"robot_task_id\":%u,\"robot_phase\":%u,\"robot_target_region\":%u,"
        "\"robot_confirm_open\":%s,\"robot_fault\":%s,\"robot_feedback_seen\":%s,\"robot_region_mask\":%u",
        panel_robot_name(view->robot_state),view->robot.fresh?"true":"false",
        view->robot.uart_ready?"true":"false",view->robot.id,view->robot.phase,view->robot.target,
        view->robot.modal?"true":"false",view->robot.fault?"true":"false",view->robot.feedback_seen?"true":"false",view->robot.region_mask);
    fprintf(file,",\"robot_window_open\":%s,\"robot_tab\":%d,\"robot_mode\":%u,\"robot_track_region\":%u,"
        "\"robot_track_armed\":%s,\"robot_track_mask\":%u,\"robot_cycle_ready\":%s,\"robot_cycle_active\":%s,"
        "\"robot_cycle_limit\":%u,\"robot_cycle_completed\":%u,\"robot_mode_error\":%u,\"robot_transaction_pending\":%s",
        view->robot.window?"true":"false",view->robot.tab,view->robot.mode,view->robot.track_region,
        view->robot.track_armed?"true":"false",view->robot.track_mask,view->robot.cycle_ready?"true":"false",
        view->robot.cycle_active?"true":"false",view->robot.cycle_limit,view->robot.cycle_completed,view->robot.mode_error,
        view->robot.transaction_pending?"true":"false");
    fprintf(file,",\"robot_auto_mode\":%u,\"robot_auto_waiting\":%s,\"robot_auto_latched\":%s,\"robot_grab_auto_choice\":%s",
        view->robot.auto_mode,view->robot.auto_waiting?"true":"false",view->robot.auto_latched?"true":"false",
        view->robot.grab_auto_choice?"true":"false");
    fprintf(file,",\"power_state\":\"%s\",\"power_confirm_open\":%s,\"power_error\":%d,\"power_slow\":%s",
        panel_power_name(view->power.phase),view->power.phase==PANEL_POWER_CONFIRM?"true":"false",
        view->power.error,view->power.slow?"true":"false");
    /* Keep the old null measurement fields for snapshot/schema compatibility. */
    fputs(",\"raw_signal_peak\":null,\"raw_signal_source\":\"not_connected\",\"peak_frequency_hz\":",file);
    if(s->frequency.valid)fprintf(file,"%.2f",s->frequency.hz);else fputs("null",file);
    fprintf(file,",\"frequency_source\":\"single_mic_hpf_output\",\"hotspot_associated\":false,"
        "\"frequency_valid\":%s,\"frequency_fresh\":%s,\"frequency_reason\":\"%s\","
        "\"frequency_channel\":%u,\"frequency_sequence\":%u,\"frequency_sampled_ms\":%llu,"
        "\"frequency_age_ms\":",s->frequency.valid?"true":"false",s->frequency.fresh?"true":"false",
        s->frequency.reason?s->frequency.reason:"unavailable",s->frequency.channel,s->frequency.sequence,
        (unsigned long long)s->frequency.sampled_ms);
    if(s->frequency.fresh)fprintf(file,"%llu",(unsigned long long)s->frequency.age_ms);else fputs("null",file);
    fprintf(file,",\"acoustic_control_known\":%s,\"acoustic_enabled\":%s,\"enable_applied\":%s,"
        "\"acoustic_button_known\":%s,\"acoustic_button_on\":%s,\"acoustic_switch_pending\":%s,\"acoustic_switch_error\":%s",
        s->acoustic_known?"true":"false",s->acoustic_enabled?"true":"false",s->enable_applied?"true":"false",
        view->acoustic_known?"true":"false",view->acoustic_on?"true":"false",view->acoustic_pending?"true":"false",
        view->acoustic_error_until_ms?"true":"false");
    fprintf(file,",\"sidebar_visible\":%s,\"sidebar_alpha\":%u,\"touch_connected\":%s,"
        "\"touch_device\":\"%s\",\"touch_x\":%d,\"touch_y\":%d,\"touch_taps\":%llu,"
        "\"touch_reports\":%llu,\"capture_ready\":%s,\"capture_busy\":%s,\"capture_feedback\":%d,"
        "\"capture_count_known\":%s,\"capture_count\":%u,\"gallery_open\":%s,\"gallery_busy\":%s,"
        "\"gallery_error\":%s,\"gallery_detail\":%s,\"gallery_page\":%u,\"gallery_pages\":%u,"
        "\"gallery_items\":%u,\"gallery_json\":%s,\"gallery_json_page\":%u",view->sidebar_visible?"true":"false",PANEL_SIDEBAR_ALPHA,
        view->touch_connected?"true":"false",touch->path,view->touch_x,view->touch_y,
        (unsigned long long)view->taps,(unsigned long long)touch->reports,
        view->capture_ready?"true":"false",view->capture_busy?"true":"false",view->capture_feedback,
        view->capture_count_known?"true":"false",view->capture_count,
        view->gallery_open?"true":"false",view->gallery_busy?"true":"false",view->gallery_error?"true":"false",
        view->gallery_detail?"true":"false",view->gallery_page,view->gallery_pages,view->gallery_items,
        view->gallery_json?"true":"false",view->gallery_json_page);
    fprintf(file,",\"capture_reason\":\"%s\",\"capture_checked_ms\":%llu,\"capture_status_known\":%s,"
        "\"capture_s2mm_status\":%u,\"capture_error_bits\":%u",view->capture_reason,
        (unsigned long long)view->capture_checked_ms,view->capture_status_known?"true":"false",
        view->capture_s2mm_status,view->capture_error_bits);
    fprintf(file,",\"render_us\":%llu,\"ui_write_us\":%llu,\"ui_verify_us\":%llu,"
        "\"ui_retire_us\":%llu,\"ui_loop_us\":%llu,\"ui_max_loop_us\":%llu,"
        "\"vpp_counter_available\":%s,\"vpp_accepted_sof\":%u,\"vpp_rate_valid\":%s,\"vpp_rate_millihz\":%u",
        (unsigned long long)perf->render_us,(unsigned long long)perf->write_us,(unsigned long long)perf->verify_us,
        (unsigned long long)perf->retire_us,(unsigned long long)perf->loop_us,(unsigned long long)perf->max_loop_us,
        perf->vpp_known?"true":"false",perf->rate.sequence,perf->rate.valid?"true":"false",perf->rate.millihz);
    const struct panel_image_view *image=&view->image;
    fprintf(file,",\"image_known\":%s,\"image_busy\":%s,\"image_pending\":%s,\"image_error\":%s,"
        "\"image_mode\":%u,\"image_threshold\":%u,\"image_brightness\":%d,\"image_gain_shift\":%d,"
        "\"image_roi_enabled\":%s,\"image_roi\":[%u,%u,%u,%u],\"image_mode_drawer\":%s,"
        "\"image_settings\":%s,\"image_roi_editing\":%s,\"image_sampled_ms\":%llu,"
        "\"region_selected\":%d,\"region_drawer\":%s,\"follow_enabled\":%s,\"follow_visible\":%s,"
        "\"follow_hold\":%s,\"follow_fault\":%s,\"follow_applied\":%s,\"follow_mode\":%u,"
        "\"follow_size\":[%u,%u],\"follow_target\":[%u,%u],\"follow_last_good_ms\":%llu}\n",
        image->known?"true":"false",image->busy?"true":"false",image->pending?"true":"false",image->error?"true":"false",
        image->active.mode,image->active.threshold,image->active.brightness,image->active.gain,image->active.roi?"true":"false",
        image->active.x,image->active.y,image->active.w,image->active.h,image->popup?"true":"false",
        image->settings?"true":"false",image->editing?"true":"false",(unsigned long long)image->sampled_ms,
        panel_region_selected(image),image->region_popup?"true":"false",image->follow?"true":"false",
        image->follow_visible?"true":"false",image->follow_hold?"true":"false",image->follow_fault?"true":"false",
        image->follow_applied?"true":"false",image->follow_mode,panel_follow_width(image),panel_follow_height(image),
        image->follow_x,image->follow_y,(unsigned long long)image->follow_last_good_ms);
    if(fclose(file)==0) rename("/run/video-status-panel.state.new","/run/video-status-panel.state");
}
static int copy_slot(void *memory,unsigned slot,const uint32_t *pixels,struct panel_perf *perf,int verify) {
    if(slot>1) return -1;
    volatile uint32_t *target=(volatile uint32_t *)((unsigned char *)memory+slot*PANEL_SLOT_BYTES);
    uint64_t begin=video_probe_ns();
    for(unsigned i=0;i<PANEL_WIDTH*PANEL_HEIGHT;i++) target[i]=pixels[i];
    publish_pixels();
    uint64_t written=video_probe_ns();
    if(verify)for(unsigned i=0;i<PANEL_WIDTH*PANEL_HEIGHT;i++) if(target[i]!=pixels[i]) return -1;
    publish_pixels();
    if(perf){perf->write_us=(written-begin)/1000;perf->verify_us=verify?(video_probe_ns()-written)/1000:0;}
    return 0;
}
static int retire(const struct video_mixer_io *io,struct panel_swap *s) {
    uint64_t until=now_ms()+250;
    do {
        if(stopping) return -1;
        int done=panel_retire_step(io,s); if(done) return done==1?0:-1;
        usleep(1000);
    } while(now_ms()<until);
    fprintf(stderr,"FLIP_TIMEOUT: both buffers remain immutable, no early reuse\n"); return -1;
}
static int preview(const char *path,int visible) {
    uint32_t pixels[PANEL_WIDTH*PANEL_HEIGHT];
    struct panel_status s={.online=1,.valid=1,.col=32,.row=18,.peak=36420,.age_ms=12,
        .sound="active",.position="stable",.raw_valid=1,.raw_col=32,.raw_row=18};
    struct panel_temperature t={.valid=1,.millidegrees=94800};
    s.frequency=(struct panel_frequency){.fresh=1,.candidate_valid=1,.valid=1,.channel=0,.hz=2000.3,.reason="live"};
    struct panel_view view={.sidebar_visible=visible,.touch_connected=1,.acoustic_known=1,.acoustic_on=1,
        .capture_ready=1,.capture_count_known=1,.capture_count=12};
    s.acoustic_known=s.acoustic_enabled=s.enable_applied=1;
    panel_render_view(pixels,&s,&t,42,1,754,&view);
    FILE *out=fopen(path,"wb"); if(!out) return 1;
    fprintf(out,"P6\n%u %u\n255\n",PANEL_WIDTH,PANEL_HEIGHT);
    for(unsigned i=0;i<PANEL_WIDTH*PANEL_HEIGHT;i++) {
        uint32_t p=pixels[i]; unsigned a=p>>24;
        unsigned char rgb[3];
        for(unsigned c=0;c<3;c++) rgb[c]=(unsigned char)((((p>>(c*8))&255)*a+180*(255-a))/255);
        if(fwrite(rgb,1,3,out)!=3) {fclose(out); return 1;}
    }
    return fclose(out)!=0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")) {puts(PANEL_VERSION); return 0;}
    if(argc==2 && !strcmp(argv[1],"--temperature")) {
        struct panel_temperature t={0};
        panel_temp_sample(&t,"/sys/bus/iio/devices",now_ms());
        if(!t.valid) {fprintf(stderr,"XADC temperature unavailable\n"); return 1;}
        printf("XADC %.1f C level=%d device=%s\n",t.millidegrees/1000.0,
            panel_temp_level(&t),t.path); return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--touch-check")) {
        struct panel_touch_reader touch; panel_touch_init(&touch);
        if(!panel_touch_open(&touch)) {fprintf(stderr,"No unique compatible direct touchscreen or invalid config\n");return 1;}
        printf("TOUCH %s name=%s id=%04x:%04x range=%d..%d,%d..%d swap=%d invert=%d,%d\n",
            touch.path,touch.name,touch.id.vendor,touch.id.product,touch.ax.minimum,touch.ax.maximum,
            touch.ay.minimum,touch.ay.maximum,touch.swap,touch.invert_x,touch.invert_y);
        close(touch.fd); return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--preview")) return preview(argv[2],1);
    if(argc==3 && !strcmp(argv[1],"--preview-collapsed")) return preview(argv[2],0);
    int verify_always=argc==2&&!strcmp(argv[1],"--run-verify");
    int check=argc==2 && !strcmp(argv[1],"--check"),continuous=argc==2 && (!strcmp(argv[1],"--run")||verify_always);
    unsigned long long seconds=0;
    if(argc==3 && !strcmp(argv[1],"--seconds")) {
        if(!mix_parse_integer(argv[2],&seconds) || seconds<1 || seconds>300) return 2;
    } else if(!check && !continuous) {
        fprintf(stderr,"Usage: %s --version | --temperature | --touch-check | --check | --preview FILE.ppm | --preview-collapsed FILE.ppm | --seconds 1..300 | --run | --run-verify (diagnostic full readback)\n",argv[0]); return 2;
    }
    char uio[512];
    if(preflight(uio,sizeof(uio))) {fprintf(stderr,"UIO/reserved-DDR/running-video preflight failed\n"); return 3;}
    if(check) {printf("%s %s buffers=%08x,%08x sysfs-only-check=pass\n",PANEL_VERSION,uio,panel_address(0),panel_address(1)); return 0;}
    int rc=4,lock=-1,fd=-1,mem=-1,dirty=0;
    void *regs=MAP_FAILED,*memory=MAP_FAILED;
    uint32_t *pixels=NULL,saved[MIX_UI_REG_COUNT]; unsigned saved_ier=0,saved_gie=0;
    struct panel_status status; panel_unavailable(&status);
    struct panel_temperature temperature={0};
    struct panel_view view={.sidebar_visible=1,.touch_x=-1,.touch_y=-1};
    struct panel_capture_job capture_job={.fd=-1};
    struct panel_gallery gallery={.fd=-1};view.gallery_ready=access("/usr/bin/camera_album",X_OK)==0;
    struct panel_image_job image_job={.fd=-1};view.image.ready=access(PANEL_IMAGE_HELPER,X_OK)==0;
    struct panel_power_job power_job={0};
    struct panel_touch_reader touch; panel_touch_init(&touch);
    if(!touch.config_ok) fprintf(stderr,"Invalid touch mapping config: interaction disabled\n");
    struct panel_swap swap={0};
    struct panel_perf perf={.vpp={.fd=-1}};
    struct video_mixer_io io={NULL,rd,wr,pause_io};
    lock=open("/run/video-mixer-ui-test.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)) {perror("shared UI ownership lock"); goto finish;}
    fd=open(uio,O_RDWR|O_CLOEXEC); if(fd<0) goto finish;
    regs=mmap(NULL,VIDEO_MIXER_MAP_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(regs==MAP_FAILED) goto finish;
    io.context=regs;
    uint32_t control=rd(regs,MIX_CTRL);
    saved_ier=rd(regs,MIX_IER); saved_gie=rd(regs,MIX_GIE);
    if(!(control&MIX_AUTO_RESTART) || (control&MIX_FLUSH) || rd(regs,MIX_LAYERS)!=1 ||
        rd(regs,MIX_WIDTH)!=1280 || rd(regs,MIX_HEIGHT)!=720 || rd(regs,MIX_FORMAT)!=0 || saved_ier || saved_gie) {
        fprintf(stderr,"Refusing: primary-only RGB or exclusive poll ownership mismatch\n"); goto finish;
    }
    for(unsigned i=0;i<MIX_UI_REG_COUNT;i++) saved[i]=rd(regs,mix_ui_offsets[i]);
    mem=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC); if(mem<0) goto finish;
    memory=mmap(NULL,PANEL_MAP_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,mem,MIX_UI_BASE);
    if(memory==MAP_FAILED) goto finish;
    pixels=malloc(PANEL_BYTES); if(!pixels) goto finish;
    panel_temp_sample(&temperature,"/sys/bus/iio/devices",now_ms());
    panel_touch_tick(&touch,&view,now_ms());
    query_status(&status);
    enable_view(&view,&status);
    panel_robot_update(&view,&status,now_ms());
    panel_frequency_read(&status.frequency,"/run/acoustic-frequency.state",now_ms(),status.online,status.sound);
    panel_render_view(pixels,&status,&temperature,0,0,now_ms()/1000,&view);
    if(copy_slot(memory,0,pixels,NULL,1)||copy_slot(memory,1,pixels,NULL,1)) {
        fprintf(stderr,"DDR initial readback failed\n"); goto finish;
    }
    unsigned verified_slots=3u; /* Both were verified while the UI layer was off. */
    panel_perf_init(&perf);
    struct sigaction action={0}; action.sa_handler=stop_handler; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL); sigaction(SIGHUP,&action,NULL);
    if(mix_ui_drain(&io)) goto finish;
    dirty=1;
    /* Configure once; ongoing flips only change one latched pointer. */
    if(panel_enable_idle(&io)) goto finish;
    uint32_t pending=rd(regs,MIX_ISR)&3; if(pending) wr(regs,MIX_ISR,pending);
    wr(regs,MIX_IER,1); /* Poll real per-frame ap_done, leave external IRQ GIE=0. */
    printf("PANEL_ACTIVE %s buffers=%08x,%08x %ux%u at=%u,%u target=5Hz\n",
        PANEL_VERSION,panel_address(0),panel_address(1),PANEL_WIDTH,PANEL_HEIGHT,PANEL_X,PANEL_Y);
    fflush(stdout);
    uint64_t start=now_ms(),next=start;
    panel_capture_spawn(&capture_job,1,start);
    while(!stopping && (continuous || now_ms()-start<seconds*1000)) {
        uint64_t loop_begin=video_probe_ns();
        unsigned back=swap.front^1u;
        panel_touch_tick(&touch,&view,now_ms());
        panel_temp_sample(&temperature,"/sys/bus/iio/devices",now_ms());
        query_status(&status);
        enable_view(&view,&status);
        panel_robot_update(&view,&status,now_ms());
        panel_capture_tick(&capture_job,&view,now_ms());
        panel_gallery_tick(&gallery,&view,now_ms());
        panel_follow_observe(&view.image,&status,now_ms());
        panel_follow_schedule(&view.image,now_ms());
        panel_image_tick(&image_job,&view.image,now_ms());
        panel_power_tick(&power_job,&view,now_ms());
        panel_frequency_read(&status.frequency,"/run/acoustic-frequency.state",now_ms(),status.online,status.sound);
        uint64_t rendered=video_probe_ns();
        panel_render_view(pixels,&status,&temperature,swap.flips+1,back,now_ms()/1000,&view);
        perf.render_us=(video_probe_ns()-rendered)/1000;
        /* /dev/mem is uncached and publish_pixels provides the DMA ordering.
         * Full volatile readback was a diagnostic, not a display fence: it
         * issued 921600 independent loads per refresh (~120ms on the board).
         * Verify each slot's first write; retain opt-in full-readback A/B. */
        int verify=verify_always||!(verified_slots&(1u<<back));
        if(swap.pending || back==swap.front || copy_slot(memory,back,pixels,&perf,verify)) {
            fprintf(stderr,"FREE-buffer invariant/readback failure\n"); goto finish;
        }
        verified_slots|=1u<<back;
        uint64_t submitted=video_probe_ns();
        if(panel_submit(&io,&swap,back) || retire(&io,&swap)) {
            if(stopping) rc=0;
            goto finish;
        }
        perf.retire_us=(video_probe_ns()-submitted)/1000;
        perf.loop_us=(video_probe_ns()-loop_begin)/1000;
        panel_perf_tick(&perf,now_ms());
        status_file(&swap,&status,&temperature,&view,&touch,&perf,1);
        if(swap.flips%25==0) {
            printf("PANEL_FLIP count=%llu front=%c addr=%08x events=%llu sound=%s position=%s valid=%d temp_valid=%d temp_mc=%d level=%d\n",
                (unsigned long long)swap.flips,'A'+swap.front,panel_address(swap.front),
                (unsigned long long)swap.completions,status.sound,status.position,status.valid,
                temperature.valid,temperature.millidegrees,panel_temp_level(&temperature)); fflush(stdout);
            printf("PANEL_TIMING render_us=%llu write_us=%llu verify_us=%llu retire_us=%llu loop_us=%llu vpp_known=%d vpp_rate_millihz=%u gallery=%d busy=%d detail=%d\n",
                (unsigned long long)perf.render_us,(unsigned long long)perf.write_us,(unsigned long long)perf.verify_us,
                (unsigned long long)perf.retire_us,(unsigned long long)perf.loop_us,perf.vpp_known,perf.rate.millihz,
                view.gallery_open,view.gallery_busy,view.gallery_detail);fflush(stdout);
        }
        next+=PANEL_PERIOD_MS;
        if(next<now_ms()) next=now_ms();
        while(!stopping && now_ms()<next) {
            panel_touch_tick(&touch,&view,now_ms());
            panel_capture_tick(&capture_job,&view,now_ms());usleep(10000);
            panel_gallery_tick(&gallery,&view,now_ms());
            panel_image_tick(&image_job,&view.image,now_ms());
            panel_power_tick(&power_job,&view,now_ms());
        }
    }
    rc=0;
finish:
    panel_capture_stop(&capture_job);
    panel_gallery_stop(&gallery);
    panel_image_stop(&image_job);
    if(dirty) {
        if(mix_ui_drain(&io)) {fprintf(stderr,"PANEL_RESTORE_FAILED: immutable buffers retained, no hard reset\n"); rc=5;}
        else {
            wr(regs,MIX_IER,0);
            uint32_t pending=rd(regs,MIX_ISR)&3; if(pending) wr(regs,MIX_ISR,pending);
            mix_ui_restore_idle(&io,saved); wr(regs,MIX_IER,saved_ier); wr(regs,MIX_GIE,saved_gie);
            int mismatch=rd(regs,MIX_LAYERS)!=1 || rd(regs,MIX_IER)!=saved_ier || rd(regs,MIX_GIE)!=saved_gie;
            for(unsigned i=0;i<MIX_UI_REG_COUNT;i++) mismatch|=rd(regs,mix_ui_offsets[i])!=saved[i];
            if(mismatch) {fprintf(stderr,"PANEL_RESTORE_READBACK_FAILED\n"); rc=5;}
            else printf("PANEL_RESTORED flips=%llu events=%llu primary-only\n",
                (unsigned long long)swap.flips,(unsigned long long)swap.completions);
        }
        status_file(&swap,&status,&temperature,&view,&touch,&perf,0);
    }
    if(rc) fprintf(stderr,"PANEL_EXIT rc=%d errno=%d\n",rc,errno);
    free(pixels);
    panel_perf_close(&perf);
    panel_touch_close(&touch,&view);
    if(memory!=MAP_FAILED) munmap(memory,PANEL_MAP_BYTES);
    if(regs!=MAP_FAILED) munmap(regs,VIDEO_MIXER_MAP_SIZE);
    if(mem>=0) close(mem);
    if(fd>=0) close(fd);
    if(lock>=0) close(lock);
    return rc;
}

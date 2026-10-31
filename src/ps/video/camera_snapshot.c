#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "camera_snapshot_core.h"
#include "camera_album_core.h"
#include "video_status_panel.h"
#include "video_status_temp.h"

static volatile sig_atomic_t cancelled;
static void cancel(int sig){(void)sig;cancelled=1;}
static uint64_t mono_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static uint32_t reg_read(const void *p,unsigned off) {
    uint32_t v=((const volatile uint32_t *)p)[off/4];__sync_synchronize();return v;
}
static int text_file(const char *path,char *data,size_t size) {
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    struct stat st;ssize_t n=-1;
    if(!fstat(fd,&st) && S_ISREG(st.st_mode))n=read(fd,data,size-1);
    close(fd);if(n<0 || (size_t)n>=size-1)return -1;
    data[n]=0;return (int)n;
}
static int camera_uio(char path[512]) {
    DIR *d=opendir("/sys/class/uio");if(!d)return -1;
    struct dirent *e;unsigned found=0;
    while((e=readdir(d))) {
        if(strncmp(e->d_name,"uio",3) || !e->d_name[3])continue;
        int digits=1;for(const char *p=e->d_name+3;*p;p++)if(*p<'0'||*p>'9')digits=0;
        if(!digits)continue;
        char key[512],value[128];unsigned long long address,size;char tail;
        snprintf(key,sizeof(key),"/sys/class/uio/%s/name",e->d_name);
        if(text_file(key,value,sizeof(value))<0)continue;
        value[strcspn(value,"\r\n")]=0;if(strcmp(value,"dma@44a00000"))continue;
        snprintf(key,sizeof(key),"/sys/class/uio/%s/maps/map0/addr",e->d_name);
        if(text_file(key,value,sizeof(value))<0 || sscanf(value,"%llx %c",&address,&tail)!=1 || address!=0x44a00000)continue;
        snprintf(key,sizeof(key),"/sys/class/uio/%s/maps/map0/size",e->d_name);
        if(text_file(key,value,sizeof(value))<0 || sscanf(value,"%llx %c",&size,&tail)!=1 || size<0x10000)continue;
        snprintf(path,512,"/dev/%s",e->d_name);found++;
    }
    closedir(d);return found==1?0:-1;
}
static int reserved_memory(void) {
    unsigned char reg[8];const char *p="/sys/firmware/devicetree/base/reserved-memory/video-framebuffer@1000000/";
    char path[256];snprintf(path,sizeof(path),"%sreg",p);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    int ok=read(fd,reg,8)==8 && !memcmp(reg,"\x01\x00\x00\x00\x03\x00\x00\x00",8);close(fd);
    snprintf(path,sizeof(path),"%sno-map",p);return ok && access(path,F_OK)==0;
}
struct snapshot_trace {
    const void *regs;
    uint64_t begin_ns,bytes_read,max_gap_ns;
    unsigned attempts,progress,wait_gaps,guard_gaps,overwrites,mismatches,bad_observations,geometry_changes;
    int system_errno;
    const char *last_reject;
    struct snap_observation last;
};
static struct snapshot_trace trace;
static const char *geometry_error(const void *r) {
    uint32_t status=reg_read(r,0x34);
    if(status&VIDEO_VDMA_HALTED)return "vdma_halted";
    if(status&VIDEO_VDMA_S2MM_ERRORS)return "vdma_error";
    if((reg_read(r,0x30)&7u)!=3u)return "vdma_control";
    if(reg_read(r,0xa0)!=SNAP_HEIGHT || reg_read(r,0xa4)!=SNAP_STRIDE)return "frame_geometry";
    if((reg_read(r,0xa8)&0xffffffu)!=SNAP_STRIDE)return "frame_stride";
    if(reg_read(r,0x44)!=0)return "frame_register_bank";
    for(unsigned i=0;i<3;i++)if(reg_read(r,0xac+4*i)!=SNAP_BASE+i*SNAP_BYTES)return "frame_addresses";
    return NULL;
}
static int geometry(const void *r) {return geometry_error(r)==NULL;}
static struct snap_observation observe(const void *r) {
    struct snap_observation o;
    o.control=reg_read(r,0x30);o.status=reg_read(r,0x34);
    /* This generated core returns zero at optional FRMPTR_STS 0x24.
     * PARKPTR WrFrmStore is live while clean; it latches on errors, so every
     * guard must also reject ALL S2MM errors, never clear them here. */
    o.writer=(reg_read(r,0x28)>>24)&31u;o.ns=mono_ns();trace.last=o;return o;
}
static int healthy(struct snap_observation o) {
    return o.writer<3 && !(o.status&(VIDEO_VDMA_S2MM_ERRORS|VIDEO_VDMA_HALTED)) && (o.control&7u)==3u;
}
/* A lightweight liveness check, not buffer ownership: no camera DDR reads,
 * no interrupt consumption, no W1C writes. Two clean changes are required. */
static const char *check_progress(const void *r) {
    struct snap_observation previous=observe(r);
    uint64_t deadline=previous.ns+SNAP_CHECK_PROGRESS_NS;
    if(!healthy(previous))return "vdma_not_healthy";
    unsigned changes=0;
    while(!cancelled) {
        usleep(1000);
        struct snap_observation current=observe(r);
        if(!healthy(current))return current.status&VIDEO_VDMA_S2MM_ERRORS?"vdma_error":"vdma_not_healthy";
        if(current.ns<previous.ns)return "clock_reversed";
        if(current.ns>=deadline)return "progress_timeout";
        if(current.writer!=previous.writer) {
            changes++;trace.progress++;
            if(changes>=2)return geometry_error(r);
        }
        previous=current;
    }
    return "cancelled";
}
static int guarded_observation(struct snap_guard *guard,const void *r) {
    int ok=snap_guard_step(guard,observe(r));
    if(guard->max_gap_ns>trace.max_gap_ns)trace.max_gap_ns=guard->max_gap_ns;
    if(!ok) {
        if(guard->reject==SNAP_GAP){trace.guard_gaps++;trace.last_reject="frame_guard_gap";}
        else if(guard->reject==SNAP_OVERWRITE){trace.overwrites++;trace.last_reject="frame_overwrite";}
        else {trace.bad_observations++;trace.last_reject="vdma_not_healthy";}
    }
    return ok;
}
struct image_info {
    unsigned index,attempts;uint64_t completed_observed_ns,start_ns,end_ns;
    struct snap_guard guard;uint32_t status;
};
/* Read-only sampling, no VDMA stop/park/reset/status-clear or interrupt reads.
 * Wait for live progress, then copy the previously working slot. Per-chunk
 * pointer/error/time guards and a second guarded byte-for-byte read reject
 * overwrite, excessive preemption and unstable DMA tails. No hardware fence:
 * provenance remains guarded_best_effort, not frame-atomic. */
static int frame_copy(const void *r,const void *memory,unsigned char *pixels,struct image_info *info) {
    uint64_t deadline=mono_ns()+1800000000ull;
    unsigned attempts=0;
    while(!cancelled && mono_ns()<deadline) {
        struct snap_observation old=observe(r);if(!healthy(old)){trace.bad_observations++;return -2;}
        struct snap_observation cur;
        do {
            usleep(50);cur=observe(r);
            if(!healthy(cur) || cancelled){trace.bad_observations++;return -2;}
            if(cur.ns>=deadline)return -3;
            uint64_t gap=cur.ns-old.ns;
            if(gap>trace.max_gap_ns)trace.max_gap_ns=gap;
            if(gap>=SNAP_MAX_GAP_NS){trace.wait_gaps++;old=cur;continue;}
            if(cur.writer!=old.writer){trace.progress++;break;}
            old=cur;
        } while(1);
        attempts++;trace.attempts=attempts;
        struct snap_guard guard={.target=old.writer,.last_ns=cur.ns};
        const unsigned char *src=(const unsigned char *)memory+old.writer*SNAP_BYTES;
        _Alignas(16) unsigned char verify[SNAP_CHUNK];int ok=1;
        uint64_t start=mono_ns();
        for(unsigned pass=0;pass<2 && ok;pass++) {
            for(unsigned off=0;off<SNAP_BYTES && ok;off+=SNAP_CHUNK) {
                if(cancelled || !guarded_observation(&guard,r)){ok=0;break;}
                unsigned bytes=SNAP_BYTES-off;if(bytes>SNAP_CHUNK)bytes=SNAP_CHUNK;
                /* Aligned libc block reads verified on this ARM/devmem map:
                 * ~17ms/frame instead of ~59ms scalar volatile reads.
                 * Full barriers prohibit reusing the first-pass read. */
                __sync_synchronize();
                if(!pass)memcpy(pixels+off,src+off,bytes);
                else {memcpy(verify,src+off,bytes);if(memcmp(verify,pixels+off,bytes)) {
                    trace.mismatches++;trace.last_reject="frame_mismatch";ok=0;
                }}
                trace.bytes_read+=bytes;
                __sync_synchronize();
                if(!guarded_observation(&guard,r))ok=0;
            }
        }
        uint64_t end=mono_ns();
        if(ok && geometry(r)) {
            *info=(struct image_info){old.writer,attempts,cur.ns,start,end,guard,reg_read(r,0x34)};
            return 0;
        }
        if(!healthy(trace.last))return -2;
        if(ok){trace.geometry_changes++;trace.last_reject="frame_geometry_changed";}
    }
    return -3;
}
static int daemon_status(char *data,size_t size) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/run/pl-control.sock");
    int rc=-1;size_t used=0;
    if(connect(fd,(struct sockaddr *)&a,sizeof(a)) && errno!=EINPROGRESS)goto done;
    struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t n=sizeof(error);
    if(poll(&p,1,20)<=0 || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n) || error || send(fd,"status\n",7,MSG_NOSIGNAL)!=7)goto done;
    uint64_t end=mono_ns()+60000000ull;p.events=POLLIN;
    while(mono_ns()<end && used<size-1 && !cancelled) {
        if(poll(&p,1,5)<=0)continue;
        ssize_t got=recv(fd,data+used,size-1-used,0);if(got<=0)break;
        used+=(size_t)got;if(memchr(data,'\n',used)){rc=(int)used;break;}
    }
done:close(fd);data[used]=0;return rc;
}
static int directory_fd(const char *path,int create) {
    if(create && mkdir(path,0755) && errno!=EEXIST)return -1;
    return open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
}
static int root_fd(void) {
    const char *parents[]={"/var/lib/acoustic-camera",SNAP_ROOT};
    for(unsigned i=0;i<2;i++){int fd=directory_fd(parents[i],1);if(fd<0)return -1;close(fd);}
    int fd=directory_fd(SNAP_ROOT,0);struct statfs filesystem;
    if(fd>=0 && (fstatfs(fd,&filesystem) || filesystem.f_type!=0xef53)) {
        close(fd);errno=ENODEV;return -1;
    }
    return fd; /* Require persistent EXT4, refuse initramfs fallback. */
}
static unsigned count_shots(int root) {
    int fd=openat(root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;
    DIR *d=fdopendir(fd);if(!d){close(fd);return 0;}
    unsigned n=0;struct dirent *e;
    while((e=readdir(d)))if(!strncmp(e->d_name,"shot-",5)) {
        int dir=openat(root,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat a,b;
        if(dir>=0) {
            if(!fstatat(dir,"camera.bmp",&a,AT_SYMLINK_NOFOLLOW) && S_ISREG(a.st_mode) && a.st_size==54+SNAP_BYTES &&
               !fstatat(dir,"measurement.json",&b,AT_SYMLINK_NOFOLLOW) && S_ISREG(b.st_mode) && b.st_size>0)n++;
            close(dir);
        }
    }
    closedir(d);return n;
}
static int file_sync_close(FILE *f) {
    int ok=!ferror(f) && !fflush(f) && !fsync(fileno(f));
    if(fclose(f))ok=0;
    return ok;
}
static FILE *new_file(int dir,const char *name) {
    int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0644);
    if(fd<0)return NULL;
    FILE *f=fdopen(fd,"wb");if(!f)close(fd);return f;
}
static void number_or_null(FILE *f,int valid,int value) {if(valid)fprintf(f,"%d",value);else fputs("null",f);}
static int measurement(int dir,const char *id,uint64_t trigger,const struct image_info *image,const unsigned char *pixels) {
    uint64_t begin=mono_ns();char raw[4096],fft[2048],boot[80];struct panel_status s;panel_unavailable(&s);
    int n=daemon_status(raw,sizeof(raw));if(n>0)panel_parse_status(raw,(size_t)n,&s);
    int fn=text_file("/run/acoustic-frequency.state",fft,sizeof(fft));
    uint64_t read_ns=mono_ns();
    if(fn>0)panel_frequency_parse(fft,(size_t)fn,read_ns/1000000,&s.frequency);
    panel_frequency_gate(&s.frequency,s.online,s.sound);
    struct panel_temperature t={0};panel_temp_sample(&t,"/sys/bus/iio/devices",read_ns/1000000);
    if(text_file("/proc/sys/kernel/random/boot_id",boot,sizeof(boot))<0)strcpy(boot,"unavailable");
    boot[strcspn(boot,"\r\n")]=0;
    struct timespec utc;clock_gettime(CLOCK_REALTIME,&utc);struct tm tm;gmtime_r(&utc.tv_sec,&tm);
    char date[40];strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%SZ",&tm);
    FILE *f=new_file(dir,"measurement.json");if(!f)return 0;
    fprintf(f,"{\n\"schema_version\":1,\"producer\":\"%s\",\"capture_id\":",SNAP_VERSION);snap_json_string(f,id);
    fputs(",\"boot_id\":",f);snap_json_string(f,boot);fputs(",\"wallclock_utc\":",f);snap_json_string(f,date);
    fprintf(f,",\"wallclock_verified\":false,\n\"association\":{\"kind\":\"same_request_best_effort\",\"same_hardware_frame\":false,"
        "\"trigger_monotonic_ns\":%llu,\"frame_progress_observed_ns\":%llu,\"copy_start_ns\":%llu,\"copy_end_ns\":%llu,"
        "\"measurement_begin_ns\":%llu,\"measurement_read_ns\":%llu,\"measurement_end_ns\":%llu,\"read_after_copy_ms\":%.3f},\n",
        (unsigned long long)trigger,(unsigned long long)image->completed_observed_ns,(unsigned long long)image->start_ns,
        (unsigned long long)image->end_ns,(unsigned long long)begin,(unsigned long long)read_ns,(unsigned long long)mono_ns(),
        (read_ns-image->end_ns)/1000000.0);
    fprintf(f,"\"image\":{\"file\":\"camera.bmp\",\"format\":\"BMP_BGR24_TOP_DOWN\",\"width\":%u,\"height\":%u,\"stride\":%u,"
        "\"source\":\"camera_vdma_s2mm_before_preprocess_heatmap_ui\",\"framebuffer\":\"0x%08x\",\"frame_index\":%u,"
        "\"integrity\":\"two_pass_pointer_time_guarded_best_effort\",\"frame_atomic\":false,\"pixel_crc32\":\"%08x\","
        "\"attempts\":%u,\"guard_polls\":%u,\"guard_max_gap_ns\":%llu,\"guard_limit_ns\":%llu,\"s2mm_status\":\"0x%08x\"},\n",
        SNAP_WIDTH,SNAP_HEIGHT,SNAP_STRIDE,SNAP_BASE+image->index*SNAP_BYTES,image->index,snap_crc32(pixels,SNAP_BYTES),
        image->attempts,image->guard.polls,(unsigned long long)image->guard.max_gap_ns,SNAP_MAX_GAP_NS,image->status);
    fprintf(f,"\"measurement\":{\"daemon_usable\":%s,\"sound_state\":\"%s\",\"position_state\":\"%s\","
        "\"position_valid\":%s,\"grid\":{\"width\":64,\"height\":36,\"raw_col\":",s.online?"true":"false",s.sound,s.position,s.valid?"true":"false");
    number_or_null(f,s.raw_valid,s.raw_col);fputs(",\"raw_row\":",f);number_or_null(f,s.raw_valid,s.raw_row);
    fputs(",\"stable_col\":",f);number_or_null(f,s.valid,s.col);fputs(",\"stable_row\":",f);number_or_null(f,s.valid,s.row);
    fputs("},\"grid_energy_peak\":",f);if(s.online)fprintf(f,"%u",s.peak);else fputs("null",f);
    fprintf(f,",\"grid_energy_units\":\"uncalibrated_counts_not_SPL\",\"observation_age_ms\":%u,\"acoustic_control_known\":%s,"
        "\"acoustic_enabled\":%s,\"enable_applied\":%s,\"peak_frequency_hz\":",s.age_ms,s.acoustic_known?"true":"false",
        s.acoustic_enabled?"true":"false",s.enable_applied?"true":"false");
    if(s.frequency.valid)fprintf(f,"%.2f",s.frequency.hz);else fputs("null",f);
    fprintf(f,",\"frequency_source\":\"single_mic_hpf_output\",\"hotspot_associated\":false,\"frequency_channel\":%u,"
        "\"frequency_sequence\":%u,\"frequency_sampled_ms\":%llu,\"frequency_fresh\":%s,\"frequency_reason\":",
        s.frequency.channel,s.frequency.sequence,(unsigned long long)s.frequency.sampled_ms,s.frequency.fresh?"true":"false");
    snap_json_string(f,s.frequency.reason?s.frequency.reason:"unavailable");
    fputs(",\"temperature_millicelsius\":",f);number_or_null(f,t.valid,t.millidegrees);
    fputs(",\"temperature_source\":\"xadc\",\"raw_signal_peak\":null,\"raw_signal_source\":\"not_connected\"},\n\"diagnostics\":{\"daemon_reply\":",f);
    if(n>0)snap_json_string(f,raw);else fputs("null",f);
    fputs(",\"fft_reply\":",f);if(fn>0)snap_json_string(f,fft);else fputs("null",f);
    fputs("}}\n",f);return file_sync_close(f);
}
static void receipt(int success,const char *id,const char *reason,unsigned count) {
    uint64_t now=mono_ns();
    printf("{\"version\":\"%s\",\"saved\":%s,\"capture_id\":\"%s\",\"reason\":\"%s\",\"count\":%u,"
        "\"ready\":%s,\"checked_monotonic_ns\":%llu,\"s2mm_status\":",
        SNAP_VERSION,success?"true":"false",id?id:"",reason,count,
        !strcmp(reason,"ready")?"true":"false",(unsigned long long)now);
    if(trace.regs) {
        trace.last=observe(trace.regs);
        printf("%u,\"s2mm_errors\":%u",trace.last.status,video_vdma_errors(trace.last.status,1));
    } else fputs("null,\"s2mm_errors\":null",stdout);
    printf(",\"diagnostics\":{\"errno\":%d,\"elapsed_us\":%llu,\"progress_changes\":%u,"
        "\"attempts\":%u,\"bytes_read\":%llu,\"max_gap_ns\":%llu,\"wait_gaps\":%u,"
        "\"guard_gaps\":%u,\"overwrites\":%u,\"mismatches\":%u,\"bad_observations\":%u,\"geometry_changes\":%u",
        trace.system_errno,(unsigned long long)((now-trace.begin_ns)/1000),trace.progress,trace.attempts,
        (unsigned long long)trace.bytes_read,(unsigned long long)trace.max_gap_ns,trace.wait_gaps,trace.guard_gaps,
        trace.overwrites,trace.mismatches,trace.bad_observations,trace.geometry_changes);
    if(trace.regs)printf(",\"control\":%u,\"park\":%u,\"writer\":%u,\"vsize\":%u,\"hsize\":%u,\"stride\":%u",
        trace.last.control,reg_read(trace.regs,0x28),trace.last.writer,reg_read(trace.regs,0xa0),
        reg_read(trace.regs,0xa4),reg_read(trace.regs,0xa8));
    puts("}}");fflush(stdout);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")){puts(SNAP_VERSION);return 0;}
    int check=argc==2 && !strcmp(argv[1],"--check"),capture=argc==2 && !strcmp(argv[1],"--capture");
    if(argc!=2 || (!check && !capture && strcmp(argv[1],"--count")))return 2;
    trace=(struct snapshot_trace){.begin_ns=mono_ns()};
    int root=root_fd();if(root<0){trace.system_errno=errno;receipt(0,NULL,"storage",0);return 3;}
    unsigned count=count_shots(root);
    if(!strcmp(argv[1],"--count")){receipt(0,NULL,"inventory",count);close(root);return 0;}
    uint64_t trigger=mono_ns();int rc=3,lock=-1,uio=-1,mem=-1,dir=-1;
    void *r=MAP_FAILED,*memory=MAP_FAILED;unsigned char *pixels=NULL;char pending[PATH_MAX]={0},id[128]={0};
    const char *reason="preflight";char path[512];
    struct sigaction sa={0};sa.sa_handler=cancel;sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);sigaction(SIGHUP,&sa,NULL);
    lock=open("/run/camera-snapshot.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0){trace.system_errno=errno;reason="lock_open";goto done;}
    if(flock(lock,LOCK_EX|LOCK_NB)){trace.system_errno=errno;reason="busy";goto done;}
    count=count_shots(root);
    if(camera_uio(path)){reason="uio_identity";goto done;}
    if(!reserved_memory()){reason="reserved_memory";goto done;}
    uio=open(path,O_RDONLY|O_CLOEXEC);if(uio<0){trace.system_errno=errno;reason="uio_open";goto done;}
    r=mmap(NULL,0x10000,PROT_READ,MAP_SHARED,uio,0);
    if(r==MAP_FAILED){trace.system_errno=errno;reason="uio_map";goto done;}
    trace.regs=r;
    reason=geometry_error(r);if(reason)goto done;
    struct statvfs space;
    if(fstatvfs(root,&space)){trace.system_errno=errno;reason="storage_query";goto done;}
    if(count>=500 || (uint64_t)space.f_bavail*space.f_frsize<64ull*1024*1024){reason="storage_full";goto done;}
    mem=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
    if(mem<0){trace.system_errno=errno;reason="devmem_open";goto done;}
    memory=mmap(NULL,SNAP_MAP_BYTES,PROT_READ,MAP_SHARED,mem,SNAP_BASE);
    if(memory==MAP_FAILED){trace.system_errno=errno;reason="devmem_map";goto done;}
    if(check) {
        reason=check_progress(r);if(reason)goto done;
        receipt(0,NULL,"ready",count);rc=0;goto cleanup;
    }
    uint64_t sequence=album_next_sequence(root,count);
    if(!sequence){reason="sequence_state";goto done;}
    pixels=malloc(SNAP_BYTES);if(!pixels){reason="memory";goto done;}
    struct image_info image;int result=frame_copy(r,memory,pixels,&image);
    if(result || cancelled){reason=cancelled?"cancelled":result==-2?"vdma_not_healthy":
        trace.last_reject?trace.last_reject:"frame_wait_timeout";goto done;}
    char boot[80];if(text_file("/proc/sys/kernel/random/boot_id",boot,sizeof(boot))<0){reason="boot_identity";goto done;}
    for(unsigned i=0;i<8;i++)if(!((boot[i]>='0'&&boot[i]<='9')||(boot[i]>='a'&&boot[i]<='f'))){reason="boot_identity";goto done;}
    snprintf(pending,sizeof(pending),"%s/.pending-XXXXXXXX",SNAP_ROOT);
    if(!mkdtemp(pending)){pending[0]=0;reason="storage";goto done;}
    snprintf(id,sizeof(id),"shot-%.8s-%llu-%s",boot,(unsigned long long)(trigger/1000000),strrchr(pending,'-')+1);
    dir=directory_fd(pending,0);if(dir<0){reason="storage";goto done;}
    /* Read measurements immediately after image acquisition, BEFORE SD writes. */
    if(!measurement(dir,id,trigger,&image,pixels)){reason="json_write";goto done;}
    FILE *bmp=new_file(dir,"camera.bmp");if(!bmp){reason="image_write";goto done;}
    unsigned char header[54];snap_bmp_header(header);
    int ok=fwrite(header,1,sizeof(header),bmp)==sizeof(header) && fwrite(pixels,1,SNAP_BYTES,bmp)==SNAP_BYTES;
    if(!file_sync_close(bmp) || !ok){reason="image_write";goto done;}
    /* Optional album assets derive from this immutable CPU copy, never DDR.
     * Measurement acquisition is already complete, so cache work can't shift
     * its recorded near-time association. Original pair remains authoritative. */
    char seq[64];int seq_len=snprintf(seq,sizeof(seq),"%llu\n",(unsigned long long)sequence);
    if(!album_atomic_file(dir,"album.seq",seq,(size_t)seq_len,NULL,0))
        fprintf(stderr,"Optional album sequence sidecar unavailable\n");
    unsigned char *thumb=malloc(ALBUM_TBYTES);
    if(thumb) {
        album_reduce(pixels,thumb,ALBUM_TW,ALBUM_TH);
        if(!album_cache_write(dir,snap_crc32(pixels,SNAP_BYTES),thumb))fprintf(stderr,"Optional thumbnail cache unavailable\n");
        free(thumb);
    }
    if(cancelled || fsync(dir)){reason=cancelled?"cancelled":"sync";goto done;}
    /* Unique identifier; do not overwrite any pre-existing shot directory. */
    if(renameat2(root,strrchr(pending,'/')+1,root,id,RENAME_NOREPLACE)) {
        reason="publish";goto done;
    }
    pending[0]=0;
    if(fsync(root)){reason="commit_uncertain";goto done;}
    rc=0;reason="saved";count++;
done:receipt(rc==0,id,reason,count);
cleanup:
    /* Only remove this request's private known files, never historical shots. */
    if(pending[0]) {
        if(dir>=0){unlinkat(dir,"camera.bmp",0);unlinkat(dir,"measurement.json",0);
            unlinkat(dir,"thumbnail.bmp",0);unlinkat(dir,"thumbnail-v1.meta",0);unlinkat(dir,"album.seq",0);}
        rmdir(pending);
    }
    if(dir>=0)close(dir);
    free(pixels);
    if(memory!=MAP_FAILED)munmap(memory,SNAP_MAP_BYTES);
    if(r!=MAP_FAILED)munmap(r,0x10000);
    if(mem>=0)close(mem);
    if(uio>=0)close(uio);
    if(lock>=0)close(lock);
    close(root);return rc;
}

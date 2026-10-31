#define _GNU_SOURCE
#include "vpp_control_logic.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define TOOL_VERSION "20261015-video-preprocess-control-v3"
#define UIO_NAME "video_preprocess@40040000"
#define UIO_BASE 0x40040000ull
#define MAP_SIZE 0x10000u
#ifndef VPP_UIO_CLASS
#define VPP_UIO_CLASS "/sys/class/uio"
#endif
#ifndef VPP_DEVICE_DIR
#define VPP_DEVICE_DIR "/dev"
#endif
#ifndef VPP_LOCK_PATH
#define VPP_LOCK_PATH "/run/video-preprocess-control.lock"
#endif
static volatile sig_atomic_t stopping;
static void on_signal(int sig) { (void)sig; stopping = 1; }
static int stopped(void *ctx) { (void)ctx; return stopping != 0; }
static uint32_t mmio_read(void *ctx, unsigned offset) {
    __sync_synchronize();
    uint32_t value = ((volatile uint32_t *)ctx)[offset / 4];
    __sync_synchronize();
    return value;
}
static void mmio_write(void *ctx, unsigned offset, uint32_t value) {
    __sync_synchronize();
    ((volatile uint32_t *)ctx)[offset / 4] = value;
    __sync_synchronize();
}
static uint64_t milliseconds(void *ctx) {
    (void)ctx;
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(3); }
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
static void pause_poll(void *ctx) { (void)ctx; usleep(1000); }
static int decimal(const char *s, unsigned max, unsigned *value) {
    if (!s[0]) return 0;
    unsigned v = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        unsigned digit = (unsigned)(*s - '0');
        if (v > max / 10u || (v == max / 10u && digit > max % 10u)) return 0;
        v = v * 10u + digit;
    }
    *value = v;
    return 1;
}
static int signed_decimal(const char *s,unsigned limit,int32_t *value) {
    int negative=s[0]=='-';unsigned magnitude;
    if(!decimal(s+negative,limit,&magnitude))return 0;
    *value=negative?-(int32_t)magnitude:(int32_t)magnitude;
    return 1;
}
static int read_text(const char *path, char *text, size_t capacity) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int rc = fgets(text, (int)capacity, f) ? 0 : -1;
    if (!rc && !strchr(text, '\n') && !feof(f)) rc = -1;
    fclose(f);
    if (!rc) text[strcspn(text, "\r\n")] = 0;
    return rc;
}
static int sys_number(const char *s, unsigned long long *value) {
    char *end;
    if (*s < '0' || *s > '9') return 0;
    errno = 0;
    *value = strtoull(s, &end, 0);
    return !errno && end != s && !*end;
}
static int find_device(char *device, size_t capacity) {
    DIR *dir = opendir(VPP_UIO_CLASS);
    if (!dir) return -1;
    struct dirent *entry;
    unsigned found = 0;
    int invalid = 0;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "uio", 3) || !entry->d_name[3]) continue;
        const char *digits = entry->d_name + 3;
        while (*digits >= '0' && *digits <= '9') ++digits;
        if (*digits) continue;
        char path[512], text[128];
        int n = snprintf(path, sizeof(path), VPP_UIO_CLASS "/%s/name", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path) || read_text(path, text, sizeof(text)) || strcmp(text, UIO_NAME)) continue;
        ++found;
        const char *keys[] = {"addr", "size", "offset"};
        unsigned long long values[3] = {0};
        for (unsigned i = 0; i < 3; ++i) {
            n = snprintf(path, sizeof(path), VPP_UIO_CLASS "/%s/maps/map0/%s", entry->d_name, keys[i]);
            if (n < 0 || (size_t)n >= sizeof(path) || read_text(path, text, sizeof(text)) || !sys_number(text, &values[i])) invalid = 1;
        }
        if (values[0] != UIO_BASE || values[1] < MAP_SIZE || values[2]) invalid = 1;
        n = snprintf(device, capacity, VPP_DEVICE_DIR "/%s", entry->d_name);
        if (n < 0 || (size_t)n >= capacity) invalid = 1;
    }
    closedir(dir);
    if (found != 1 || invalid) { errno = found > 1 ? EEXIST : invalid ? EINVAL : ENODEV; return -1; }
    return 0;
}
static const char *mode_name(unsigned mode) {
    static const char *names[]={"bypass","gray","binary","brightness","invert","gain"};
    return mode<sizeof(names)/sizeof(*names)?names[mode]:"invalid";
}
static const char *reason(enum vpp_result rc) {
    switch (rc) {
    case VPP_OK: return "applied";
    case VPP_BAD_HW: return "hardware_identity_or_register_mismatch";
    case VPP_BUSY: return "existing_pending_configuration";
    case VPP_CHANGED: return "configuration_changed_or_readback_mismatch";
    case VPP_TIMEOUT: return "timeout_waiting_for_frame_boundary";
    case VPP_INTERRUPTED: return "interrupted";
    case VPP_INVALID_FRAME: return "invalid_frame_statistics";
    }
    return "unknown";
}
static void output_roi(const struct vpp_config *c) {
    unsigned x=c->xy0&65535u,y=c->xy0>>16,x1=c->xy1&65535u,y1=c->xy1>>16;
    printf("{\"enabled\":%s,\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u,\"x1\":%u,\"y1\":%u}",
           c->enabled?"true":"false",x,y,x1-x,y1-y,x1,y1);
}
static void output(const char *device, const struct vpp_state *s, const char *result,
                   int submitted, const struct vpp_config *request) {
    printf("{\"version\":\"%s\",\"device\":\"%s\",\"base\":\"0x40040000\","
           "\"hardware_id\":\"0x56505031\",\"protocol\":3,\"width\":1280,\"height\":720,"
           "\"result\":\"%s\",\"commit_written\":%s,\"active_mode\":\"%s\","
           "\"active_threshold\":%u,\"pending\":%s,\"pending_mode\":",
           TOOL_VERSION, device, result, submitted ? "true" : "false", mode_name(s->active.value & VPP_MODE_MASK),
           (s->active.value >> 8) & 255u, s->status & VPP_STATUS_PENDING ? "true" : "false");
    if (s->status & VPP_STATUS_PENDING) printf("\"%s\",\"pending_threshold\":%u", mode_name(s->pending.value & VPP_MODE_MASK), (s->pending.value >> 8) & 255u);
    else printf("null,\"pending_threshold\":null");
    printf(",\"seen_sof\":%s,\"pipeline_nonempty\":%s,\"frame_seq\":%" PRIu32
           ",\"pixels\":%" PRIu32 ",\"apply_seq\":%" PRIu32 ",\"requested_mode\":",
           s->status & VPP_STATUS_SOF ? "true" : "false", s->status & VPP_STATUS_PIPE ? "true" : "false",
           s->frame, s->pixels, s->apply);
    if (request) printf("\"%s\",\"requested_threshold\":%u", mode_name(request->value&VPP_MODE_MASK), (request->value>>8)&255u);
    else printf("null,\"requested_threshold\":null");
    printf(",\"active_roi\":");output_roi(&s->active);
    printf(",\"pending_roi\":");if(s->status&VPP_STATUS_PENDING)output_roi(&s->pending);else printf("null");
    printf(",\"requested_roi\":");if(request)output_roi(request);else printf("null");
    printf(",\"active_brightness\":%" PRId32 ",\"active_gain_shift\":%" PRId32,
           s->active.brightness,s->active.gain_shift);
    if(s->status&VPP_STATUS_PENDING)
        printf(",\"pending_brightness\":%" PRId32 ",\"pending_gain_shift\":%" PRId32,
               s->pending.brightness,s->pending.gain_shift);
    else printf(",\"pending_brightness\":null,\"pending_gain_shift\":null");
    if(request)printf(",\"requested_brightness\":%" PRId32 ",\"requested_gain_shift\":%" PRId32,
                      request->brightness,request->gain_shift);
    else printf(",\"requested_brightness\":null,\"requested_gain_shift\":null");
    printf(",\"statistics_published\":%s",s->published&1u?"true":"false");
    puts(",\"screen_presentation_confirmed\":false}");
}
static void output_stats(const char *device,const struct vpp_stats *s,enum vpp_result rc,int captured,int fresh) {
    int valid=captured && (s->status&VPP_STATS_VALID);
    printf("{\"version\":\"%s\",\"device\":\"%s\",\"protocol\":3,\"operation\":\"statistics\","
           "\"result\":\"%s\",\"video_config_written\":false,\"snapshot_available\":%s,"
           "\"fresh\":%s,\"frame_valid\":%s,\"usable\":%s,\"frame_seq\":",
           TOOL_VERSION,device,rc==VPP_OK?"statistics":reason(rc),captured?"true":"false",
           fresh?"true":"false",valid?"true":"false",rc==VPP_OK?"true":"false");
    if(captured) {
        printf("%" PRIu32 ",\"apply_seq\":%" PRIu32 ",\"area_pixels\":%" PRIu32
               ",\"bright_pixels\":%" PRIu32 ",\"dark_pixels\":%" PRIu32
               ",\"frame_pixels\":%" PRIu32 ",\"mode\":\"%s\",\"threshold\":%u,\"roi\":",
               s->frame,s->apply,s->area,s->bright,s->area-s->bright,s->total,
               mode_name(s->config.value&VPP_MODE_MASK),(s->config.value>>8)&255u);
        output_roi(&s->config);
        printf(",\"brightness\":%" PRId32 ",\"gain_shift\":%" PRId32,s->config.brightness,s->config.gain_shift);
    } else printf("null,\"apply_seq\":null,\"area_pixels\":null,\"bright_pixels\":null,\"dark_pixels\":null,\"frame_pixels\":null,\"mode\":null,\"threshold\":null,\"roi\":null,\"brightness\":null,\"gain_shift\":null");
    printf(",\"bright_fraction\":");
    if(rc==VPP_OK && s->area)printf("%.6f",(double)s->bright/s->area);else printf("null");
    puts(",\"statistics_basis\":\"original_grayscale_before_processing\",\"screen_presentation_confirmed\":false}");
}
static void usage(const char *program) {
    printf("%s --check | --status | --stats [--after-frame 0..4294967295] [--timeout-ms 20..5000]\n"
           "  | [--mode bypass|gray|binary|brightness|invert|gain] [--threshold 0..255]\n"
           "    [--brightness -255..255] [--gain-shift -2..2] [--roi X Y WIDTH HEIGHT | --full-frame | --clear-roi] [--timeout-ms 20..5000]\n"
           "--check: sysfs only; --status: read-only MMIO. --version, --help. Hardware protocol 3 only.\n"
           "Unspecified fields preserve ACTIVE, not uncommitted SHADOW values.\n"
           "--roi enables a valid rectangle; --full-frame disables ROI preserving mode; --clear-roi also restores bypass.\n"
           "Six modes are exclusive. Tone parameters do not auto-select mode; bypass always outputs original RGB.\n"
           "Gain shift -2/-1/0/1/2 means /4,/2,x1,x2,x4, floor right shifts, saturated left shifts.\n"
           "--stats writes snapshot bank only, waits for a newly completed matching frame; no display changes.\n"
           "Configuration commits at accepted SOF; timeout does NOT cancel a pending commit.\n", program);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts(TOOL_VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--help")) { usage(argv[0]); return 0; }
    int check = 0, status = 0, stats = 0, have_mode = 0, have_threshold = 0, have_timeout = 0;
    int have_roi=0,full_frame=0,clear_roi=0,have_after=0,have_brightness=0,have_gain_shift=0;
    int32_t brightness=0,gain_shift=0;
    unsigned roi_values[4]={0},after_frame=0;
    unsigned mode = 0, threshold = 128, timeout_ms = 1000;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check") && !check) check = 1;
        else if (!strcmp(argv[i], "--status") && !status) status = 1;
        else if (!strcmp(argv[i], "--stats") && !stats) stats = 1;
        else if (!strcmp(argv[i], "--roi") && !have_roi && !full_frame && !clear_roi && i+4<argc) {
            for(unsigned j=0;j<4;++j)if(!decimal(argv[i+1+(int)j],j%2?VPP_HEIGHT:VPP_WIDTH,&roi_values[j]))goto invalid;
            if(!roi_values[2] || !roi_values[3] || roi_values[0]>VPP_WIDTH-roi_values[2] || roi_values[1]>VPP_HEIGHT-roi_values[3])goto invalid;
            i+=4;have_roi=1;
        } else if (!strcmp(argv[i], "--full-frame") && !full_frame && !have_roi && !clear_roi)full_frame=1;
        else if (!strcmp(argv[i], "--clear-roi") && !clear_roi && !have_roi && !full_frame)clear_roi=1;
        else if (!strcmp(argv[i], "--after-frame") && !have_after && i+1<argc && decimal(argv[i+1],UINT32_MAX,&after_frame)){++i;have_after=1;}
        else if (!strcmp(argv[i], "--mode") && !have_mode && i + 1 < argc) {
            const char *s = argv[++i];
            if (!strcmp(s, "bypass")) mode = 0;
            else if (!strcmp(s, "gray")) mode = 1;
            else if (!strcmp(s, "binary")) mode = 2;
            else if (!strcmp(s, "brightness")) mode = 3;
            else if (!strcmp(s, "invert")) mode = 4;
            else if (!strcmp(s, "gain")) mode = 5;
            else goto invalid;
            have_mode = 1;
        } else if (!strcmp(argv[i], "--threshold") && !have_threshold && i + 1 < argc && decimal(argv[i + 1], 255, &threshold)) { ++i; have_threshold = 1; }
        else if (!strcmp(argv[i], "--brightness") && !have_brightness && i+1<argc && signed_decimal(argv[i+1],255,&brightness)) {++i;have_brightness=1;}
        else if (!strcmp(argv[i], "--gain-shift") && !have_gain_shift && i+1<argc && signed_decimal(argv[i+1],2,&gain_shift)) {++i;have_gain_shift=1;}
        else if (!strcmp(argv[i], "--timeout-ms") && !have_timeout && i + 1 < argc && decimal(argv[i + 1], 5000, &timeout_ms) && timeout_ms >= 20) { ++i; have_timeout = 1; }
        else goto invalid;
    }
    int changing = have_mode || have_threshold || have_roi || full_frame || clear_roi || have_brightness || have_gain_shift;
    if (check + status + stats + changing != 1 || (have_timeout && !changing && !stats) ||
        (have_after && !stats) || (clear_roi && have_mode && mode!=0)) goto invalid;
    char device[512];
    if (find_device(device, sizeof(device))) { fprintf(stderr, "Expected %s base=0x40040000 size>=64K offset=0: %s\n", UIO_NAME, strerror(errno)); return 3; }
    if (check) { printf("%s %s name=%s base=0x40040000 sysfs_only=pass\n", TOOL_VERSION, device, UIO_NAME); return 0; }
    int lock = open(VPP_LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("video control ownership lock"); if (lock >= 0) close(lock); return 3; }
    int writable=changing || stats;
    int fd = open(device, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) { perror(device); close(lock); return 3; }
    void *mapping = mmap(NULL, MAP_SIZE, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) { perror("mmap"); close(fd); close(lock); return 3; }
    struct vpp_io io = {mapping, mmio_read, mmio_write, milliseconds, pause_poll, stopped};
    struct vpp_state s = {0};
    enum vpp_result rc = vpp_identity(&io);
    if (rc == VPP_OK) rc = vpp_observe(&io, &s);
    int submitted = 0;
    struct vpp_config request=s.active;
    struct vpp_stats sample={0};int captured=0,fresh=0;
    if(changing || stats) {
        struct sigaction action={0};action.sa_handler=on_signal;sigemptyset(&action.sa_mask);
        sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);sigaction(SIGHUP,&action,NULL);
    }
    if (rc == VPP_OK && changing) {
        if (!have_mode) mode = s.active.value & VPP_MODE_MASK;
        if (!have_threshold) threshold = (s.active.value >> 8) & 255u;
        if(clear_roi)mode=0;
        request.value=(threshold<<8)|mode;
        if(have_brightness)request.brightness=brightness;
        if(have_gain_shift)request.gain_shift=gain_shift;
        if(have_roi) {
            request.xy0=(roi_values[1]<<16)|roi_values[0];
            request.xy1=((roi_values[1]+roi_values[3])<<16)|(roi_values[0]+roi_values[2]);request.enabled=1;
        } else if(full_frame || clear_roi) {
            request.xy0=0;request.xy1=(VPP_HEIGHT<<16)|VPP_WIDTH;request.enabled=0;
        }
        rc = vpp_apply(&io, &request, timeout_ms, &s, &submitted);
    }
    if(rc==VPP_OK && stats)rc=vpp_collect(&io,timeout_ms,have_after,after_frame,&sample,&captured,&fresh);
    /* Never label data as valid hardware if identity/coherency validation failed. */
    if (rc == VPP_OK || rc == VPP_BUSY || rc == VPP_TIMEOUT || rc == VPP_INTERRUPTED || rc==VPP_INVALID_FRAME) {
        if(stats)output_stats(device,&sample,rc,captured,fresh);
        else output(device, &s, changing ? reason(rc) : "status", submitted, changing?&request:NULL);
    }
    if (rc != VPP_OK) {
        fprintf(stderr, "%s; commit_written=%s. No reset/cancel/retry performed.\n", reason(rc), submitted ? "true" : "false");
        if (submitted) fprintf(stderr, "A pending commit may still apply at a later SOF. Inspect --status before another command.\n");
    }
    munmap(mapping, MAP_SIZE); close(fd); close(lock);
    return rc == VPP_OK ? 0 : rc == VPP_TIMEOUT ? 5 : rc == VPP_BUSY ? 6 : rc == VPP_INTERRUPTED ? 130 : rc==VPP_INVALID_FRAME?7:4;
invalid:
    usage(argv[0]);
    return 2;
}

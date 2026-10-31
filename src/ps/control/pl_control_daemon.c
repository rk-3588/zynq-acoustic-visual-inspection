#define _GNU_SOURCE
#include "pl_tracking.h"
#include "pl_enable.h"
#include "pl_uio.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define VERSION "20261013-pl-control-enable-v3"
#ifndef ENABLE_BOOT_MARKER
#define ENABLE_BOOT_MARKER "/run/pl-acoustic-enable.initialized"
#endif
#ifndef SOCKET_PATH
#define SOCKET_PATH "/run/pl-control.sock"
#endif
#ifndef GATE_PATH
#define GATE_PATH "/run/acoustic-display.state"
#endif
#ifndef LOCK_PATH
#define LOCK_PATH "/run/pl-control.lock"
#endif
#ifndef UIO_SYSFS
#define UIO_SYSFS "/sys/class/uio"
#endif
#ifndef UIO_DEVICES
#define UIO_DEVICES "/dev"
#endif
#define CONTROL 0x00u
#define STATUS 0x04u
#define HOTSPOT_X 0x08u
#define HOTSPOT_Y 0x0cu
#define HOTSPOT_VALUE 0x10u
#define CONFIDENCE 0x14u
#define THRESHOLD 0x18u
#define MODE 0x1cu
#define CLIENTS 8u

static volatile sig_atomic_t running=1;
static void stop_handler(int sig) {(void)sig;running=0;}
static uint64_t monotonic_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}
static uint32_t rd(volatile uint8_t *base,unsigned offset) {
    uint32_t value=*(volatile uint32_t *)(base+offset);__sync_synchronize();return value;
}
static void wr(volatile uint8_t *base,unsigned offset,uint32_t value) {
    __sync_synchronize();*(volatile uint32_t *)(base+offset)=value;__sync_synchronize();
}
struct snapshot {uint32_t control,status,x,y,peak,confidence,threshold,mode;int good;unsigned tries;};
static void read_snapshot(volatile uint8_t *base,struct snapshot *s) {
    s->good=0;
    for(unsigned attempt=1;attempt<=3;attempt++) {
        uint32_t status=rd(base,STATUS);
        s->x=rd(base,HOTSPOT_X);s->y=rd(base,HOTSPOT_Y);
        s->peak=rd(base,HOTSPOT_VALUE);s->confidence=rd(base,CONFIDENCE);
        uint32_t x=rd(base,HOTSPOT_X),y=rd(base,HOTSPOT_Y);
        s->status=rd(base,STATUS);s->tries=attempt;
        if((status&1) && (s->status&1) && !(s->status&8) &&
            pl_coordinate_valid(s->x,s->y) && x==s->x && y==s->y &&
            s->peak<=0xffffff && s->peak==s->confidence) {s->good=1;break;}
    }
    s->control=rd(base,CONTROL);s->threshold=rd(base,THRESHOLD);s->mode=rd(base,MODE);
}
struct gate_reader {
    struct stat previous;
    int seen,updated;
    uint64_t last_update;
    enum pl_gate gate;
    const char *health;
    struct pl_display_ack ack;
};
static int same_file(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec;
}
static enum pl_gate read_gate(struct gate_reader *g,uint64_t now) {
    memset(&g->ack,0,sizeof(g->ack));
    int fd=open(GATE_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0) {memset(g,0,sizeof(*g));g->health="missing";return PL_GATE_UNKNOWN;}
    char text[4096];struct stat info;ssize_t count;
    if(fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_size<=0 || info.st_size>=(off_t)sizeof(text)) {
        close(fd);g->gate=PL_GATE_UNKNOWN;g->health="invalid";return PL_GATE_UNKNOWN;
    }
    count=read(fd,text,sizeof(text));close(fd);
    enum pl_gate gate=PL_GATE_UNKNOWN;
    if(count!=info.st_size || !pl_parse_gate(text,(size_t)count,&gate) ||
        !pl_enable_parse(text,(size_t)count,&g->ack)) {
        g->gate=PL_GATE_UNKNOWN;g->health="invalid";return PL_GATE_UNKNOWN;
    }
    if(g->ack.observed_ms>now || now-g->ack.observed_ms>=PL_SOURCE_TIMEOUT_MS) {
        g->gate=PL_GATE_UNKNOWN;g->health="stale_observation";return PL_GATE_UNKNOWN;
    }
    if(!g->seen) {
        g->seen=1;g->previous=info;g->gate=gate;g->health="awaiting_update";
        return PL_GATE_UNKNOWN; /* An old active file must not activate a restarted daemon. */
    }
    if(!same_file(&g->previous,&info)) {
        g->previous=info;g->last_update=now;g->updated=1;
    }
    g->gate=gate;
    if(!g->updated) {g->health="awaiting_update";return PL_GATE_UNKNOWN;}
    if(now-g->last_update>=PL_SOURCE_TIMEOUT_MS) {g->health="stale";return PL_GATE_UNKNOWN;}
    g->health=gate==PL_GATE_UNKNOWN?"stopped":"fresh";return gate;
}
struct runtime {
    struct snapshot snapshot;
    struct gate_reader source;
    struct pl_tracker tracker;
    enum pl_gate gate;
    uint64_t observed_ms;
    unsigned rejected;
    int enabled,enable_applied;
    uint64_t enable_changed_ms;
    char device[512];
};
static void sample(volatile uint8_t *base,struct runtime *r,uint64_t now) {
    read_snapshot(base,&r->snapshot);r->gate=read_gate(&r->source,now);
    int enabled=(r->snapshot.control&1u)!=0;
    if(enabled!=r->enabled) {
        r->enabled=enabled;r->enable_changed_ms=now;
        pl_tracker_clear(&r->tracker,enabled?"enable_pending":"disabled");
    }
    r->enable_applied=pl_enable_confirmed(r->enabled,r->enable_changed_ms,now,
        r->source.health && !strcmp(r->source.health,"fresh"),&r->source.ack);
    if(!r->snapshot.good)r->rejected++;
    pl_enable_tracker_step(&r->tracker,now,r->enabled,r->enable_applied,
        r->gate,r->snapshot.good,r->snapshot.x,r->snapshot.y);
    r->observed_ms=now;
}
static int reply(int fd,const char *s) {
    size_t n=strlen(s);return send(fd,s,n,MSG_NOSIGNAL|MSG_DONTWAIT)==(ssize_t)n?0:-1;
}
static int status_json(int fd,const struct runtime *r) {
    const struct snapshot *s=&r->snapshot;const struct pl_tracker *t=&r->tracker;
    uint64_t now=monotonic_ms();int expired=now-r->observed_ms>PL_PERIOD_MS*3;
    int applied=!expired && r->enable_applied && pl_enable_confirmed(r->enabled,r->enable_changed_ms,now,
        r->source.health && !strcmp(r->source.health,"fresh"),&r->source.ack);
    int usable=r->enabled && applied;
    char out[3072],col[24]="null",row[24]="null",rawx[24]="null",rawy[24]="null",age[32]="null";
    if(t->have_position && !expired && usable) {snprintf(col,sizeof(col),"%d",t->col);snprintf(row,sizeof(row),"%d",t->row);}
    /* Register coordinates are odd cell centres in the legacy 128x72 ABI;
     * raw_x/raw_y are the UI's 0-based 64x36 grid, like filtered_col/row. */
    if(usable && s->good) {snprintf(rawx,sizeof(rawx),"%u",(s->x-1)/2);snprintf(rawy,sizeof(rawy),"%u",(s->y-1)/2);}
    if(r->source.updated)snprintf(age,sizeof(age),"%llu",(unsigned long long)(now-r->source.last_update));
    const char *gate=expired?"unknown":r->gate==PL_GATE_ACTIVE?"active":r->gate==PL_GATE_QUIET?"quiet":"unknown";
    int n=snprintf(out,sizeof(out),
        "{\"control\":%u,\"status\":%u,\"hotspot_x\":%u,\"hotspot_y\":%u,"
        "\"hotspot_value\":%u,\"confidence\":%u,\"threshold\":%u,\"mode\":%u,"
        "\"version\":\"%s\",\"uio_device\":\"%s\",\"uio_address\":\"0x44a20000\","
        "\"sound_state\":\"%s\",\"sound_present\":%s,\"gate_source\":\"acoustic_display_control\","
        "\"gate_health\":\"%s\",\"gate_age_ms\":%s,\"position_state\":\"%s\","
        "\"position_valid\":%s,\"raw_x\":%s,\"raw_y\":%s,\"filtered_col\":%s,\"filtered_row\":%s,"
        "\"stable_ms\":%llu,\"window_samples\":%u,\"window_inliers\":%u,"
        "\"read_quality\":\"%s\",\"read_attempts\":%u,\"rejected_samples\":%u,"
        "\"observation_age_ms\":%llu,\"snapshot_atomic\":false,\"pl_freshness_verified\":false,"
        "\"confidence_kind\":\"legacy_peak_copy\",\"control_enable_effect\":\"linux_overlay_and_position\","
        "\"acoustic_enabled\":%s,\"enable_applied\":%s,\"enable_changed_ms\":%llu,"
        "\"display_enable_observed_ms\":%llu,\"effective_alpha\":%u,\"pl_compute_paused\":false,\"raw_coordinate_space\":\"grid64x36\"}\n",
        s->control,s->status,s->x,s->y,s->peak,s->confidence,s->threshold,s->mode,
        VERSION,r->device,gate,!strcmp(gate,"unknown")?"null":!strcmp(gate,"active")?"true":"false",
        expired?"sampling_stale":r->source.health,age,expired?"unavailable":!r->enabled?"disabled":!applied?"enable_pending":t->state,
        t->valid && !expired && usable?"true":"false",rawx,rawy,col,row,
        (unsigned long long)(expired?0:t->stable_ms),t->count,t->inliers,
        expired?"stale":!r->enabled?"disabled":!applied?"enable_pending":s->good?"best_effort":"suspect",s->tries,r->rejected,
        (unsigned long long)(now-r->observed_ms),r->enabled?"true":"false",applied?"true":"false",
        (unsigned long long)r->enable_changed_ms,(unsigned long long)r->source.ack.observed_ms,r->source.ack.alpha);
    return n>0 && (size_t)n<sizeof(out)?reply(fd,out):-1;
}
static int parse_u32(const char *s,uint32_t *value) {
    char *end;errno=0;unsigned long v=strtoul(s,&end,0);
    if(errno || end==s || *end || *s=='-' || v>UINT32_MAX)return -1;
    *value=(uint32_t)v;return 0;
}
static void handle_command(int fd,char *buf,volatile uint8_t *base,struct runtime *r) {
    buf[strcspn(buf,"\r\n")]=0;
    if(!strcmp(buf,"status"))status_json(fd,r);
    else if(!strcmp(buf,"version"))reply(fd,VERSION "\n");
    else if(!strcmp(buf,"enable") || !strcmp(buf,"disable")) {
        uint32_t v=rd(base,CONTROL);
        int enabled=!strcmp(buf,"enable");
        v=enabled?v|1u:v&~1u;wr(base,CONTROL,v);
        if(rd(base,CONTROL)!=v){reply(fd,"ERR control readback\n");return;}
        r->snapshot.control=v;
        if(enabled!=r->enabled) {
            r->enabled=enabled;r->enable_changed_ms=monotonic_ms();r->enable_applied=0;
            pl_tracker_clear(&r->tracker,enabled?"enable_pending":"disabled");
        }
        reply(fd,"OK requested; query status enable_applied\n");
    } else if(!strncmp(buf,"threshold ",10) || !strncmp(buf,"mode ",5)) {
        int threshold=buf[0]=='t';uint32_t v;
        if(parse_u32(buf+(threshold?10:5),&v))reply(fd,"ERR invalid value\n");
        else {
            wr(base,threshold?THRESHOLD:MODE,v);
            if(threshold)r->snapshot.threshold=v;else r->snapshot.mode=v;
            reply(fd,"OK\n"); /* Legacy whole-word mode semantics; bit16 controls HPF bypass. */
        }
    } else if(!strcmp(buf,"help"))reply(fd,"status|enable|disable|threshold N|mode N|version|help\n");
    else reply(fd,"ERR unknown command\n");
}
static int initialize_enable(volatile uint8_t *base) {
    int fd=open(ENABLE_BOOT_MARKER,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd>=0) {
        struct stat st;int valid=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==geteuid();
        close(fd);return valid?0:-1;
    }
    if(errno!=ENOENT)return -1;
    const char *temp=ENABLE_BOOT_MARKER ".new";
    fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    int ok=write(fd,"default_on\n",11)==11;
    if(close(fd))ok=0;
    uint32_t control=rd(base,CONTROL)|1u;
    if(ok){wr(base,CONTROL,control);ok=rd(base,CONTROL)==control;}
    if(!ok || rename(temp,ENABLE_BOOT_MARKER)){unlink(temp);return -1;}
    fprintf(stderr,"ENABLE first boot initialization: default ON, other control bits preserved\n");return 0;
}
int main(int argc,char **argv) {
    int check=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--version")) {puts(VERSION);return 0;}
        if(!strcmp(argv[i],"--check-uio"))check=1;
        else if(strcmp(argv[i],"-d")) {fprintf(stderr,"Usage: %s [-d|--version|--check-uio]\n",argv[0]);return 2;}
    }
    struct runtime r={0};
    if(check) {
        if(pl_find_uio(UIO_SYSFS,UIO_DEVICES,r.device,sizeof(r.device))) {perror("acoustic UIO identity");return 1;}
        printf("%s %s address=0x44a20000 size>=0x10000\n",r.device,PL_UIO_NAME);return 0;
    }
    signal(SIGINT,stop_handler);signal(SIGTERM,stop_handler);signal(SIGPIPE,SIG_IGN);
    int lock=open(LOCK_PATH,O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)) {perror("daemon lock");if(lock>=0)close(lock);return 1;}
    int found=0;
    for(unsigned i=0;i<50 && running;i++) {
        if(!pl_find_uio(UIO_SYSFS,UIO_DEVICES,r.device,sizeof(r.device))) {found=1;break;}
        struct timespec delay={0,100000000};nanosleep(&delay,NULL);
    }
    if(!found) {perror("acoustic UIO identity");close(lock);return 1;}
    int uio=open(r.device,O_RDWR|O_SYNC|O_CLOEXEC);
    if(uio<0) {perror("open UIO");close(lock);return 1;}
    volatile uint8_t *base=mmap(NULL,PL_MAP_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,uio,0);
    if(base==MAP_FAILED) {perror("mmap UIO");close(uio);close(lock);return 1;}
    if(initialize_enable(base)) {perror("enable initialization");munmap((void *)base,PL_MAP_SIZE);close(uio);close(lock);return 1;}
    r.enabled=(rd(base,CONTROL)&1u)!=0;r.enable_changed_ms=monotonic_ms();
    pl_tracker_clear(&r.tracker,r.enabled?"enable_pending":"disabled");
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    struct sockaddr_un addr={.sun_family=AF_UNIX};strcpy(addr.sun_path,SOCKET_PATH);
    unlink(SOCKET_PATH);
    if(server<0 || bind(server,(struct sockaddr *)&addr,sizeof(addr)) || listen(server,CLIENTS)) {
        perror("socket bind/listen");if(server>=0)close(server);
        munmap((void *)base,PL_MAP_SIZE);close(uio);close(lock);return 1;
    }
    chmod(SOCKET_PATH,0660);
    struct pollfd fds[CLIENTS+1];uint64_t deadline[CLIENTS+1]={0};
    for(unsigned i=0;i<=CLIENTS;i++)fds[i]=(struct pollfd){.fd=-1,.events=POLLIN};
    fds[0].fd=server;uint64_t next=monotonic_ms();
    fprintf(stderr,"START %s UIO=%s source=%s period_ms=%u\n",VERSION,r.device,GATE_PATH,PL_PERIOD_MS);
    while(running) {
        uint64_t now=monotonic_ms();
        if(now>=next) {sample(base,&r,now);next=now+PL_PERIOD_MS;}
        int wait=next>now?(int)(next-now):0;
        if(poll(fds,CLIENTS+1,wait)<0 && errno!=EINTR) {perror("poll");break;}
        now=monotonic_ms();
        if(now>=next) {sample(base,&r,now);next=now+PL_PERIOD_MS;}
        if(fds[0].revents&POLLIN) {
            int client=accept4(server,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
            if(client>=0) {
                unsigned i;for(i=1;i<=CLIENTS;i++)if(fds[i].fd<0)break;
                if(i>CLIENTS)close(client);
                else {fds[i].fd=client;fds[i].revents=0;deadline[i]=now+250;}
            }
        }
        for(unsigned i=1;i<=CLIENTS;i++)if(fds[i].fd>=0) {
            if(fds[i].revents&(POLLIN|POLLHUP)) {
                char buf[128]={0};ssize_t n=recv(fds[i].fd,buf,sizeof(buf)-1,MSG_DONTWAIT);
                if(n>0)handle_command(fds[i].fd,buf,base,&r);
                close(fds[i].fd);fds[i].fd=-1;
            } else if((fds[i].revents&(POLLERR|POLLNVAL)) || now>=deadline[i]) {
                close(fds[i].fd);fds[i].fd=-1;
            }
        }
    }
    for(unsigned i=1;i<=CLIENTS;i++)if(fds[i].fd>=0)close(fds[i].fd);
    close(server);unlink(SOCKET_PATH);munmap((void *)base,PL_MAP_SIZE);close(uio);close(lock);return 0;
}

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "video_robot_core.h"
#include "video_robot_modes_core.h"

static volatile sig_atomic_t stopping;
static const char *device="/dev/ttyPS1",*socket_path="/run/robot-control.sock";
static const char *state_path="/run/robot-control.state",*store_dir="/var/lib/acoustic-camera";
static const char *pl_path="/run/pl-control.sock";
static const char *regions_path="/etc/robot-regions.conf";
static struct robot_regions regions={.timeout_ms=30000};
static int regions_fault;
static unsigned timeout_ms=30000;
static struct robot_job job;
static unsigned wire_sequence;
static int serial_fd=-1,persist_fault,feedback_seen;
static uint64_t deadline;
static struct robot_unknown_timer unknown_timer;
static unsigned tx_frames,rx_frames,ignored_frames,rejected_frames,expired_frames;
static unsigned rx_bytes,rx_chunks,rx_max_fragment_gap_ms;
static const char *last_event="startup_no_send";
static uint64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;}
static void stop_handler(int s){(void)s;stopping=1;}
static void log_event(const char *s){last_event=s;printf("ROBOT %s id=%u region=%u phase=%u tx=%u rx=%u\n",s,job.id,job.region,job.phase,tx_frames,rx_frames);fflush(stdout);}
static int save_job(void) {
    char path[512],tmp[512],line[160];
    snprintf(path,sizeof(path),"%s/robot-task.state",store_dir);
    snprintf(tmp,sizeof(tmp),"%s/robot-task.state.new",store_dir);
    job.sequence=wire_sequence;
    int n=snprintf(line,sizeof(line),"ROBOT4 %u %u %u %016llx %u %u %u %u %u %u %u\n",
        job.id,job.region,job.phase,job.nonce,job.protocol,job.wire_region,job.mode,job.value,job.sequence,
        job.last_grab_id,job.last_grab_known);
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return -1;
    int ok=write(fd,line,(size_t)n)==n&&fsync(fd)==0;close(fd);
    if(!ok||rename(tmp,path))return -1;
    fd=open(store_dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return -1;
    ok=fsync(fd)==0;close(fd);return ok?0:-1;
}
static int load_job(void) {
    char path[512],line[160],extra;snprintf(path,sizeof(path),"%s/robot-task.state",store_dir);
    FILE *f=fopen(path,"r");if(!f)return errno==ENOENT?0:-1;
    int ok=fgets(line,sizeof(line),f)!=NULL&&fgetc(f)==EOF;fclose(f);
    int x=0,y=0,z=0;
    if(!ok)return -1;
    if(sscanf(line,"ROBOT4 %u %u %u %llx %u %u %u %u %u %u %u %c",&job.id,&job.region,&job.phase,&job.nonce,
       &job.protocol,&job.wire_region,&job.mode,&job.value,&job.sequence,&job.last_grab_id,&job.last_grab_known,&extra)==11){
        if(job.mode>ROBOT_MODE_CYCLE||job.sequence>65535||job.protocol<1||job.protocol>3)return -1;
        if(job.last_grab_id>65535||job.last_grab_known>1)return -1;
        if(job.mode==ROBOT_MODE_GRAB&&(job.region<1||job.region>3||job.wire_region>3||
           (job.protocol==3&&job.wire_region<1)))return -1;
        if(job.mode!=ROBOT_MODE_GRAB&&(job.protocol!=3||job.region||job.wire_region||
           (job.mode==ROBOT_MODE_TRACK&&(job.value<1||job.value>12))||job.value>255))return -1;
    }else if(sscanf(line,"ROBOT3 %u %u %u %llx %u %u %c",&job.id,&job.region,&job.phase,&job.nonce,&job.protocol,&job.wire_region,&extra)==6){
        if(job.protocol<1||job.protocol>3||job.wire_region>3||(job.protocol==3&&job.wire_region<1))return -1;
    }else if(sscanf(line,"ROBOT2 %u %u %u %llx %d %d %d %c",&job.id,&job.region,&job.phase,&job.nonce,&x,&y,&z,&extra)==7){
        if(x<INT16_MIN||x>INT16_MAX||y<INT16_MIN||y>INT16_MAX||z<INT16_MIN||z>INT16_MAX)return -1;
        job.protocol=2;job.wire_region=0;
    }else if(sscanf(line,"ROBOT1 %u %u %u %llx %c",&job.id,&job.region,&job.phase,&job.nonce,&extra)==4){job.protocol=1;job.wire_region=0;}
    else return -1;
    if((!job.id&&job.protocol!=3)||job.id>65535||
       (job.mode==ROBOT_MODE_GRAB&&(job.region<1||job.region>3))||job.phase>ROBOT_EXECUTING||!job.nonce)return -1;
    if(strncmp(line,"ROBOT4 ",7)){
        job.sequence=job.id;
        if(job.protocol==3&&job.phase==ROBOT_COMPLETE){job.last_grab_id=job.id;job.last_grab_known=1;}
    }
    if(robot_inflight(&job))job.phase=ROBOT_UNKNOWN;
    return 0;
}
static int load_regions(void) {
    int fd=open(regions_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return errno==ENOENT?0:-1;
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>4096||(st.st_mode&022)
#ifndef ROBOT_SERVICE_TEST
       ||st.st_uid!=0
#endif
       ){close(fd);return -1;}
    FILE *f=fdopen(fd,"r");if(!f){close(fd);return -1;}
    int rc=robot_regions_read(f,&regions);fclose(f);return rc;
}
static void mode_state_json(char *data,size_t cap);
static void state_json(char *data,size_t cap) {
    char mode_data[768];mode_state_json(mode_data,sizeof(mode_data));
    snprintf(data,cap,"{\"version\":\"%s\",\"monotonic_ms\":%llu,\"uart_ready\":%d,\"fault\":%d,"
        "\"id\":%u,\"region\":%u,\"phase\":%u,\"nonce\":\"%016llx\",\"feedback_seen\":%d,"
        "\"tx_frames\":%u,\"rx_frames\":%u,\"ignored_frames\":%u,\"rejected_frames\":%u,"
        "\"region_mask\":%u,\"wire_region\":%u,\"baudrate\":9600,\"protocol\":\"region-no-crc\","
        "\"timeout_ms\":%u,\"unknown_hold_ms\":%u,\"expired_frames\":%u,\"rx_bytes\":%u,\"rx_chunks\":%u,"
        "\"rx_max_fragment_gap_ms\":%u,\"host_rx_idle_ms\":%u,"
        "\"event\":\"%s\",%s}\n",
        ROBOT_VERSION,(unsigned long long)ms(),serial_fd>=0,persist_fault||regions_fault,job.id,job.region,job.phase,
        job.nonce,feedback_seen,tx_frames,rx_frames,ignored_frames,rejected_frames,regions.mask,
        job.wire_region,timeout_ms,ROBOT_UNKNOWN_HOLD_MS,expired_frames,rx_bytes,rx_chunks,rx_max_fragment_gap_ms,ROBOT_HOST_RX_IDLE_MS,last_event,mode_data);
}
static void publish(void) {
    char tmp[512],data[ROBOT_STATE_CAP];snprintf(tmp,sizeof(tmp),"%s.new",state_path);state_json(data,sizeof(data));
    int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;
    int ok=write(fd,data,strlen(data))==(ssize_t)strlen(data);close(fd);if(ok)rename(tmp,state_path);
}
static int open_serial(void) {
#ifndef ROBOT_SERVICE_TEST
    char path[PATH_MAX],console[256]={0};
    if(!realpath("/sys/class/tty/ttyPS1/device/of_node",path)||!strstr(path,"/serial@e0001000"))return -1;
    int console_fd=open("/sys/class/tty/console/active",O_RDONLY|O_CLOEXEC);if(console_fd<0)return -1;
    ssize_t count=read(console_fd,console,sizeof(console)-1);close(console_fd);
    if(count<0||strstr(console,"ttyPS1"))return -1;
#endif
    int fd=open(device,O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
    if(flock(fd,LOCK_EX|LOCK_NB)||ioctl(fd,TIOCEXCL)){close(fd);return -1;}
    struct termios t;
    if(tcgetattr(fd,&t)){close(fd);return -1;}
    cfmakeraw(&t);t.c_cflag=(t.c_cflag&~(CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS))|CS8|CLOCAL|CREAD;
    t.c_iflag&=~(IXON|IXOFF|IXANY);t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
    if(cfsetispeed(&t,B9600)||cfsetospeed(&t,B9600)||tcsetattr(fd,TCSANOW,&t)) {close(fd);return -1;}
    return fd; /* No flush, probe, handshake or application packet. */
}
static int current_status(struct panel_status *out) {
    panel_unavailable(out);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return 0;
    struct sockaddr_un a={.sun_family=AF_UNIX};snprintf(a.sun_path,sizeof(a.sun_path),"%s",pl_path);
    if(connect(fd,(struct sockaddr *)&a,sizeof(a))&&errno!=EINPROGRESS){close(fd);return 0;}
    struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t len=sizeof(error);
    if(poll(&p,1,20)<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)||error||send(fd,"status\n",7,MSG_NOSIGNAL)!=7){close(fd);return 0;}
    char data[4096];size_t used=0;uint64_t end=ms()+60;
    while(used<sizeof(data)&&ms()<end){p.events=POLLIN;int left=(int)(end-ms());if(left<=0||poll(&p,1,left)<=0)break;
        ssize_t n=recv(fd,data+used,sizeof(data)-used,0);if(n<=0)break;used+=(size_t)n;if(memchr(data,'\n',used))break;}
    close(fd);return panel_parse_status(data,used,out);
}
static unsigned current_target(int latest){struct panel_status s;return current_status(&s)?
    (latest?robot_manual_target(&s):robot_target(&s)):0;}
static int send_command(unsigned region,unsigned long long nonce,int latest);
#include "video_robot_modes.h"
static int send_command(unsigned region,unsigned long long nonce,int latest) {
    if(nonce&&nonce==job.nonce){log_event("duplicate_no_resend");return job.mode==ROBOT_MODE_GRAB&&(latest||region==job.region)?0:-1;}
    if(!nonce||region<1||region>3||persist_fault||serial_fd<0||robot_blocked(&job)||mode_transaction_busy()){log_event("rejected_not_ready");return -1;}
    unsigned measured=current_target(latest);
    if(latest){
        if(!measured){log_event("rejected_no_live_target");return -1;}
        if(region!=measured){printf("ROBOT latest_region hint=%u resolved=%u\n",region,measured);fflush(stdout);}
        region=measured;
    }else if(measured!=region){log_event("rejected_position_changed");return -1;}
    if(regions_fault||!(regions.mask&(1u<<(region-1)))){log_event("rejected_region_not_commissioned");return -1;}
    job.id=mode_allocate_id();
    /* High-rate mode heartbeats can wrap IDs long before another grab. Avoid
     * the known Arduino cached completed-grab ID, which could suppress motion. */
    if(job.last_grab_known&&job.id==job.last_grab_id)job.id=mode_allocate_id();
    job.region=region;job.phase=ROBOT_WAITING;job.nonce=nonce;
    job.mode=ROBOT_MODE_GRAB;job.value=0;
    job.protocol=3;job.wire_region=regions.wire[region-1];
    /* Commit before the first byte; uncertain/crashed jobs never auto-resend. */
    if(save_job()){persist_fault=1;job.phase=ROBOT_UNKNOWN;log_event("persist_failed_no_send");return -1;}
    unsigned char frame[ROBOT_COMMAND_SIZE];robot_command(frame,job.id,job.wire_region);size_t done=0;uint64_t end=ms()+300;
    while(done<sizeof(frame)&&ms()<end&&!stopping){ssize_t n=write(serial_fd,frame+done,sizeof(frame)-done);
        if(n>0)done+=(size_t)n;
        else if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;
        else{struct pollfd p={serial_fd,POLLOUT,0};poll(&p,1,10);}}
    if(done!=sizeof(frame)){job.phase=ROBOT_UNKNOWN;if(save_job())persist_fault=1;log_event("partial_send_unknown");return -1;}
    tx_frames++;deadline=ms()+timeout_ms;log_event("sent_waiting_result");return 0;
}
static void client(int server) {
    int fd=accept4(server,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)return;
    struct ucred peer;socklen_t size=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)||peer.uid!=geteuid()){close(fd);return;}
    char command[128];size_t used=0;uint64_t end=ms()+70;
    while(used<sizeof(command)-1&&ms()<end){struct pollfd p={fd,POLLIN,0};int left=(int)(end-ms());if(left<=0||poll(&p,1,left)<=0)break;
        ssize_t n=recv(fd,command+used,sizeof(command)-1-used,0);if(n<=0)break;used+=(size_t)n;if(memchr(command,'\n',used))break;}
    command[used]=0;int accepted=-1;unsigned region,id;unsigned long long nonce;char extra;
    if(!strcmp(command,"status\n"))accepted=0;
    else if(sscanf(command,"execute %u %16llx %c",&region,&nonce,&extra)==2)
        accepted=modes.auto_mode?-1:send_command(region,nonce,1);
    else if(sscanf(command,"ack-stopped %u %c",&id,&extra)==1&&job.phase==ROBOT_UNKNOWN&&job.id==id&&!persist_fault){
        job.phase=ROBOT_READY;if(save_job()){persist_fault=1;job.phase=ROBOT_UNKNOWN;}
        else {accepted=0;log_event("operator_ack_stopped_no_send");}}
    else accepted=mode_client_command(command,peer.pid);
    char reply[ROBOT_STATE_CAP+8],data[ROBOT_STATE_CAP];state_json(data,sizeof(data));snprintf(reply,sizeof(reply),"%s %s",accepted?"ERR":"OK",data);
    (void)send(fd,reply,strlen(reply),MSG_NOSIGNAL);close(fd);publish();
}
static void expire_unknown(uint64_t now){
    if(persist_fault||regions_fault){unknown_timer.active=0;return;}
    if(job.mode==ROBOT_MODE_GRAB&&job.phase==ROBOT_UNKNOWN)mode_auto_clear();
    if(!robot_unknown_due(&unknown_timer,&job,now)||mode_transaction_busy())return;
    /* Expire the local result notice; do not invent a completion, reconnect
     * the controller, resend a task, or restore automatic operator intent. */
    job.phase=ROBOT_READY;feedback_seen=0;deadline=0;modes.error=RM_OK;
    if(save_job()){persist_fault=1;job.phase=ROBOT_UNKNOWN;log_event("unknown_expiry_persist_failed_no_send");}
    else log_event("unknown_expired_ready_no_retry");
    unknown_timer.active=0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts(ROBOT_VERSION);return 0;}
#ifdef ROBOT_SERVICE_TEST
    if(argc!=8)return 2;
    device=argv[1];socket_path=argv[2];state_path=argv[3];store_dir=argv[4];pl_path=argv[5];timeout_ms=(unsigned)atoi(argv[6]);
    regions_path=argv[7];
#else
    if(argc!=1||geteuid()!=0)return 2;
#endif
    umask(0077);
    if(mkdir(store_dir,0700)&&errno!=EEXIST){perror("robot store");return 3;}
    char lockpath[512];snprintf(lockpath,sizeof(lockpath),"%s/robot.lock",store_dir);
    int lock=open(lockpath,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){perror("robot lock");return 3;}
    persist_fault=load_job()!=0;
    wire_sequence=job.sequence;
    regions_fault=load_regions()!=0;
#ifndef ROBOT_SERVICE_TEST
    timeout_ms=regions.timeout_ms;
#endif
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(server<0)return 3;
    struct sockaddr_un a={.sun_family=AF_UNIX};snprintf(a.sun_path,sizeof(a.sun_path),"%s",socket_path);
    unlink(socket_path);
    if(bind(server,(struct sockaddr *)&a,sizeof(a))||chmod(socket_path,0600)||listen(server,4)){perror("robot socket");close(server);return 3;}
    signal(SIGTERM,stop_handler);signal(SIGINT,stop_handler);signal(SIGPIPE,SIG_IGN);
    struct robot_parser parser={0};uint64_t next_open=0,next_publish=0;
    log_event(persist_fault?"persistent_state_invalid":"startup_no_send");
    while(!stopping){uint64_t now=ms();
        if(serial_fd<0&&now>=next_open){serial_fd=open_serial();next_open=now+2000;
            if(serial_fd>=0)log_event("uart_open_no_send");else fprintf(stderr,"ROBOT uart_open_failed errno=%d\n",errno);}
        if(job.mode==ROBOT_MODE_GRAB&&robot_inflight(&job)&&now>=deadline){job.phase=ROBOT_UNKNOWN;if(save_job())persist_fault=1;log_event("timeout_unknown_no_retry");}
        mode_tick(now);
        expire_unknown(ms());
        mode_auto_tick(ms());
        if(now>=next_publish){publish();next_publish=now+200;}
        struct pollfd p[2]={{server,POLLIN,0},{serial_fd,POLLIN,0}};
        if(poll(p,2,30)<0&&errno!=EINTR)break;
        /* Drain results BEFORE accepting a new task, never bind stale packets to a new job. */
        if(serial_fd>=0&&(p[1].revents&(POLLIN|POLLERR|POLLHUP))){unsigned char bytes[128];ssize_t n=read(serial_fd,bytes,sizeof(bytes));
            if(n>0){
                uint64_t delivered=ms();unsigned gap=parser.used&&delivered>=parser.last_ms?
                    (unsigned)(delivered-parser.last_ms):0;
                rx_bytes+=(unsigned)n;rx_chunks++;
                if(gap>rx_max_fragment_gap_ms)rx_max_fragment_gap_ms=gap;
                if(modes.transaction==MT_CONFIG_TRACK||modes.transaction==MT_CONFIG_CYCLE){
                    printf("ROBOT_CONFIG_RX_CHUNK bytes=%zd gap_ms=%u buffered=%u\n",n,gap,parser.used);fflush(stdout);}
                for(ssize_t i=0;i<n;i++){struct robot_packet packet;
                int have=robot_rx_byte(&parser,bytes[i],ms(),&packet);
                while(have){
                unsigned result=packet.payload[0];
                int progress_expired=result==2&&job.phase==ROBOT_UNKNOWN&&(!deadline||ms()>=deadline);
                rx_frames++;if(mode_receive(&packet,ms())){}
                else if(!progress_expired&&robot_reply(&job,&packet)){feedback_seen=1;
                    if(result==0){job.last_grab_id=job.id;job.last_grab_known=1;}
                    if(save_job())persist_fault=1;
                    log_event(result==2?"matching_executing":"matching_final_result_unverified_motion");}
                else ignored_frames++;
                have=robot_rx_next(&parser,&packet);}}}
            rejected_frames+=parser.rejected;parser.rejected=0;
            expired_frames+=parser.expired;parser.expired=0;
            if((n<0&&errno!=EAGAIN&&errno!=EINTR)||(p[1].revents&(POLLERR|POLLHUP))){close(serial_fd);serial_fd=-1;memset(&parser,0,sizeof(parser));
                mode_uart_lost();
                if(robot_inflight(&job)){job.phase=ROBOT_UNKNOWN;if(save_job())persist_fault=1;}log_event("uart_error_unknown");}}
        if(p[0].revents&POLLIN)client(server);
    }
    if(robot_inflight(&job)){job.phase=ROBOT_UNKNOWN;if(save_job())persist_fault=1;}
    if(serial_fd>=0)close(serial_fd);
    serial_fd=-1;log_event("stopped_no_send");publish();close(server);unlink(socket_path);close(lock);return 0;
}

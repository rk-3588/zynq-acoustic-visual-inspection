#define _GNU_SOURCE
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "video_readonly_probe.h"
#include "video_ui_watchdog_core.h"

#define WATCH_PID "/run/video-status-panel-watchdog.pid"
#define WATCH_STATE "/run/video-status-panel-watchdog.state"
#define PANEL_PID "/run/video-status-panel.pid"
#define PANEL_EXE "/usr/bin/video_status_panel"
#define PANEL_SERVICE "/etc/init.d/video-status-panel"
static volatile sig_atomic_t stopping;
static void stop_watch(int sig){(void)sig;stopping=1;}
static uint32_t panel_pid(void) {
    char text[64],path[96],exe[512];unsigned long long pid;
    if(mix_read_text(PANEL_PID,text,sizeof(text)) || !mix_parse_integer(text,&pid) || !pid || pid>0x7fffffff)return 0;
    snprintf(path,sizeof(path),"/proc/%llu/exe",pid);
    ssize_t n=readlink(path,exe,sizeof(exe)-1);
    if(n<0)return 0;
    exe[n]=0;
    return !strcmp(exe,PANEL_EXE) && !kill((pid_t)pid,0)?(uint32_t)pid:0;
}
static int recover(uint32_t pid) {
    char ui[32],watch[32];snprintf(ui,sizeof(ui),"%u",pid);snprintf(watch,sizeof(watch),"%ld",(long)getpid());
    if(stopping)return -1;
    pid_t child=fork();if(child<0)return -1;
    if(!child){execl(PANEL_SERVICE,PANEL_SERVICE,"recover",ui,watch,(char *)NULL);_exit(127);}
    int status;pid_t done;
    do{done=waitpid(child,&status,0);}while(done<0&&errno==EINTR);
    return done==child&&WIFEXITED(status)?WEXITSTATUS(status):-1;
}
static void state(const struct ui_watch_policy *p,struct ui_watch_sample s,unsigned recoveries,int result) {
    /* Current volatile status only: no fault archive or append-only log. */
    FILE *f=fopen(WATCH_STATE ".new","w");if(!f)return;
    fprintf(f,"{\"version\":\"%s\",\"pid\":%ld,\"ui_pid\":%u,\"ui_on\":%s,\"camera_ok\":%s,"
        "\"rate_valid\":%s,\"rate_millihz\":%u,\"consecutive\":%u,\"cooldown_remaining_ms\":%llu,"
        "\"recovery_attempts\":%u,\"last_result\":%d,\"sampled_ms\":%llu}\n",
        UI_WATCHDOG_VERSION,(long)getpid(),s.pid,s.ui_on?"true":"false",s.camera_ok?"true":"false",
        p->rate_valid?"true":"false",p->millihz,p->hits,
        (unsigned long long)(p->cooldown_until>s.ns?(p->cooldown_until-s.ns)/1000000:0),
        recoveries,result,(unsigned long long)(s.ns/1000000));
    if(!fclose(f))rename(WATCH_STATE ".new",WATCH_STATE);
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts(UI_WATCHDOG_VERSION);return 0;}
    int check=argc==2&&!strcmp(argv[1],"--check");
    if(!check&&(argc!=2||strcmp(argv[1],"--run")))return 2;
    struct video_ro_probe vpp={.fd=-1},mixer={.fd=-1},camera={.fd=-1};
    if(video_probe_open(&vpp,"video_preprocess@40040000",0x40040000u) ||
       video_probe_open(&mixer,VIDEO_MIXER_UIO_NAME,VIDEO_MIXER_BASE) ||
       video_probe_open(&camera,"dma@44a00000",0x44a00000u) ||
       video_probe_read(&vpp,0)!=0x56505031u) {
        video_probe_close(&vpp);video_probe_close(&mixer);video_probe_close(&camera);return 3;
    }
    if(check){puts(UI_WATCHDOG_VERSION " readonly-uio-check=pass");goto done;}
    int lock=open("/run/video-status-panel-watchdog.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)) {if(lock>=0)close(lock);goto failed;}
    struct sigaction action={0};action.sa_handler=stop_watch;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGHUP,&action,NULL);
    struct ui_watch_policy policy={0};unsigned attempts=0;int result=0;
    uint64_t next=video_probe_ns();
    while(!stopping) {
        struct ui_watch_sample s={.ns=video_probe_ns(),.sequence=video_probe_read(&vpp,0x1c),.pid=panel_pid()};
        /* Do not read Mixer ap_ctrl/ISR: the UI owns the completion protocol. */
        s.ui_on=s.pid && video_probe_read(&mixer,MIX_LAYERS)==3;
        s.camera_ok=ui_watch_camera_ok(video_probe_read(&camera,0),video_probe_read(&camera,4),
            video_probe_read(&camera,0x30),video_probe_read(&camera,0x34));
        if(ui_watch_step(&policy,s)&&!stopping) {
            attempts++;result=recover(s.pid);
            ui_watch_attempt_finished(&policy,video_probe_ns());
        }
        state(&policy,s,attempts,result);
        next+=1000000000ull;
        uint64_t now=video_probe_ns();if(next<=now)next=now+1000000000ull;
        struct timespec deadline={(time_t)(next/1000000000ull),(long)(next%1000000000ull)};
        while(!stopping&&clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL)==EINTR){}
    }
    close(lock);
done:
    video_probe_close(&vpp);video_probe_close(&mixer);video_probe_close(&camera);return 0;
failed:
    video_probe_close(&vpp);video_probe_close(&mixer);video_probe_close(&camera);return 4;
}

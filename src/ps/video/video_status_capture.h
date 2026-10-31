#ifndef VIDEO_STATUS_CAPTURE_H
#define VIDEO_STATUS_CAPTURE_H
#include <sys/prctl.h>
#include <sys/wait.h>
#include "camera_snapshot_core.h"
#ifndef PANEL_CAPTURE_HELPER
#define PANEL_CAPTURE_HELPER "/usr/bin/camera_snapshot"
#endif
#define PANEL_CAPTURE_CHECK_PERIOD_MS 5000u
#define PANEL_CAPTURE_READY_TTL_MS 8000u
struct panel_capture_job {
    pid_t pid;int fd,kind;size_t used;char reply[4096];
    uint64_t deadline_ms,next_check_ms;
};
struct panel_capture_result {
    int saved,ready,status_known;
    unsigned count;uint32_t status,errors;
    char reason[40];
};
static inline int panel_capture_spawn(struct panel_capture_job *job,int kind,uint64_t now) {
    if(job->pid)return -1;
    int pipes[2];if(pipe2(pipes,O_CLOEXEC))return -1;
    if(fcntl(pipes[0],F_SETFL,O_NONBLOCK)){close(pipes[0]);close(pipes[1]);return -1;}
    pid_t parent=getpid(),pid=fork();
    if(pid==0) {
        /* The helper never owns/writes video hardware. Do not inherit signal
         * handlers; abort private output if the panel disappears mid-capture. */
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        if(prctl(PR_SET_PDEATHSIG,SIGTERM) || getppid()!=parent)_exit(127);
        if(dup2(pipes[1],STDOUT_FILENO)<0)_exit(127);
        close(pipes[0]);close(pipes[1]);
        execl(PANEL_CAPTURE_HELPER,"camera_snapshot",kind==1?"--check":"--capture",(char *)NULL);
        _exit(127);
    }
    close(pipes[1]);if(pid<0){close(pipes[0]);return -1;}
    *job=(struct panel_capture_job){.pid=pid,.fd=pipes[0],.kind=kind,.deadline_ms=now+(kind==1?1500u:10000u)};return 0;
}
static inline int panel_capture_receipt_detail(const char *data,size_t size,struct panel_capture_result *result) {
    struct pl_json j={data,data+size,0};unsigned seen=0;int status_known=0,errors_known=0;
    struct panel_capture_result parsed={0};uint64_t total=0;
    if(!size||size>=4096)return 0;
    pl_json_space(&j);if(j.p==j.end || *j.p++!='{')return 0;
    for(;;) {
        const char *key;size_t n;pl_json_space(&j);if(!pl_json_string(&j,&key,&n))return 0;
        pl_json_space(&j);if(j.p==j.end || *j.p++!=':')return 0;
        pl_json_space(&j);const char *begin=j.p;if(!pl_json_value(&j))return 0;
        unsigned bit=0;
        if(n==7&&!memcmp(key,"version",7)) {
            bit=1;if(!panel_freq_equal(begin,j.p,"\"" SNAP_VERSION "\""))return 0;
        } else if(n==5&&!memcmp(key,"saved",5)) {
            bit=2;if(panel_freq_equal(begin,j.p,"true"))parsed.saved=1;
            else if(!panel_freq_equal(begin,j.p,"false"))return 0;
        } else if(n==5&&!memcmp(key,"count",5)) {
            bit=4;if(!panel_freq_uint(begin,j.p,&total)||total>1000000)return 0;
            parsed.count=(unsigned)total;
        } else if(n==6&&!memcmp(key,"reason",6)) {
            bit=8;if(!panel_string_value(begin,j.p,parsed.reason,sizeof(parsed.reason)))return 0;
        } else if(n==5&&!memcmp(key,"ready",5)) {
            bit=16;if(panel_freq_equal(begin,j.p,"true"))parsed.ready=1;
            else if(!panel_freq_equal(begin,j.p,"false"))return 0;
        } else if(n==11&&!memcmp(key,"s2mm_status",11)) {
            bit=32;if(!panel_freq_equal(begin,j.p,"null")) {
                if(!panel_freq_uint(begin,j.p,&total)||total>UINT32_MAX)return 0;
                parsed.status=(uint32_t)total;status_known=1;
            }
        } else if(n==11&&!memcmp(key,"s2mm_errors",11)) {
            bit=64;if(!panel_freq_equal(begin,j.p,"null")) {
                if(!panel_freq_uint(begin,j.p,&total)||total>UINT32_MAX)return 0;
                parsed.errors=(uint32_t)total;errors_known=1;
            }
        }
        if(bit && (seen&bit))return 0;
        seen|=bit;
        pl_json_space(&j);if(j.p==j.end)return 0;
        if(*j.p=='}'){j.p++;break;}if(*j.p++!=',')return 0;
    }
    pl_json_space(&j);if(j.p!=j.end || (seen&15u)!=15u || status_known!=errors_known)return 0;
    if(status_known && parsed.errors!=video_vdma_errors(parsed.status,1))return 0;
    if(parsed.saved&&strcmp(parsed.reason,"saved"))return 0;
    if(parsed.ready&&(parsed.saved||strcmp(parsed.reason,"ready")))return 0;
    parsed.status_known=status_known;*result=parsed;return 1;
}
static inline int panel_capture_receipt(const char *data,size_t size,int *saved,unsigned *count) {
    struct panel_capture_result result;
    if(!panel_capture_receipt_detail(data,size,&result))return 0;
    *saved=result.saved;*count=result.count;return 1;
}
static inline void panel_capture_tick(struct panel_capture_job *job,struct panel_view *view,uint64_t now) {
    if(view->capture_feedback_until_ms && now>=view->capture_feedback_until_ms) {
        view->capture_feedback=0;view->capture_feedback_until_ms=0;
    }
    if(view->capture_ready&&view->capture_checked_ms&&now-view->capture_checked_ms>PANEL_CAPTURE_READY_TTL_MS) {
        view->capture_ready=0;strcpy(view->capture_reason,"check_expired");
    }
    if(job->pid) {
    ssize_t n;
    while(job->used<sizeof(job->reply)-1 && (n=read(job->fd,job->reply+job->used,sizeof(job->reply)-1-job->used))>0)
        job->used+=(size_t)n;
    if(now>=job->deadline_ms) {
        kill(job->pid,SIGTERM);
        if(now-job->deadline_ms>1000)kill(job->pid,SIGKILL);
    }
    int status=0;pid_t done=waitpid(job->pid,&status,WNOHANG);
    if(done==0 || (done<0&&errno==EINTR))return;
    /* Child may write its receipt between our first read and waitpid. */
    while(job->used<sizeof(job->reply)-1 && (n=read(job->fd,job->reply+job->used,sizeof(job->reply)-1-job->used))>0)
        job->used+=(size_t)n;
    struct panel_capture_result result={0};
    int valid=panel_capture_receipt_detail(job->reply,job->used,&result);
    int ok=done==job->pid && WIFEXITED(status) && WEXITSTATUS(status)==0 && valid;
    char previous[40];strcpy(previous,view->capture_reason);
    if(valid){view->capture_count_known=1;view->capture_count=result.count;}
    view->capture_status_known=valid&&result.status_known;
    view->capture_s2mm_status=result.status;view->capture_error_bits=result.errors;
    view->capture_checked_ms=now;
    snprintf(view->capture_reason,sizeof(view->capture_reason),"%s",now>=job->deadline_ms?"helper_timeout":
        valid?result.reason:done==job->pid&&WIFEXITED(status)&&WEXITSTATUS(status)==127?"helper_exec":"helper_protocol");
    if(job->kind==1) {
        int ready=ok&&result.ready&&result.status_known&&
            !(result.status&(VIDEO_VDMA_S2MM_ERRORS|VIDEO_VDMA_HALTED));
        if(ready!=view->capture_ready || strcmp(previous,view->capture_reason)) {
            job->reply[job->used]=0;
            printf("CAPTURE_CHECK ready=%d %s\n",ready,job->reply);fflush(stdout);
        }
        view->capture_ready=ready;
        if(!ready&&view->capture_request) {
            view->capture_request=0;view->capture_busy=0;
            view->capture_feedback=-1;view->capture_feedback_until_ms=now+4000;
        }
    }
    else {
        view->capture_ready=0;
        view->capture_busy=0;view->capture_feedback=ok&&result.saved?1:-1;view->capture_feedback_until_ms=now+4000;
        job->reply[job->used]=0;
        printf("CAPTURE_RESULT ok=%d %s\n",ok&&result.saved,job->reply);fflush(stdout);
    }
    uint64_t next=now+(job->kind==2?250u:PANEL_CAPTURE_CHECK_PERIOD_MS);
    close(job->fd);*job=(struct panel_capture_job){.fd=-1,.next_check_ms=next};
    }
    if(view->capture_request) {
        view->capture_request=0;
        if(!view->capture_ready||panel_capture_spawn(job,2,now)) {
            view->capture_ready=0;view->capture_busy=0;
            view->capture_feedback=-1;view->capture_feedback_until_ms=now+4000;
            job->next_check_ms=now+250;
        } else {
            view->capture_busy=1;view->capture_ready=0;view->capture_feedback=0;
            printf("CAPTURE_STARTED\n");fflush(stdout);
        }
    } else if(!view->capture_busy&&!view->gallery_busy&&now>=job->next_check_ms) {
        if(panel_capture_spawn(job,1,now)) {
            view->capture_ready=0;strcpy(view->capture_reason,"helper_spawn");
            job->next_check_ms=now+PANEL_CAPTURE_CHECK_PERIOD_MS;
        }
    }
}
static inline void panel_capture_stop(struct panel_capture_job *job) {
    if(job->pid){kill(job->pid,SIGTERM);waitpid(job->pid,NULL,WNOHANG);}
    if(job->fd>=0)close(job->fd);
    *job=(struct panel_capture_job){.fd=-1};
}
#endif

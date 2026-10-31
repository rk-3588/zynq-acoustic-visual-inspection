#ifndef VIDEO_STATUS_POWER_H
#define VIDEO_STATUS_POWER_H
#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include "video_status_view.h"
#ifndef PANEL_POWEROFF_PATH
#define PANEL_POWEROFF_PATH "/sbin/poweroff"
#endif
extern char **environ;
struct panel_power_job {pid_t pid;uint64_t deadline_ms;};
static inline void panel_power_fail(struct panel_power_view *p,int error) {
    p->phase=PANEL_POWER_FAILED;p->error=error;p->request=p->slow=0;p->revision++;
    fprintf(stderr,"POWER_OFF_REQUEST_FAILED errno=%d\n",error);
}
static inline void panel_power_tick(struct panel_power_job *job,struct panel_view *v,uint64_t now) {
    struct panel_power_view *p=&v->power;
    if(p->phase==PANEL_POWER_CONFIRM&&now>=p->confirm_until) {
        p->phase=PANEL_POWER_IDLE;p->revision++;
    }
    if(p->request) {
        p->request=0;
        if(p->phase!=PANEL_POWER_PENDING||job->pid||panel_power_blocked(v)) {
            if(!job->pid)panel_power_fail(p,EBUSY);
            return;
        }
        if(geteuid()!=0) {panel_power_fail(p,EPERM);return;}
        if(access(PANEL_POWEROFF_PATH,X_OK)) {panel_power_fail(p,errno);return;}
        /* Fixed absolute program and no options: never use a shell, -f, or a
         * direct reboot syscall. The system init owns orderly service stops.
         * Existing hardware/job descriptors are CLOEXEC. Do not set PDEATHSIG:
         * the shutdown request must survive init stopping this UI process. */
        char *args[]={"poweroff",NULL};
        int error=posix_spawn(&job->pid,PANEL_POWEROFF_PATH,NULL,NULL,args,environ);
        if(error) {job->pid=0;panel_power_fail(p,error);return;}
        job->deadline_ms=now+5000;
        printf("POWER_OFF_USER_CONFIRMED pid=%ld orderly-request no-force\n",(long)job->pid);
        fflush(stdout);
    }
    if(!job->pid)return;
    int status=0;pid_t done=waitpid(job->pid,&status,WNOHANG);
    if(done==job->pid) {
        job->pid=0;
        if(WIFEXITED(status)&&WEXITSTATUS(status)==0) {
            p->phase=PANEL_POWER_ACCEPTED;p->error=p->slow=0;p->revision++;
            puts("POWER_OFF_REQUEST_ACCEPTED (init owns shutdown)");fflush(stdout);
        } else panel_power_fail(p,EIO);
    } else if(done<0&&errno!=EINTR) {
        int error=errno;job->pid=0;panel_power_fail(p,error);
    } else if(now>=job->deadline_ms) {
        /* A slow request is not killed or retried: it may already be stopping
         * services. Keep the modal locked against duplicate requests. */
        p->slow=1;
    }
}
#endif

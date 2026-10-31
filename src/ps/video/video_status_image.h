#ifndef VIDEO_STATUS_IMAGE_H
#define VIDEO_STATUS_IMAGE_H
#include <sys/prctl.h>
#include <sys/wait.h>
#include "camera_album_core.h"
#include "video_status_view.h"
#include "video_status_follow.h"
#ifndef PANEL_IMAGE_HELPER
#define PANEL_IMAGE_HELPER "/usr/bin/video_preprocess_control"
#endif
struct panel_image_job {
    pid_t pid;int fd,cancelling,overflow;unsigned command;size_t used;
    uint64_t next_ms,deadline_ms,cancel_ms;
    struct panel_image_config requested;
    unsigned saved_mode;
    int tracking;
    char reply[8192];
};
static inline int panel_image_bool(const char *a,const char *b,const char *key,int *out) {
    const char *s,*e;if(!album_field(a,b,key,&s,&e))return 0;
    if(e-s==4&&!memcmp(s,"true",4)){*out=1;return 1;}
    if(e-s==5&&!memcmp(s,"false",5)){*out=0;return 1;}return 0;
}
static inline int panel_image_signed(const char *a,const char *b,const char *key,int limit,int *out) {
    const char *s,*e;if(!album_field(a,b,key,&s,&e)||s==e)return 0;
    int negative=*s=='-';if(negative)s++;
    uint64_t value;if(!panel_freq_uint(s,e,&value)||value>(unsigned)limit)return 0;
    *out=negative?-(int)value:(int)value;return 1;
}
static inline int panel_image_parse(const char *text,size_t n,struct panel_image_config *c,int *pending,int applied) {
    if(!n||n>=8192||memchr(text,0,n))return 0;
    const char *end=text+n,*roi,*re;uint64_t value;char label[80];
    if(!album_string(text,end,"version",label,sizeof(label))||strcmp(label,"20261015-video-preprocess-control-v3")||
       !album_string(text,end,"hardware_id",label,sizeof(label))||strcmp(label,"0x56505031")||
       !album_uint(text,end,"protocol",&value)||value!=3||
       !album_uint(text,end,"width",&value)||value!=1280||
       !album_uint(text,end,"height",&value)||value!=720||
       !album_string(text,end,"result",label,sizeof(label))||strcmp(label,applied?"applied":"status"))return 0;
    if(!album_string(text,end,"active_mode",label,sizeof(label)))return 0;
    unsigned mode;for(mode=0;mode<6;mode++)if(!strcmp(label,panel_image_mode_cli(mode)))break;
    if(mode==6||!album_uint(text,end,"active_threshold",&value)||value>255)return 0;
    struct panel_image_config got={.mode=mode,.threshold=(unsigned)value};
    if(!panel_image_signed(text,end,"active_brightness",255,&got.brightness)||
       !panel_image_signed(text,end,"active_gain_shift",2,&got.gain)||
       !panel_image_bool(text,end,"pending",pending)||!album_field(text,end,"active_roi",&roi,&re)||
       !panel_image_bool(roi,re,"enabled",&got.roi))return 0;
    const char *keys[]={"x","y","width","height","x1","y1"};unsigned v[6];
    for(unsigned k=0;k<6;k++){if(!album_uint(roi,re,keys[k],&value)||value>1280)return 0;v[k]=(unsigned)value;}
    if(!v[2]||!v[3]||v[0]>=1280||v[1]>=720||v[0]+v[2]>1280||v[1]+v[3]>720||
       v[4]!=v[0]+v[2]||v[5]!=v[1]+v[3])return 0;
    got.x=v[0];got.y=v[1];got.w=v[2];got.h=v[3];*c=got;return 1;
}
/* Every argument is a fixed token or a bounded decimal, never shell syntax. */
static inline unsigned panel_image_argv(unsigned command,const struct panel_image_config *request,
                                       char *argv[20],char numbers[7][24],unsigned parameter_mode) {
    unsigned n=0;argv[n++]=(char *)PANEL_IMAGE_HELPER;
    if(command==PIMG_STATUS)argv[n++]="--status";
    else if(command==PIMG_MODE){argv[n++]="--mode";argv[n++]=(char *)panel_image_mode_cli(request->mode);}
    else if(command==PIMG_FULL){argv[n++]="--full-frame";argv[n++]="--mode";argv[n++]=(char *)panel_image_mode_cli(request->mode);}
    else if(command==PIMG_ROI||command==PIMG_FOLLOW||command==PIMG_FOLLOW_MODE||command==PIMG_FOLLOW_PARAMS) {
        argv[n++]="--roi";unsigned v[]={request->x,request->y,request->w,request->h};
        for(unsigned k=0;k<4;k++){snprintf(numbers[k],24,"%u",v[k]);argv[n++]=numbers[k];}
        argv[n++]="--mode";argv[n++]=(char *)panel_image_mode_cli(request->mode);
    } else if(command==PIMG_ORIGINAL||command==PIMG_DEFAULTS)argv[n++]="--clear-roi";
    else if(command!=PIMG_PARAMS)return 0;
    if(command==PIMG_PARAMS||command==PIMG_DEFAULTS||command==PIMG_FOLLOW_PARAMS) {
        unsigned offset=command==PIMG_FOLLOW_PARAMS?4:0;
        snprintf(numbers[offset],24,"%u",command==PIMG_DEFAULTS?128:request->threshold);
        snprintf(numbers[offset+1],24,"%d",command==PIMG_DEFAULTS?0:request->brightness);
        snprintf(numbers[offset+2],24,"%d",command==PIMG_DEFAULTS?0:request->gain);
        if(command==PIMG_DEFAULTS||parameter_mode==2){argv[n++]="--threshold";argv[n++]=numbers[offset];}
        if(command==PIMG_DEFAULTS||parameter_mode==3){argv[n++]="--brightness";argv[n++]=numbers[offset+1];}
        if(command==PIMG_DEFAULTS||parameter_mode==5){argv[n++]="--gain-shift";argv[n++]=numbers[offset+2];}
    }
    if(command!=PIMG_STATUS){argv[n++]="--timeout-ms";argv[n++]="1000";}
    argv[n]=NULL;return n;
}
static inline int panel_image_matches(unsigned command,const struct panel_image_config *got,const struct panel_image_config *wanted,unsigned parameter_mode) {
    if(command==PIMG_MODE)return got->mode==wanted->mode;
    if(command==PIMG_FULL)return got->mode==wanted->mode&&!got->roi&&got->x==0&&got->y==0&&got->w==1280&&got->h==720;
    if(command==PIMG_ROI||command==PIMG_FOLLOW||command==PIMG_FOLLOW_MODE||command==PIMG_FOLLOW_PARAMS) {
        if(!got->roi||got->mode!=wanted->mode||got->x!=wanted->x||got->y!=wanted->y||got->w!=wanted->w||got->h!=wanted->h)return 0;
        if(command!=PIMG_FOLLOW_PARAMS)return 1;
    }
    if(command==PIMG_PARAMS||command==PIMG_FOLLOW_PARAMS)return got->mode==wanted->mode&&
        (parameter_mode!=2||got->threshold==wanted->threshold)&&
        (parameter_mode!=3||got->brightness==wanted->brightness)&&(parameter_mode!=5||got->gain==wanted->gain);
    if(command==PIMG_ORIGINAL||command==PIMG_DEFAULTS) {
        if(got->mode||got->roi||got->x||got->y||got->w!=1280||got->h!=720)return 0;
        return command==PIMG_ORIGINAL||(got->threshold==128&&!got->brightness&&!got->gain);
    }
    return command==PIMG_STATUS;
}
static inline int panel_image_spawn(struct panel_image_job *j,struct panel_image_view *v,uint64_t now) {
    unsigned command=v->request?v->request:PIMG_STATUS;
    if(command!=PIMG_STATUS)v->slider_dragging=0;
    struct panel_image_config request=v->active;
    unsigned selected=panel_image_selected_mode(v),saved_mode=selected;
    if(command!=PIMG_STATUS&&!panel_image_available(v)) {
        /* A queued request makes available() false; validate without that flag. */
        if(!v->ready||!v->known||v->busy||v->pending)return 0;
    }
    if(command==PIMG_MODE){if(v->request_mode>=6)return 0;request.mode=saved_mode=v->request_mode;
        if(v->follow)command=PIMG_FOLLOW_MODE;}
    if(command==PIMG_FULL&&v->follow)request.mode=v->follow_mode;
    if(command==PIMG_ROI){if(!v->preview_valid||v->w<8||v->h<8||v->x+v->w>1280||v->y+v->h>720)return 0;
        request.x=v->x;request.y=v->y;request.w=v->w;request.h=v->h;request.roi=1;request.mode=selected;}
    if(command==PIMG_PARAMS){
        if(selected==2)request.threshold=(unsigned)v->draft_threshold;
        if(selected==3)request.brightness=v->draft_brightness;
        if(selected==5)request.gain=v->draft_gain;
        if(v->follow)command=PIMG_FOLLOW_PARAMS;
        if(request.threshold>255||request.brightness<-255||request.brightness>255||request.gain<-2||request.gain>2)return 0;}
    if(command==PIMG_FOLLOW||command==PIMG_FOLLOW_MODE||command==PIMG_FOLLOW_PARAMS) {
        if(!v->follow||v->follow_mode>=6)return 0;
        unsigned w=command==PIMG_FOLLOW_PARAMS?v->draft_follow_w:panel_follow_width(v);
        unsigned h=command==PIMG_FOLLOW_PARAMS?v->draft_follow_h:panel_follow_height(v);
        if(w<80||w>1280||h<60||h>720)return 0;
        request.mode=v->follow_visible?saved_mode:0;request.roi=1;request.w=w;request.h=h;
        if(command==PIMG_FOLLOW&&!v->follow_visible&&v->active.roi){request=v->active;request.mode=0;}
        else panel_follow_rectangle(v->follow_have?v->follow_cx:640,v->follow_have?v->follow_cy:360,w,h,&request.x,&request.y);
    }
    char *argv[20],numbers[7][24];if(!panel_image_argv(command,&request,argv,numbers,selected))return 0;
    int pipefd[2];if(pipe2(pipefd,O_CLOEXEC))return 0;
    if(fcntl(pipefd[0],F_SETFL,O_NONBLOCK)){close(pipefd[0]);close(pipefd[1]);return 0;}
    pid_t parent=getpid(),child=fork();
    if(!child) {
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent||dup2(pipefd[1],STDOUT_FILENO)<0)_exit(127);
        close(pipefd[0]);close(pipefd[1]);execv(PANEL_IMAGE_HELPER,argv);_exit(127);
    }
    close(pipefd[1]);if(child<0){close(pipefd[0]);return 0;}
    j->pid=child;j->fd=pipefd[0];j->used=0;j->overflow=0;j->cancelling=0;
    j->command=command;j->requested=request;j->saved_mode=saved_mode;j->tracking=v->follow_visible;j->deadline_ms=now+1800;
    v->request=0;v->busy=1;return 1;
}
static inline void panel_image_read(struct panel_image_job *j) {
    for(unsigned budget=0;budget<4;budget++) {
        if(j->used==sizeof(j->reply)-1){char extra;if(read(j->fd,&extra,1)>0)j->overflow=1;return;}
        ssize_t n=read(j->fd,j->reply+j->used,sizeof(j->reply)-1-j->used);
        if(n<=0)return;
        j->used+=(size_t)n;
    }
}
static inline void panel_image_tick(struct panel_image_job *j,struct panel_image_view *v,uint64_t now) {
    if(v->known&&now-v->sampled_ms>6000){v->known=0;v->slider_dragging=0;v->revision++;}
    if(j->pid) {
        panel_image_read(j);
        if((now>=j->deadline_ms||j->overflow)&&!j->cancelling){kill(j->pid,SIGTERM);j->cancelling=1;j->cancel_ms=now;}
        if(j->cancelling&&now-j->cancel_ms>=500)kill(j->pid,SIGKILL);
        int status=0;pid_t done=waitpid(j->pid,&status,WNOHANG);
        if(done==j->pid||(done<0&&errno!=EINTR)) {
            panel_image_read(j);j->reply[j->used]=0;
            struct panel_image_config config={0};int pending=0;
            int ok=done==j->pid&&WIFEXITED(status)&&!WEXITSTATUS(status)&&!j->cancelling&&!j->overflow&&
                panel_image_parse(j->reply,j->used,&config,&pending,j->command!=PIMG_STATUS)&&
                (j->command==PIMG_STATUS||(!pending&&panel_image_matches(j->command,&config,&j->requested,j->saved_mode)));
            v->busy=0;
            int changed=0;
            if(ok) {
                changed=!v->known||memcmp(&v->active,&config,sizeof(config))||v->pending!=pending;
                if(changed)v->slider_dragging=0;
                if(v->settings&&j->command==PIMG_STATUS&&(!v->known||v->active.mode!=config.mode)) {
                    v->draft_threshold=(int)config.threshold;v->draft_brightness=config.brightness;v->draft_gain=config.gain;
                }
                v->active=config;v->known=1;v->pending=pending;v->sampled_ms=now;
                if(j->command!=PIMG_STATUS){v->error=0;if(j->command!=PIMG_FOLLOW)v->revision++;}
                else if(changed)v->revision++;
                if(j->command==PIMG_FOLLOW_MODE)v->follow_mode=j->saved_mode;
                if(j->command==PIMG_FOLLOW_PARAMS){v->follow_w=config.w;v->follow_h=config.h;}
                if(j->command==PIMG_FOLLOW||j->command==PIMG_FOLLOW_MODE||j->command==PIMG_FOLLOW_PARAMS)
                    v->follow_applied=j->tracking&&v->follow_visible;
                if(j->command==PIMG_FULL||j->command==PIMG_ROI||j->command==PIMG_ORIGINAL||j->command==PIMG_DEFAULTS) {
                    v->follow=0;v->follow_visible=v->follow_have=v->follow_applied=v->follow_fault=0;
                }
                if(j->command==PIMG_ROI){v->editing=0;v->preview_valid=0;}
                if(j->command==PIMG_PARAMS||j->command==PIMG_FOLLOW_PARAMS||j->command==PIMG_DEFAULTS){v->draft_threshold=(int)config.threshold;
                    v->draft_brightness=config.brightness;v->draft_gain=config.gain;}
            } else {v->known=0;v->error=1;v->slider_dragging=0;v->revision++;if(v->follow){v->follow_fault=1;v->follow_applied=0;}}
            if(j->command!=PIMG_STATUS||!ok||changed) {
                printf("IMAGE_RESULT command=%u ok=%d mode=%u roi=%d pending=%d\n",j->command,ok,v->active.mode,v->active.roi,v->pending);fflush(stdout);
            }
            close(j->fd);j->fd=-1;j->pid=0;j->cancelling=0;j->next_ms=now+(ok?2000:1000);
        }
    }
    if(v->ready&&!j->pid&&(v->request||now>=j->next_ms)) {
        if(!panel_image_spawn(j,v,now)){v->request=0;v->error=1;v->known=0;
            if(v->follow){v->follow_fault=1;v->follow_applied=0;}
            j->next_ms=now+2000;}
    }
}
static inline void panel_image_stop(struct panel_image_job *j) {
    if(j->pid){kill(j->pid,SIGTERM);waitpid(j->pid,NULL,WNOHANG);}
    if(j->fd>=0)close(j->fd);
    j->pid=0;j->fd=-1;
}
#endif

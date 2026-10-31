#ifndef VIDEO_STATUS_ROBOT_H
#define VIDEO_STATUS_ROBOT_H
#include <sys/random.h>
#include <sys/stat.h>
#include "video_status_view.h"
#ifndef PANEL_ROBOT_SOCKET
#define PANEL_ROBOT_SOCKET "/run/robot-control.sock"
#endif
#ifndef PANEL_ROBOT_STATE_FILE
#define PANEL_ROBOT_STATE_FILE "/run/robot-control.state"
#endif
#ifndef PANEL_ROBOT_STATE_UID
#define PANEL_ROBOT_STATE_UID 0u
#endif
/* Strict metadata schema; never overwrite local UI/confirmation state. */
static inline int panel_robot_parse(const char *data,size_t len,struct panel_robot_view *r,uint64_t now){
    static const char *keys[]={"version","monotonic_ms","uart_ready","fault","id","region","phase","feedback_seen","region_mask",
        "mode","transaction_pending","track_armed","track_region","track_known","track_mask","cycle_known","cycle_ready",
        "cycle_active","cycle_stage","cycle_reason","cycle_limit","cycle_completed","cycle_stopping","mode_error","lease_valid","reply_age_ms",
        "auto_mode","auto_waiting","auto_latched"};
    static const unsigned limits[]={0,0,1,1,65535,3,ROBOT_EXECUTING,1,7,2,1,1,12,1,4095,1,1,1,2,4,255,65535,1,RM_BAD_CONFIG,1,UINT32_MAX,2,1,1};
    uint32_t seen=0,value[29]={0};uint64_t stamp=0;
    struct pl_json j={data,data+len,0};pl_json_space(&j);if(j.p==j.end||*j.p++!='{')return 0;
    while(j.p<j.end){const char *key;size_t n;pl_json_space(&j);if(!pl_json_string(&j,&key,&n))return 0;
        pl_json_space(&j);if(j.p==j.end||*j.p++!=':')return 0;pl_json_space(&j);const char *start=j.p;
        if(!pl_json_value(&j))return 0;
        unsigned k;for(k=0;k<29;k++)if(strlen(keys[k])==n&&!memcmp(keys[k],key,n))break;
        if(k<29){if(seen&(1u<<k))return 0;seen|=1u<<k;
            if(!k){char version[64];if(!panel_string_value(start,j.p,version,sizeof(version))||strcmp(version,ROBOT_VERSION))return 0;}
            else if(k==1){if(start==j.p)return 0;for(const char *p=start;p<j.p;p++){
                if(*p<'0'||*p>'9'||stamp>UINT64_MAX/10||(stamp==UINT64_MAX/10&&(unsigned)(*p-'0')>UINT64_MAX%10))return 0;
                stamp=stamp*10+(unsigned)(*p-'0');}}
            else if(!panel_number(start,j.p,&value[k])||value[k]>limits[k])return 0;
        }
        pl_json_space(&j);if(j.p==j.end)return 0;char c=*j.p++;if(c=='}')break;if(c!=',')return 0;
    }
    pl_json_space(&j);if(seen!=((1u<<29)-1)||j.p!=j.end||now<stamp||now-stamp>1500)return 0;
    /* A queued socket reply may be older than the atomically published file
     * just read in this tick. A valid ACK must not roll state backwards. */
    if(stamp<r->status_ms){r->fresh=now>=r->status_ms&&now-r->status_ms<=1500;return r->fresh;}
    r->fresh=1;r->status_ms=stamp;r->uart_ready=(int)value[2];r->fault=(int)value[3];r->id=value[4];r->phase=value[6];
    r->feedback_seen=(int)value[7];r->region_mask=value[8];r->mode=value[9];r->transaction_pending=value[10];
    r->track_armed=value[11];r->track_region=value[12];r->track_known=value[13];r->track_mask=value[14];
    r->cycle_known=value[15];r->cycle_ready=value[16];r->cycle_active=value[17];r->cycle_stage=value[18];
    r->cycle_reason=value[19];r->cycle_limit=value[20];r->cycle_completed=value[21];r->cycle_stopping=value[22];
    r->mode_error=value[23];r->lease_valid=value[24];r->reply_age_ms=value[25];
    r->auto_mode=value[26];r->auto_waiting=value[27];r->auto_latched=value[28];return 1;
}
static inline int panel_robot_submit_text(const char *command){
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un a={.sun_family=AF_UNIX};
    if(strlen(PANEL_ROBOT_SOCKET)>=sizeof(a.sun_path)){close(fd);return -1;}
    strcpy(a.sun_path,PANEL_ROBOT_SOCKET);
    if(connect(fd,(struct sockaddr *)&a,sizeof(a))&&errno!=EINPROGRESS){close(fd);return -1;}
    struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t size=sizeof(error);
    if(poll(&p,1,15)<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)||error){close(fd);return -1;}
    size_t length=strlen(command);
    if(send(fd,command,length,MSG_NOSIGNAL)!=(ssize_t)length){close(fd);return -1;}return fd;
}
static inline void panel_robot_update(struct panel_view *v,const struct panel_status *s,uint64_t now){
    static struct {int fd,kind;size_t used;uint64_t deadline,next_lease;unsigned long long nonce,lease_nonce;char data[4096];} ipc={.fd=-1};
    struct panel_robot_view *r=&v->robot;r->target=robot_manual_target(s);r->target_ms=now;r->track_target=robot_track_region(s);
    r->col=r->target?s->raw_col:-1;r->row=r->target?s->raw_row:-1;
    if(r->modal&&r->confirm_kind==0)r->modal_region=r->target;
    char data[ROBOT_STATE_CAP];int fd=open(PANEL_ROBOT_STATE_FILE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);r->fresh=0;
    if(fd>=0){struct stat st;ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==PANEL_ROBOT_STATE_UID)n=read(fd,data,sizeof(data));
        close(fd);if(n>0&&n<(ssize_t)sizeof(data))panel_robot_parse(data,(size_t)n,r,now);}
    if(r->modal&&(r->confirm_kind==0?now>=r->modal_until:!panel_robot_confirmable(r,now))){
        r->modal=0;r->revision++;r->error=2;r->error_until=now+3000;}
    if(ipc.fd>=0){ssize_t n=recv(ipc.fd,ipc.data+ipc.used,sizeof(ipc.data)-ipc.used,MSG_DONTWAIT);
        if(n>0)ipc.used+=(size_t)n;
        int complete=memchr(ipc.data,'\n',ipc.used)!=NULL;
        if(complete||n==0||(n<0&&errno!=EAGAIN&&errno!=EINTR)||now>=ipc.deadline||ipc.used==sizeof(ipc.data)){
            int ok=complete&&ipc.used>4&&!memcmp(ipc.data,"OK ",3)&&panel_robot_parse(ipc.data+3,ipc.used-3,r,now);
            if(!ok){r->error=1;r->error_until=now+3000;if(ipc.kind>=2)ipc.lease_nonce=0;}
            else if((ipc.kind>=2&&ipc.kind<=5)||ipc.kind==7||ipc.kind==8){ipc.lease_nonce=ipc.nonce;ipc.next_lease=now;}
            else if(ipc.kind==9)ipc.lease_nonce=0;
            close(ipc.fd);ipc.fd=-1;r->submitting=0;
        }
    }
    if(!r->fresh||r->phase==ROBOT_UNKNOWN||(!r->auto_mode&&(r->phase==ROBOT_COMPLETE||r->phase==ROBOT_FAILED)))ipc.lease_nonce=0;
    if(ipc.fd<0&&(r->request||r->operation)){
        unsigned region=(unsigned)r->request;int op=r->operation;unsigned param=r->operation_value;
        r->request=r->operation=0;unsigned long long nonce=0;char command[96]={0};int valid=0;
        if(getrandom(&nonce,sizeof(nonce),GRND_NONBLOCK)!=(ssize_t)sizeof(nonce)||!nonce)nonce=0;
        if(region){region=r->target;valid=panel_robot_ready(r,now);snprintf(command,sizeof(command),"execute %u %016llx\n",region,nonce);}
        else if(op==1){valid=panel_robot_idle(r,now);snprintf(command,sizeof(command),"inspect %016llx\n",nonce);}
        else if(op==2){valid=param==r->track_target&&panel_robot_track_ready(r,now);snprintf(command,sizeof(command),"track-start %u %016llx\n",param,nonce);}
        else if(op==3){valid=r->fresh&&r->uart_ready&&r->mode==ROBOT_MODE_TRACK;snprintf(command,sizeof(command),"track-hold %016llx\n",nonce);}
        else if(op==4){valid=param<=255&&panel_robot_idle(r,now)&&r->cycle_known&&r->cycle_ready;snprintf(command,sizeof(command),"cycle-start %u %016llx\n",param,nonce);}
        else if(op==5){valid=r->fresh&&r->uart_ready&&r->mode==ROBOT_MODE_CYCLE;snprintf(command,sizeof(command),"cycle-stop %016llx\n",nonce);}
        else if(op==7||op==8){valid=panel_robot_auto_toggle(r,op==7?1:2,now)&&!r->auto_mode;
            snprintf(command,sizeof(command),op==7?"auto-track %016llx\n":"auto-grab %016llx\n",nonce);}
        else if(op==9){valid=r->fresh&&r->uart_ready;snprintf(command,sizeof(command),"auto-off %016llx\n",nonce);}
        if(!valid||!nonce||(ipc.fd=panel_robot_submit_text(command))<0){r->error=1;r->error_until=now+3000;
            if(op==3||op==5||op==9)ipc.lease_nonce=0;}
        else {ipc.used=0;ipc.kind=op;ipc.nonce=nonce;ipc.deadline=now+1500;r->submitting=1;r->error=0;
            printf("ROBOT_USER_REQUEST operation=%d grab_region=%u (no automatic motion retry)\n",op,region);fflush(stdout);}
    }else if(ipc.fd<0&&ipc.lease_nonce&&now>=ipc.next_lease){
        char command[64];snprintf(command,sizeof(command),"lease %016llx\n",ipc.lease_nonce);
        ipc.next_lease=now+250;ipc.fd=panel_robot_submit_text(command);
        if(ipc.fd<0)ipc.lease_nonce=0;
        else {ipc.used=0;ipc.kind=6;ipc.deadline=now+500;}
    }
    if(r->error&&now>=r->error_until)r->error=0;
    v->robot_state=panel_robot_state_of(r);
}
#endif

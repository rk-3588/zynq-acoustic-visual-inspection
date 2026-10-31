/* Headless real UI IPC/tick path; endpoints are compile-time test substitutions. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
static const char *test_socket,*test_state;
#define PANEL_ROBOT_SOCKET test_socket
#define PANEL_ROBOT_STATE_FILE test_state
#define PANEL_ROBOT_STATE_UID getuid()
#include "video_status_render.h"
#include "video_status_robot.h"
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    test_socket=argv[1];test_state=argv[2];
    setvbuf(stdin,NULL,_IONBF,0);
    struct panel_view v={0};
    struct panel_status s={.online=1,.valid=1,.col=32,.row=18,.sound="active",.position="stable",
        .acoustic_known=1,.acoustic_enabled=1,.enable_applied=1};
    uint64_t next=0;
    for(;;){
        struct pollfd p={STDIN_FILENO,POLLIN,0};
        if(poll(&p,1,40)>0){
            if(p.revents&POLLHUP)break;
            char line[80];if(!fgets(line,sizeof(line),stdin))break;
            uint64_t now=now_ms();
            if(!strcmp(line,"OPEN\n"))panel_robot_action(&v,70,now);
            else if(!strcmp(line,"CLOSE\n"))panel_robot_action(&v,90,now);
            else if(!strcmp(line,"INSPECT\n"))panel_robot_action(&v,95,now);
            else if(!strcmp(line,"TRACK\n")){panel_robot_action(&v,96,now);panel_robot_action(&v,72,now);}
            else if(!strcmp(line,"HOLD\n"))panel_robot_action(&v,97,now);
            else if(!strcmp(line,"AUTO_OFF\n")){v.robot.operation=9;}
            else if(!strcmp(line,"AUTO_GRAB\n")){panel_robot_action(&v,104,now);panel_robot_action(&v,105,now);}
            else if(!strcmp(line,"STOP\n"))panel_robot_action(&v,99,now);
            else if(!strcmp(line,"CYCLE\n")){v.robot.cycle_draft=2;panel_robot_action(&v,98,now);panel_robot_action(&v,72,now);}
            else if(!strcmp(line,"QUIT\n"))break;
            fprintf(stderr,"UI_TEST phase=%u fresh=%d pending=%u track_known=%u target=%u operation=%d modal=%d\n",
                v.robot.phase,v.robot.fresh,v.robot.transaction_pending,v.robot.track_known,v.robot.track_target,
                v.robot.operation,v.robot.modal);
        }
        uint64_t now=now_ms();if(now>=next){panel_robot_update(&v,&s,now);next=now+200;}
    }
    return 0;
}

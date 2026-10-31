#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef FREQUENCY_PROGRAM
#define FREQUENCY_PROGRAM "/usr/bin/acoustic_frequency"
#endif
#ifndef FREQUENCY_STATE
#define FREQUENCY_STATE "/run/acoustic-frequency.state"
#endif
#ifndef FREQUENCY_FEED_LOCK
#define FREQUENCY_FEED_LOCK "/run/acoustic-frequency-feed.lock"
#endif
#define FEED_VERSION "20261013-acoustic-frequency-feed-v1"
static volatile sig_atomic_t stopping;
static void stop_handler(int sig){(void)sig;stopping=1;}
static int publish(const char *line,size_t size) {
    const char *temporary=FREQUENCY_STATE ".new";
    if(size<2 || line[0]!='{' || line[size-1]!='}')return -1;
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
    if(fd<0)return -1;
    size_t used=0;
    while(used<size) {
        ssize_t n=write(fd,line+used,size-used);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){close(fd);unlink(temporary);return -1;}
        used+=(size_t)n;
    }
    if(close(fd) || rename(temporary,FREQUENCY_STATE)){unlink(temporary);return -1;}
    return 0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")){puts(FEED_VERSION);return 0;}
    if(argc!=1){fprintf(stderr,"Usage: %s [--version]\n",argv[0]);return 2;}
    int lock=open(FREQUENCY_FEED_LOCK,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)){perror("frequency feed ownership");if(lock>=0)close(lock);return 3;}
    struct sigaction action={0};action.sa_handler=stop_handler;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGHUP,&action,NULL);
    unlink(FREQUENCY_STATE);unlink(FREQUENCY_STATE ".new");
    printf("FREQUENCY_FEED_ACTIVE %s channel=0 source=single_mic_hpf_output\n",FEED_VERSION);fflush(stdout);
    while(!stopping) {
        int pipefd[2];if(pipe2(pipefd,O_CLOEXEC)){perror("pipe");break;}
        pid_t child=fork();
        if(child<0){close(pipefd[0]);close(pipefd[1]);perror("fork");break;}
        if(!child) {
            close(pipefd[0]);
            if(dup2(pipefd[1],STDOUT_FILENO)<0)_exit(126);
            close(pipefd[1]);close(lock);
            execl(FREQUENCY_PROGRAM,FREQUENCY_PROGRAM,"--capture","--channel","0","--blocks","100",(char *)NULL);
            perror("acoustic_frequency exec");_exit(127);
        }
        close(pipefd[1]);
        char line[2048];size_t used=0;int failed=0;
        while(!stopping && !failed) {
            struct pollfd p={pipefd[0],POLLIN,0};
            int ready=poll(&p,1,100);
            if(ready<0){if(errno==EINTR)continue;failed=1;break;}
            if(!ready)continue;
            char data[1024];ssize_t n=read(pipefd[0],data,sizeof(data));
            if(n<0 && errno==EINTR)continue;
            if(n<=0)break;
            for(ssize_t i=0;i<n;i++) {
                if(data[i]=='\n') {
                    if(publish(line,used)){failed=1;break;}
                    used=0;
                } else {
                    if(used>=sizeof(line)-1){failed=1;break;}
                    line[used++]=data[i];
                }
            }
        }
        close(pipefd[0]);
        if(stopping || failed)kill(child,SIGTERM);
        int status=0;pid_t waited;
        do {waited=waitpid(child,&status,0);}while(waited<0 && errno==EINTR);
        /* Only this child's normal, complete run can immediately restart. */
        if(waited!=child || !WIFEXITED(status) || WEXITSTATUS(status) || failed || used) {
            unlink(FREQUENCY_STATE);
            if(!stopping) {
                fprintf(stderr,"FREQUENCY_FEED_RETRY child_status=%d invalid_stream=%d\n",status,failed || used!=0);
                for(unsigned i=0;i<50 && !stopping;i++)usleep(100000);
            }
        }
    }
    unlink(FREQUENCY_STATE);unlink(FREQUENCY_STATE ".new");
    puts("FREQUENCY_FEED_STOPPED");close(lock);return stopping?0:4;
}

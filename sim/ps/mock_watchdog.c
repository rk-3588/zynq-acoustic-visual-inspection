/* Isolated service-lifecycle mock. Does not read any hardware or restart UI. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stopped;
static void stop(int sig){(void)sig;stopped=1;}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--check"))return 0;
    if(argc!=2||strcmp(argv[1],"--run"))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);
    while(!stopped){struct timespec delay={0,10000000};nanosleep(&delay,0);}
    return 0;
}

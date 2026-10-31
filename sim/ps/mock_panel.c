/* Service-test daemon only: no hardware, serial, video or USB access. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static void stop_handler(int sig) { (void)sig; stopping=1; }

static FILE *mock_file(const char *name, const char *mode) {
    char path[4096];
    if (snprintf(path,sizeof(path),"%s/%s",getenv("MOCK_PANEL_DIR"),name)>=(int)sizeof(path))
        exit(90);
    return fopen(path,mode);
}

int main(int argc,char **argv) {
    char mode[64]="normal";
    FILE *f=mock_file("mode","r");
    if (f) { if (!fgets(mode,sizeof(mode),f)) return 91; fclose(f); }
    mode[strcspn(mode,"\r\n")]=0;
    if (argc==2 && !strcmp(argv[1],"--check")) return !strcmp(mode,"unready");
    if (argc!=2 || strcmp(argv[1],"--run")) return 92;
    unsigned count=0;
    f=mock_file("count","r");
    if (f) { if (fscanf(f,"%u",&count)!=1) return 93; fclose(f); }
    ++count;
    f=mock_file("count","w"); if (!f) return 94;
    fprintf(f,"%u\n",count); fclose(f);
    if ((!strcmp(mode,"fail-first") && count==1) ||
        (!strcmp(mode,"fail-second") && count==2)) return 7;
    signal(SIGINT,stop_handler);
    signal(SIGTERM,!strcmp(mode,"ignore-term")?SIG_IGN:stop_handler);
    if (strcmp(mode,"no-active")) {
        printf("PANEL_ACTIVE mock run=%u pid=%ld\n",count,(long)getpid());
        fflush(stdout);
    }
    if (!strcmp(mode,"exit-first") && count==1) {
        struct timespec delay={0,150000000}; nanosleep(&delay,NULL); return 8;
    }
    while (!stopping) { struct timespec delay={0,10000000}; nanosleep(&delay,NULL); }
    if (strcmp(mode,"no-restore")) printf("PANEL_RESTORED mock run=%u primary-only\n",count);
    return 0;
}

#define _GNU_SOURCE
#include "video_output_observe.h"
static int number(const char *text,unsigned lo,unsigned hi,unsigned *out) {
    char *end;errno=0;unsigned long n=strtoul(text,&end,10);
    if(errno||end==text||*end||*text=='-'||n<lo||n>hi)return 0;
    *out=(unsigned)n;return 1;
}
int main(int argc,char **argv) {
    unsigned count=10,interval=200;
    if(argc==2 && !strcmp(argv[1],"--version")){puts("20261016-video-observer-v1");return 0;}
    if(argc==5 && !strcmp(argv[1],"--count") && !strcmp(argv[3],"--interval-ms")) {
        if(!number(argv[2],1,10000,&count)||!number(argv[4],20,10000,&interval))return 2;
    }else if(argc!=1){fprintf(stderr,"Usage: video_observer [--count 1..10000 --interval-ms 20..10000]\n");return 2;}
    for(unsigned i=0;i<count;i++) {
        struct vo_snapshot s;vo_sample(&s);vo_print(stdout,"probe",&s);
        if(i+1<count)usleep(interval*1000u);
    }
    return 0; // Missing monitor is explicit in JSON, never a reason to change video.
}

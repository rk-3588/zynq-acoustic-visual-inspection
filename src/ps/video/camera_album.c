#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <signal.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <time.h>
#include "camera_album_core.h"

static volatile sig_atomic_t interrupted;
static void interrupt(int sig){(void)sig;interrupted=1;}
static struct {uint64_t read_us,crc_us,reduce_us,cache_us;unsigned cache_hits,cache_misses;} perf;
static uint64_t album_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
struct catalog_item {struct album_meta meta;int64_t modified;};
static int compare(const void *a,const void *b) {
    const struct catalog_item *x=a,*y=b;
    if(x->meta.sequence!=y->meta.sequence)return x->meta.sequence>y->meta.sequence?-1:1;
    /* Legacy v1 has no persistent sequence; wallclock ordering is best effort.
     * UI explicitly shows boot-relative time, not an apparently verified RTC. */
    if(x->modified!=y->modified)return x->modified>y->modified?-1:1;
    return strcmp(y->meta.id,x->meta.id);
}
static int metadata(int dir,const char *id,struct album_meta *meta,char *json) {
    int n=album_read_text(dir,"measurement.json",json,ALBUM_JSON);
    if(n<=0||!album_parse(json,(size_t)n,id,meta))return 0;
    int bmp=album_bmp_open(dir,"camera.bmp",SNAP_WIDTH,SNAP_HEIGHT);if(bmp<0)return 0;
    close(bmp);meta->sequence=album_read_sequence(dir,"album.seq");return 1;
}
static unsigned scan(int root,struct catalog_item *items,unsigned *bad) {
    int fd=openat(root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;
    DIR *dir=fdopendir(fd);if(!dir){close(fd);return 0;}
    unsigned n=0;struct dirent *e;char json[ALBUM_JSON];*bad=0;
    while(!interrupted&&(e=readdir(dir)))if(album_id_valid(e->d_name)) {
        if(n==ALBUM_LIMIT){(*bad)++;continue;}
        int shot=openat(root,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct stat st;
        if(shot>=0&&!fstatat(shot,"camera.bmp",&st,AT_SYMLINK_NOFOLLOW)&&metadata(shot,e->d_name,&items[n].meta,json)) {
            items[n].modified=(int64_t)st.st_mtime;n++;
        } else (*bad)++;
        if(shot>=0)close(shot);
    }
    closedir(dir);qsort(items,n,sizeof(items[0]),compare);return n;
}
static unsigned char *original(int dir,const struct album_meta *m) {
    uint64_t begin=album_ns();
    int fd=album_bmp_open(dir,"camera.bmp",SNAP_WIDTH,SNAP_HEIGHT);if(fd<0)return NULL;
    unsigned char *pixels=malloc(SNAP_BYTES);if(!pixels){close(fd);return NULL;}
    int ok=album_read_exact(fd,pixels,SNAP_BYTES);close(fd);
    perf.read_us+=(album_ns()-begin)/1000;
    uint32_t crc=(uint32_t)strtoul(m->crc,NULL,16);
    begin=album_ns();
    int verified=ok&&!interrupted&&snap_crc32(pixels,SNAP_BYTES)==crc;
    perf.crc_us+=(album_ns()-begin)/1000;
    if(!verified){free(pixels);return NULL;}
    return pixels;
}
static int thumbnail(int root,struct album_entry *entry) {
    int dir=openat(root,entry->meta.id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 0;
    unsigned char bgr[ALBUM_TBYTES];uint32_t crc=(uint32_t)strtoul(entry->meta.crc,NULL,16);
    uint64_t begin=album_ns();
    int ok=album_cache_read(dir,crc,bgr);
    perf.cache_us+=(album_ns()-begin)/1000;
    if(ok)perf.cache_hits++;else perf.cache_misses++;
    if(!ok&&!interrupted) {
        unsigned char *source=original(dir,&entry->meta);
        if(source) {
            begin=album_ns();album_reduce(source,bgr,ALBUM_TW,ALBUM_TH);
            perf.reduce_us+=(album_ns()-begin)/1000;free(source);ok=1;
            struct statvfs space;
            if(!interrupted&&!fstatvfs(root,&space)&&(uint64_t)space.f_bavail*space.f_frsize>=64ull*1024*1024)
                (void)album_cache_write(dir,crc,bgr); /* Optional derived cache; don't fail browsing on read-only media. */
        }
    }
    if(ok)album_rgba(bgr,entry->pixels,ALBUM_TW*ALBUM_TH);
    close(dir);entry->meta.thumbnail_ok=(unsigned)ok;return ok;
}
static int build_page(int root,unsigned page,struct album_packet *p) {
    struct catalog_item *items=calloc(ALBUM_LIMIT,sizeof(*items));if(!items)return 0;
    unsigned bad=0,total=scan(root,items,&bad),pages=(total+ALBUM_PAGE-1)/ALBUM_PAGE;
    if(pages&&page>=pages)page=pages-1;
    if(!pages)page=0;
    p->magic=ALBUM_MAGIC;p->bytes=ALBUM_PAGE_BYTES;p->kind=1;p->page=page;p->total=total;p->bad=bad;
    struct statvfs space;if(!fstatvfs(root,&space))p->free_bytes=(uint64_t)space.f_bavail*space.f_frsize;
    for(unsigned i=page*ALBUM_PAGE;i<total&&p->n<ALBUM_PAGE;i++) {
        struct album_entry *e=&p->entries[p->n++];e->meta=items[i].meta;
        if(!e->meta.sequence)e->meta.sequence=total-i; /* Display order only for pre-sequence v1 legacy. */
        thumbnail(root,e);if(interrupted)break;
    }
    free(items);return !interrupted;
}
static int build_detail(int root,const char *id,struct album_packet *p) {
    p->magic=ALBUM_MAGIC;p->bytes=sizeof(*p);p->kind=2;p->n=1;
    int dir=openat(root,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 0;
    struct album_meta *m=&p->entries[0].meta;
    int ok=metadata(dir,id,m,p->json);
    unsigned char *source=ok?original(dir,m):NULL;close(dir);
    if(!source)return 0;
    unsigned char *reduced=malloc(ALBUM_PW*ALBUM_PH*3u);if(!reduced){free(source);return 0;}
    uint64_t begin=album_ns();album_reduce(source,reduced,ALBUM_PW,ALBUM_PH);
    perf.reduce_us+=(album_ns()-begin)/1000;free(source);
    album_rgba(reduced,p->preview,ALBUM_PW*ALBUM_PH);free(reduced);return !interrupted;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts(ALBUM_VERSION);return 0;}
    if(argc==2&&!strcmp(argv[1],"--sizes")) {
        printf("packet=%zu page=%zu metadata=%zu\n",sizeof(struct album_packet),(size_t)ALBUM_PAGE_BYTES,sizeof(struct album_meta));return 0;
    }
    uint64_t page=0;int list=argc==2&&!strcmp(argv[1],"--list");
    int is_page=argc==3&&!strcmp(argv[1],"--page")&&panel_freq_uint(argv[2],argv[2]+strlen(argv[2]),&page)&&page<ALBUM_LIMIT;
    int detail=argc==3&&!strcmp(argv[1],"--detail")&&album_id_valid(argv[2]);
    if(!list&&!is_page&&!detail)return 2;
    struct sigaction sa={0};sa.sa_handler=interrupt;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
    int root=open(SNAP_ROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return 3;
    struct statfs fs;if(fstatfs(root,&fs)||fs.f_type!=0xef53){close(root);return 3;}
    if(list) {
        struct catalog_item *items=calloc(ALBUM_LIMIT,sizeof(*items));if(!items){close(root);return 3;}
        unsigned bad=0,n=scan(root,items,&bad);
        printf("{\"version\":\"%s\",\"total\":%u,\"bad\":%u,\"items\":[",ALBUM_VERSION,n,bad);
        for(unsigned i=0;i<n;i++)printf("%s{\"id\":\"%s\",\"sequence\":%llu}",i?",":"",items[i].meta.id,(unsigned long long)items[i].meta.sequence);
        puts("]}");free(items);close(root);return interrupted?3:0;
    }
    struct album_packet *p=calloc(1,sizeof(*p));if(!p){close(root);return 3;}
    uint64_t begin=album_ns();
    int ok=detail?build_detail(root,argv[2],p):build_page(root,(unsigned)page,p);
    if(!ok) {
        p->magic=ALBUM_MAGIC;p->status=1;p->kind=detail?2:1;
        p->bytes=ALBUM_PAGE_BYTES;
    }
    uint64_t built=album_ns();
    if(interrupted)ok=0;
    else ok=album_write_exact(STDOUT_FILENO,p,p->bytes)&&ok;
    fprintf(stderr,"ALBUM_TIMING kind=%u ok=%d build_us=%llu pipe_us=%llu read_us=%llu crc_us=%llu reduce_us=%llu cache_us=%llu cache_hits=%u cache_misses=%u bytes=%u\n",
        p->kind,ok,(unsigned long long)((built-begin)/1000),(unsigned long long)((album_ns()-built)/1000),
        (unsigned long long)perf.read_us,(unsigned long long)perf.crc_us,(unsigned long long)perf.reduce_us,
        (unsigned long long)perf.cache_us,perf.cache_hits,perf.cache_misses,p->bytes);
    free(p);close(root);return ok?0:3;
}

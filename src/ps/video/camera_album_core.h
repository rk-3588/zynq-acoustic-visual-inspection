#ifndef CAMERA_ALBUM_CORE_H
#define CAMERA_ALBUM_CORE_H
#include "camera_snapshot_core.h"
#include "video_status_frequency.h"
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#define ALBUM_VERSION "20261030-camera-album-v2"
#define ALBUM_MAGIC 0x414c4231u
#define ALBUM_TW 160u
#define ALBUM_TH 90u
#define ALBUM_TBYTES (ALBUM_TW*ALBUM_TH*3u)
#define ALBUM_PW 640u
#define ALBUM_PH 360u
#define ALBUM_PAGE 4u
#define ALBUM_LIMIT 500u
#define ALBUM_JSON 8192u
struct album_meta {
    uint64_t sequence,trigger_ns;
    char id[128],boot[48],utc[40],sound[24],position[24],crc[16];
    int32_t raw_col,raw_row,stable_col,stable_row,temp_mc;
    uint32_t position_valid,frequency_valid,energy_valid,energy,temp_valid,thumbnail_ok;
    uint32_t frequency_channel;
    double frequency,read_after_copy_ms;
};
struct album_entry {struct album_meta meta;uint32_t pixels[ALBUM_TW*ALBUM_TH];};
/* Fixed-width local little-endian pipe protocol, never an untrusted pointer.
 * page response ends before preview; detail response includes the whole packet. */
struct album_packet {
    uint64_t free_bytes;
    uint32_t magic,bytes,kind,status,page,total,bad,n;
    struct album_entry entries[ALBUM_PAGE];
    uint32_t preview[ALBUM_PW*ALBUM_PH];
    char json[ALBUM_JSON];
};
#define ALBUM_PAGE_BYTES offsetof(struct album_packet,preview)
_Static_assert(sizeof(uint32_t)==4 && sizeof(uint64_t)==8,"wire integer widths");
static inline int album_id_valid(const char *s) {
    size_t n=strlen(s);if(n<6 || n>=128 || strncmp(s,"shot-",5))return 0;
    for(size_t i=5;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||
        (s[i]>='0'&&s[i]<='9')||s[i]=='-'))return 0;
    return 1;
}
static inline int album_read_exact(int fd,void *data,size_t size) {
    size_t used=0;while(used<size) {
        ssize_t n=read(fd,(unsigned char *)data+used,size-used);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return 0;
        used+=(size_t)n;
    }return 1;
}
static inline int album_write_exact(int fd,const void *data,size_t size) {
    size_t used=0;while(used<size) {
        ssize_t n=write(fd,(const unsigned char *)data+used,size-used);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return 0;
        used+=(size_t)n;
    }return 1;
}
static inline int album_read_text(int dir,const char *name,char *text,size_t size) {
    int fd=openat(dir,name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
    struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<(off_t)size;
    if(ok)ok=album_read_exact(fd,text,(size_t)st.st_size);
    close(fd);if(!ok)return -1;text[st.st_size]=0;return (int)st.st_size;
}
static inline void album_bmp_header(unsigned char h[54],unsigned w,unsigned height) {
    snap_bmp_header(h);snap_le32(h+2,54+w*height*3);snap_le32(h+18,w);
    snap_le32(h+22,(uint32_t)-(int32_t)height);snap_le32(h+34,w*height*3);
}
static inline int album_bmp_open(int dir,const char *name,unsigned w,unsigned height) {
    int fd=openat(dir,name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
    unsigned char h[54],expected[54];struct stat st;album_bmp_header(expected,w,height);
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==54+(off_t)w*height*3&&
        album_read_exact(fd,h,sizeof(h))&&!memcmp(h,expected,sizeof(h));
    if(!ok){close(fd);return -1;}return fd;
}
/* Integer box average: no image decoder/library, only the known BMP layout. */
static inline void album_reduce(const unsigned char *src,unsigned char *dst,unsigned w,unsigned h) {
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned x0=x*SNAP_WIDTH/w,x1=(x+1)*SNAP_WIDTH/w;
        unsigned y0=y*SNAP_HEIGHT/h,y1=(y+1)*SNAP_HEIGHT/h,sums[3]={0};
        for(unsigned yy=y0;yy<y1;yy++)for(unsigned xx=x0;xx<x1;xx++)
            for(unsigned c=0;c<3;c++)sums[c]+=src[yy*SNAP_STRIDE+xx*3+c];
        unsigned count=(x1-x0)*(y1-y0);
        for(unsigned c=0;c<3;c++)dst[(y*w+x)*3+c]=(unsigned char)((sums[c]+count/2)/count);
    }
}
static inline void album_rgba(const unsigned char *src,uint32_t *dst,unsigned pixels) {
    for(unsigned i=0;i<pixels;i++)dst[i]=src[i*3+2]|((uint32_t)src[i*3+1]<<8)|((uint32_t)src[i*3]<<16)|0xff000000u;
}
/* Derived cache only. Original BMP/measurement JSON are never rewritten.
 * Both checksum/source marker and actual thumbnail CRC must match. */
static inline int album_cache_read(int dir,uint32_t source_crc,unsigned char *bgr) {
    unsigned char meta[12]={0};int fd=openat(dir,"thumbnail-v1.meta",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 0;
    struct stat st;
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==12&&album_read_exact(fd,meta,12);close(fd);
    uint32_t marker,source,crc;memcpy(&marker,meta,4);memcpy(&source,meta+4,4);memcpy(&crc,meta+8,4);
    if(!ok||marker!=0x54484d31u||source!=source_crc)return 0;
    fd=album_bmp_open(dir,"thumbnail.bmp",ALBUM_TW,ALBUM_TH);if(fd<0)return 0;
    ok=album_read_exact(fd,bgr,ALBUM_TBYTES);close(fd);
    return ok&&snap_crc32(bgr,ALBUM_TBYTES)==crc;
}
static inline int album_atomic_file(int dir,const char *name,const void *a,size_t na,const void *b,size_t nb) {
    char temporary[80];int fd=-1;
    for(unsigned i=0;i<16;i++) {
        snprintf(temporary,sizeof(temporary),".thumb-%ld-%u",(long)getpid(),i);
        fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644);
        if(fd>=0||errno!=EEXIST)break;
    }
    if(fd<0)return 0;
    int ok=album_write_exact(fd,a,na)&&album_write_exact(fd,b,nb)&&!fsync(fd);
    if(close(fd))ok=0;
    if(ok)ok=renameat(dir,temporary,dir,name)==0;
    if(!ok)unlinkat(dir,temporary,0);
    return ok;
}
static inline int album_cache_write(int dir,uint32_t source_crc,const unsigned char *bgr) {
    unsigned char h[54];album_bmp_header(h,ALBUM_TW,ALBUM_TH);
    uint32_t meta[3]={0x54484d31u,source_crc,snap_crc32(bgr,ALBUM_TBYTES)};
    return album_atomic_file(dir,"thumbnail.bmp",h,sizeof(h),bgr,ALBUM_TBYTES)&&
        album_atomic_file(dir,"thumbnail-v1.meta",meta,sizeof(meta),NULL,0)&&!fsync(dir);
}
static inline int album_field(const char *s,const char *end,const char *name,const char **first,const char **last) {
    struct pl_json j={s,end,0};int found=0;pl_json_space(&j);
    if(j.p==j.end||*j.p++!='{')return 0;
    pl_json_space(&j);if(j.p<j.end&&*j.p=='}')return 0;
    for(;;) {
        const char *key;size_t len;pl_json_space(&j);if(!pl_json_string(&j,&key,&len))return 0;
        pl_json_space(&j);if(j.p==j.end||*j.p++!=':')return 0;
        pl_json_space(&j);const char *begin=j.p;if(!pl_json_value(&j))return 0;
        if(strlen(name)==len&&!memcmp(key,name,len)) {
            if(found)return 0;
            found=1;*first=begin;*last=j.p;
        }
        pl_json_space(&j);if(j.p==j.end)return 0;
        if(*j.p=='}'){j.p++;break;}if(*j.p++!=',')return 0;
    }
    pl_json_space(&j);return found&&j.p==j.end;
}
static inline int album_string(const char *s,const char *end,const char *key,char *out,size_t cap) {
    const char *a,*b,*start;size_t n;
    if(!album_field(s,end,key,&a,&b))return 0;
    struct pl_json j={a,b,0};if(!pl_json_string(&j,&start,&n)||j.p!=b||n>=cap)return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)start[i]<32||(unsigned char)start[i]>126||start[i]=='\\')return 0;
    memcpy(out,start,n);out[n]=0;return 1;
}
static inline int album_uint(const char *s,const char *end,const char *key,uint64_t *n) {
    const char *a,*b;return album_field(s,end,key,&a,&b)&&panel_freq_uint(a,b,n);
}
static inline int album_true(const char *s,const char *end,const char *key) {
    const char *a,*b;return album_field(s,end,key,&a,&b)&&panel_freq_equal(a,b,"true");
}
static inline int32_t album_coordinate(const char *s,const char *end,const char *key,unsigned limit) {
    uint64_t n;return album_uint(s,end,key,&n)&&n<limit?(int32_t)n:-1;
}
static inline int album_parse(const char *text,size_t n,const char *id,struct album_meta *m) {
    memset(m,0,sizeof(*m));m->raw_col=m->raw_row=m->stable_col=m->stable_row=-1;
    m->frequency_channel=UINT32_MAX;
    const char *end=text+n,*a,*b,*g,*ge;uint64_t v;char actual[128];
    if(!album_id_valid(id)||!album_uint(text,end,"schema_version",&v)||v!=1||
       !album_string(text,end,"capture_id",actual,sizeof(actual))||strcmp(id,actual)||
       !album_string(text,end,"boot_id",m->boot,sizeof(m->boot))||
       !album_string(text,end,"wallclock_utc",m->utc,sizeof(m->utc)))return 0;
    strcpy(m->id,id);
    if(!album_field(text,end,"image",&a,&b)||!album_uint(a,b,"width",&v)||v!=SNAP_WIDTH||
       !album_uint(a,b,"height",&v)||v!=SNAP_HEIGHT||!album_string(a,b,"pixel_crc32",m->crc,sizeof(m->crc)))return 0;
    if(strlen(m->crc)!=8)return 0;
    for(unsigned i=0;i<8;i++)if(!((m->crc[i]>='0'&&m->crc[i]<='9')||(m->crc[i]>='a'&&m->crc[i]<='f')))return 0;
    if(!album_field(text,end,"association",&a,&b)||!album_uint(a,b,"trigger_monotonic_ns",&m->trigger_ns))return 0;
    const char *x,*xe;if(album_field(a,b,"read_after_copy_ms",&x,&xe))panel_freq_double(x,xe,&m->read_after_copy_ms);
    if(!album_field(text,end,"measurement",&a,&b)||!album_string(a,b,"sound_state",m->sound,sizeof(m->sound))||
       !album_string(a,b,"position_state",m->position,sizeof(m->position))||!album_field(a,b,"grid",&g,&ge))return 0;
    m->raw_col=album_coordinate(g,ge,"raw_col",64);m->raw_row=album_coordinate(g,ge,"raw_row",36);
    m->stable_col=album_coordinate(g,ge,"stable_col",64);m->stable_row=album_coordinate(g,ge,"stable_row",36);
    m->position_valid=album_true(a,b,"position_valid")&&m->stable_col>=0&&m->stable_row>=0;
    if(m->position_valid&&(strcmp(m->sound,"active")||strcmp(m->position,"stable")))return 0;
    if(!m->position_valid)m->stable_col=m->stable_row=-1;
    if(album_uint(a,b,"grid_energy_peak",&v)&&v<=0xffffff){m->energy=(uint32_t)v;m->energy_valid=1;}
    if(album_field(a,b,"peak_frequency_hz",&x,&xe)&&panel_freq_double(x,xe,&m->frequency)&&m->frequency>=300&&m->frequency<=10000)
        m->frequency_valid=1;
    if(album_uint(a,b,"frequency_channel",&v)&&v<16)m->frequency_channel=(uint32_t)v;
    if(m->frequency_channel==UINT32_MAX||strcmp(m->sound,"active"))m->frequency_valid=0;
    if(album_field(a,b,"temperature_millicelsius",&x,&xe)) {
        double t;if(panel_freq_double(x,xe,&t)&&t>=-55000&&t<=150000){m->temp_mc=(int32_t)t;m->temp_valid=1;}
    }
    return 1;
}
static inline uint64_t album_read_sequence(int dir,const char *name) {
    char text[64];uint64_t sequence;
    if(album_read_text(dir,name,text,sizeof(text))<0)return 0;
    text[strcspn(text,"\r\n")]=0;
    return panel_freq_uint(text,text+strlen(text),&sequence)&&sequence<=1000000000000ull?sequence:0;
}
static inline uint64_t album_next_sequence(int root,unsigned legacy_count) {
    uint64_t last=0;char text[64];errno=0;
    int got=album_read_text(root,".album-sequence",text,sizeof(text));
    if(got>=0) {
        text[strcspn(text,"\r\n")]=0;
        if(!panel_freq_uint(text,text+strlen(text),&last)||last>=1000000000000ull)return 0;
    } else if(errno==ENOENT) {
        /* One-time upgrade: v1 images are kept immutable. A future missing
         * counter recovers the highest already published sidecar sequence. */
        int scan=openat(root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(scan<0)return 0;
        DIR *d=fdopendir(scan);if(!d){close(scan);return 0;}struct dirent *e;
        while((e=readdir(d)))if(album_id_valid(e->d_name)) {
            int fd=openat(root,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(fd>=0){uint64_t n=album_read_sequence(fd,"album.seq");if(n>last)last=n;close(fd);}
        }
        closedir(d);
    } else return 0;
    if(last<legacy_count)last=legacy_count;
    unsigned len=(unsigned)snprintf(text,sizeof(text),"%llu\n",(unsigned long long)(last+1));
    return album_atomic_file(root,".album-sequence",text,len,NULL,0)&&!fsync(root)?last+1:0;
}
static inline unsigned album_json_lines(const char *text,char lines[][49],unsigned capacity) {
    /* Literal full JSON, wrapped at36 mono22 glyphs for a472px drawer. */
    unsigned row=0,col=0;
    for(const unsigned char *p=(const unsigned char *)text;*p&&row<capacity;p++) {
        if(*p=='\r')continue;
        if(*p=='\n'){lines[row][col]=0;row++;col=0;continue;}
        lines[row][col++]=*p>=32&&*p<127?(char)*p:'?';
        if(col==36){lines[row][col]=0;row++;col=0;}
    }
    if(col&&row<capacity){lines[row][col]=0;row++;}return row;
}
#endif

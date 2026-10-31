#ifndef VIDEO_STATUS_TEMP_H
#define VIDEO_STATUS_TEMP_H
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_TEMP_PERIOD_MS 1000u
#define PANEL_TEMP_WARN_MC 90000
#define PANEL_TEMP_HOT_MC 95000
struct panel_temperature {
    int valid, millidegrees;
    uint64_t next_ms, sampled_ms;
    char path[PATH_MAX];
};
static inline int panel_temp_number(const char *text, double *value) {
    char *end;
    errno=0; double n=strtod(text,&end);
    if (end==text || errno || !isfinite(n)) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    *value=n; return 1;
}
/* IIO temperature units after (raw + offset) * scale are millidegrees C. */
static inline int panel_temp_decode(const char *raw,const char *offset,
                                    const char *scale,int *mc) {
    double r,o,s;
    if (!panel_temp_number(raw,&r) || !panel_temp_number(offset,&o) ||
        !panel_temp_number(scale,&s) || r<0 || r>4095 || (unsigned)r!=r ||
        o < -10000 || o > 10000 || s<=0 || s>1000) return 0;
    double t=(r+o)*s;
    if (!isfinite(t) || t < -55000 || t > 150000) return 0;
    *mc=(int)(t>=0 ? t+0.5 : t-0.5); return 1;
}
static inline int panel_temp_level(const struct panel_temperature *t) {
    if (!t->valid) return -1;
    return t->millidegrees>=PANEL_TEMP_HOT_MC ? 2 :
           t->millidegrees>=PANEL_TEMP_WARN_MC ? 1 : 0;
}
static inline int panel_temp_read(const char *dir,const char *name,char *data,size_t size) {
    char path[PATH_MAX];
    int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    if (n<0 || (size_t)n>=sizeof(path)) return 0;
    FILE *f=fopen(path,"r"); if (!f) return 0;
    size_t got=fread(data,1,size-1,f);
    int ok=got>0 && !ferror(f) && feof(f);
    fclose(f); data[got]=0; return ok;
}
static inline int panel_temp_identity(const char *path) {
    char name[64];
    if (!panel_temp_read(path,"name",name,sizeof(name))) return 0;
    name[strcspn(name,"\r\n")]=0;
    return !strcmp(name,"xadc");
}
static inline void panel_temp_sample(struct panel_temperature *t,const char *root,uint64_t now) {
    if (t->next_ms && now<t->next_ms) return;
    t->next_ms=now+PANEL_TEMP_PERIOD_MS;
    t->valid=0; t->millidegrees=0; t->sampled_ms=0;
    if (!*t->path || !panel_temp_identity(t->path)) {
        t->path[0]=0;
        DIR *dir=opendir(root);
        if (!dir) return;
        struct dirent *entry;
        while ((entry=readdir(dir))) {
            if (strncmp(entry->d_name,"iio:device",10)) continue;
            char path[PATH_MAX];
            int n=snprintf(path,sizeof(path),"%s/%s",root,entry->d_name);
            if (n>0 && (size_t)n<sizeof(path) && panel_temp_identity(path)) {
                strcpy(t->path,path); break;
            }
        }
        closedir(dir);
        if (!*t->path) return;
    }
    char raw[64],offset[64],scale[64]; int mc;
    if (!panel_temp_read(t->path,"in_temp0_raw",raw,sizeof(raw)) ||
        !panel_temp_read(t->path,"in_temp0_offset",offset,sizeof(offset)) ||
        !panel_temp_read(t->path,"in_temp0_scale",scale,sizeof(scale)) ||
        !panel_temp_decode(raw,offset,scale,&mc)) return;
    t->millidegrees=mc; t->sampled_ms=now; t->valid=1;
}
#endif

#ifndef PL_UIO_H
#define PL_UIO_H
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PL_UIO_NAME "acoustic_ctrl@44a20000"
#define PL_UIO_ADDRESS 0x44a20000ull
#define PL_MAP_SIZE 0x10000u
static inline int pl_read_text(const char *path,char *text,size_t size) {
    FILE *f=fopen(path,"r");if(!f)return -1;
    int rc=fgets(text,(int)size,f)?0:-1;fclose(f);
    if(!rc)text[strcspn(text,"\r\n")]=0;
    return rc;
}
static inline int pl_uio_number(const char *s) {
    if(strncmp(s,"uio",3) || !s[3])return 0;
    for(s+=3;*s;s++)if(*s<'0' || *s>'9')return 0;
    return 1;
}
static inline int pl_uio_integer(const char *s,unsigned long long *value) {
    char *end;errno=0;*value=strtoull(s,&end,0);
    return !errno && end!=s && !*end && *s!='-';
}
static inline int pl_find_uio(const char *sysfs,const char *devices,char *result,size_t capacity) {
    DIR *directory=opendir(sysfs);if(!directory)return -1;
    struct dirent *entry;unsigned found=0;char chosen[512]={0};
    while((entry=readdir(directory))) {
        if(!pl_uio_number(entry->d_name))continue;
        char path[512],name[128],text[80];unsigned long long addr,size;
        int n=snprintf(path,sizeof(path),"%s/%s/name",sysfs,entry->d_name);
        if(n<0 || (size_t)n>=sizeof(path) || pl_read_text(path,name,sizeof(name)) || strcmp(name,PL_UIO_NAME))continue;
        n=snprintf(path,sizeof(path),"%s/%s/maps/map0/addr",sysfs,entry->d_name);
        if(n<0 || (size_t)n>=sizeof(path) || pl_read_text(path,text,sizeof(text)) ||
            !pl_uio_integer(text,&addr) || addr!=PL_UIO_ADDRESS)continue;
        n=snprintf(path,sizeof(path),"%s/%s/maps/map0/size",sysfs,entry->d_name);
        if(n<0 || (size_t)n>=sizeof(path) || pl_read_text(path,text,sizeof(text)) ||
            !pl_uio_integer(text,&size) || size<PL_MAP_SIZE)continue;
        n=snprintf(chosen,sizeof(chosen),"%s/%s",devices,entry->d_name);
        if(n<0 || (size_t)n>=sizeof(chosen))continue;
        found++;
    }
    closedir(directory);
    if(found!=1) {errno=found?EEXIST:ENODEV;return -1;}
    if(strlen(chosen)>=capacity) {errno=ENAMETOOLONG;return -1;}
    strcpy(result,chosen);return 0;
}
#endif

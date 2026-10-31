#ifndef VIDEO_STATUS_TOUCH_H
#define VIDEO_STATUS_TOUCH_H
#include "video_status_view.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PANEL_TOUCH_CONFIG "/etc/video-status-panel-touch.conf"
#define PANEL_TOUCH_SLOTS 32
struct panel_touch_reader {
    int fd, x, y, have_xy, down, slot, slots, dropping, monotonic;
    int active[PANEL_TOUCH_SLOTS], swap, invert_x, invert_y, config_ok;
    struct input_absinfo ax,ay;
    char path[128],name[256];
    struct input_id id;
    struct panel_tap tap;
    uint64_t next_probe_ms, reports;
};
static inline int panel_bit(const unsigned char *bits,unsigned bit) {
    return (bits[bit/8]>>(bit%8))&1;
}
static inline int panel_axis_pixel(int value,int minimum,int maximum,int extent) {
    if(maximum<=minimum || extent<2 || value<minimum || value>maximum) return -1;
    return (int)(((int64_t)value-minimum)*(extent-1)/((int64_t)maximum-minimum));
}
static inline int panel_touch_xy(const struct panel_touch_reader *r,int *x,int *y) {
    int nx=panel_axis_pixel(r->x,r->ax.minimum,r->ax.maximum,r->swap?720:1280);
    int ny=panel_axis_pixel(r->y,r->ay.minimum,r->ay.maximum,r->swap?1280:720);
    if(nx<0 || ny<0) return 0;
    *x=r->swap?ny:nx; *y=r->swap?nx:ny;
    if(r->invert_x) *x=1279-*x;
    if(r->invert_y) *y=719-*y;
    return r->have_xy;
}
static inline void panel_touch_init(struct panel_touch_reader *r) {
    memset(r,0,sizeof(*r)); r->fd=-1; r->config_ok=1;
    FILE *f=fopen(PANEL_TOUCH_CONFIG,"r");
    if(!f) {if(errno!=ENOENT) r->config_ok=0; return;}
    char line[128]; unsigned seen=0;
    while(fgets(line,sizeof(line),f)) {
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r') continue;
        char key[32],extra; int value;
        if(sscanf(line,"%31[^=]=%d %c",key,&value,&extra)!=2 || (value!=0 && value!=1)) {r->config_ok=0;break;}
        unsigned bit=0;
        if(!strcmp(key,"swap_xy")) {bit=1;r->swap=value;}
        else if(!strcmp(key,"invert_x")) {bit=2;r->invert_x=value;}
        else if(!strcmp(key,"invert_y")) {bit=4;r->invert_y=value;}
        else {r->config_ok=0;break;}
        if(seen&bit) {r->config_ok=0;break;}
        seen|=bit;
    }
    if(ferror(f)) r->config_ok=0;
    fclose(f);
}
/* Capability identity excludes this USB product's keyboard/mouse interfaces. */
static inline int panel_touch_candidate(int fd,struct panel_touch_reader *r) {
    unsigned char prop[(INPUT_PROP_MAX+8)/8]={0},abs[(ABS_MAX+8)/8]={0},key[(KEY_MAX+8)/8]={0};
    if(ioctl(fd,EVIOCGPROP(sizeof(prop)),prop)<0 || !panel_bit(prop,INPUT_PROP_DIRECT) ||
        ioctl(fd,EVIOCGBIT(EV_ABS,sizeof(abs)),abs)<0 || !panel_bit(abs,ABS_X) || !panel_bit(abs,ABS_Y) ||
        ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(key)),key)<0 || !panel_bit(key,BTN_TOUCH)) return 0;
    if(ioctl(fd,EVIOCGABS(ABS_X),&r->ax) || ioctl(fd,EVIOCGABS(ABS_Y),&r->ay) ||
        r->ax.minimum>=r->ax.maximum || r->ay.minimum>=r->ay.maximum) return 0;
    if(ioctl(fd,EVIOCGNAME(sizeof(r->name)-1),r->name)<0 || ioctl(fd,EVIOCGID,&r->id)) return 0;
    r->name[sizeof(r->name)-1]=0; r->slots=0;
    if(panel_bit(abs,ABS_MT_SLOT) && panel_bit(abs,ABS_MT_TRACKING_ID)) {
        struct input_absinfo slot;
        if(ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&slot) || slot.minimum!=0 ||
            slot.maximum<0 || slot.maximum>=PANEL_TOUCH_SLOTS) return 0;
        r->slots=slot.maximum+1;
    }
    return 1;
}
static inline int panel_touch_resync(struct panel_touch_reader *r) {
    unsigned char key[(KEY_MAX+8)/8]={0};
    if(ioctl(r->fd,EVIOCGABS(ABS_X),&r->ax) || ioctl(r->fd,EVIOCGABS(ABS_Y),&r->ay) ||
        ioctl(r->fd,EVIOCGKEY(sizeof(key)),key)<0) return 0;
    r->x=r->ax.value; r->y=r->ay.value; r->have_xy=1;
    r->down=panel_bit(key,BTN_TOUCH); r->slot=0;
    memset(r->active,0,sizeof(r->active));
    if(r->slots) {
        struct input_absinfo slot;
        if(ioctl(r->fd,EVIOCGABS(ABS_MT_SLOT),&slot) || slot.value<0 || slot.value>=r->slots) return 0;
        r->slot=slot.value;
    }
    if(r->slots && r->down) {
        int values[PANEL_TOUCH_SLOTS+1]; values[0]=ABS_MT_TRACKING_ID;
        if(ioctl(r->fd,EVIOCGMTSLOTS((r->slots+1)*sizeof(int)),values)<0) return 0;
        for(int i=0;i<r->slots;i++) r->active[i]=values[i+1]>=0;
    }
    panel_tap_cancel(&r->tap); r->tap.was_down=r->down; r->tap.blocked=r->down;
    return 1;
}
static inline int panel_touch_open(struct panel_touch_reader *r) {
    if(!r->config_ok) return 0;
    DIR *dir=opendir("/dev/input"); if(!dir) return 0;
    struct panel_touch_reader chosen={.fd=-1}; int count=0;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"event",5) || !entry->d_name[5]) continue;
        int digits=1;
        for(const char *p=entry->d_name+5;*p;p++) if(*p<'0' || *p>'9') digits=0;
        if(!digits) continue;
        char path[128]; int n=snprintf(path,sizeof(path),"/dev/input/%s",entry->d_name);
        if(n<0 || (size_t)n>=sizeof(path)) continue;
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW); if(fd<0) continue;
        struct panel_touch_reader candidate={.fd=fd};
        if(panel_touch_candidate(fd,&candidate)) {
            count++;
            if(count==1) {chosen=candidate;strcpy(chosen.path,path);} else close(fd);
        } else close(fd);
    }
    closedir(dir);
    if(count!=1) {if(chosen.fd>=0) close(chosen.fd);return 0;}
    r->fd=chosen.fd; r->ax=chosen.ax; r->ay=chosen.ay; r->slots=chosen.slots; r->id=chosen.id;
    strcpy(r->path,chosen.path); strcpy(r->name,chosen.name);
    clockid_t clock=CLOCK_MONOTONIC;
    r->monotonic=ioctl(r->fd,EVIOCSCLOCKID,&clock)==0;
    if(!panel_touch_resync(r)) {close(r->fd);r->fd=-1;return 0;}
    return 1;
}
static inline void panel_touch_close(struct panel_touch_reader *r,struct panel_view *v) {
    if(r->fd>=0) close(r->fd);
    r->fd=-1; r->path[0]=0; r->dropping=0; panel_tap_cancel_view(&r->tap,v);
    v->touch_connected=0; v->pressed=0;
}
static inline void panel_touch_tick(struct panel_touch_reader *r,struct panel_view *v,uint64_t now) {
    if(r->fd<0) {
        if(now<r->next_probe_ms) return;
        r->next_probe_ms=now+2000;
        if(!panel_touch_open(r)) return;
        v->touch_connected=1;
        printf("TOUCH_CONNECTED %s name=%s id=%04x:%04x range=%d..%d,%d..%d\n",
            r->path,r->name,r->id.vendor,r->id.product,r->ax.minimum,r->ax.maximum,r->ay.minimum,r->ay.maximum);
        fflush(stdout);
    }
    for(unsigned batch=0;batch<4;batch++) {
        struct input_event events[64]; ssize_t bytes=read(r->fd,events,sizeof(events));
        if(bytes<0 && (errno==EAGAIN || errno==EINTR)) return;
        if(bytes<=0 || bytes%(ssize_t)sizeof(events[0])) {panel_touch_close(r,v);return;}
        for(unsigned i=0;i<(unsigned)bytes/sizeof(events[0]);i++) {
            const struct input_event *e=&events[i];
            if(e->type==EV_SYN && e->code==SYN_DROPPED) {
                if(r->tap.roi_gesture){v->image.dragging=0;v->image.preview_valid=0;}
                r->dropping=1; panel_tap_cancel_view(&r->tap,v); v->pressed=0; continue;
            }
            if(r->dropping) {
                if(e->type==EV_SYN && e->code==SYN_REPORT) {
                    r->dropping=0;
                    if(!panel_touch_resync(r)) {panel_touch_close(r,v);return;}
                }
                continue;
            }
            if(e->type==EV_KEY && e->code==BTN_TOUCH && (e->value==0 || e->value==1)) r->down=e->value;
            if(e->type==EV_ABS) {
                if(e->code==ABS_X) {r->x=e->value;r->have_xy=1;}
                if(e->code==ABS_Y) {r->y=e->value;r->have_xy=1;}
                if(e->code==ABS_MT_SLOT) {
                    if(e->value<0 || e->value>=r->slots) panel_tap_cancel_view(&r->tap,v);
                    r->slot=e->value;
                }
                if(e->code==ABS_MT_TRACKING_ID && r->slot>=0 && r->slot<r->slots)
                    r->active[r->slot]=e->value>=0;
            }
            if(e->type==EV_SYN && e->code==SYN_REPORT) {
                int contacts=0,x=-1,y=-1;
                for(int j=0;j<r->slots;j++) contacts+=r->active[j];
                if(r->down && !contacts) contacts=1;
                uint64_t stamp=now;
                /* UAPI aliases preserve the 32-bit evdev ABI with libc time64. */
                if(r->monotonic) stamp=(uint64_t)e->input_event_sec*1000+(uint64_t)e->input_event_usec/1000;
                int valid=panel_touch_xy(r,&x,&y); r->reports++;
                int action=panel_tap_report(&r->tap,v,r->down,contacts,x,y,valid,stamp);
                if(action==1) {
                    printf("SIDEBAR_TOGGLE visible=%d taps=%llu at=%d,%d\n",v->sidebar_visible,(unsigned long long)v->taps,x,y);fflush(stdout);
                } else if(action==2) {
                    printf("ACOUSTIC_TAP request=%d taps=%llu at=%d,%d\n",v->acoustic_request,(unsigned long long)v->taps,x,y);fflush(stdout);
                } else if(action==3) {
                    printf("CAPTURE_TAP taps=%llu at=%d,%d\n",(unsigned long long)v->taps,x,y);fflush(stdout);
                } else if(action>=70&&action<=72) {
                    printf("ROBOT_TOUCH action=%d modal=%d region=%u request=%d\n",action,
                        v->robot.modal,v->robot.modal_region,v->robot.request);fflush(stdout);
                } else if(action>=20) {
                    printf("IMAGE_TOUCH action=%d mode=%u roi=%d request=%u draft_threshold=%d draft_brightness=%d\n",
                        action,v->image.active.mode,v->image.active.roi,v->image.request,v->image.draft_threshold,v->image.draft_brightness);fflush(stdout);
                } else if(action>=4) {
                    printf("GALLERY_TOUCH action=%d open=%d detail=%d page=%u json=%d taps=%llu\n",
                        action,v->gallery_open,v->gallery_detail,v->gallery_page,v->gallery_json,(unsigned long long)v->taps);fflush(stdout);
                }
                if(!r->down) memset(r->active,0,sizeof(r->active));
            }
        }
    }
}
#endif

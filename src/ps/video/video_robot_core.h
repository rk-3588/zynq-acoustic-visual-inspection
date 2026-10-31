#ifndef VIDEO_ROBOT_CORE_H
#define VIDEO_ROBOT_CORE_H
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "video_status_panel.h"
#include "video_robot_protocol.h"
#define ROBOT_VERSION "20261031-robot-control-v7"
#ifndef ROBOT_UNKNOWN_HOLD_MS
#define ROBOT_UNKNOWN_HOLD_MS 5000u
#endif
#define ROBOT_STATE_CAP 2048u
enum robot_mode {ROBOT_MODE_GRAB,ROBOT_MODE_TRACK,ROBOT_MODE_CYCLE};
/* Keep historical phase numbers; ACCEPTED belongs to the superseded CRC protocol. */
enum robot_phase {ROBOT_READY,ROBOT_WAITING,ROBOT_UNKNOWN,ROBOT_COMPLETE,ROBOT_FAILED,ROBOT_BUSY,ROBOT_INVALID,
    ROBOT_ACCEPTED,ROBOT_EXECUTING};
struct robot_xyz {int16_t x,y,z;}; /* Read-only migration of ROBOT2 state. */
struct robot_job {unsigned id,region,phase;unsigned long long nonce;unsigned protocol,wire_region;
    unsigned mode,value,sequence,last_grab_id,last_grab_known;};
struct robot_regions {unsigned wire[3],mask,timeout_ms;};
static inline int robot_inflight(const struct robot_job *j){
    return j->phase==ROBOT_WAITING||j->phase==ROBOT_ACCEPTED||j->phase==ROBOT_EXECUTING;
}
static inline int robot_blocked(const struct robot_job *j){return robot_inflight(j)||j->phase==ROBOT_UNKNOWN;}
static inline unsigned robot_region(int col){return col<0||col>=64?0u:1u+(3u*(2u*(unsigned)col+1u))/128u;}
static inline unsigned robot_target(const struct panel_status *s){
    return s->online&&s->valid&&s->acoustic_known&&s->acoustic_enabled&&s->enable_applied&&
        s->age_ms<=150&&!strcmp(s->sound,"active")&&!strcmp(s->position,"stable")?robot_region(s->col):0u;
}
/* Manual confirmation uses the current measured cell, independently of the
 * stable-position filter. Quiet, disabled or stale data supplies no target. */
static inline unsigned robot_manual_target(const struct panel_status *s){
    return s->online&&s->raw_valid&&s->acoustic_known&&s->acoustic_enabled&&s->enable_applied&&
        s->age_ms<=150&&!strcmp(s->sound,"active")&&s->raw_row>=0&&s->raw_row<36?
        robot_region(s->raw_col):0u;
}
struct robot_unknown_timer {uint64_t since;int active;};
static inline int robot_unknown_due(struct robot_unknown_timer *t,const struct robot_job *j,uint64_t now){
    if(j->mode!=ROBOT_MODE_GRAB||j->phase!=ROBOT_UNKNOWN){t->active=0;return 0;}
    if(!t->active){t->since=now;t->active=1;}
    return now>=t->since&&now-t->since>=ROBOT_UNKNOWN_HOLD_MS;
}
/* Only TYPE 02 resolves a grab. Accept/config/status replies never resolve it. */
static inline int robot_reply(struct robot_job *j,const struct robot_packet *p){
    if(j->mode!=ROBOT_MODE_GRAB||!robot_blocked(j)||!j->nonce||j->protocol!=3||p->type!=2||p->payload_size!=1||
       p->id!=j->id||p->payload[0]>3)return 0;
    unsigned result=p->payload[0];
    j->phase=result==0?ROBOT_COMPLETE:result==1?ROBOT_FAILED:result==2?ROBOT_EXECUTING:ROBOT_INVALID;
    return 1;
}
/* Explicit image-to-arm region mapping; disabled until commissioning. */
static inline int robot_regions_read(FILE *f,struct robot_regions *out){
    struct robot_regions t={.timeout_ms=30000};char line[160];unsigned used_wire=0,have_timeout=0;
    memset(out,0,sizeof(*out));out->timeout_ms=30000;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')&&!feof(f))return -1;
        char *p=line;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;
        if(!*p||*p=='#')continue;
        int setting=!strncmp(p,"timeout_ms",10)&&(p[10]==' '||p[10]=='\t');
        if(setting)p+=10;
        long v[2];
        for(unsigned i=0;i<(setting?1u:2u);i++){
            char *end;errno=0;v[i]=strtol(p,&end,10);
            if(errno||p==end||(*end&&*end!=' '&&*end!='\t'&&*end!='\r'&&*end!='\n'))return -1;
            p=end;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;
        }
        if(*p)return -1;
        if(setting){if(have_timeout++||v[0]<20000||v[0]>180000)return -1;t.timeout_ms=(unsigned)v[0];continue;}
        if(v[0]<1||v[0]>3||v[1]<1||v[1]>3||(t.mask&(1u<<(v[0]-1)))||(used_wire&(1u<<(v[1]-1))))return -1;
        t.wire[v[0]-1]=(unsigned)v[1];t.mask|=1u<<(v[0]-1);used_wire|=1u<<(v[1]-1);
    }
    if(ferror(f))return -1;
    *out=t;return 0;
}
#endif

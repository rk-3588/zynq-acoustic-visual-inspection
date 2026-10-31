#ifndef VIDEO_ROBOT_MODES_CORE_H
#define VIDEO_ROBOT_MODES_CORE_H
#include "video_robot_core.h"
enum robot_mode_error {RM_OK,RM_TIMEOUT,RM_REJECTED,RM_UNCALIBRATED,RM_CONFLICT,
    RM_LEASE_LOST,RM_NO_TARGET,RM_IO,RM_BAD_CONFIG};
struct robot_mode_config {unsigned track_known,track_mask,cycle_known,cycle_mask,
    cycle_ready,cycle_move_ms,cycle_dwell_ms;};
struct robot_track_filter {int initialized,xq,yq;unsigned region,candidate,count;uint64_t sampled_ms;};
static inline unsigned robot_get16(const unsigned char *p){return (unsigned)p[0]+256u*p[1];}
static inline int robot_mode_config_reply(struct robot_mode_config *c,const struct robot_packet *p){
    if(p->type==0x26&&p->payload_size==123&&p->payload[0]==1){
        unsigned mask=robot_get16(p->payload+1);if(mask&~4095u)return 0;
        for(unsigned n=0;n<12;n++)if(mask&(1u<<n))for(unsigned j=0;j<5;j++){
            unsigned pos=robot_get16(p->payload+3+10*n+2*j);if(pos<500||pos>2500)return 0;}
        c->track_known=1;c->track_mask=mask;return 1;
    }
    if(p->type==0x28&&p->payload_size==26&&p->payload[0]==1){
        unsigned mask=p->payload[1],move=robot_get16(p->payload+2),dwell=robot_get16(p->payload+4);
        if(mask>3||move<100||move>3000||dwell>3000)return 0;
        for(unsigned n=0;n<2;n++)if(mask&(1u<<n))for(unsigned j=0;j<5;j++){
            unsigned pos=robot_get16(p->payload+6+10*n+2*j);if(pos<500||pos>2500)return 0;}
        c->cycle_known=1;c->cycle_mask=mask;c->cycle_move_ms=move;c->cycle_dwell_ms=dwell;
        c->cycle_ready=mask==3&&memcmp(p->payload+6,p->payload+16,10)!=0;return 1;
    }
    return 0;
}
static inline unsigned robot_track_region(const struct panel_status *s){
    return robot_target(s)&&s->row>=0&&s->row<36?(unsigned)(s->row/12)*4u+(unsigned)(s->col/16)+1u:0;
}
/* Fixed point; alpha=.65 is the new-sample weight. Pixel centres map the
 * current 64x36 grid into protocol 128x72 before the 3px boundary buffer. */
static inline unsigned robot_track_observe(struct robot_track_filter *f,const struct panel_status *s,uint64_t now){
    unsigned raw=robot_track_region(s);if(!raw){memset(f,0,sizeof(*f));return 0;}
    if(f->sampled_ms&&now-f->sampled_ms<100)return f->region;
    f->sampled_ms=now;
    int x=(2*s->col+1)*256,y=(2*s->row+1)*256;
    if(!f->initialized){f->initialized=1;f->xq=x;f->yq=y;f->region=raw;return raw;}
    f->xq=(65*x+35*f->xq+50)/100;f->yq=(65*y+35*f->yq+50)/100;
    unsigned cx=(unsigned)f->xq/(32u*256u),cy=(unsigned)f->yq/(24u*256u);
    unsigned oldx=(f->region-1)%4,oldy=(f->region-1)/4;
    if(cx>oldx&&f->xq<((int)(oldx+1)*32+3)*256)cx=oldx;
    if(cx<oldx&&f->xq>((int)oldx*32-3)*256)cx=oldx;
    if(cy>oldy&&f->yq<((int)(oldy+1)*24+3)*256)cy=oldy;
    if(cy<oldy&&f->yq>((int)oldy*24-3)*256)cy=oldy;
    unsigned next=cy*4+cx+1;
    if(next==f->region){f->candidate=f->count=0;return f->region;}
    if(f->candidate==next)f->count++;else{f->candidate=next;f->count=1;}
    if(f->count>=2){f->region=next;f->candidate=f->count=0;}
    return f->region;
}
#endif

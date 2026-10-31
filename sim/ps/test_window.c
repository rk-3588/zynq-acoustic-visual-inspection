#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "video_status_render.h"
#include "video_status_robot.h"
static struct panel_view fresh(void){
    return (struct panel_view){.sidebar_visible=1,.touch_connected=1,.capture_ready=1,.gallery_ready=1,
        .acoustic_known=1,.acoustic_on=1,.robot={.fresh=1,.uart_ready=1,.feedback_seen=1,.region_mask=7,
        .target=2,.track_target=7,.col=32,.row=18,.target_ms=1000,.status_ms=1000,.cycle_draft=1,
        .track_known=1,.track_mask=4095,.cycle_known=1,.cycle_ready=1}};
}
static int tap(struct panel_view *v,int action,uint64_t now){
    struct panel_tap t={0};struct panel_hitbox b=panel_robot_box(action);
    assert(!panel_tap_report(&t,v,1,1,b.x+b.w/2,b.y+b.h/2,1,now));
    return panel_tap_report(&t,v,0,0,b.x+b.w/2,b.y+b.h/2,1,now+40);
}
static void save(const char *root,const char *name,const struct panel_view *v){
    uint32_t *p=malloc(PANEL_BYTES+32);assert(p);
    for(unsigned n=0;n<8;n++)p[PANEL_WIDTH*PANEL_HEIGHT+n]=0xabc987de;
    struct panel_status s={.online=1,.sound="active",.position="stable",.valid=1,.col=32,.row=18,
        .raw_valid=1,.raw_col=32,.raw_row=18,.peak=24000};
    struct panel_temperature t={.valid=1,.millidegrees=65000};panel_render_view(p,&s,&t,1,0,125,v);
    for(unsigned n=0;n<8;n++)assert(p[PANEL_WIDTH*PANEL_HEIGHT+n]==0xabc987de);
    char path[4096];snprintf(path,sizeof(path),"%s/%s.ppm",root,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
    for(unsigned n=0;n<PANEL_WIDTH*PANEL_HEIGHT;n++){
        unsigned a=p[n]>>24;unsigned char rgb[3];
        for(unsigned c=0;c<3;c++)rgb[c]=(unsigned char)((((p[n]>>(8*c))&255)*a+72*(255-a))/255);
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));free(p);
}
int main(int argc,char **argv){
    assert(argc==2);struct panel_view v={0};
    assert(tap(&v,70,1000)==70&&v.robot.window&&!v.robot.modal&&!v.robot.operation);
    assert(panel_robot_box(70).x==824&&panel_robot_box(70).w==80);
    for(int page=0;page<3;page++){
        v=fresh();tap(&v,70,1000);tap(&v,91+page,1050);
        const char *names[]={"grab","track","test"};save(argv[1],names[page],&v);
        if(page==1){
            assert(tap(&v,96,1100)==96&&!v.robot.modal&&v.robot.operation==7);
            v.robot.operation=0;v.robot.auto_mode=1;v.robot.auto_waiting=1;
            assert(panel_power_blocked(&v));assert(!panel_robot_idle(&v.robot,1100));
            v.robot.target=v.robot.track_target=0;assert(tap(&v,96,1150)==96&&v.robot.operation==9&&!v.robot.modal);
        }else{
            int action=page?98:94;assert(tap(&v,action,1100)==action&&v.robot.modal);
            save(argv[1],page?"confirm_test":"confirm_grab",&v);
            assert(tap(&v,71,1150)==71&&!v.robot.modal&&!v.robot.request&&!v.robot.operation);
            assert(tap(&v,action,1200)==action);assert(tap(&v,72,1250)==72);
            if(page)assert(v.robot.operation==4);else assert(v.robot.request==2);
        }
    }
    v=fresh();tap(&v,70,1000);assert(tap(&v,104,1050)==104&&v.robot.grab_auto_choice&&!v.robot.operation);
    panel_robot_action(&v,94,1100);assert(!v.robot.modal&&!v.robot.request&&!v.robot.operation);
    assert(tap(&v,105,1150)==105&&v.robot.operation==8&&!v.robot.modal);
    v.robot.operation=0;v.robot.auto_mode=2;v.robot.auto_latched=1;v.robot.auto_waiting=1;
    save(argv[1],"automatic_grab",&v);
    assert(!panel_robot_ready(&v.robot,1200)&&panel_power_blocked(&v));
    assert(tap(&v,103,1200)==103&&!v.robot.grab_auto_choice&&v.robot.operation==9);
    v=fresh();tap(&v,70,1000);tap(&v,92,1050);v.robot.auto_mode=1;v.robot.mode=1;
    v.robot.phase=ROBOT_EXECUTING;v.robot.track_armed=1;v.robot.track_region=7;
    save(argv[1],"tracking_active",&v);assert(tap(&v,90,1100)==90&&!v.robot.window&&v.robot.auto_mode==1&&!v.robot.operation);
    v=fresh();tap(&v,70,1000);tap(&v,92,1050);v.robot.track_known=0;
    assert(!tap(&v,96,1100));save(argv[1],"uncalibrated",&v);
    /* Selecting the mode is not authorization to send until its switch is ON.
     * A stale/multi-contact/revised switch touch must not enable automation. */
    for(unsigned change=0;change<4;change++){
        v=fresh();tap(&v,70,1000);tap(&v,92,1050);struct panel_tap t={0};struct panel_hitbox b=panel_robot_box(96);
        panel_tap_report(&t,&v,1,1,b.x+20,b.y+20,1,1100);
        if(change==0)v.robot.revision++;
        if(change==1)v.robot.fresh=0;
        if(change==2)panel_tap_report(&t,&v,1,2,b.x+20,b.y+20,1,1110);
        panel_tap_report(&t,&v,0,0,b.x+20,b.y+20,1,change==3?6000:1140);
        assert(!v.robot.operation);
    }
    const char *labels="手动夹取自动夹取自动跟踪等待稳定目标本次目标已触发静音后才等待下一次实时跟踪中";
    for(unsigned font=0;font<3;font++){const char *p=labels;while(*p){uint32_t cp=panel_utf8(&p);
        const struct panel_glyph *g=panel_glyph_find(font,cp);assert(g&&g->code==cp);}}
    assert(panel_robot_box(71).w==panel_robot_box(72).w);
    puts("PASS_ARM_AUTO_UI_SWITCH_NO_MODAL_MODE_SELECTION_NOT_ENABLE_MANUAL_TEST_CONFIRM_STALE_MULTI_TOUCH_POWER_RENDER_GLYPHS");return 0;
}

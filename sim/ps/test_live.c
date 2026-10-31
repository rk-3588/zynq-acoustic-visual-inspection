#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "video_status_render.h"
#include "video_status_robot.h"
int main(void){
    struct panel_status s={.online=1,.acoustic_known=1,.acoustic_enabled=1,.enable_applied=1,
        .raw_valid=1,.raw_col=0,.raw_row=18,.sound="active",.position="moving"};
    for(int col=0;col<64;col++){
        s.raw_col=col;assert(robot_manual_target(&s)==robot_region(col));
        assert(!robot_target(&s));assert(!robot_track_region(&s));
    }
    s.raw_col=32;s.age_ms=151;assert(!robot_manual_target(&s));s.age_ms=0;
    s.raw_valid=0;assert(!robot_manual_target(&s));s.raw_valid=1;
    strcpy(s.sound,"quiet");assert(!robot_manual_target(&s));strcpy(s.sound,"active");
    s.acoustic_enabled=0;assert(!robot_manual_target(&s));s.acoustic_enabled=1;
    struct panel_view v={.robot={.fresh=1,.uart_ready=1,.region_mask=7,.target=1,
        .target_ms=1000,.status_ms=1000}};
    panel_robot_action(&v,94,1000);assert(v.robot.modal);
    v.robot.target=3;v.robot.target_ms=v.robot.status_ms=1100;
    /* The old frozen left region must not cancel a live right confirmation. */
    assert(v.robot.modal_region==1&&panel_robot_confirmable(&v.robot,1100));
    panel_robot_action(&v,72,1100);assert(v.robot.request==3&&!v.robot.modal);
    v.robot.request=0;panel_robot_action(&v,94,1200);assert(v.robot.modal);
    v.robot.target=0;assert(!panel_robot_confirmable(&v.robot,1250));
    panel_robot_action(&v,72,1250);assert(v.robot.modal&&!v.robot.request);
    v.robot.target=2;assert(panel_robot_confirmable(&v.robot,1250));
    panel_robot_action(&v,71,1250);assert(!v.robot.modal&&!v.robot.request);
    struct robot_unknown_timer t={0};struct robot_job j={.mode=ROBOT_MODE_GRAB,.phase=ROBOT_UNKNOWN};
    assert(!robot_unknown_due(&t,&j,0));assert(!robot_unknown_due(&t,&j,4999));
    assert(robot_unknown_due(&t,&j,5000));j.phase=ROBOT_READY;assert(!robot_unknown_due(&t,&j,6000));
    j.phase=ROBOT_UNKNOWN;assert(!robot_unknown_due(&t,&j,7000));
    j.mode=ROBOT_MODE_TRACK;assert(!robot_unknown_due(&t,&j,999999));
    const char *label="实时网格坐标等待声源";
    while(*label){uint32_t c=panel_utf8(&label);const struct panel_glyph *g=panel_glyph_find(PANEL_F_LABEL,c);assert(g&&g->code==c);}
    puts("PASS_MANUAL_MOVING_RAW_ALL_REGIONS_CONFIRM_USES_LATEST_QUIET_STALE_GUARDS_UNKNOWN_5S_GLYPHS");
    return 0;
}

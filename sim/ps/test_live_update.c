#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static char fixture[1024];
#define PANEL_ROBOT_STATE_FILE fixture
#define PANEL_ROBOT_STATE_UID getuid()
#define PANEL_ROBOT_SOCKET "/tmp/arm-live-test-no-real-robot.sock"
#include "video_status_render.h"
#include "video_status_robot.h"
static void publish(uint64_t now){
    FILE *f=fopen(fixture,"w");assert(f);
    fprintf(f,"{\"version\":\"%s\",\"monotonic_ms\":%llu,\"uart_ready\":1,\"fault\":0,"
        "\"id\":390,\"region\":3,\"phase\":0,\"feedback_seen\":0,\"region_mask\":7,"
        "\"mode\":0,\"transaction_pending\":0,\"track_armed\":0,\"track_region\":0,\"track_known\":0,\"track_mask\":0,"
        "\"cycle_known\":0,\"cycle_ready\":0,\"cycle_active\":0,\"cycle_stage\":0,\"cycle_reason\":0,\"cycle_limit\":0,"
        "\"cycle_completed\":0,\"cycle_stopping\":0,\"mode_error\":0,\"lease_valid\":0,\"reply_age_ms\":4294967295,"
        "\"auto_mode\":0,\"auto_waiting\":0,\"auto_latched\":0}\n",ROBOT_VERSION,(unsigned long long)now);
    assert(!fclose(f));
}
int main(int argc,char **argv){
    assert(argc==2);snprintf(fixture,sizeof(fixture),"%s/robot.state",argv[1]);
    struct panel_view v={0};
    struct panel_status s={.online=1,.acoustic_known=1,.acoustic_enabled=1,.enable_applied=1,
        .raw_valid=1,.raw_col=0,.raw_row=18,.sound="active",.position="moving"};
    publish(1000);panel_robot_update(&v,&s,1000);assert(v.robot.target==1);
    panel_robot_action(&v,94,1000);assert(v.robot.modal);unsigned rev=v.robot.revision;
    s.raw_col=63;publish(1020);panel_robot_update(&v,&s,1020);
    assert(v.robot.modal&&v.robot.modal_region==3&&v.robot.revision==rev&&panel_robot_confirmable(&v.robot,1020));
    s.raw_valid=0;strcpy(s.sound,"quiet");publish(1040);panel_robot_update(&v,&s,1040);
    assert(v.robot.modal&&!v.robot.target&&!panel_robot_confirmable(&v.robot,1040));
    s.raw_valid=1;s.raw_col=32;strcpy(s.sound,"active");publish(1060);panel_robot_update(&v,&s,1060);
    assert(v.robot.modal&&v.robot.target==2);panel_robot_action(&v,72,1060);assert(v.robot.request==2);
    v.robot.request=0;panel_robot_action(&v,94,1100);assert(v.robot.modal);
    publish(11100);panel_robot_update(&v,&s,11100);assert(!v.robot.modal&&!v.robot.request);
    puts("PASS_REAL_UI_TICK_RAW_REGION_CHANGE_MODAL_SURVIVES_QUIET_REAPPEAR_CONFIRM_LATEST_EXPIRY");
    return 0;
}

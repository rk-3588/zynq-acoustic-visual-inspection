#include <assert.h>
#include <stdio.h>
#include "video_ui_watchdog_core.h"
static struct ui_watch_sample sample(uint64_t second,uint32_t sequence) {
    return (struct ui_watch_sample){.ns=second*1000000000ull,.sequence=sequence,.pid=42,.ui_on=1,.camera_ok=1};
}
int main(void) {
    struct ui_watch_policy p={0};
    for(unsigned i=1;i<20;i++)assert(!ui_watch_step(&p,sample(i,i*60)));
    p=(struct ui_watch_policy){0};
    assert(!ui_watch_step(&p,sample(1,0)));
    assert(!ui_watch_step(&p,sample(2,80)));
    assert(!ui_watch_step(&p,sample(3,160)));
    assert(ui_watch_step(&p,sample(4,240)));
    ui_watch_attempt_finished(&p,4500000000ull);
    for(unsigned i=5;i<15;i++)assert(!ui_watch_step(&p,sample(i,i*80)));
    assert(!ui_watch_step(&p,sample(15,15*80))); /* crosses cooldown boundary */
    assert(!ui_watch_step(&p,sample(16,16*80)));
    assert(!ui_watch_step(&p,sample(17,17*80)));
    assert(ui_watch_step(&p,sample(18,18*80)));
    /* No cap: repeated faults remain recoverable beyond three attempts. */
    for(unsigned attempt=0;attempt<10;attempt++) {
        uint64_t t=100+attempt*40;
        ui_watch_attempt_finished(&p,(t-11)*1000000000ull);
        assert(!ui_watch_step(&p,sample(t,0)));
        assert(!ui_watch_step(&p,sample(t+1,80)));
        assert(!ui_watch_step(&p,sample(t+2,160)));
        assert(ui_watch_step(&p,sample(t+3,240)));
    }
    for(unsigned mode=0;mode<6;mode++) {
        p=(struct ui_watch_policy){0};
        for(unsigned i=1;i<=3;i++)assert(!ui_watch_step(&p,sample(i,i*80)));
        struct ui_watch_sample bad=sample(4,320);
        if(mode==0)bad.ui_on=0;
        if(mode==1)bad.camera_ok=0;
        if(mode==2)bad.pid=43;
        if(mode==3)bad.sequence=0; /* reset, not natural wrap */
        if(mode==4)bad.ns=8000000000ull; /* missed sample */
        if(mode==5)bad.sequence=3*80+60; /* one normal interval breaks streak */
        assert(!ui_watch_step(&p,bad));assert(p.hits==0);
    }
    for(unsigned rate=71;rate<=91;rate++) {
        p=(struct ui_watch_policy){0};
        for(unsigned i=1;i<4;i++)assert(!ui_watch_step(&p,sample(i,i*rate)));
        assert(ui_watch_step(&p,sample(4,4*rate))==(rate>=72&&rate<=90));
    }
    p=(struct ui_watch_policy){0};
    for(unsigned i=1;i<=4;i++)assert(ui_watch_step(&p,sample(i,UINT32_MAX-100+i*80))==(i==4));
    assert(ui_watch_camera_ok(0x1408b,0x11000,0x1408b,0x11000));
    for(unsigned bit=0;bit<32;bit++) {
        uint32_t mask=1u<<bit;
        if(mask&VIDEO_VDMA_MM2S_ERRORS)assert(!ui_watch_camera_ok(1,mask,1,0));
        if(mask&VIDEO_VDMA_S2MM_ERRORS)assert(!ui_watch_camera_ok(1,0,1,mask));
    }
    assert(!ui_watch_camera_ok(1,1,1,0));assert(!ui_watch_camera_ok(1,0,1,1));
    assert(!ui_watch_camera_ok(0,0,1,0));assert(!ui_watch_camera_ok(1,0,0,0));
    puts("PASS_UI_WATCHDOG_3_INTERVALS_THRESHOLDS_COOLDOWN_NO_CAP_GUARDS_WRAP");
}

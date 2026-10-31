/* Board variant: retain the tested camera divider, toggle horizontal mirror
 * in the final sensor-table write, then run the unchanged production startup.
 * No live camera changes, vertical flip, geometry, PL or UI changes. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ov5640_720p_regs.h"

enum { diagnostic_count=OV5640_720P_REG_COUNT };
static struct ov5640_reg diagnostic_registers[diagnostic_count];
static int make_configuration(void) {
    memcpy(diagnostic_registers,ov5640_720p_regs,sizeof(diagnostic_registers));
    unsigned clocks=0,mirror=diagnostic_count;
    for(unsigned i=0;i<diagnostic_count;i++) {
        if(diagnostic_registers[i].addr==0x3035) {
            if(diagnostic_registers[i].value!=0x11)return 0;
            diagnostic_registers[i].value=0x21;clocks++;
        }
        if(diagnostic_registers[i].addr==0x3821)mirror=i;
    }
    if(clocks!=1||mirror==diagnostic_count||diagnostic_registers[mirror].value!=0x07)return 0;
    diagnostic_registers[mirror].value=0x01;
    unsigned changes=0;
    for(unsigned i=0;i<diagnostic_count;i++) {
        if(diagnostic_registers[i].addr!=ov5640_720p_regs[i].addr)return 0;
        if(diagnostic_registers[i].value!=ov5640_720p_regs[i].value) {
            changes++;
            if(!((diagnostic_registers[i].addr==0x3035&&diagnostic_registers[i].value==0x21)||
                 (i==mirror&&diagnostic_registers[i].value==0x01)))return 0;
        }
    }
    return changes==2;
}
#define ov5640_720p_regs diagnostic_registers
#define main production_init_main
#include "video_pipeline_init.c"
#undef main
#undef ov5640_720p_regs
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--version")){puts("20261030-video-init-half-clock-hflip-v2");return 0;}
    if(!make_configuration()){fprintf(stderr,"Unexpected sensor table; no hardware touched\n");return 2;}
    if(argc==2&&!strcmp(argv[1],"--selftest")) {
        puts("PASS_ONLY_3035_11_TO_21_AND_LAST_3821_07_TO_01_OTHER_REGISTERS_UNCHANGED");return 0;
    }
    if(argc!=1)return 2;
    puts("CAMERA_CONFIGURATION sysdiv=2 register3035=21 register3821=01 horizontal_toggle=1 geometry=1280x720");
    return production_init_main(argc,argv);
}

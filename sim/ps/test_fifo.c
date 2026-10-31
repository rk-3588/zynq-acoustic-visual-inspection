#include <assert.h>
#include <stdio.h>
#include "video_robot_protocol.h"
static unsigned feed(struct robot_parser *s,const unsigned char *p,unsigned n,uint64_t now,struct robot_packet *out){
    unsigned count=0;
    for(unsigned i=0;i<n;i++){
        int have=robot_rx_byte(s,p[i],now,out);
        while(have){count++;have=robot_rx_next(s,out);}
    }
    return count;
}
int main(void){
    struct robot_parser s={0};struct robot_packet out;
    unsigned char frame[130]={0xaa,0x55,0x26,0x34,0x12,1};
    frame[6]=0xff;frame[7]=0x0f;frame[128]=13;frame[129]=10;
    /* UART RX watermark=56, RX timeout=10. Continuous 9600/8N1 bytes
     * appear to userspace as batches, NOT individual 1.04ms arrivals. */
    assert(!feed(&s,frame,56,1000,&out));
    assert(!feed(&s,frame+56,56,1059,&out));
    assert(feed(&s,frame+112,18,1120,&out)==1);
    assert(!s.expired&&out.type==0x26&&out.id==0x1234&&out.length==130);
    /* A long real host-delivery idle still expires a partial candidate. */
    assert(!feed(&s,frame,8,1500,&out));
    assert(!feed(&s,frame,56,1501+ROBOT_HOST_RX_IDLE_MS,&out));
    assert(!feed(&s,frame+56,56,1560+ROBOT_HOST_RX_IDLE_MS,&out));
    assert(feed(&s,frame+112,18,1621+ROBOT_HOST_RX_IDLE_MS,&out)==1);
    assert(s.expired==1&&out.id==0x1234);
    puts("PASS_FIFO_56_56_18_DELIVERY_59MS_GAPS_130_BYTE_FRAME_HOST_IDLE_BOUND_RESYNC");
}

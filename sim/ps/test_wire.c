#include <assert.h>
#include <stdio.h>
#include "video_robot_protocol.h"
static unsigned count;
static struct robot_packet last;
static void feed(struct robot_parser *s,const unsigned char *p,unsigned n,uint64_t *now){
    for(unsigned i=0;i<n;i++){
        int have=robot_rx_byte(s,p[i],(*now)++,&last);
        while(have){count++;have=robot_rx_next(s,&last);}
    }
}
int main(void){
    unsigned char tx[18];robot_command(tx,2,2);
    assert(!memcmp(tx,(unsigned char[]){0xaa,0x55,1,2,0,2,13,10},8));
    robot_command(tx,65535,3);assert(tx[3]==255&&tx[4]==255);
    robot_command(tx,0,1);assert(!tx[3]&&!tx[4]);
    for(unsigned type=0;type<256;type++){
        unsigned n=robot_request_size(type);unsigned char data[11]={0};
        if(n){assert(robot_encode_request(tx,sizeof(tx),type,0x1234,data,n-7)==n);
            assert(tx[2]==type&&tx[3]==0x34&&tx[4]==0x12&&tx[n-2]==13&&tx[n-1]==10);
            assert(!robot_encode_request(tx,n-1,type,1,data,n-7));assert(!robot_encode_request(tx,sizeof(tx),type,1,data,n-6));}
    }
    struct robot_parser s={0};uint64_t now=0;
    const unsigned char packet[]={0xaa,0x55,2,2,0,2,13,10};
    const unsigned char noise[]={0,0xaa,0xaa,0x55,0xfe,13,10};
    feed(&s,noise,sizeof(noise),&now);feed(&s,packet,8,&now);
    assert(count==1&&last.type==2&&last.id==2&&last.payload[0]==2);
    for(unsigned type=0;type<256;type++){
        unsigned n=robot_response_size(type);if(!n)continue;
        unsigned char p[130]={0xaa,0x55,(unsigned char)type,0x34,0x12};
        for(unsigned i=5;i<n-2;i++)p[i]=(unsigned char)(i*17u);
        if(n>30){memcpy(p+6,(unsigned char[]){13,10,0xaa,0x55,2,2,0,0,13,10},10);}
        p[n-2]=13;p[n-1]=10;unsigned before=count;feed(&s,p,n,&now);
        assert(count==before+1&&last.type==type&&last.length==n&&last.id==0x1234);
    }
    /* Host delivery timestamps include FIFO latency, not wire byte times. */
    assert(!s.expired);feed(&s,packet,4,&now);now+=ROBOT_HOST_RX_IDLE_MS+1;unsigned before=count;
    feed(&s,packet,8,&now);assert(s.expired==1&&count==before+1);
    /* Reject the old LEN/CRC status framing and recover at a new header. */
    const unsigned char crc[]={0xaa,0x55,1,2,42,0,0,0x32,0x17,13,10};
    before=count;feed(&s,crc,sizeof(crc),&now);assert(count==before);feed(&s,packet,8,&now);assert(count==before+1);
    /* No CRC means some payload bit flips are UNDETECTABLE, by design. */
    unsigned char changed[8];memcpy(changed,packet,8);changed[5]^=1;feed(&s,changed,8,&now);
    assert(last.payload[0]==3);
    /* A malformed long packet may contain several complete short candidates. */
    unsigned char bad[108]={0xaa,0x55,0x24};memcpy(bad+90,packet,8);memcpy(bad+98,packet,8);
    before=count;feed(&s,bad,108,&now);assert(count==before+2);
    puts("PASS_WIRE_9600_GOLDEN_LE16_FIXED_TYPE_LENGTHS_PAYLOAD_DELIMITERS_HOST_GAP_RESYNC_NO_CRC_LIMIT");
    return 0;
}

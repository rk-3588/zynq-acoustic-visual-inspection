#ifndef VIDEO_ROBOT_PROTOCOL_H
#define VIDEO_ROBOT_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ROBOT_BAUD 9600u
#define ROBOT_COMMAND_SIZE 8u
#define ROBOT_STATUS_SIZE 8u
#define ROBOT_FRAME_MAX 130u
/* The controller's on-wire idle rule is 50ms. Linux read() delivery is not a
 * wire timestamp: a 56-byte FIFO batch takes 58.3ms at 9600/8N1, followed by
 * the UART RX timeout / scheduler delay. Use a separate bounded host gap.
 * Request deadlines (450/650ms) and grab completion deadlines are unchanged. */
#define ROBOT_WIRE_INTERBYTE_MS 50u
#define ROBOT_HOST_RX_IDLE_MS 200u
struct robot_packet {unsigned type,id,length,payload_size;unsigned char payload[ROBOT_FRAME_MAX-7];};
struct robot_parser {unsigned char p[ROBOT_FRAME_MAX];unsigned used,rejected,expired;uint64_t last_ms;};
static inline void robot_put16(unsigned char *p,uint16_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static inline unsigned robot_response_size(unsigned type){
    switch(type){
        case 0x02:case 0x20:case 0x25:case 0x27:return 8;
        case 0x23:return 30;case 0x24:return 108;case 0x26:return 130;
        case 0x28:return 33;case 0x29:return 13;default:return 0;
    }
}
static inline unsigned robot_request_size(unsigned type){
    switch(type){
        case 0x01:case 0x03:case 0x10:case 0x12:case 0x13:case 0x16:
        case 0x30:case 0x31:case 0x33:case 0x34:case 0x40:case 0x41:
        case 0x42:case 0x43:case 0x44:case 0x47:return 8;
        case 0x11:case 0x14:case 0x15:case 0x45:return 12;
        case 0x17:case 0x32:case 0x46:return 18;default:return 0;
    }
}
/* Framing only; the caller validates parameters and explicitly arms motion. */
static inline unsigned robot_encode_request(unsigned char *out,size_t cap,unsigned type,unsigned id,
                                            const unsigned char *payload,size_t n){
    unsigned total=robot_request_size(type);
    if(!total||cap<total||id>65535||!payload||n!=total-7u)return 0;
    out[0]=0xaa;out[1]=0x55;out[2]=(unsigned char)type;robot_put16(out+3,(uint16_t)id);
    memcpy(out+5,payload,n);out[total-2]=13;out[total-1]=10;return total;
}
static inline void robot_command(unsigned char p[ROBOT_COMMAND_SIZE],unsigned id,unsigned region){
    unsigned char value=(unsigned char)region;
    (void)robot_encode_request(p,ROBOT_COMMAND_SIZE,1,id,&value,1);
}
static inline void robot_resync(struct robot_parser *s){
    if(s->used){memmove(s->p,s->p+1,s->used-1);s->used--;}
}
static inline int robot_rx_next(struct robot_parser *s,struct robot_packet *out){
    while(s->used){
        if(s->p[0]!=0xaa){robot_resync(s);continue;}
        if(s->used<2)return 0;
        if(s->p[1]!=0x55){robot_resync(s);continue;}
        if(s->used<3)return 0;
        unsigned n=robot_response_size(s->p[2]);
        if(!n){s->rejected++;robot_resync(s);continue;}
        if(s->used<n)return 0;
        if(s->p[n-2]!=13||s->p[n-1]!=10){s->rejected++;robot_resync(s);continue;}
        out->type=s->p[2];out->id=(unsigned)s->p[3]+256u*s->p[4];out->length=n;out->payload_size=n-7;
        memcpy(out->payload,s->p+5,n-7);
        memmove(s->p,s->p+n,s->used-n);s->used-=n;return 1;
    }
    return 0;
}
/* Fixed length by TYPE: embedded 0D 0A/AA 55 in long payloads remain data.
 * now is host delivery time, NOT a byte's actual arrival time at the pin.
 * This timeout is between host deliveries, not total frame duration. */
static inline int robot_rx_byte(struct robot_parser *s,unsigned char b,uint64_t now,struct robot_packet *out){
    if(s->used&&(now<s->last_ms||now-s->last_ms>ROBOT_HOST_RX_IDLE_MS)){s->used=0;s->expired++;}
    s->last_ms=now;
    if(s->used==ROBOT_FRAME_MAX){s->rejected++;robot_resync(s);}
    s->p[s->used++]=b;
    return robot_rx_next(s,out);
}
#endif

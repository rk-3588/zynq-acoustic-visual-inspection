/* Offline stdin replay against a frozen historical header. No UART/device IO. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#ifndef UART_PROTOCOL_HEADER
#error UART_PROTOCOL_HEADER must name a frozen protocol header
#endif
#include UART_PROTOCOL_HEADER

static int digit(char ch) {
    if(ch>='0' && ch<='9')return ch-'0';
    if(ch>='a' && ch<='f')return ch-'a'+10;
    if(ch>='A' && ch<='F')return ch-'A'+10;
    return -1;
}
static void emit(const struct robot_packet *packet, uint64_t now) {
    printf("{\"type\":\"%02X\",\"task_id\":%u,\"length\":%u,\"delivery_ms\":%" PRIu64 ",\"payload_hex\":\"",
           packet->type,packet->id,packet->length,now);
    for(unsigned i=0;i<packet->payload_size;i++)printf("%02x",packet->payload[i]);
    puts("\"}");
}
int main(void) {
    char line[8192];
    struct robot_parser parser={0};
    struct robot_packet packet;
    while(fgets(line,sizeof(line),stdin)) {
        char *hex;
        errno=0;
        unsigned long long now=strtoull(line,&hex,10);
        if(errno || hex==line || *hex!=' ')return 2;
        hex++;
        size_t n=strcspn(hex,"\r\n");
        if(!n || n%2 || n+2>=sizeof(line))return 2;
        for(size_t i=0;i<n;i+=2) {
            int hi=digit(hex[i]),lo=digit(hex[i+1]);
            if(hi<0 || lo<0)return 2;
            int have=robot_rx_byte(&parser,(unsigned char)(hi*16+lo),(uint64_t)now,&packet);
            while(have) {emit(&packet,(uint64_t)now);have=robot_rx_next(&parser,&packet);}
        }
    }
    printf("{\"kind\":\"summary\",\"rejected\":%u,\"expired\":%u,\"buffered_bytes\":%u}\n",
           parser.rejected,parser.expired,parser.used);
    return ferror(stdin)?2:0;
}

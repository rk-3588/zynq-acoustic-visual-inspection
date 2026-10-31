#include "delay.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static unsigned checked=0;
// Independent integer Q5 reference, including pointer wrap and all weights.
static bool check(const int first[4],unsigned now) {
    int q[16];
    static const unsigned same_direction[8]={7,6,5,4,3,2,1,0};
    ap_uint<40> packed=0;
    for(unsigned i=0;i<4;i++) {
        if(first[i]<-255 || first[i]>255) return false;
        q[i]=first[i];q[i+4]=-first[i];
        packed.range((3-i)*10+9,(3-i)*10)=first[i]&1023;
    }
    for(unsigned i=0;i<8;i++)q[i+8]=2*q[same_direction[i]];
    int minimum=*std::min_element(q,q+16);
    ap_uint<192> al,ar;ap_uint<80> rl;ap_uint<96> rr;
    delay(packed,now,al,rl,ar,rr);
    for(unsigned i=0;i<16;i++) {
        unsigned relative=(unsigned)(q[i]-minimum);
        if(relative>1023) return false;
        unsigned right=(now-relative/32)&4095,left=(right-1)&4095;
        unsigned frac=relative%32;
        if(ap_uint<12>(ar.range(i*12+11,i*12)).to_uint()!=right ||
           ap_uint<12>(al.range(i*12+11,i*12)).to_uint()!=left ||
           ap_uint<5>(rl.range(i*5+4,i*5)).to_uint()!=frac ||
           ap_uint<6>(rr.range(i*6+5,i*6)).to_uint()!=32-frac) {
            std::cerr<<"FAIL: lane="<<i<<" now="<<now<<" q="<<q[i]<<" min="<<minimum<<"\n";
            return false;
        }
    }
    checked++;return true;
}
static std::vector<uint64_t> read_rom(const char *path) {
    std::ifstream f(path);std::vector<uint64_t> words;std::string line;
    if(!f) return words;
    while(std::getline(f,line)) {
        if(line.size()<40 || line.find_first_not_of("01")<40) continue;
        uint64_t word=0;
        for(unsigned i=0;i<40;i++)word=(word<<1)|(line[i]-'0');
        words.push_back(word);
    }
    return words;
}
int main(int argc,char **argv) {
    constexpr double pi=3.14159265358979323846,fs=46875.0,c=343.0,r=.025;
    const double focal=std::hypot(31.5,17.5)/std::tan(pi/6);
    std::vector<uint64_t> rom;
    if(argc>1) {
        rom=read_rom(argv[1]);
        if(rom.size()!=9216) {std::cerr<<"FAIL: ROM size/read\n";return 1;}
    }
    const unsigned pointers[]={0,31,32,2048,4064,4095};
    int max_signed=0,max_relative=0;
    for(unsigned y=0;y<36;y++)for(unsigned x=0;x<64;x++) {
        double dx=x-31.5,dy=y-17.5,norm=std::sqrt(dx*dx+dy*dy+focal*focal);
        int first[4],q[16];uint64_t packed=0;
        for(unsigned i=0;i<4;i++) {
            double angle=(45+45*i)*pi/180;
            double value=r*(std::cos(angle)*dx+std::sin(angle)*dy)*fs/c/norm;
            first[i]=(int)std::floor(value*32+.5);
            q[i]=first[i];q[i+4]=-first[i];
            packed=(packed<<10)|(first[i]&1023);
        }
        if(!rom.empty() && rom[y*64+x]!=packed) {std::cerr<<"FAIL: ROM geometry\n";return 1;}
        for(unsigned i=0;i<8;i++) {
            q[8+i]=2*q[7-i];
            // Independent physical angles from the user-confirmed board view.
            double angle=-45.0*i*pi/180;
            double ideal=2*r*(std::cos(angle)*dx+std::sin(angle)*dy)*fs/c/norm*32;
            if(std::fabs(q[8+i]-ideal)>1.000001) {std::cerr<<"FAIL: outer physical direction\n";return 1;}
        }
        for(unsigned i=0;i<16;i++)max_signed=std::max(max_signed,std::abs(q[i]));
        max_relative=std::max(max_relative,*std::max_element(q,q+16)-*std::min_element(q,q+16));
        for(unsigned p:pointers)if(!check(first,p))return 1;
    }
    for(unsigned i=2304;i<rom.size();i++)if(rom[i]) {std::cerr<<"FAIL: ROM tail\n";return 1;}
    // Boundary and deterministic random cases, within the no-overflow domain.
    const int bounds[][4]={{255,-255,0,1},{-255,255,-1,0},{0,0,0,0},{1,-1,31,-31}};
    for(auto &v:bounds)for(unsigned p:pointers)if(!check(v,p))return 1;
    uint32_t random=0x16ac0250;
    for(unsigned trial=0;trial<512;trial++) {
        int first[4];
        for(unsigned i=0;i<4;i++) {random=random*1664525u+1013904223u;first[i]=(int)(random%511)-255;}
        if(!check(first,random&4095))return 1;
    }
    std::cout<<"PASS: dual-ring delay transactions="<<checked<<", all 16 lanes/addresses/weights, signed & wrap cases\n"
             <<"GEOMETRY: max_abs_q5="<<max_signed<<" max_relative_q5="<<max_relative
             <<" ROM_checked="<<!rom.empty()<<" grid=64x36 fs=46875 inner=25mm outer=50mm reverse-order\n";
    return 0;
}

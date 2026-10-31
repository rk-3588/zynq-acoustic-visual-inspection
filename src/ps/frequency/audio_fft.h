#ifndef AUDIO_FFT_H
#define AUDIO_FFT_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#define AUDIO_N 2048
#define AUDIO_PI 3.14159265358979323846
struct audio_result {
    double frequency, rms, prominence_db, mean;
    uint32_t peak, clipped_samples;
    int32_t minimum, maximum;
    int valid;
};
static int audio_compare(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y);
}
/* Only a spectral candidate: not a replacement for acoustic quiet gating,
 * not a calibrated SNR or dB SPL, not associated with a particular hotspot. */
static struct audio_result audio_analyse(const int32_t *samples,double fs,double low,double high) {
    struct audio_result out={0};
    if(!samples || !isfinite(fs) || !isfinite(low) || !isfinite(high) ||
       fs<=0 || low<0 || high<=low || low>=fs/2)return out;
    if(high>=fs/2)high=fs/2-fs/AUDIO_N;
    out.minimum=out.maximum=samples[0];
    double real[AUDIO_N],imag[AUDIO_N]={0},power[AUDIO_N/2+1],noise[AUDIO_N/2+1];
    double mean=0,energy=0;
    for(unsigned i=0;i<AUDIO_N;i++) {
        mean+=samples[i]; uint32_t mag=samples[i]<0?(uint32_t)(-(int64_t)samples[i]):(uint32_t)samples[i];
        if(mag>out.peak)out.peak=mag;
        if(samples[i]<out.minimum)out.minimum=samples[i];
        if(samples[i]>out.maximum)out.maximum=samples[i];
        if(samples[i]<=-8388608 || samples[i]>=8388607)out.clipped_samples++;
    }
    mean/=AUDIO_N;
    out.mean=mean;
    for(unsigned i=0;i<AUDIO_N;i++) {
        double x=samples[i]-mean; energy+=x*x;
        real[i]=x*(.5-.5*cos(2*AUDIO_PI*i/(AUDIO_N-1)));
    }
    out.rms=sqrt(energy/AUDIO_N);
    for(unsigned i=1,j=0;i<AUDIO_N;i++) {
        unsigned bit=AUDIO_N>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j) {double t=real[i];real[i]=real[j];real[j]=t;}
    }
    for(unsigned len=2;len<=AUDIO_N;len<<=1) {
        double angle=-2*AUDIO_PI/len,step_r=cos(angle),step_i=sin(angle);
        for(unsigned base=0;base<AUDIO_N;base+=len) {
            double wr=1,wi=0;
            for(unsigned j=0;j<len/2;j++) {
                unsigned a=base+j,b=a+len/2;
                double tr=wr*real[b]-wi*imag[b],ti=wr*imag[b]+wi*real[b];
                real[b]=real[a]-tr;imag[b]=imag[a]-ti;real[a]+=tr;imag[a]+=ti;
                double nr=wr*step_r-wi*step_i;wi=wr*step_i+wi*step_r;wr=nr;
            }
        }
    }
    unsigned first=(unsigned)ceil(low*AUDIO_N/fs),last=(unsigned)floor(high*AUDIO_N/fs);
    if(first<1)first=1;
    if(last>=AUDIO_N/2)last=AUDIO_N/2-1;
    if(first>=last || out.rms==0)return out;
    for(unsigned k=0;k<=AUDIO_N/2;k++)power[k]=real[k]*real[k]+imag[k]*imag[k];
    unsigned peak=first,count=0;
    for(unsigned k=first;k<=last;k++) {
        if(power[k]>power[peak])peak=k;
        noise[count++]=power[k];
    }
    qsort(noise,count,sizeof(double),audio_compare);
    double floor_power=fmax(noise[count/2],1e-30),p=power[peak];
    if(p<=0)return out;
    out.prominence_db=10*log10(p/floor_power);
    double delta=0;
    if(peak>first && peak<last) {
        double a=log(fmax(power[peak-1],1e-30)),b=log(p),c=log(fmax(power[peak+1],1e-30));
        double denominator=a-2*b+c;
        if(denominator< -1e-12)delta=fmax(-.5,fmin(.5,.5*(a-c)/denominator));
    }
    out.frequency=(peak+delta)*fs/AUDIO_N;
    out.valid=out.prominence_db>=10; return out;
}
#endif

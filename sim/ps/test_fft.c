#include "audio_fft.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int32_t samples[AUDIO_N]; double tones[]={800,2000,3000,3500,997.3};
    for(unsigned t=0;t<sizeof(tones)/sizeof(tones[0]);t++) {
        for(unsigned i=0;i<AUDIO_N;i++) samples[i]=(int32_t)llround(123456+100000*sin(2*AUDIO_PI*tones[t]*i/46875));
        struct audio_result r=audio_analyse(samples,46875,300,10000);
        printf("tone=%.1f estimate=%.3f rms=%.1f prominence=%.1f\n",tones[t],r.frequency,r.rms,r.prominence_db);
        assert(r.valid && fabs(r.frequency-tones[t])<1);
        assert(r.rms>69000 && r.rms<72000);
    }
    for(unsigned i=0;i<AUDIO_N;i++)samples[i]=500000;
    assert(!audio_analyse(samples,46875,300,10000).valid);
    for(unsigned i=0;i<AUDIO_N;i++)samples[i]=0;
    assert(!audio_analyse(samples,46875,300,10000).valid);
    /* A small tone riding on a large DC component must survive mean removal. */
    for(unsigned i=0;i<AUDIO_N;i++)samples[i]=(int32_t)llround(5000000+20*sin(2*AUDIO_PI*800*i/46875));
    struct audio_result weak=audio_analyse(samples,46875,300,10000);
    assert(weak.valid && fabs(weak.frequency-800)<2 && weak.rms>13 && weak.rms<16);
    /* Strongest in-band component, not necessarily the fundamental. */
    for(unsigned i=0;i<AUDIO_N;i++)samples[i]=(int32_t)llround(
        10000*sin(2*AUDIO_PI*2000*i/46875)+50000*sin(2*AUDIO_PI*3500*i/46875));
    assert(fabs(audio_analyse(samples,46875,300,10000).frequency-3500)<1);
    assert(fabs(audio_analyse(samples,46875,300,2500).frequency-2000)<1);
    samples[0]=-8388608;samples[1]=8388607;
    struct audio_result clipped=audio_analyse(samples,46875,300,10000);
    assert(clipped.clipped_samples==2 && clipped.minimum==-8388608 && clipped.maximum==8388607);
    assert(clipped.peak==8388608);
    assert(!audio_analyse(NULL,46875,300,10000).valid);
    assert(!audio_analyse(samples,0,300,10000).valid);
    assert(!audio_analyse(samples,NAN,300,10000).valid);
    assert(!audio_analyse(samples,46875,300,100).valid);
    puts("PASS FFT tones/DC/silence/weak/two-tone/clipping/invalid-input");return 0;
}

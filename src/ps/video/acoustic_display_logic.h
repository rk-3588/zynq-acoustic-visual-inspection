#ifndef ACOUSTIC_DISPLAY_LOGIC_H
#define ACOUSTIC_DISPLAY_LOGIC_H
#include <math.h>
#include <stdint.h>
#include <string.h>

struct display_config {
    double noise_peak, on_ratio, off_ratio, contrast_min, hotspot_floor_ratio, red_core_ratio;
    double attack_seconds, release_seconds, slew_bits_per_second;
    unsigned on_count, off_count, period_ms, alpha;
    int adaptive, low_contrast_stabilize;
};
struct display_logic {
    int gate, filter_ready, palette_ready;
    unsigned above, below, alpha;
    double filtered_log, low_log, high_log;
    int palette_low_contrast, palette_floor_ready;
    double palette_floor_log, palette_reference_log;
    float colors[6];
};
static inline struct display_config display_defaults(void) {
    return (struct display_config){.noise_peak=0,.on_ratio=2,.off_ratio=1.4,
        .contrast_min=.03,.hotspot_floor_ratio=.65,.red_core_ratio=.95,
        .attack_seconds=.10,.release_seconds=.60,
        .slew_bits_per_second=6,.on_count=3,.off_count=8,.period_ms=50,
        .alpha=100,.adaptive=1,.low_contrast_stabilize=1};
}
static inline double display_limit(double x,double a,double b) {
    return fmax(a,fmin(b,x));
}
static inline double display_smooth(double old,double target,
                                    const struct display_config *c,double dt) {
    double tau=target>old ? c->attack_seconds : c->release_seconds;
    double delta=(target-old)*(1-exp(-dt/tau));
    return old+display_limit(delta,-c->slew_bits_per_second*dt,c->slew_bits_per_second*dt);
}
// peak is a single PL snapshot scalar. max/min are a best-effort, stable-read
// display-map pair, NOT an atomic acoustic snapshot or detection confidence.
static inline void display_step(const struct display_config *c,struct display_logic *s,
    uint32_t peak,int valid,uint32_t minimum,uint32_t maximum,int stats_valid,double dt) {
    static const float fixed[6]={24,21,18,15,12,6};
    if(!valid || !c->noise_peak) {
        s->gate=0;s->above=s->below=s->alpha=0;s->filter_ready=0;
        return;
    }
    double p=log2(fmax(1,peak));
    if(!s->filter_ready) {s->filtered_log=p;s->filter_ready=1;}
    else {
        double tau=p>s->filtered_log ? c->attack_seconds : c->release_seconds;
        s->filtered_log+=(p-s->filtered_log)*(1-exp(-dt/tau));
    }
    double energy=exp2(s->filtered_log);
    double contrast=stats_valid && maximum ? (maximum-minimum)/(double)maximum : 0;
    int spatial_ok=!c->adaptive || (stats_valid && contrast>=c->contrast_min);
    if(!s->gate) {
        s->below=0;
        // Require consecutive RAW samples too. A single impulse must not
        // enter the gate later just because its smoothed tail remains high.
        s->above=(peak>=c->noise_peak*c->on_ratio &&
                  energy>=c->noise_peak*c->on_ratio && spatial_ok) ? s->above+1 : 0;
        if(s->above>=c->on_count) {s->gate=1;s->above=0;s->palette_ready=0;}
    } else {
        s->above=0;
        int leave=energy<c->noise_peak*c->off_ratio ||
            (c->adaptive && (!stats_valid || contrast<c->contrast_min*.5));
        s->below=leave ? s->below+1 : 0;
        if(s->below>=c->off_count) {s->gate=0;s->below=0;}
    }
    if(!c->adaptive) memcpy(s->colors,fixed,sizeof(fixed));
    else if(stats_valid && maximum>minimum) {
        // Keep the upper part of the map's LINEAR energy range. The ratio is
        // not a percentage of pixels or a physical resolution improvement.
        // .08 restores the previous broad palette; .65 concentrates the map.
        // Keep the existing noise floor and dynamic range protection.
        double floor_energy=fmax(c->noise_peak*1.05,
            fmax(minimum+c->hotspot_floor_ratio*(maximum-minimum),maximum/64.0));
        double target_high=log2(maximum),target_low=log2(fmax(1,floor_energy));
        target_low=fmin(target_low,target_high-.02);
        // Anchor the whole palette to CURRENT max in both directions. An
        // absolute smoothed floor can exceed the entire map after an energy
        // drop, hiding the hotspot although the quiet gate is still active.
        // Smooth the RELATIVE log-span (shape/contrast), not absolute gain.
        // Uniform gain changes then translate all thresholds together.
        double high=fmin(target_high,23.999);
        target_low=fmin(target_low,high-.02);
        double target_span=fmax(.02,high-target_low);
        double span=s->palette_ready ?
            display_smooth(s->high_log-s->low_log,target_span,c,dt) : target_span;
        // Do not broaden beyond the current noise/concentration floor.
        // Keep the floor below current max, so at least its peak is eligible.
        span=display_limit(span,.02,target_span);
        double low=high-span;
        // Low spatial contrast is weak directional information, not grounds
        // for stretching a 2-3% energy difference over every saturated color.
        // Mode hysteresis avoids switching policy on every noisy map.
        int was_low=s->palette_low_contrast;
        if(!c->low_contrast_stabilize) s->palette_low_contrast=0;
        else if(!s->palette_low_contrast && contrast<.15) s->palette_low_contrast=1;
        else if(s->palette_low_contrast && contrast>.22) s->palette_low_contrast=0;
        if(s->palette_low_contrast) {
            double raw_high=log2(maximum);
            // Real gain steps must not leave the old floor above the signal
            // or below all background pixels. Small changes get a deadband
            // and 0.4 s low-pass smoothing of the footprint threshold.
            if(!s->palette_floor_ready || !was_low ||
               fabs(raw_high-s->palette_reference_log)>.18) {
                s->palette_floor_log=low;
            } else {
                double change=low-s->palette_floor_log;
                if(fabs(change)>.01)
                    s->palette_floor_log+=change*(1-exp(-dt/.4));
            }
            s->palette_reference_log=raw_high;
            s->palette_floor_ready=1;
            double guard_low=log2(fmax(c->noise_peak*1.05,
                                  minimum+.20*(maximum-minimum)));
            double guard_high=raw_high-.02;
            // Keep some background uncolored, and keep the current peak
            // eligible. If the interval collapses use the peak-safe bound.
            s->palette_floor_log=fmin(fmax(s->palette_floor_log,guard_low),guard_high);
            low=s->palette_floor_log;
            // ~13.3% energy span; do NOT lower the cutoff to obtain it.
            // Raise upper color thresholds instead. Flat maps therefore
            // tend to cool colors, rather than all red or all-screen color.
            high=fmin(23.999,fmax(raw_high,low+.18));
            low=fmin(low,high-.18);
            span=high-low;
        } else {
            s->palette_floor_ready=0;
            // After leaving low-contrast mode, restore the exact v5 target
            // span immediately instead of carrying its artificial ceiling.
            if(was_low) {span=target_span;low=high-span;}
        }
        s->low_log=low;s->high_log=high;s->palette_ready=1;
        s->colors[0]=24;
        // Separate the red core from total colored footprint. This fraction
        // is in log-energy space, not a fraction of colored pixels.
        s->colors[1]=(float)(low+c->red_core_ratio*span);
        s->colors[2]=(float)(low+.62*span);
        s->colors[3]=(float)(low+.42*span);
        s->colors[4]=(float)(low+.22*span);
        s->colors[5]=(float)low;
    }
    if(!s->gate || (c->adaptive && !stats_valid)) s->alpha=0;
    else s->alpha=s->alpha+20>c->alpha ? c->alpha : s->alpha+20;
}
#endif

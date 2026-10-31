#ifndef VIDEO_STATUS_PANEL_H
#define VIDEO_STATUS_PANEL_H
#include "video_mixer_ui.h"
#include "pl_tracking.h"
#include "video_status_frequency.h"
#include <limits.h>

#define PANEL_VERSION "20261031-video-status-arm-live-v30"
#define PANEL_WIDTH 1280u
#ifndef PANEL_HEIGHT
#define PANEL_HEIGHT 720u
#endif
#define PANEL_STRIDE (PANEL_WIDTH * 4u)
#define PANEL_BYTES (PANEL_STRIDE * PANEL_HEIGHT)
#define PANEL_X 0u
#define PANEL_Y 0u
#define PANEL_SLOT_BYTES 0x400000u
#define PANEL_MAP_BYTES (2u * PANEL_SLOT_BYTES)
#define PANEL_PERIOD_MS 200u
_Static_assert(PANEL_BYTES <= PANEL_SLOT_BYTES, "UI must fit in one reserved slot");
static inline uint32_t panel_address(unsigned slot) {
    return MIX_UI_BASE + slot * PANEL_SLOT_BYTES;
}
/* Keep the original 320x180 static RGBA test configuration unchanged. */
static inline int panel_enable_idle(const struct video_mixer_io *io) {
    if (!(io->read(io->context, MIX_CTRL) & MIX_IDLE) ||
        io->read(io->context, MIX_WIDTH) != 1280 ||
        io->read(io->context, MIX_HEIGHT) != 720 ||
        io->read(io->context, MIX_FORMAT) != 0 ||
        io->read(io->context, MIX_LAYERS) != MIX_PRIMARY_ONLY) return -1;
    const uint32_t values[] = {256, PANEL_X, PANEL_Y, PANEL_WIDTH,
        PANEL_STRIDE, PANEL_HEIGHT, 0, MIX_UI_BASE, 0, 0};
    for (unsigned i=0; i<MIX_UI_REG_COUNT; ++i)
        io->write(io->context, mix_ui_offsets[i], values[i]);
    for (unsigned i=0; i<MIX_UI_REG_COUNT; ++i)
        if (io->read(io->context, mix_ui_offsets[i]) != values[i]) return -2;
    io->write(io->context, MIX_LAYERS, 3);
    if (io->read(io->context, MIX_LAYERS) != 3) return -3;
    io->write(io->context, MIX_CTRL, MIX_AUTO_RESTART | MIX_START);
    return 0;
}
struct panel_swap {
    unsigned front, target, pending, events, armed;
    uint64_t flips, completions;
};
/* Register readback is NOT flip completion. Generated v_mix RTL snapshots
 * layer1_buf1 in state1, once per frame. Wait for two fresh ap_done events
 * after submission: an old in-flight frame, then at least one new frame.
 * No CPU writes to either slot while pending. ISR is TOW; GIE stays zero. */
static inline int panel_submit(const struct video_mixer_io *io,
                                struct panel_swap *s, unsigned target) {
    if (s->pending || target > 1 || target == s->front) return -1;
    if (io->read(io->context, MIX_LAYERS) != 3 ||
        io->read(io->context, MIX_IER) != 1 || io->read(io->context, MIX_GIE) != 0 ||
        !(io->read(io->context, MIX_CTRL) & MIX_AUTO_RESTART)) return -2;
    io->write(io->context, MIX_UI_BUF1, panel_address(target));
    if (io->read(io->context, MIX_UI_BUF1) != panel_address(target)) return -3;
    uint32_t pending = io->read(io->context, MIX_ISR) & 1;
    if (pending) io->write(io->context, MIX_ISR, pending);
    s->target = target; s->pending = 1; s->events = 0; s->armed = 0;
    return 0;
}
static inline int panel_retire_step(const struct video_mixer_io *io, struct panel_swap *s) {
    if (!s->pending) return -1;
    unsigned bit = io->read(io->context, MIX_ISR) & 1;
    if (!s->armed) {
        if (bit) io->write(io->context, MIX_ISR, 1);
        else s->armed = 1; /* Require a low before accepting each fresh event. */
        return 0;
    }
    if (!bit) return 0;
    io->write(io->context, MIX_ISR, 1);
    s->armed = 0; s->events++; s->completions++;
    if (s->events < 2) return 0;
    s->front = s->target; s->pending = 0; s->flips++;
    return 1;
}

struct panel_status {
    int online, valid, col, row;
    uint32_t peak, age_ms;
    char sound[16], position[24];
    int raw_valid, raw_col, raw_row;
    struct panel_frequency frequency;
    int acoustic_known,acoustic_enabled,enable_applied;
};
static inline void panel_unavailable(struct panel_status *s) {
    memset(s, 0, sizeof(*s)); s->col = s->row = s->raw_col = s->raw_row = -1;
    s->frequency.reason="unavailable";
    strcpy(s->sound, "unknown"); strcpy(s->position, "unavailable");
}
static inline int panel_string_value(const char *p, const char *end, char *out, size_t cap) {
    struct pl_json j = {p, end, 0}; const char *start; size_t n;
    if (!pl_json_string(&j, &start, &n) || j.p != end || n >= cap) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((start[i] >= 'a' && start[i] <= 'z') ||
              (start[i] >= '0' && start[i] <= '9') || start[i] == '_' || start[i] == '-')) return 0;
    memcpy(out, start, n); out[n] = 0; return 1;
}
static inline int panel_number(const char *p, const char *end, uint32_t *out) {
    if (p == end || *p < '0' || *p > '9') return 0;
    uint64_t n = 0;
    for (; p < end; ++p) {
        if (*p < '0' || *p > '9') return 0;
        n = n * 10 + (unsigned)(*p - '0');
        if (n > UINT32_MAX) return 0;
    }
    *out = (uint32_t)n; return 1;
}
/* Parse the real daemon reply, reject malformed/duplicate known fields.
 * This does not invent a second acoustic gate/stability algorithm. */
static inline int panel_parse_status(const char *text, size_t len, struct panel_status *s) {
    panel_unavailable(s);
    if (!len || len > 4096) return 0;
    struct pl_json j = {text, text + len, 0};
    struct panel_status candidate; panel_unavailable(&candidate);
    static const char *keys[] = {"sound_state", "position_state", "position_valid",
        "filtered_col", "filtered_row", "hotspot_value", "observation_age_ms", "version", "gate_health",
        "raw_x", "raw_y", "read_quality", "acoustic_enabled", "enable_applied"};
    unsigned seen = 0; char version[64] = "", health[32] = "", quality[32]="";
    pl_json_space(&j);
    if (j.p == j.end || *j.p++ != '{') return 0;
    while (j.p < j.end) {
        const char *key; size_t n;
        pl_json_space(&j);
        if (!pl_json_string(&j, &key, &n)) return 0;
        pl_json_space(&j); if (j.p == j.end || *j.p++ != ':') return 0;
        pl_json_space(&j); const char *start = j.p;
        if (!pl_json_value(&j)) return 0;
        const char *end = j.p; unsigned k;
        for (k = 0; k < sizeof(keys)/sizeof(keys[0]); ++k)
            if (strlen(keys[k]) == n && !memcmp(keys[k], key, n)) break;
        if (k < sizeof(keys)/sizeof(keys[0])) {
            if (seen & (1u << k)) return 0;
            seen |= 1u << k; uint32_t value = 0;
            switch (k) {
            case 0: if (!panel_string_value(start,end,candidate.sound,sizeof(candidate.sound))) return 0; break;
            case 1: if (!panel_string_value(start,end,candidate.position,sizeof(candidate.position))) return 0; break;
            case 2:
                if (end-start == 4 && !memcmp(start,"true",4)) candidate.valid=1;
                else if (end-start != 5 || memcmp(start,"false",5)) return 0;
                break;
            case 3: case 4:
                if (end-start == 4 && !memcmp(start,"null",4)) value=UINT32_MAX;
                else if (!panel_number(start,end,&value) || value >= (k==3 ? 64u : 36u)) return 0;
                if (k==3) candidate.col=value==UINT32_MAX ? -1 : (int)value;
                else candidate.row=value==UINT32_MAX ? -1 : (int)value;
                break;
            case 5: if (!panel_number(start,end,&candidate.peak) || candidate.peak>0xffffff) return 0; break;
            case 6: if (!panel_number(start,end,&candidate.age_ms)) return 0; break;
            case 7: if (!panel_string_value(start,end,version,sizeof(version))) return 0; break;
            case 8: if (!panel_string_value(start,end,health,sizeof(health))) return 0; break;
            case 9: case 10:
                if(end-start==4 && !memcmp(start,"null",4))value=UINT32_MAX;
                else if (!panel_number(start,end,&value)) return 0;
                if (k==9) candidate.raw_col=value<64 ? (int)value : -1;
                else candidate.raw_row=value<36 ? (int)value : -1;
                break;
            case 11: if (!panel_string_value(start,end,quality,sizeof(quality))) return 0; break;
            case 12: case 13:
                if(end-start==4 && !memcmp(start,"true",4))value=1;
                else if(end-start==5 && !memcmp(start,"false",5))value=0;
                else return 0;
                if(k==12)candidate.acoustic_enabled=(int)value;else candidate.enable_applied=(int)value;
                break;
            }
        }
        pl_json_space(&j); if (j.p == j.end) return 0;
        if (*j.p == '}') {j.p++; break;}
        if (*j.p++ != ',') return 0;
    }
    pl_json_space(&j);
    int new_version=!strcmp(version,"20261013-pl-control-enable-v3");
    if (j.p != j.end || (seen & 0x1ff) != 0x1ff || (!new_version && strcmp(version,"20261006-pl-control-v2")) ||
        candidate.age_ms > 150 || strcmp(health,"fresh")) return 0;
    if(new_version) {
        if((seen&0x3000)!=0x3000)return 0;
        candidate.acoustic_known=1;
        if(!candidate.acoustic_enabled || !candidate.enable_applied) {
            if(candidate.valid || candidate.col!=-1 || candidate.row!=-1 || candidate.raw_col!=-1 || candidate.raw_row!=-1)return 0;
            if(strcmp(candidate.position,candidate.acoustic_enabled?"enable_pending":"disabled"))return 0;
        }
    }
    if (strcmp(candidate.sound,"active") && strcmp(candidate.sound,"quiet")) return 0;
    if (candidate.valid && (strcmp(candidate.sound,"active") || strcmp(candidate.position,"stable") ||
        candidate.col<0 || candidate.row<0)) return 0;
    if (!candidate.valid) candidate.col=candidate.row=-1;
    candidate.raw_valid=(seen & 0xe00)==0xe00 && !strcmp(quality,"best_effort") &&
        !strcmp(candidate.sound,"active") && candidate.raw_col>=0 && candidate.raw_row>=0 &&
        (!new_version || (candidate.acoustic_enabled && candidate.enable_applied));
    if (!candidate.raw_valid) candidate.raw_col=candidate.raw_row=-1;
    candidate.online=1; *s=candidate; return 1;
}
#endif

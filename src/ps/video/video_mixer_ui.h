#ifndef VIDEO_MIXER_UI_H
#define VIDEO_MIXER_UI_H
#include "video_mixer.h"

#define MIX_UI_VERSION "20261006-mixer-ui-test-v1"
#define MIX_UI_BASE 0x03000000u
#define MIX_UI_WIDTH 320u
#define MIX_UI_HEIGHT 180u
#define MIX_UI_STRIDE (MIX_UI_WIDTH * 4u)
#define MIX_UI_BYTES (MIX_UI_STRIDE * MIX_UI_HEIGHT)
#define MIX_UI_X 32u
#define MIX_UI_Y 32u
#define MIX_UI_ALPHA 0x200u
#define MIX_UI_STARTX 0x208u
#define MIX_UI_STARTY 0x210u
#define MIX_UI_W 0x218u
#define MIX_UI_S 0x220u
#define MIX_UI_H 0x228u
#define MIX_UI_SCALE 0x230u
#define MIX_UI_BUF1 0x240u
#define MIX_UI_BUF2 0x24cu
#define MIX_UI_BUF3 0x258u

static const unsigned mix_ui_offsets[] = {
    MIX_UI_ALPHA, MIX_UI_STARTX, MIX_UI_STARTY, MIX_UI_W, MIX_UI_S,
    MIX_UI_H, MIX_UI_SCALE, MIX_UI_BUF1, MIX_UI_BUF2, MIX_UI_BUF3
};
#define MIX_UI_REG_COUNT (sizeof(mix_ui_offsets) / sizeof(mix_ui_offsets[0]))

/* This test exclusively uses the gap after the project's two triple buffers.
 * Bounds use 64 bits so wraparound cannot turn an invalid range into valid. */
static inline int mix_ui_memory_safe(uint64_t base, uint64_t size) {
    const uint64_t banks[] = {0x01100000ull, 0x02100000ull};
    const uint64_t bank_bytes = 3ull * 1280 * 720 * 3;
    if (!size || base < 0x01000000ull || base >= 0x04000000ull ||
        size > 0x04000000ull - base || (base & 7)) return 0;
    for (unsigned i = 0; i < 2; ++i)
        if (base < banks[i] + bank_bytes && base + size > banks[i]) return 0;
    return 1;
}

/* Linux ABGR8888 / RGBA8: memory bytes R,G,B,A (not ARGB byte order). */
static inline uint32_t mix_ui_pixel(unsigned x, unsigned y) {
    static const uint32_t colors[] = {0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0x00ffffffu};
    unsigned band = x / 80u;
    if (band > 3) band = 3;
    unsigned a = y < 60 ? 255u : y < 120 ? 128u :
        (((x / 20u) + (y / 20u)) & 1u) ? 0u : 255u;
    if (x < 2 || x >= MIX_UI_WIDTH - 2 || y < 2 || y >= MIX_UI_HEIGHT - 2)
        return 0xffffffffu;
    return colors[band] | (a << 24);
}

static inline int mix_ui_drain(const struct video_mixer_io *io) {
    if (io->read(io->context, MIX_CTRL) & MIX_FLUSH) return -2;
    io->write(io->context, MIX_CTRL, 0);
    for (unsigned i = 0; i < 500; ++i) {
        if (io->read(io->context, MIX_CTRL) & MIX_IDLE) return 0;
        io->pause_us(io->context, 1000);
    }
    /* A drain failure must not leave the known-good primary stream stopped. */
    io->write(io->context, MIX_CTRL, MIX_AUTO_RESTART | MIX_START);
    return -1;
}

/* Called ONLY while idle, AFTER the static buffer is filled and published. */
static inline int mix_ui_enable_idle(const struct video_mixer_io *io) {
    if (!(io->read(io->context, MIX_CTRL) & MIX_IDLE) ||
        io->read(io->context, MIX_WIDTH) != 1280 ||
        io->read(io->context, MIX_HEIGHT) != 720 ||
        io->read(io->context, MIX_FORMAT) != 0 ||
        io->read(io->context, MIX_LAYERS) != MIX_PRIMARY_ONLY) return -1;
    const uint32_t values[] = {256, MIX_UI_X, MIX_UI_Y, MIX_UI_WIDTH,
        MIX_UI_STRIDE, MIX_UI_HEIGHT, 0, MIX_UI_BASE, 0, 0};
    for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i)
        io->write(io->context, mix_ui_offsets[i], values[i]);
    for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i)
        if (io->read(io->context, mix_ui_offsets[i]) != values[i]) return -2;
    io->write(io->context, MIX_LAYERS, 3);
    if (io->read(io->context, MIX_LAYERS) != 3) return -3;
    io->write(io->context, MIX_CTRL, MIX_AUTO_RESTART | MIX_START);
    return 0;
}

static inline void mix_ui_restore_idle(const struct video_mixer_io *io,
                                       const uint32_t *saved) {
    io->write(io->context, MIX_LAYERS, MIX_PRIMARY_ONLY);
    for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i)
        io->write(io->context, mix_ui_offsets[i], saved[i]);
    io->write(io->context, MIX_CTRL, MIX_AUTO_RESTART | MIX_START);
}
#endif

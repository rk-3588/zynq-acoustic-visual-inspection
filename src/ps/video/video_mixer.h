#ifndef VIDEO_MIXER_H
#define VIDEO_MIXER_H

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_MIXER_VERSION "20261006-video-mixer-primary-v1"
#define VIDEO_MIXER_UIO_NAME "video_mixer@44a30000"
#define VIDEO_MIXER_BASE 0x44a30000ull
#define VIDEO_MIXER_MAP_SIZE 0x10000u

/* Verified against this build's system_v_mix_0_0_CTRL_s_axi.v (v_mix 6.1). */
#define MIX_CTRL 0x000u
#define MIX_GIE 0x004u
#define MIX_IER 0x008u
#define MIX_ISR 0x00cu
#define MIX_WIDTH 0x010u
#define MIX_HEIGHT 0x018u
#define MIX_FORMAT 0x020u
#define MIX_BACKGROUND_R 0x028u
#define MIX_BACKGROUND_G 0x030u
#define MIX_BACKGROUND_B 0x038u
#define MIX_LAYERS 0x040u
#define MIX_START 0x001u
#define MIX_IDLE 0x004u
#define MIX_FLUSH 0x020u
#define MIX_AUTO_RESTART 0x080u
#define MIX_PRIMARY_ONLY 0x001u

static inline int mix_read_text(const char *path, char *text, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int rc = fgets(text, (int)size, f) ? 0 : -1;
    fclose(f);
    if (!rc) text[strcspn(text, "\r\n")] = 0;
    return rc;
}

static inline int mix_parse_integer(const char *text, unsigned long long *value) {
    char *end;
    errno = 0;
    *value = strtoull(text, &end, 0);
    return !errno && end != text && !*end && *text != '-';
}

/* Match the explicit DT name AND address; never assume a /dev/uioN number. */
static inline int video_mixer_find_uio(const char *sysfs, const char *devices,
                                      char *result, size_t capacity) {
    DIR *directory = opendir(sysfs);
    if (!directory) return -1;
    struct dirent *entry;
    unsigned found = 0;
    char chosen[512] = {0};
    while ((entry = readdir(directory))) {
        const char *suffix = entry->d_name;
        if (strncmp(suffix, "uio", 3) || !suffix[3]) continue;
        int numeric = 1;
        for (suffix += 3; *suffix; ++suffix)
            if (*suffix < '0' || *suffix > '9') numeric = 0;
        if (!numeric) continue;
        char path[512], name[128], text[80];
        unsigned long long address, size;
        int n = snprintf(path, sizeof(path), "%s/%s/name", sysfs, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path) ||
            mix_read_text(path, name, sizeof(name)) || strcmp(name, VIDEO_MIXER_UIO_NAME)) continue;
        n = snprintf(path, sizeof(path), "%s/%s/maps/map0/addr", sysfs, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path) || mix_read_text(path, text, sizeof(text)) ||
            !mix_parse_integer(text, &address) || address != VIDEO_MIXER_BASE) continue;
        n = snprintf(path, sizeof(path), "%s/%s/maps/map0/size", sysfs, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path) || mix_read_text(path, text, sizeof(text)) ||
            !mix_parse_integer(text, &size) || size < VIDEO_MIXER_MAP_SIZE) continue;
        n = snprintf(chosen, sizeof(chosen), "%s/%s", devices, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(chosen)) continue;
        ++found;
    }
    closedir(directory);
    if (found != 1) { errno = found ? EEXIST : ENODEV; return -1; }
    if (strlen(chosen) >= capacity) { errno = ENAMETOOLONG; return -1; }
    strcpy(result, chosen);
    return 0;
}

struct video_mixer_io {
    void *context;
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    void (*pause_us)(void *, unsigned);
};

/* Arm the downstream core before MM2S supplies pixels. No DDR UI reads:
 * bit0 = streaming main layer; all overlay/logo bits remain zero.
 * The caller must NOT infer frame progress from this configuration readback. */
static inline int video_mixer_configure_primary(const struct video_mixer_io *io,
                                                unsigned width, unsigned height) {
    if (!width || width > 1280 || !height || height > 720) return -4;
    uint32_t control = io->read(io->context, MIX_CTRL);
    if (control & MIX_FLUSH) return -5; /* flush requires a HARD reset */

    /* On an explicit service restart, let a current frame drain while the old
     * readers still run. Never resize an active frame or flush AXI transactions. */
    io->write(io->context, MIX_CTRL, 0);
    unsigned poll;
    for (poll = 0; poll < 200; ++poll) {
        if (io->read(io->context, MIX_CTRL) & MIX_IDLE) break;
        io->pause_us(io->context, 1000);
    }
    if (poll == 200) return -1;

    io->write(io->context, MIX_GIE, 0);
    io->write(io->context, MIX_IER, 0);
    /* ISR is toggle-on-write, not W1C: clear only bits actually observed set. */
    uint32_t pending = io->read(io->context, MIX_ISR) & 3u;
    if (pending) io->write(io->context, MIX_ISR, pending);
    io->write(io->context, MIX_WIDTH, width);
    io->write(io->context, MIX_HEIGHT, height);
    io->write(io->context, MIX_BACKGROUND_R, 0);
    io->write(io->context, MIX_BACKGROUND_G, 0);
    io->write(io->context, MIX_BACKGROUND_B, 0);
    io->write(io->context, MIX_LAYERS, MIX_PRIMARY_ONLY);

    if (io->read(io->context, MIX_WIDTH) != width ||
        io->read(io->context, MIX_HEIGHT) != height ||
        io->read(io->context, MIX_FORMAT) != 0 || /* compiled RGB */
        io->read(io->context, MIX_LAYERS) != MIX_PRIMARY_ONLY) return -2;

    io->write(io->context, MIX_CTRL, MIX_AUTO_RESTART | MIX_START);
    control = io->read(io->context, MIX_CTRL);
    if (!(control & MIX_AUTO_RESTART) || (control & MIX_FLUSH)) {
        io->write(io->context, MIX_CTRL, 0);
        return -3;
    }
    return 0;
}
#endif

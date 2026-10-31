#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "video_mixer_ui.h"

static volatile sig_atomic_t stopped;
static void stop_signal(int value) { (void)value; stopped = 1; }
static uint32_t read_register(void *p, unsigned off) {
    uint32_t value = ((volatile uint32_t *)p)[off / 4];
    __sync_synchronize(); return value;
}
static void write_register(void *p, unsigned off, uint32_t value) {
    ((volatile uint32_t *)p)[off / 4] = value; __sync_synchronize();
}
static void pause_io(void *p, unsigned us) { (void)p; usleep(us); }
static void publish_pixels(void) {
    __sync_synchronize();
#if defined(__arm__) || defined(__aarch64__)
    /* Complete writes on the uncached/write-combined /dev/mem mapping before
     * enabling a non-coherent HP0 DMA reader. msync is not a DMA cache API. */
    __asm__ __volatile__("dsb sy" ::: "memory");
#endif
}
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | p[3];
}
static int read_property(const char *path, void *data, size_t capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t size = read(fd, data, capacity); close(fd); return (int)size;
}
static int check_reserved(void) {
    /* Exact current DT contract, not merely an arbitrary /dev/mem address. */
    const char *root = "/sys/firmware/devicetree/base/reserved-memory/";
    char path[256]; unsigned char reg[16], cells[8];
    snprintf(path, sizeof(path), "%s#address-cells", root);
    if (read_property(path, cells, sizeof(cells)) != 4 || be32(cells) != 1) return -1;
    snprintf(path, sizeof(path), "%s#size-cells", root);
    if (read_property(path, cells, sizeof(cells)) != 4 || be32(cells) != 1) return -1;
    snprintf(path, sizeof(path), "%svideo-framebuffer@1000000/reg", root);
    if (read_property(path, reg, sizeof(reg)) != 8 ||
        be32(reg) != 0x01000000u || be32(reg + 4) != 0x03000000u) return -1;
    snprintf(path, sizeof(path), "%svideo-framebuffer@1000000/no-map", root);
    if (read_property(path, reg, sizeof(reg)) != 0) return -1;
    return mix_ui_memory_safe(MIX_UI_BASE, MIX_UI_BYTES) ? 0 : -1;
}
static int pipeline_running(void) {
    char data[2048];
    int n = read_property("/run/video-pipeline.state", data, sizeof(data) - 1);
    if (n < 0) return 0;
    data[n] = 0;
    return strstr(data, "video: running rc=0") != NULL &&
        strstr(data, "mixer: primary-only armed") != NULL;
}
static void dump(const struct video_mixer_io *io, const char *label) {
    printf("%s ctrl=%08x layers=%08x geometry=%ux%u\n", label,
        io->read(io->context, MIX_CTRL), io->read(io->context, MIX_LAYERS),
        io->read(io->context, MIX_WIDTH), io->read(io->context, MIX_HEIGHT));
    for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i)
        printf("REG %03x=%08x\n", mix_ui_offsets[i], io->read(io->context, mix_ui_offsets[i]));
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts(MIX_UI_VERSION); return 0; }
    unsigned seconds = 0;
    if (argc == 3 && !strcmp(argv[1], "--seconds")) {
        unsigned long long n;
        if (mix_parse_integer(argv[2], &n) && n >= 1 && n <= 300) seconds = (unsigned)n;
    }
    int check = argc == 2 && !strcmp(argv[1], "--check");
    if (!seconds && !check) {
        fprintf(stderr, "Usage: %s --version | --check | --seconds 1..300\n", argv[0]); return 2;
    }
    char uio[512];
    if (video_mixer_find_uio("/sys/class/uio", "/dev", uio, sizeof(uio)) ||
        check_reserved() || !pipeline_running()) {
        fprintf(stderr, "Refusing: Mixer identity, running pipeline or reserved DDR contract mismatch\n"); return 3;
    }
    printf("%s uio=%s UI DDR=%08x bytes=%u stride=%u at=%u,%u size=%ux%u\n",
        MIX_UI_VERSION, uio, MIX_UI_BASE, MIX_UI_BYTES, MIX_UI_STRIDE,
        MIX_UI_X, MIX_UI_Y, MIX_UI_WIDTH, MIX_UI_HEIGHT);
    if (check) { puts("PASS_SYSFS_DT_STATE_CHECK no register writes"); return 0; }
    int rc = 4, lock = -1, fd = -1, mem = -1, dirty = 0;
    void *regs = MAP_FAILED, *buffer = MAP_FAILED;
    uint32_t saved[MIX_UI_REG_COUNT];
    struct video_mixer_io io = {NULL, read_register, write_register, pause_io};
    lock = open("/run/video-mixer-ui-test.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) { perror("test lock"); goto finish; }
    fd = open(uio, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("UIO open"); goto finish; }
    regs = mmap(NULL, VIDEO_MIXER_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) { perror("UIO mmap"); goto finish; }
    io.context = regs;
    uint32_t control = read_register(regs, MIX_CTRL);
    if (!(control & MIX_AUTO_RESTART) || (control & MIX_FLUSH) ||
        read_register(regs, MIX_LAYERS) != 1 || read_register(regs, MIX_WIDTH) != 1280 ||
        read_register(regs, MIX_HEIGHT) != 720 || read_register(regs, MIX_FORMAT) != 0) {
        fprintf(stderr, "Refusing: not the expected running primary-only RGB mixer\n"); goto finish;
    }
    for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i) saved[i] = read_register(regs, mix_ui_offsets[i]);
    dump(&io, "BEFORE");
    mem = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (mem < 0) { perror("reserved DDR open"); goto finish; }
    /* O_SYNC ARM /dev/mem mapping + no-map reserved DDR, never malloc's VA.
     * Buffer is static and immutable for the whole display interval. No cache
     * ownership transfer, tearing-prone redraw or unsupported DMA API claim. */
    buffer = mmap(NULL, MIX_UI_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, mem, MIX_UI_BASE);
    if (buffer == MAP_FAILED) { perror("reserved DDR mmap"); goto finish; }
    volatile uint32_t *pixels = buffer;
    for (unsigned y = 0; y < MIX_UI_HEIGHT; ++y)
        for (unsigned x = 0; x < MIX_UI_WIDTH; ++x)
            pixels[y * MIX_UI_WIDTH + x] = mix_ui_pixel(x, y);
    publish_pixels();
    for (unsigned y = 0; y < MIX_UI_HEIGHT; ++y)
        for (unsigned x = 0; x < MIX_UI_WIDTH; ++x)
            if (pixels[y * MIX_UI_WIDTH + x] != mix_ui_pixel(x, y)) {
                fprintf(stderr, "DDR readback mismatch\n"); goto finish;
            }
    publish_pixels();
    struct sigaction action = {0};
    action.sa_handler = stop_signal; sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL); sigaction(SIGHUP, &action, NULL);
    if (mix_ui_drain(&io)) { fprintf(stderr, "Primary drain failed, no overlay enabled\n"); goto finish; }
    dirty = 1;
    if (stopped || mix_ui_enable_idle(&io)) { fprintf(stderr, "Overlay setup interrupted/failed\n"); goto finish; }
    dump(&io, "UI_ON");
    printf("UI_TEST_ACTIVE seconds=%u; automatic restoration enabled\n", seconds); fflush(stdout);
    for (unsigned i = 0; i < seconds * 10 && !stopped; ++i) usleep(100000);
    rc = 0;
finish:
    if (dirty) {
        if (mix_ui_drain(&io)) {
            fprintf(stderr, "RESTORE_FAILED: core did not drain; do not reset VDMA blindly\n"); rc = 5;
        } else {
            mix_ui_restore_idle(&io, saved);
            int mismatch = read_register(regs, MIX_LAYERS) != 1;
            for (unsigned i = 0; i < MIX_UI_REG_COUNT; ++i)
                mismatch |= read_register(regs, mix_ui_offsets[i]) != saved[i];
            if (mismatch) { fprintf(stderr, "RESTORE_READBACK_FAILED\n"); rc = 5; }
            else puts("UI_TEST_RESTORED primary-only and original overlay registers");
            dump(&io, "AFTER");
        }
    }
    if (buffer != MAP_FAILED) munmap(buffer, MIX_UI_BYTES);
    if (regs != MAP_FAILED) munmap(regs, VIDEO_MIXER_MAP_SIZE);
    if (mem >= 0) close(mem);
    if (fd >= 0) close(fd);
    if (lock >= 0) close(lock);
    return rc;
}

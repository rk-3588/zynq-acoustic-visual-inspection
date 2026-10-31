/* Host MMIO model exercising the exact production header. */
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define XST_SUCCESS 0U
#define XST_FAILURE 1U
#define BOARD_USB_PHY_UNIT_TEST
static u32 Xil_In32(u32 address);
static void Xil_Out32(u32 address, u32 value);
static int usleep(unsigned int duration);
static void xil_printf(const char *format, ...);
#include "board_usb_phy.h"

static u32 mio, clock_reg, data, direction, enable, locked;
static unsigned int sleeps, writes, assert_seen, release_seen, block_clock;
static u32 original_data, original_direction, original_enable;

static u32 Xil_In32(u32 address)
{
    switch (address) {
    case BOARD_MIO8_CTRL: return mio;
    case BOARD_APER_CLK_CTRL: return clock_reg;
    case BOARD_GPIO_DATA0: return data;
    case BOARD_GPIO_DIRM0: return direction;
    case BOARD_GPIO_OEN0: return enable;
    case BOARD_SLCR_LOCKSTA: return locked;
    default: assert(!"unexpected MMIO read"); return 0;
    }
}

static void Xil_Out32(u32 address, u32 value)
{
    ++writes;
    switch (address) {
    case BOARD_SLCR_UNLOCK: assert(value == 0xdf0dU); locked = 0; break;
    case BOARD_SLCR_LOCK: assert(value == 0x767bU); locked = 1; break;
    case BOARD_MIO8_CTRL: assert(!locked); mio = value; break;
    case BOARD_APER_CLK_CTRL:
        assert(!locked); if (!block_clock) clock_reg = value; break;
    case BOARD_GPIO_DIRM0: direction = value; break;
    case BOARD_GPIO_OEN0: enable = value; break;
    case BOARD_GPIO_MASK_LSW0: {
        u32 mask = (~(value >> 16)) & 0xffffU;
        assert(mask == BOARD_GPIO8_MASK);
        if (value == BOARD_GPIO8_WRITE_LOW) { assert_seen++; assert(mio & 1U); }
        else { assert(value == BOARD_GPIO8_WRITE_HI); release_seen++; }
        data = (data & ~mask) | (value & mask);
        break;
    }
    default: assert(!"unexpected MMIO write");
    }
    assert((data & ~BOARD_GPIO8_MASK) == (original_data & ~BOARD_GPIO8_MASK));
    assert((direction & ~BOARD_GPIO8_MASK) == (original_direction & ~BOARD_GPIO8_MASK));
    assert((enable & ~BOARD_GPIO8_MASK) == (original_enable & ~BOARD_GPIO8_MASK));
}

static int usleep(unsigned int duration)
{
    assert((mio & 1U) == 0);
    assert((direction & enable & BOARD_GPIO8_MASK) != 0);
    if (sleeps++ == 0) { assert(duration == 20000U); assert(!(data & BOARD_GPIO8_MASK)); }
    else { assert(sleeps == 2); assert(duration == 700000U); assert(data & BOARD_GPIO8_MASK); }
    return 0;
}

static void xil_printf(const char *format, ...) { (void)format; }

static void init(unsigned int seed, unsigned int is_locked)
{
    mio = 0x600U | (seed & 1U);
    clock_reg = 0x00300404U;
    original_data = data = seed * 1103515245U;
    original_direction = direction = seed * 1664525U;
    original_enable = enable = seed * 22695477U;
    locked = is_locked;
    sleeps = writes = assert_seen = release_seen = block_clock = 0;
}

int main(void)
{
    unsigned int n, l;
    for (l = 0; l < 2; ++l) for (n = 0; n < 1000; ++n) {
        init(n, l);
        assert(BoardUsbPhyReset() == XST_SUCCESS);
        assert(locked == l && mio == 0x600U);
        assert(clock_reg == 0x00700404U);
        assert(sleeps == 2 && assert_seen == 1 && release_seen == 1);
    }
    init(0, 1); mio |= 0x20U;
    assert(BoardUsbPhyReset() == XST_FAILURE && writes == 0);
    init(0, 1); block_clock = 1;
    assert(BoardUsbPhyReset() == XST_FAILURE);
    assert(locked == 1 && assert_seen == 0 && sleeps == 0);
    puts("PASS: 2000 reset cases, unrelated pins/clock bits/lock preserved, safe failure paths");
    return 0;
}

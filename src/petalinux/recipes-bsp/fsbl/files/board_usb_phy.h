/* SPDX-License-Identifier: MIT
 * Zynq-7020 board: USB3320 RESETB is driven through a diode by PS MIO8.
 * Called once by FSBL before handing off to U-Boot, never by running Linux.
 */
#ifndef BOARD_USB_PHY_H
#define BOARD_USB_PHY_H

#ifndef BOARD_USB_PHY_UNIT_TEST
#include "xil_io.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "sleep.h"
#endif

#define BOARD_USB_PHY_VERSION "20261016-usb3320-mio8-v1"
#define BOARD_SLCR_LOCK       0xf8000004U
#define BOARD_SLCR_UNLOCK     0xf8000008U
#define BOARD_SLCR_LOCKSTA    0xf800000cU
#define BOARD_APER_CLK_CTRL   0xf800012cU
#define BOARD_MIO8_CTRL       0xf8000720U
#define BOARD_GPIO_MASK_LSW0  0xe000a000U
#define BOARD_GPIO_DATA0      0xe000a040U
#define BOARD_GPIO_DIRM0      0xe000a204U
#define BOARD_GPIO_OEN0       0xe000a208U
#define BOARD_GPIO8_MASK      0x00000100U
#define BOARD_GPIO_CLK_MASK   0x00400000U
#define BOARD_GPIO8_WRITE_LOW 0xfeff0000U
#define BOARD_GPIO8_WRITE_HI  0xfeff0100U

static u32 BoardUsbPhyReset(void)
{
    u32 mio = Xil_In32(BOARD_MIO8_CTRL);
    u32 was_locked;
    u32 readback_ok;

    /* Preserve I/O type/pulls. Refuse to steal MIO8 from another peripheral. */
    if ((mio & 0x000000feU) != 0U) {
        xil_printf("[USBPHY] MIO8 is not GPIO; reset skipped\r\n");
        return XST_FAILURE;
    }
    was_locked = Xil_In32(BOARD_SLCR_LOCKSTA) & 1U;
    if (was_locked != 0U)
        Xil_Out32(BOARD_SLCR_UNLOCK, 0x0000df0dU);

    /* Enable only GPIO's APB clock, preserving USB/UART/camera clock bits. */
    Xil_Out32(BOARD_APER_CLK_CTRL,
              Xil_In32(BOARD_APER_CLK_CTRL) | BOARD_GPIO_CLK_MASK);
    if ((Xil_In32(BOARD_APER_CLK_CTRL) & BOARD_GPIO_CLK_MASK) == 0U) {
        if (was_locked != 0U)
            Xil_Out32(BOARD_SLCR_LOCK, 0x0000767bU);
        return XST_FAILURE;
    }

    /* Prepare low while tristated, then enable exactly GPIO8. Masked data
     * writes do not modify any other bank0 output latch (including UART pins).
     */
    Xil_Out32(BOARD_MIO8_CTRL, mio | 1U);
    Xil_Out32(BOARD_GPIO_MASK_LSW0, BOARD_GPIO8_WRITE_LOW);
    Xil_Out32(BOARD_GPIO_DIRM0,
              Xil_In32(BOARD_GPIO_DIRM0) | BOARD_GPIO8_MASK);
    Xil_Out32(BOARD_GPIO_OEN0,
              Xil_In32(BOARD_GPIO_OEN0) | BOARD_GPIO8_MASK);
    Xil_Out32(BOARD_MIO8_CTRL, mio & ~1U);
    (void)Xil_In32(BOARD_MIO8_CTRL); /* Complete the MMIO write before delay. */
    if (was_locked != 0U)
        Xil_Out32(BOARD_SLCR_LOCK, 0x0000767bU);

    (void)usleep(20000U);
    /* Release RESETB; leave MIO8 driven high through U-Boot/Linux startup. */
    Xil_Out32(BOARD_GPIO_MASK_LSW0, BOARD_GPIO8_WRITE_HI);
    (void)Xil_In32(BOARD_GPIO_DATA0);
    /* Board RESETB has 10k/10uF RC. Allow >5 nominal time constants. */
    (void)usleep(700000U);

    /* MIO7/8 are output-only: INPUT/sysfs value cannot verify this pin.
     * Verify output latch/configuration, not the nonexistent input receiver.
     */
    readback_ok =
        (Xil_In32(BOARD_MIO8_CTRL) == (mio & ~1U)) &&
        ((Xil_In32(BOARD_GPIO_DATA0) & BOARD_GPIO8_MASK) != 0U) &&
        ((Xil_In32(BOARD_GPIO_DIRM0) & BOARD_GPIO8_MASK) != 0U) &&
        ((Xil_In32(BOARD_GPIO_OEN0) & BOARD_GPIO8_MASK) != 0U);
    if (readback_ok == 0U)
        return XST_FAILURE;

    xil_printf("[USBPHY] " BOARD_USB_PHY_VERSION
               " MIO8 reset done: low=20ms release-wait=700ms\r\n");
    return XST_SUCCESS;
}
#endif

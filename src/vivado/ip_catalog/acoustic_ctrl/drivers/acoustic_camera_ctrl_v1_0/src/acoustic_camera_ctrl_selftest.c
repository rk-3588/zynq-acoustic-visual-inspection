
/***************************** Include Files *******************************/
#include "acoustic_camera_ctrl.h"
#include "xparameters.h"
#include "stdio.h"
#include "xil_io.h"

/************************** Constant Definitions ***************************/
/************************** Function Definitions ***************************/
/**
 *
 * Run a self-test on the driver/device. Note this may be a destructive test if
 * resets of the device are performed.
 *
 * If the hardware system is not built correctly, this function may never
 * return to the caller.
 *
 * @param   baseaddr_p is the base address of the ACOUSTIC_CAMERA_CTRLinstance to be worked on.
 *
 * @return
 *
 *    - XST_SUCCESS   if all self-test code passed
 *    - XST_FAILURE   if any self-test code failed
 *
 * @note    Caching must be turned off for this function to work.
 * @note    Self test may fail if data memory and device are not on the same bus.
 *
 */
XStatus ACOUSTIC_CAMERA_CTRL_Reg_SelfTest(void * baseaddr_p)
{
	u32 baseaddr;

	baseaddr = (u32) baseaddr_p;

	xil_printf("******************************\n\r");
	xil_printf("* User Peripheral Self Test\n\r");
	xil_printf("******************************\n\n\r");

	/* Only CONTROL, THRESHOLD and MODE are writable.  STATUS and the
	 * hotspot registers are hardware inputs and must not be used for a
	 * destructive read/write self-test. */
	xil_printf("Control register test...\n\r");
	ACOUSTIC_CAMERA_CTRL_mWriteReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_CONTROL_OFFSET,
			ACOUSTIC_CAMERA_CTRL_CONTROL_ENABLE);
	if ((ACOUSTIC_CAMERA_CTRL_mReadReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_CONTROL_OFFSET) & 0xffU) !=
			ACOUSTIC_CAMERA_CTRL_CONTROL_ENABLE) {
		xil_printf("Error reading CONTROL register\n\r");
		return XST_FAILURE;
	}
	ACOUSTIC_CAMERA_CTRL_mWriteReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_THRESHOLD_OFFSET, 0x00000100U);
	ACOUSTIC_CAMERA_CTRL_mWriteReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_MODE_OFFSET, 0x00000001U);
	if (ACOUSTIC_CAMERA_CTRL_mReadReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_THRESHOLD_OFFSET) != 0x00000100U ||
		ACOUSTIC_CAMERA_CTRL_mReadReg(baseaddr,
			ACOUSTIC_CAMERA_CTRL_MODE_OFFSET) != 0x00000001U) {
		xil_printf("Error reading THRESHOLD/MODE registers\n\r");
		return XST_FAILURE;
	}
	xil_printf("   - control register read/write passed\n\n\r");

	return XST_SUCCESS;
}

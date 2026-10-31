
#ifndef ACOUSTIC_CAMERA_CTRL_H
#define ACOUSTIC_CAMERA_CTRL_H


/****************** Include Files ********************/
#include "xil_types.h"
#include "xstatus.h"

#define ACOUSTIC_CAMERA_CTRL_CONTROL_OFFSET       0x00U
#define ACOUSTIC_CAMERA_CTRL_STATUS_OFFSET        0x04U
#define ACOUSTIC_CAMERA_CTRL_HOTSPOT_X_OFFSET     0x08U
#define ACOUSTIC_CAMERA_CTRL_HOTSPOT_Y_OFFSET     0x0cU
#define ACOUSTIC_CAMERA_CTRL_HOTSPOT_VALUE_OFFSET 0x10U
#define ACOUSTIC_CAMERA_CTRL_CONFIDENCE_OFFSET    0x14U
#define ACOUSTIC_CAMERA_CTRL_THRESHOLD_OFFSET     0x18U
#define ACOUSTIC_CAMERA_CTRL_MODE_OFFSET          0x1cU

#define ACOUSTIC_CAMERA_CTRL_CONTROL_ENABLE       (1U << 0)
#define ACOUSTIC_CAMERA_CTRL_CONTROL_ACOUSTIC_RST (1U << 1)
#define ACOUSTIC_CAMERA_CTRL_CONTROL_VIDEO_RST    (1U << 2)
#define ACOUSTIC_CAMERA_CTRL_CONTROL_CAPTURE      (1U << 3)
#define ACOUSTIC_CAMERA_CTRL_CONTROL_IRQ_ENABLE   (1U << 8)


/**************************** Type Definitions *****************************/
/**
 *
 * Write a value to a ACOUSTIC_CAMERA_CTRL register. A 32 bit write is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is written.
 *
 * @param   BaseAddress is the base address of the ACOUSTIC_CAMERA_CTRLdevice.
 * @param   RegOffset is the register offset from the base to write to.
 * @param   Data is the data written to the register.
 *
 * @return  None.
 *
 * @note
 * C-style signature:
 * 	void ACOUSTIC_CAMERA_CTRL_mWriteReg(u32 BaseAddress, unsigned RegOffset, u32 Data)
 *
 */
#define ACOUSTIC_CAMERA_CTRL_mWriteReg(BaseAddress, RegOffset, Data) \
  	Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))

/**
 *
 * Read a value from a ACOUSTIC_CAMERA_CTRL register. A 32 bit read is performed.
 * If the component is implemented in a smaller width, only the least
 * significant data is read from the register. The most significant data
 * will be read as 0.
 *
 * @param   BaseAddress is the base address of the ACOUSTIC_CAMERA_CTRL device.
 * @param   RegOffset is the register offset from the base to write to.
 *
 * @return  Data is the data from the register.
 *
 * @note
 * C-style signature:
 * 	u32 ACOUSTIC_CAMERA_CTRL_mReadReg(u32 BaseAddress, unsigned RegOffset)
 *
 */
#define ACOUSTIC_CAMERA_CTRL_mReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))

/************************** Function Prototypes ****************************/
/**
 *
 * Run a self-test on the driver/device. Note this may be a destructive test if
 * resets of the device are performed.
 *
 * If the hardware system is not built correctly, this function may never
 * return to the caller.
 *
 * @param   baseaddr_p is the base address of the ACOUSTIC_CAMERA_CTRL instance to be worked on.
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
XStatus ACOUSTIC_CAMERA_CTRL_Reg_SelfTest(void * baseaddr_p);

#endif // ACOUSTIC_CAMERA_CTRL_H

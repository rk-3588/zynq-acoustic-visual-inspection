#ifndef VIDEO_PREPROCESS_REGS_H
#define VIDEO_PREPROCESS_REGS_H
/* No physical base address: verify the actual BD/XSA and UIO mapping first. */
#define VPP_ID_VALUE       0x56505031u
#define VPP_PROTOCOL_VERSION 3u
#define VPP_CAPABILITIES_VALUE 31u
#define VPP_MODE_MASK 7u
#define VPP_REG_ID         0x00u
#define VPP_REG_MODE       0x04u
#define VPP_REG_THRESHOLD  0x08u
#define VPP_REG_COMMIT     0x0cu
#define VPP_REG_ACTIVE     0x10u
#define VPP_REG_PENDING    0x14u
#define VPP_REG_STATUS     0x18u
#define VPP_REG_FRAME_SEQ  0x1cu
#define VPP_REG_PIXELS     0x20u
#define VPP_REG_VERSION    0x24u
#define VPP_REG_GEOMETRY   0x28u
#define VPP_REG_APPLY_SEQ  0x2cu
#define VPP_REG_ROI_XY0    0x30u /* low16=x0, high16=y0 */
#define VPP_REG_ROI_XY1    0x34u /* low16=x1 exclusive, high16=y1 exclusive */
#define VPP_REG_ROI_ENABLE 0x38u
#define VPP_REG_ACTIVE_ROI_XY0 0x3cu
#define VPP_REG_ACTIVE_ROI_XY1 0x40u
#define VPP_REG_ACTIVE_ROI_ENABLE 0x44u
#define VPP_REG_PENDING_ROI_XY0 0x48u
#define VPP_REG_PENDING_ROI_XY1 0x4cu
#define VPP_REG_PENDING_ROI_ENABLE 0x50u
#define VPP_REG_CAPABILITIES 0x54u
#define VPP_REG_STATS_PUBLISHED 0x58u /* bit0 available, bit1 latest frame valid */
#define VPP_REG_STATS_SNAPSHOT 0x60u /* write 1 freezes latest published frame */
#define VPP_REG_STATS_STATUS 0x64u
#define VPP_REG_STATS_FRAME 0x68u
#define VPP_REG_STATS_APPLY_SEQ 0x6cu
#define VPP_REG_STATS_AREA 0x70u
#define VPP_REG_STATS_BRIGHT 0x74u /* original grayscale >= committed threshold */
#define VPP_REG_STATS_TOTAL 0x78u
#define VPP_REG_STATS_CONFIG 0x7cu /* mode[2:0], threshold[15:8], ROI enabled[16] */
#define VPP_REG_STATS_ROI_XY0 0x80u
#define VPP_REG_STATS_ROI_XY1 0x84u
#define VPP_REG_BRIGHTNESS 0x88u /* signed int32, -255..255 */
#define VPP_REG_GAIN_SHIFT 0x8cu /* signed int32, -2..2 */
#define VPP_REG_ACTIVE_BRIGHTNESS 0x90u
#define VPP_REG_ACTIVE_GAIN_SHIFT 0x94u
#define VPP_REG_PENDING_BRIGHTNESS 0x98u
#define VPP_REG_PENDING_GAIN_SHIFT 0x9cu
#define VPP_REG_STATS_BRIGHTNESS 0xa0u
#define VPP_REG_STATS_GAIN_SHIFT 0xa4u
#define VPP_STATS_AVAILABLE 1u
#define VPP_STATS_VALID 2u
#define VPP_STATS_ROI_ENABLED 4u
#define VPP_STATS_MALFORMED 8u
#define VPP_MODE_BYPASS    0u
#define VPP_MODE_GRAY      1u
#define VPP_MODE_BINARY    2u
#define VPP_MODE_BRIGHTNESS 3u
#define VPP_MODE_INVERT 4u
#define VPP_MODE_GAIN 5u
#define VPP_STATUS_PENDING 1u
#define VPP_STATUS_SOF     2u
#define VPP_STATUS_PIPE    4u
#endif

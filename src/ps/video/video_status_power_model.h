#ifndef VIDEO_STATUS_POWER_MODEL_H
#define VIDEO_STATUS_POWER_MODEL_H
#include <stdint.h>
enum panel_power_phase {PANEL_POWER_IDLE,PANEL_POWER_CONFIRM,PANEL_POWER_PENDING,
    PANEL_POWER_ACCEPTED,PANEL_POWER_FAILED};
struct panel_power_view {
    unsigned phase,revision;
    int request,error,slow;
    uint64_t confirm_until;
};
static inline const char *panel_power_name(unsigned phase) {
    return phase==PANEL_POWER_CONFIRM?"confirm":phase==PANEL_POWER_PENDING?"pending":
        phase==PANEL_POWER_ACCEPTED?"accepted":phase==PANEL_POWER_FAILED?"failed":"idle";
}
#endif

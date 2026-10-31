#ifndef VIDEO_STATUS_IMAGE_MODEL_H
#define VIDEO_STATUS_IMAGE_MODEL_H
#include <stdint.h>
#include <stddef.h>
static inline int panel_image_bound(int value,int low,int high){return value<low?low:value>high?high:value;}
enum panel_image_command {PIMG_STATUS=1,PIMG_MODE,PIMG_FULL,PIMG_ROI,PIMG_PARAMS,PIMG_ORIGINAL,PIMG_DEFAULTS,
    PIMG_FOLLOW,PIMG_FOLLOW_MODE,PIMG_FOLLOW_PARAMS};
struct panel_image_config {unsigned mode,threshold,x,y,w,h;int brightness,gain,roi;};
struct panel_image_view {
    struct panel_image_config active;
    int ready,known,busy,pending,error,popup,region_popup,settings,editing,dragging,preview_valid;
    int draft_threshold,draft_brightness,draft_gain;
    int slider_dragging,slider_value;
    unsigned x,y,w,h,request,request_mode,revision;
    uint64_t sampled_ms;
    int follow,follow_have,follow_visible,follow_hold,follow_fault,follow_applied;
    int follow_cx,follow_cy;
    unsigned follow_mode,follow_w,follow_h,draft_follow_w,draft_follow_h;
    unsigned follow_x,follow_y;
    uint64_t follow_last_good_ms,follow_last_step_ms,follow_next_ms;
};
static inline unsigned panel_image_selected_mode(const struct panel_image_view *i) {
    return i->follow?i->follow_mode:i->active.mode;
}
static inline int panel_region_selected(const struct panel_image_view *i) {
    return i->follow?2:!i->known?-1:i->active.roi?1:0;
}
static inline const char *panel_region_name(int mode) {
    static const char *names[]={"全屏","手动 ROI","声源跟随"};
    return mode>=0&&mode<3?names[mode]:"未连接";
}
static inline unsigned panel_follow_width(const struct panel_image_view *i){return i->follow_w?i->follow_w:320;}
static inline unsigned panel_follow_height(const struct panel_image_view *i){return i->follow_h?i->follow_h:240;}
static inline const char *panel_follow_label(const struct panel_image_view *i) {
    return i->follow_fault?"控制失败，请重新选择":i->follow_visible?(i->follow_hold?"暂时保持":"跟随中"):
        "等待有效稳定定位";
}
static inline const char *panel_image_mode_name(unsigned mode) {
    static const char *names[]={"原图","灰度","二值化","亮度偏移","反相","移位增益"};
    return mode<6?names[mode]:"未知";
}
static inline const char *panel_image_mode_cli(unsigned mode) {
    static const char *names[]={"bypass","gray","binary","brightness","invert","gain"};
    return mode<6?names[mode]:NULL;
}
static inline int panel_image_available(const struct panel_image_view *i) {
    return i->ready&&i->known&&!i->busy&&!i->pending&&!i->request;
}
/* A periodic read-only refresh is busy, not a hardware/control fault. Keep
 * command serialization in available(), but do not paint polling as an alarm. */
static inline int panel_image_warning(const struct panel_image_view *i) {
    return !i->ready||!i->known||i->error||i->follow_fault;
}
#endif

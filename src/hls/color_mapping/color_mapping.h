#include <ap_int.h>

void color_mapping(
    ap_uint<24> val,
    ap_uint<24> *jet_rgb,
    float red_start,
    float orange_start,
    float green_start,
    float cyan_start,
    float blue_start,
    float thres_low);

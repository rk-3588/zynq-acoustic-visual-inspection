#include "color_mapping.h"
#include <cstdio>

int main() {
    ap_uint<24> val = 1000;
    ap_uint<24> color;

    color_mapping(
        val,
        &color,
        24.0f,
        21.0f,
        18.0f,
        15.0f,
        12.0f,
        6.0f);

    std::printf("color = 0x%06x\n", (unsigned)color);
    return 0;

}

// Horizontal display mirror: read column 63-col, row unchanged.
// 1280x720 video -> 64x36 acoustic tiles (20x20 display pixels per tile).
// floor(p/20) == (p*3277)>>16 for every 11-bit unsigned input.
module acoustic_pixel_mapping (
    input wire [10:0] xpos,
    input wire [10:0] ypos,
    input wire ap_rst,
    output wire [13:0] index
);
    wire [23:0] xp = xpos * 13'd3277;
    wire [23:0] yp = ypos * 13'd3277;
    wire [5:0] col = xp[21:16];
    wire [5:0] row = yp[21:16];
    assign index = (ap_rst || xpos >= 1280 || ypos >= 720)
                 ? 14'd0 : {2'b0, row, ~col};
endmodule

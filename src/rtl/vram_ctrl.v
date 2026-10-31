/*vram管理模块*/
module vram_ctrl #(
    // 输入有效相对于rx_done_edge置高的延迟
    parameter reg [13:0] LA1 = 14'd47,  //+19
    parameter reg [13:0] LA2 = 14'd3,
    parameter integer MAP_POINTS = 2304
) (
    // 系统时钟
    input         Aclk,
    // 复位信号
    input         rst,
    // 当前周期相对于最近一次传感器数据稳定的时间
    input         rx_done_edge,
    // 当前周期完成计算的结果
    input  [54:0] indata,
    // 显示模块时钟
    input         clk_vdma,
    // 显示模块读取像素的地址
    input  [13:0] addr_vdma,
    // 提供给显示模块的数据
    output [63:0] data_vdma,
    
    output reg        sample_valid,
    output reg        sample_last,
    output reg [13:0] sample_index,
    output reg [23:0] sample_value
);
    reg rx_done_edge1;
    always @(posedge Aclk) rx_done_edge1 <= rx_done_edge;
    reg scan_active;
    always @(posedge Aclk or posedge rst)
        if (rst) scan_active <= 1'b0;
        else if (rx_done_edge1) scan_active <= 1'b1;

    // 当前周期相对于最近一次传感器数据稳定的时间，now_time在每组声信号来临时置0，每个时钟周期加1，按当前时钟，最大9599
    reg [13:0] now_time;
    always @(posedge Aclk or posedge rst) begin
        if (rst) now_time <= 14'b0;
        else if (rx_done_edge1) now_time <= 14'b0;
        else now_time <= now_time + 1;
    end

    // 对indat打拍，优化时序
    reg [54:0] indata1;
    always @(posedge Aclk) indata1 <= indata;
    // 读地址
    reg [13:0] addr_read;
    always @(posedge Aclk or posedge rst) begin
        if (rst) addr_read <= 0;
        else if (now_time == LA1 - 3) addr_read <= 0;
        else if (addr_read == MAP_POINTS-1) addr_read <= addr_read;
        else addr_read <= addr_read + 1;
    end
    // 读数据
    wire [63:0] data_read;
    // 写地址
    reg  [13:0] addr_write;
    always @(posedge Aclk or posedge rst) begin
        if (rst) addr_write <= 0;
        else if (now_time == LA1 + LA2 - 1) addr_write <= 0;
        else if (addr_write == MAP_POINTS-1) addr_write <= addr_write;
        else addr_write <= addr_write + 1;
    end
    // 写使能
    reg wea;
    always @(posedge Aclk or posedge rst) begin
        if (rst) wea <= 1'b0;
        else if (scan_active && now_time == LA1 + LA2 - 1) wea <= 1'b1;
        else if (now_time == LA1 + LA2 + MAP_POINTS-1) wea <= 1'b0;
        else wea <= wea;
    end
    // 写数据
    wire [63:0] tmp;
    reg  [63:0] data_write;
    vram_add_hls u_vram_add_hls (
        .ap_rst   (rst),
        .ap_clk   (Aclk),
        .vram_read(data_read),
        .indat    (indata1),
        .ap_return(tmp)
    );
    always @(posedge Aclk) begin
        data_write <= tmp;
    end
    // 实例化vram，用于计算
    blk_mem_gen_0 vram_64x9216_calculate (
        .clka (Aclk),
        .wea  (wea),
        .addra(addr_write),
        .dina (data_write),
        .clkb (Aclk),
        .addrb(addr_read),
        .doutb(data_read)
    );
    // 复制一块vram，用于vdma读取数据
    vram_64x9216 vram_64x9216_vbma (
        .clka (Aclk),
        .wea  (wea),
        .addra(addr_write),
        .dina (data_write),
        .clkb (clk_vdma),
        .addrb(addr_vdma),
        .doutb(data_vdma)
    );
    
    always @(posedge Aclk or posedge rst) begin
    if (rst) begin
        sample_valid <= 1'b0;
        sample_last  <= 1'b0;
        sample_index <= 14'd0;
        sample_value <= 24'd0;
    end else begin
        sample_valid <= wea;
        sample_last  <= wea && (addr_write == MAP_POINTS-1);
        sample_index <= addr_write;
        sample_value <= data_write[51:28];
    end
end
endmodule

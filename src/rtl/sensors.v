/*为传感器提供时钟和复位信号，将传感器串行数据并行化*/
module sensors #(
    parameter reg [4:0] WD  = 24,
    parameter reg [4:0] NUM = 16,
    parameter integer ACTIVE_MICS = 16
) (
    input           Aclk,       // 系统时钟，用于捕获输出有效上升沿
    input           mclk,       // 传感器用时钟
    input           rst,        // 复位信号
    input [NUM-1:0] aud_adcdat, // 麦克风输出串行数据

    output                  aud_bclk,      // 麦克风通信时钟，根据data sheet，频率是采样率的64倍
    output                  aud_lrc,       // I2S WS：低电平为左声道（ICS-43434 LR=0），高电平为右声道。
    output reg              rx_done_edge,  // 输出有效标识位
    output reg [WD*NUM-1:0] adc_data       // 不考虑延迟的声音信号
);
    // 实例化分频模块，生成采样时钟和信道时钟
    divider u_divider (
        .clk_in (mclk),
        .sys_rst(rst),
        .clk_640(aud_lrc),
        .clk_10 (aud_bclk)
    );

    // LR接地的ICS-43434在WS低电平输出：检测下降沿，仅读取左声道。
    // 边沿所在SCK上升沿仅将rx_cnt清零，下一上升沿才采集MSB，保留I2S的一位延迟。
    reg  aud_lrc_d0;
    wire lrc_edge;
    always @(posedge aud_bclk or posedge rst)
        if (rst) aud_lrc_d0 <= 1'b0;
        else aud_lrc_d0 <= aud_lrc;
    assign lrc_edge = !aud_lrc && aud_lrc_d0;
    reg left_seen;
    always @(posedge aud_bclk or posedge rst)
        if (rst) left_seen <= 1'b0;
        else if (lrc_edge) left_seen <= 1'b1;

    // 对当前采集的样本数计数
    reg [4:0] rx_cnt;
    always @(posedge aud_bclk or posedge rst) begin
        if (rst) rx_cnt <= WD;
        else if (lrc_edge) rx_cnt <= 0;
        else if (rx_cnt == WD) rx_cnt <= rx_cnt;
        else rx_cnt <= rx_cnt + 1;
    end

    // 遍历传感器进行采样
    genvar i;
    generate
        for (i = 0; i < NUM; i = i + 1) begin : gen_sensor
          if (i < ACTIVE_MICS) begin : active
            // 将串行数据并行化
            always @(posedge aud_bclk or posedge rst) begin
                if (rst) adc_data[i*WD+:WD] <= 0;
                else if (left_seen && rx_cnt < WD) adc_data[i*WD+WD-1-rx_cnt] <= aud_adcdat[i];
                else adc_data[i*WD+:WD] <= adc_data[i*WD+:WD];
            end
          end else begin : masked
            // Preserve the 16-lane interface, never ingest floating outer inputs.
            always @(posedge aud_bclk or posedge rst)
                if (rst) adc_data[i*WD+:WD] <= {WD{1'b0}};
                else adc_data[i*WD+:WD] <= {WD{1'b0}};
          end
        end
    endgenerate

    // 判断当前数据是否有效
    reg rx_done;
    (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "NO" *) reg rx_done1, rx_done2;
    reg rx_done3;
    always @(posedge aud_bclk or posedge rst) begin
        if (rst) rx_done <= 1'b0;
        else if (left_seen && rx_cnt == WD) rx_done <= 1'b1;
        else rx_done <= 1'b0;
    end
    always @(posedge Aclk or posedge rst) begin
      if (rst) begin
        rx_done1 <= 0;
        rx_done2 <= 0;
        rx_done3 <= 0;
        rx_done_edge <= 0;
      end else begin
        rx_done1     <= rx_done;
        rx_done2     <= rx_done1;
        rx_done3     <= rx_done2;
        rx_done_edge <= rx_done2 && !rx_done3;
      end
    end
endmodule

/*对系统时钟分频，产生传感器所需的信号*/
module divider (
    //系统时钟
    input  wire clk_in,
    //全局复位
    input  wire sys_rst,
    //对系统时钟640分频后的信号
    output reg  clk_640,
    //对系统时钟10分频后的信号
    output reg  clk_10
);
    reg [8:0] cnt_640;  //用于640分频计数的寄存器
    //cnt_640:计数器从0到319循环计数
    always @(posedge clk_in or posedge sys_rst)
        if (sys_rst) cnt_640 <= 9'b0;
        else if (cnt_640 == 9'd319) cnt_640 <= 9'b0;
        else cnt_640 <= cnt_640 + 1'b1;
    //clk_640:640分频50%占空比输出
    always @(posedge clk_in or posedge sys_rst)
        if (sys_rst) clk_640 <= 1'b0;
        else if (cnt_640 == 9'd319) clk_640 <= !clk_640;

    reg [2:0] cnt_10;  //用于10分频计数的寄存器
    //cnt_10:计数器从0到4循环计数
    always @(posedge clk_in or posedge sys_rst)
        if (sys_rst) cnt_10 <= 2'b0;
        else if (cnt_10 == 3'd4) cnt_10 <= 2'b0;
        else cnt_10 <= cnt_10 + 1'b1;
    //cnt_10:10分频50%占空比输出
    always @(posedge clk_in or posedge sys_rst)
        if (sys_rst) clk_10 <= 1'b0;
        else if (cnt_10 == 3'd4) clk_10 <= !clk_10;
endmodule

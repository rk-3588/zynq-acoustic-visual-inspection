`timescale 1ns/1ps
// Independent I2S transmitter -> unmodified sensors -> HPF -> sampled consumer.
module tb_sensor_hpf_chain;
    reg clk=0,mclk=0,rst=1;
    always #2.381 clk=~clk;
    always #16.666667 mclk=~mclk;
    wire bclk,ws,raw_valid,filtered_valid,ready,overrun;
    wire [383:0] raw_data,filtered_data;
    reg [15:0] driver=16'hzzzz;
    tri [15:0] sd;
    assign sd=driver;
    for(genvar g=0;g<16;g=g+1) begin : bias
        pulldown(sd[g]);
    end
    sensors sensor(.Aclk(clk),.mclk(mclk),.rst(rst),.aud_adcdat(sd),
        .aud_bclk(bclk),.aud_lrc(ws),.rx_done_edge(raw_valid),.adc_data(raw_data));
    acoustic_hpf_200 filter(.Aclk(clk),.rst(rst),.sample_in(raw_data),
        .sample_valid_in(raw_valid),.mode_axi(32'b0),.sample_out(filtered_data),
        .sample_valid_out(filtered_valid),.sample_ready(ready),.input_overrun(overrun));
    reg previous_ws=0;
    integer slot=0,frame=0,epoch=0,checked=0,cycle=0,accept_cycle=0;
    reg [23:0] input_words[0:15];
    reg [383:0] expected_output=0;
    reg pending=0,initialized=0;
    longint signed dc[0:15];
    reg [15:0] next_sd;
    function automatic [23:0] word_for(input integer lane,no,era);
        case ((lane+no)%8)
            0: word_for=24'h000001;
            1: word_for=24'hffffff;
            2: word_for=24'h7fffff;
            3: word_for=24'h800000;
            default: word_for=24'h8139af ^ lane*24'h791e ^ no*24'hde53 ^ era*24'h20d9;
        endcase
    endfunction
    function automatic longint signed round_away(input longint signed value,input integer bits);
        if(value>=0) round_away=(value+(64'sd1<<(bits-1)))>>>bits;
        else round_away=-(((-value)+(64'sd1<<(bits-1)))>>>bits);
    endfunction
    always @(negedge bclk) begin
        #0.001;
        if(rst) begin previous_ws=ws;slot=0;frame=0;driver=16'hzzzz;end
        else begin
            if(ws!=previous_ws) begin
                previous_ws=ws;slot=0;
                if(!ws) begin
                    frame=frame+1;
                    for(integer c=0;c<16;c=c+1) input_words[c]=word_for(c,frame,epoch);
                end
            end else slot=slot+1;
            next_sd=16'hzzzz;
            if(!ws && frame>0 && slot>=1 && slot<=24)
                for(integer c=0;c<16;c=c+1) next_sd[c]=input_words[c][24-slot];
            driver <= #50 next_sd;
        end
    end
    always @(posedge clk) begin
        cycle=cycle+1;
        if(rst) begin
            initialized=0;pending=0;
            for(integer c=0;c<16;c=c+1) dc[c]=0;
        end else if(raw_valid) begin
            if(!ready || pending) $fatal(1,"FAIL: real sample interval overlaps HPF");
            accept_cycle=cycle;pending=1;
            for(integer c=0;c<16;c=c+1) begin
                longint signed x,u,delta,y,rounded;
                if(raw_data[24*c+:24] !== input_words[c]) $fatal(1,"FAIL: independent I2S input mismatch");
                x=$signed(input_words[c]);
                if(!initialized) begin dc[c]=x<<<12;rounded=0;end
                else begin
                    u=(x<<<12)-dc[c];
                    delta=round_away(u*64'sd27740,20);
                    dc[c]=dc[c]+delta;
                    y=u-(delta>>>1);
                    rounded=round_away(y,12);
                    if(rounded>8388607) rounded=8388607;
                    if(rounded< -8388608) rounded=-8388608;
                end
                expected_output[24*c+:24]=rounded[23:0];
            end
            initialized=1;
        end
        #0.1;
        if(!rst && filtered_valid) begin
            if(!pending || cycle-accept_cycle!=6) $fatal(1,"FAIL: sensor-to-filter valid alignment");
            if(filtered_data !== expected_output) $fatal(1,"FAIL: sensor-to-filter data mismatch");
            if(overrun) $fatal(1,"FAIL: real-rate overrun");
            pending=0;checked=checked+1;
        end
    end
    initial begin
        #1000 rst=0;
        wait(checked>=24);
        #12345 rst=1;epoch=1;
        #1000 rst=0;
        wait(checked>=48);
        $display("PASS: sensor-to-HPF chain 48 physical I2S frames x16, real-rate valid alignment, signed words and reset recovery");
        $finish;
    end
    initial begin #3000000;$fatal(1,"FAIL: chain timeout");end
endmodule

`timescale 1ns/1ps
module tb_heatmap_hflip;
  reg [10:0] xpos=0,ypos=0;
  reg ap_rst=1;
  wire [13:0] index;
  acoustic_pixel_mapping mapping(.*);
  reg acoustic_clk=0,axi_clk=0;
  always #2.381 acoustic_clk=~acoustic_clk;
  always #5.137 axi_clk=~axi_clk;
  reg acoustic_rstn=0,axi_rstn=0;
  reg sample_valid=0,sample_last=0;
  reg [13:0] sample_index=0;
  reg [23:0] sample_value=0;
  reg [31:0] threshold_axi=50;
  wire [31:0] status_axi,hotspot_x_axi,hotspot_y_axi,hotspot_value_axi,confidence_axi;
  wire new_result_axi;
  acoustic_result_adapter adapter(.*);
  reg mapping_done=0,adapter_done=0;
  integer x,y,c,r,i,n,target,seen;
  integer pixels=0,frames=0;
  initial begin
    #20;ap_rst=0;
    for(y=0;y<720;y=y+1)for(x=0;x<1280;x=x+1)begin
      xpos=x;ypos=y;#1;
      if(index!==((y/20)*64+63-x/20))
        $fatal(1,"Mapping mismatch at %0d,%0d: %0d",x,y,index);
      pixels++;
    end
    xpos=1280;ypos=1;#1;if(index!==0)$fatal(1,"Out-of-bounds x");
    xpos=1;ypos=720;#1;if(index!==0)$fatal(1,"Out-of-bounds y");
    xpos=2047;ypos=2047;#1;if(index!==0)$fatal(1,"Out-of-bounds maximum");
    xpos=100;ypos=100;ap_rst=1;#1;if(index!==0)$fatal(1,"Reset guard");
    mapping_done=1;
  end
  initial begin
    #33;acoustic_rstn=1;axi_rstn=1;#150;
    // Every column, including both edges. Sweep rows to include all 36 rows.
    for(c=0;c<64;c=c+1)begin
      r=c%36;target=r*64+c;
      for(i=0;i<2304;i=i+1)begin
        @(negedge acoustic_clk);
        sample_valid=1;sample_last=(i==2303);sample_index=i;
        sample_value=(i==target)?100+c:1;
      end
      @(negedge acoustic_clk);sample_valid=0;sample_last=0;
      seen=0;
      for(n=0;n<100;n=n+1)begin
        @(posedge axi_clk);#0.01;
        if(new_result_axi)begin
          if(hotspot_x_axi!==(2*(63-c)+1)||hotspot_y_axi!==(2*r+1)||
             hotspot_value_axi!==(100+c)||confidence_axi!==(100+c)||status_axi!==3)
            $fatal(1,"Result mismatch c=%0d r=%0d x=%0d y=%0d value=%0d status=%h",
                   c,r,hotspot_x_axi,hotspot_y_axi,hotspot_value_axi,status_axi);
          // The legacy ABI is odd cell centres, NOT 127-old_x.
          if(((hotspot_x_axi-1)/2)!==(63-c))$fatal(1,"Daemon grid conversion mismatch");
          seen=1;n=100;
        end
      end
      if(!seen)$fatal(1,"Mailbox timeout");
      frames++;#120;
    end
    adapter_done=1;
  end
  initial begin
    wait(mapping_done&&adapter_done);
    $display("PASS_HEATMAP_HFLIP pixels=%0d hotspot_frames=%0d row_value_alarm_unchanged",pixels,frames);
    $finish;
  end
  initial begin #5000000;$fatal(1,"Global timeout");end
endmodule

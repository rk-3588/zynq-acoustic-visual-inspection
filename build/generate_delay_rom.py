"""Independent geometric Q5 reference for the frozen 16-mic ROM."""
from pathlib import Path
import argparse,math,re
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--check',type=Path,default=root/'data/delay_rom.coe');p.add_argument('--output',type=Path);a=p.parse_args()
focal=math.hypot(31.5,17.5)/math.tan(math.pi/6)
words=[]
for y in range(36):
    for x in range(64):
        dx=x-31.5;dy=y-17.5;norm=math.sqrt(dx*dx+dy*dy+focal*focal);packed=0
        for i in range(4):
            angle=(45+45*i)*math.pi/180
            q=math.floor(.025*(math.cos(angle)*dx+math.sin(angle)*dy)*46875/343/norm*32+.5)
            packed=(packed<<10)|(q&1023)
        words.append(packed)
words.extend([0]*(9216-len(words)))
if a.output:
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text('memory_initialization_radix=2;\nmemory_initialization_vector=\n'+',\n'.join(f'{v:040b}' for v in words)+';\n',encoding='ascii')
actual=[int(s,2) for s in re.findall(r'(?m)^([01]{40})[,;]?$',a.check.read_text())]
assert actual==words,'ROM differs from final 64x36 geometric reference'
assert (root/'src/vivado/ip/blk_mem_gen_2/delay_rom.coe').read_bytes()==(root/'data/delay_rom.coe').read_bytes()
print('PASS_ROM_2304_ACTIVE_9216_DEPTH_DUAL_RING_REFERENCE')

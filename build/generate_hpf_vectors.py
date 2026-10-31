"""Deterministic synthetic 16-channel signed24 HPF arithmetic reference."""
from pathlib import Path
import random,sys
def rounded(v,b):return (v+(1<<(b-1)))>>b if v>=0 else -((-v+(1<<(b-1)))>>b)
def packed(values):return sum((v&0xffffff)<<(24*i) for i,v in enumerate(values))
random_source=random.Random(200);lines=[]
for epoch in range(4):
    state=None
    for n in range(400):
        if epoch==0:x=[1000*(i-8) for i in range(16)]
        elif epoch==1:x=[8388607 if 32<=n<96 else -8388608 if 96<=n<160 else 0]*16
        else:x=[random_source.randrange(-8388608,8388608) for _ in range(16)]
        bypass=(n//50)%2 if epoch==3 else 0
        if state is None:state=[v<<12 for v in x];y=[0]*16
        else:
            y=[]
            for i,v in enumerate(x):
                u=(v<<12)-state[i];delta=rounded(u*27740,20);state[i]+=delta
                y.append(max(-8388608,min(8388607,rounded(u-delta//2,12))))
        if bypass:y=x
        lines.append(f'{int(n==0):x} {bypass:x} {packed(x):096x} {packed(y):096x}\n')
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(''.join(lines),encoding='ascii')
print('GENERATED_HPF_SYNTHETIC_VECTORS',len(lines))

import json, sys
sys.path.insert(0,'tools')
import numpy as np
from pog2_record import read_wav
SR=48000
cues={c['name']:c for c in json.load(open('measurements/pog2_cues.json'))['sections']}
for n in sys.argv[1:]:
    x=read_wav('measurements/pog2/%s.wav'%n); out=[]
    for note in ('steady_A2','steady_E3','steady_A3','steady_A4','steady_E5'):
        c=cues[note]; a=int((c['start']+0.5+0.083)*SR); seg=x[a:a+int(2.9*SR)]
        hop=120; env=np.array([np.sqrt(np.mean(seg[i:i+960]**2)) for i in range(0,len(seg)-960,hop)])
        pk=np.median(env[int(1.8*SR/hop):int(2.6*SR/hop)]); f=lambda q: np.argmax(env>q*pk)*hop/SR*1000
        out.append("%s %3.0f/%3.0f/%4.0f" % (note[7:], f(.1), f(.5), f(.9)))
    print(n, "10/50/90 % ms:", "  ".join(out))

import json, sys
sys.path.insert(0,'tools')
import numpy as np
from pog2_record import read_wav
SR=48000
cues={c['name']:c for c in json.load(open('measurements/pog2_cues.json'))['sections']}
ref=read_wav('measurements/pog2/A1.wav')
c=cues['pink_noise']; a=int((c['start']+1.5)*SR); b=a+int(3.5*SR)
f=np.fft.rfftfreq(b-a,1/SR); P0=np.abs(np.fft.rfft(ref[a:b]*np.hanning(b-a)))**2
fcs=[100,200,300,500,700,1000,1500,2000,3000,4000,5000,6000]
print("vs A1 (dB): " + " ".join("%6d" % v for v in fcs))
for n in sys.argv[1:]:
    x=read_wav('measurements/pog2/%s.wav'%n); P=np.abs(np.fft.rfft(x[a:b]*np.hanning(b-a)))**2
    r=[10*np.log10(P[(f>v/1.12)&(f<v*1.12)].sum()/P0[(f>v/1.12)&(f<v*1.12)].sum()) for v in fcs]
    print("%-10s " % n + " ".join("%6.1f" % v for v in r))

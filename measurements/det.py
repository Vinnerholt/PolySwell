import json, sys
sys.path.insert(0,'tools')
import numpy as np
from pog2_record import read_wav
SR=48000
cues={c['name']:c for c in json.load(open('measurements/pog2_cues.json'))['sections']}
c=cues['sine_440']; a=int((c['start']+0.5+0.3)*SR); b=a+int(4.2*SR)
for n in sys.argv[1:]:
    x=read_wav('measurements/pog2/%s.wav'%n)[a:b]; N=1<<20
    X=np.abs(np.fft.rfft(x*np.hanning(len(x)),N)); f=np.fft.rfftfreq(N,1/SR)
    m=(f>400)&(f<480); Xm=X[m]; fm=f[m]; pk=Xm.max()
    peaks=[i for i in range(1,len(Xm)-1) if Xm[i]>Xm[i-1] and Xm[i]>=Xm[i+1] and Xm[i]>pk*10**(-30/20)]
    desc=["%.2f Hz (%+.1f c) %.1f dB" % (fm[i], 1200*np.log2(fm[i]/440), 20*np.log10(Xm[i]/pk)) for i in peaks]
    env=np.array([np.sqrt(np.mean(x[i:i+960]**2)) for i in range(0,len(x)-960,480)]); db=20*np.log10(env)
    print(n, "| components:", "; ".join(desc), "| level wobble %.1f dB" % (db.max()-db.min()))

import os, sys; os.environ["SD_ENABLE_ASIO"]="1"
import numpy as np, sounddevice as sd
dev = [i for i,d in enumerate(sd.query_devices()) if d["name"]=="Universal Audio USB" and "ASIO" in sd.query_hostapis(d["hostapi"])["name"]][0]
SR=48000; t=np.arange(int(3.0*SR))/SR
tone=np.sin(2*np.pi*1000*t)*10**(-30/20); tone[int(2.6*SR):]=0
sig=np.concatenate([np.zeros(SR//4),tone]).astype(np.float32)[:,None]
rec=sd.playrec(sig, SR, input_mapping=[1], output_mapping=[1], device=(dev,dev), dtype="float32"); sd.wait()
x=rec[:,0]; hop=120; env=np.array([np.sqrt(np.mean(x[i:i+480]**2))*np.sqrt(2) for i in range(0,len(x)-480,hop)])
db=20*np.log10(env+1e-9); t0=SR//4/hop + int(0.083*SR/hop)
steady=db[int(t0+1.0*SR/hop):int(t0+2.4*SR/hop)]
pk=np.median(10**(steady/20)); on=env[int(t0):int(t0+1.0*SR/hop)]
r10=np.argmax(on>0.1*pk)*hop/SR*1000; r90=np.argmax(on>0.9*pk)*hop/SR*1000
print("%s level %.1f dBFS, wobble %.1f dB peak-to-peak, rise 10%% %.0f ms -> 90%% %.0f ms" % (sys.argv[1] if len(sys.argv)>1 else "", 20*np.log10(pk), steady.max()-steady.min(), r10, r90))

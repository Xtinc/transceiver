import numpy as np
import matplotlib.pyplot as plt
import scipy.signal as sig
from scipy.fft import fft, fftfreq


def sfft(data1, data2, fs1: float):
    c1 = fft(data1)
    N1 = len(c1)
    c2 = fft(data2)
    N2 = len(c2)
    c3 = []
    for i, _ in enumerate(data1):
        if i < N2 / 2:
            c3.append(c2[i] / c1[i])
        else:
            c3.append(1e-12)
    yf = 20 * np.log10(np.abs(c3[: N1 // 2]))
    xf = fftfreq(N1, 1 / fs1)[: N1 // 2]
    return (xf, yf)


# 1.generate signals
fs = 24000
t = np.linspace(0, 2, 2 * fs)
test_signals = 32767 * sig.chirp(t, f0=0, f1=fs / 2, t1=2, method="linear")

# 2.save to file
with open("signals.txt", mode="w", encoding="utf-8") as fp:
    for v in test_signals:
        fp.write(str(int(v)) + "\n")

# 3.load results from file
proc_signals = np.load("result.txt")

# analysis
xf1, yf1 = sfft(test_signals, proc_signals, fs)

# plot
ax1 = plt.subplot(2, 1, 1)
ax1.plot(t, test_signals)
ax1.plot(t[::2], proc_signals)
ax1.set_title("Linear Chirp")
ax1.set_xlabel("t(sec)")

ax2 = plt.subplot(2, 1, 2)
ax2.plot(xf1, yf1)
ax2.set_title("Frequence Response")
ax2.set_xlabel("freq(Hz)")
ax2.set_ylabel("dB")
plt.show()

import numpy as np
import matplotlib.pyplot as plt
import scipy.signal as sig
import scipy.fft as fft


def spectrogram(data, fsi: int, fig, ax):
    """spectrogram"""
    data_len = len(data)
    window_size = 2048
    hop = window_size // 2
    win = sig.windows.kaiser(window_size, 20)
    sft = sig.ShortTimeFFT(win, hop, fsi, scale_to="magnitude")
    sx = sft.stft(data)
    yticks = np.arange(0, fsi // 2 + 1, 2000)
    yticklabels = [f"{int(tick/1000)}" for tick in yticks]
    ax.set(
        xlabel="Time in seconds",
        ylabel="Frequency in kHz",
        yticks=yticks,
        yticklabels=yticklabels,
    )
    im = ax.imshow(
        20 * np.log10(abs(sx)),
        origin="lower",
        aspect="auto",
        extent=sft.extent(data_len),
        cmap="inferno",
        vmin=-200,
        vmax=1,
    )
    fig.colorbar(im, label="Magnitude in dBFS", ticks=np.arange(-200, 1, 20))


def plot(data_in, fsi: int, data_out, fso: int, duration: float):
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3)
    ax1.plot(np.linspace(0, duration, len(data_in)), data_in)
    ax1.plot(np.linspace(0, duration, len(data_out)), data_out, "x")
    spectrogram(data_out, fso, fig, ax2)

    yf1 = fft.fft(data_in)
    xf1 = fft.fftfreq(len(data_in), 1 / fsi)[: len(data_in) // 2]
    yf2 = fft.fft(data_out)
    xf2 = fft.fftfreq(len(data_out), 1 / fsi)[: len(data_out) // 2]
    ax3.plot(xf1, 2.0 / len(data_in) * np.abs(yf1[0 : len(data_in) // 2]))
    ax3.plot(xf2, 2.0 / len(data_out) * np.abs(yf2[0 : len(data_out) // 2]))
    plt.show()


fs = 48000
duration = 10
t = np.linspace(0, duration, duration * fs)
test_signals = sig.chirp(t, 1, duration, fs // 2, method="linear")

with open("test_signals.txt", mode="w", encoding="utf-8") as fp:
    for v in test_signals:
        fp.write(str(v) + "\n")

# result_signals = np.loadtxt("results.txt")
result_signals = test_signals[::2]
fso = len(result_signals) / duration
plot(test_signals, fs, result_signals, fso, duration)

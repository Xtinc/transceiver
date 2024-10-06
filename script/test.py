import wave
import numpy as np
import struct
import math

# db = 0
print("math.pow(10, -12/20) : ", math.pow(10, -12 / 20))  # 0.251188643150958
print("math.pow(10, -6/20) : ", math.pow(10, -6 / 20))  # 0.5011872336272722
print("math.pow(10, -3/20) : ", math.pow(10, -3 / 20))  # 0.7079457843841379
print("math.pow(10, -1/20) : ", math.pow(10, -1 / 20))  # 0.8912509381337456
print("math.pow(10, 0/20) : ", math.pow(10, 0 / 20))  # 1.0
print("math.pow(10, 1/20) : ", math.pow(10, 1 / 20))  # 1.1220184543019633
print("math.pow(10, 3/20) : ", math.pow(10, 3 / 20))  # 1.4125375446227544
print("math.pow(10, 6/20) : ", math.pow(10, 6 / 20))  # 1.9952623149688795
print("math.pow(10, 9/20) : ", math.pow(10, 9 / 20))  # 2.8183829312644537
print("math.pow(10, 12/20) : ", math.pow(10, 12 / 20))  # 3.9810717055349722
# volume x_db
db_v = -3
db = math.pow(10, -3 / 20)
# sample/every second
framerate = 48000
# channel_num
channel_num = 2
# bytes needed every sample
sample_width = 2
bits_width = sample_width * 8
# seconds, long of data
duration = 10
# frequeny of sinewave
sinewav_frequency_l = 19000
sinewav_frequency_r = 19000
# max value of samples, depends on bits_width
max_val = 2 ** (bits_width - 1) - 1
print("max_val : ", max_val)
# volume = 32767*db #0x7FFF=32767
volume = max_val * db  # 2**(bits_width-1) - 1
# 多个声道生成波形数据
x = np.linspace(0, duration, num=duration * framerate)
y_l = np.sin(2 * np.pi * sinewav_frequency_l * x) * volume
y_r = np.sin(2 * np.pi * sinewav_frequency_r * x) * volume
# 将多个声道的波形数据转换成数组
y = zip(y_l, y_r)
y = list(y)
y = np.array(y, dtype=int)
y = y.reshape(-1)

# 最终生成的一维数组
sine_wave = y
# wav file_name
file_name = (
    "sine_"
    + str(framerate)
    + "_"
    + str(channel_num)
    + "ch_"
    + str(db_v)
    + "db_l"
    + str(sinewav_frequency_l)
    + "_r"
    + str(sinewav_frequency_r)
    + "_"
    + str(duration)
    + "s.wav"
)
print("file_name: ", file_name)
# open wav file
wf = wave.open(file_name, "wb")  # wf = wave.open("sine.wav", 'wb')
wf.setnchannels(channel_num)
wf.setframerate(framerate)
wf.setsampwidth(sample_width)
for i in sine_wave:
    data = struct.pack("<h", int(i))
    wf.writeframesraw(data)
wf.close()

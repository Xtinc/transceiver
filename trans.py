import librosa
import soundfile

# 读取原始 WAV 文件（假设采样率为 48 kHz）
audio_path = './build/debug/audioFile.wav'
y, sr = librosa.load(audio_path, sr=44100)  # 将音频重新采样为 44.1 kHz

# 保存为新的 WAV 文件
output_path = './44.1k_audio.wav'
soundfile.write(output_path,y,sr)
# librosa.output.write_wav(output_path, y, sr)

print(f"音频已成功转换为 {sr} Hz 采样率并保存到 {output_path}")

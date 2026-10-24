#!/usr/bin/env python3
"""从 Freesound #607215 录音派生“敲木鱼”应用的敲击音（16 kHz / 16 bit / 单声道）。

用法：
    python3 derive.py <607215 解码后的 44.1 kHz 单声道 WAV> <输出 WAV>

源录音：Freesound #607215 "Mokugyo.wav" by jonopodmore，Creative Commons 0。
https://freesound.org/people/jonopodmore/sounds/607215/
（本仓库使用的是其公开 HQ 预览 mp3，经 macOS `afconvert -f WAVE -d LEI16@44100 -c 1`
解码为 44.1 kHz 单声道 WAV 后输入本脚本。）

处理步骤（与试听确认的候选 D 完全一致）：
1. 从起音前 1 ms 开始截取 300 ms；
2. 44.1 kHz → 16 kHz 多相重采样（resample_poly 160/441）；
3. 以 100/62 的比例再次重采样并按 16 kHz 播放 —— 音高降低约 38%（1.84 kHz → 1.14 kHz），
   时长相应拉长，听起来像更大、更沉的木鱼；
4. 截取 320 ms，最后 50 ms 线性淡出；
5. 峰值归一化到 -1 dBFS（0.89 满量程）。设备端最高音量档使 codec 增益为 0 dB，
   因此最响时也不会削波。

依赖：numpy、scipy。
"""
import sys
import wave

import numpy as np
from scipy.signal import resample_poly

SR = 16000


def main() -> None:
    src, dst = sys.argv[1], sys.argv[2]
    w = wave.open(src)
    if w.getframerate() != 44100 or w.getnchannels() != 1 or w.getsampwidth() != 2:
        sys.exit("input must be 44.1 kHz mono 16-bit WAV")
    a = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768

    x = a[: int(0.300 * 44100)].copy()          # 录音从起音开始，起点即 0
    x = resample_poly(x, 160, 441)
    x[-int(0.001 * SR):] *= np.linspace(1, 0, int(0.001 * SR))
    y = resample_poly(x, 100, 62)[: int(0.32 * SR)]
    fade = int(0.05 * SR)
    y[-fade:] *= np.linspace(1, 0, fade)
    y = y / np.abs(y).max() * 0.89

    out = wave.open(dst, "wb")
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(SR)
    out.writeframes((y * 32767).astype("<i2").tobytes())
    out.close()


if __name__ == "__main__":
    main()

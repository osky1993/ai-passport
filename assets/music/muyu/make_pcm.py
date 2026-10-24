#!/usr/bin/env python3
"""把 16 kHz / 16 bit / 单声道 WAV 转成固件内嵌的 C 数组（只用标准库）。

用法：
    python3 make_pcm.py mokugyo_deeper_16k.wav muyu_knock_pcm.c
"""
import struct
import sys
import wave


def main() -> None:
    src, dst = sys.argv[1], sys.argv[2]
    w = wave.open(src)
    if w.getframerate() != 16000 or w.getnchannels() != 1 or w.getsampwidth() != 2:
        sys.exit("input must be 16 kHz mono 16-bit WAV")
    n = w.getnframes()
    samples = struct.unpack("<%dh" % n, w.readframes(n))
    lines = []
    for i in range(0, n, 12):
        lines.append("    " + ", ".join("%6d" % s for s in samples[i:i + 12]) + ",")
    with open(dst, "w", encoding="utf-8") as f:
        f.write(
            "// 由 assets/music/muyu/make_pcm.py 从 mokugyo_deeper_16k.wav 生成，请勿手改。\n"
            "// 来源：Freesound #607215 \"Mokugyo.wav\" by jonopodmore（CC0），处理见 derive.py。\n"
            "// 格式：16 kHz / 16 bit 有符号 / 单声道，峰值 -1 dBFS。\n"
            "#include \"muyu_tone.h\"\n\n"
            "const int16_t muyu_knock_pcm[] = {\n"
            + "\n".join(lines)
            + "\n};\n\n"
            "const size_t muyu_knock_samples = sizeof(muyu_knock_pcm) / sizeof(muyu_knock_pcm[0]);\n"
        )


if __name__ == "__main__":
    main()

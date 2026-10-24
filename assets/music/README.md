<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Music and Sound Effects

This directory holds reusable audio sources, derivation scripts, and PCM sources embedded in firmware. Each asset records its source, license, format, processing steps, and integration.

## `muyu/`: strike sound for the "Tap the Wooden Fish" app

| File | Contents |
| --- | --- |
| [`muyu/derive.py`](muyu/derive.py) | Derives the device sound from the source recording: trim, resample to 16 kHz, lower the pitch by about 38%, fade out, and normalize to -1 dBFS (requires numpy and scipy). |
| `muyu/mokugyo_deeper_16k.wav` | Derived result: 16 kHz / 16-bit / mono, 320 ms, about 10 KB. |
| [`muyu/make_pcm.py`](muyu/make_pcm.py) | Converts that WAV into a C array (Python standard library only). |
| `muyu/muyu_knock_pcm.c` | Generated `muyu_knock_pcm[]` / `muyu_knock_samples`, compiled into the firmware by `main/CMakeLists.txt`. |

- **Source**: Freesound #607215 "Mokugyo.wav" by jonopodmore, licensed Creative Commons 0 (public-domain dedication; commercial use allowed, no attribution required).
  Page `https://freesound.org/people/jonopodmore/sounds/607215/`. A small mokugyo recorded at Koya-san, Japan, struck with its original cloth-covered beater.
- **How it was obtained**: the original WAV requires a login, so the project uses the public HQ preview
  `https://cdn.freesound.org/previews/607/607215_541139-hq.mp3`, decoded with macOS
  `afconvert -f WAVE -d LEI16@44100 -c 1` before running `derive.py`.
- **Selection**: four candidates were compared by ear on a computer (real mokugyo, temple block, re-synthesized wood, and real mokugyo pitched down); the user chose the pitched-down real mokugyo (main frequency about 1.1 kHz). The earlier sine-based synthesis sounded like glass or ceramic on the device and was dropped.
- **Loudness**: PCM peaks at -1 dBFS; the three firmware volume levels map to -12 / -6 / 0 dB net codec gain, so the loudest level does not clip (see `main/muyu_core.h`).
- **Resource impact**: about 10 KB of Flash, read-only; the I2S driver copies it into DMA buffers during playback, so no extra RAM is used.

To replace the sound:

1. Produce a 16 kHz / 16-bit / mono WAV with `derive.py` (or your own pipeline), peaking no higher than -1 dBFS and fading to 0 at the end;
2. Run `python3 assets/music/muyu/make_pcm.py <wav> assets/music/muyu/muyu_knock_pcm.c`;
3. If the duration changes, recheck `MUYU_SAVE_QUIET_MS` in `main/muyu_core.h` (must exceed the sound length) and `tests/test_muyu_tone.c`;
4. Run `./tools/validate.sh` and listen to all three volume levels on the device.

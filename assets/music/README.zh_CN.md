<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 音乐与音效（Music）

本目录存放可复用的音频素材、派生脚本和固件内嵌的 PCM 源码。每个素材记录来源、许可、格式、处理步骤和集成方式。

## `muyu/`：“敲木鱼，攒功德”敲击音

| 文件 | 内容 |
| --- | --- |
| [`muyu/derive.py`](muyu/derive.py) | 从源录音派生设备音效：截取、重采样到 16 kHz、降调约 38%、淡出、归一化到 -1 dBFS（依赖 numpy、scipy）。 |
| `muyu/mokugyo_deeper_16k.wav` | 派生结果：16 kHz / 16 bit / 单声道，320 ms，约 10 KB。 |
| [`muyu/make_pcm.py`](muyu/make_pcm.py) | 把上面的 WAV 转成 C 数组（只用 Python 标准库）。 |
| `muyu/muyu_knock_pcm.c` | 生成的 `muyu_knock_pcm[]` / `muyu_knock_samples`，由 `main/CMakeLists.txt` 编译进固件。 |

- **来源**：Freesound #607215 “Mokugyo.wav”，作者 jonopodmore，许可 Creative Commons 0（公有领域贡献，可商用、无需署名）。
  页面 `https://freesound.org/people/jonopodmore/sounds/607215/`。录音为日本高野山的小木鱼，用原配布包木槌敲击。
- **取得方式**：未登录无法下载原始 WAV，本项目使用公开的 HQ 预览
  `https://cdn.freesound.org/previews/607/607215_541139-hq.mp3`，经 macOS
  `afconvert -f WAVE -d LEI16@44100 -c 1` 解码后输入 `derive.py`。
- **选择过程**：在电脑上试听比较了真实木鱼、寺庙木魚、重新合成的木头音、真实木鱼降调四种候选，用户选定“真实木鱼降调”（主频约 1.1 kHz）。早期的正弦合成音真机听感偏玻璃/陶瓷，已弃用。
- **音量**：PCM 峰值 -1 dBFS；固件三档音量对应 codec 净增益 -12 / -6 / 0 dB，最高档不削波（见 `main/muyu_core.h`）。
- **资源影响**：约 10 KB Flash，只读常量，播放时由 I2S 驱动拷入 DMA 缓冲，不占额外 RAM。

更换音效时：

1. 用 `derive.py`（或自己的处理流程）得到 16 kHz / 16 bit / 单声道 WAV，峰值不高于 -1 dBFS，结尾淡出到 0；
2. 运行 `python3 assets/music/muyu/make_pcm.py <wav> assets/music/muyu/muyu_knock_pcm.c`；
3. 若时长变化，同步检查 `main/muyu_core.h` 的 `MUYU_SAVE_QUIET_MS`（须大于音效时长）与 `tests/test_muyu_tone.c`；
4. 运行 `./tools/validate.sh` 并在真机试听三档音量。

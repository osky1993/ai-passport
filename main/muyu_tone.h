// main/muyu_tone.h —— 木鱼敲击音效（内嵌 PCM）。
//
// 音源是真实木鱼录音（Freesound #607215，CC0），降调约 38% 得到更大、更沉的木鱼声，
// 由 assets/music/muyu/derive.py 处理、make_pcm.py 转成 C 数组
// （assets/music/muyu/muyu_knock_pcm.c，约 10 KB Flash，只读）。
// 早期版本用正弦分量合成，真机听感偏“玻璃/陶瓷”，已替换。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MUYU_TONE_RATE_HZ   16000   // 与 BSP 音频格式一致：16 kHz / 16 bit / 单声道

// 敲击音 PCM（峰值 -1 dBFS，结尾已淡出到 0）。位于 Flash，只读，生命周期为整个程序。
extern const int16_t muyu_knock_pcm[];
// muyu_knock_pcm 的采样数（当前 5120 个，即 320 ms）。
extern const size_t muyu_knock_samples;

// 内嵌木鱼敲击音 PCM 的 host 测试：长度、峰值余量、起止归零、衰减与音色特征。
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "muyu_tone.h"

static int peak_abs(size_t from, size_t to) {
    int peak = 0;
    for (size_t i = from; i < to; i++) {
        int a = abs((int)muyu_knock_pcm[i]);
        if (a > peak) peak = a;
    }
    return peak;
}

int main(void) {
    const size_t n = muyu_knock_samples;
    const size_t ms = MUYU_TONE_RATE_HZ / 1000;

    // 320 ms：与 muyu_core 的保存静默阈值（必须大于音效时长）配套。
    assert(n == 320 * ms);

    // 峰值 -1 dBFS（0.89 满量程）：最高音量档 codec 0 dB 时不削波。
    const int peak = peak_abs(0, n);
    assert(peak <= 29200 && peak >= 28000);

    // 结尾淡出到 0，交给 I2S DMA 自动清零时没有台阶噪声。
    assert(muyu_knock_pcm[n - 1] == 0);
    assert(peak_abs(n - 5 * ms, n) < peak / 20);

    // 起音在前 10 ms 内达到峰值附近（木鱼是瞬态敲击声）。
    assert(peak_abs(0, 10 * ms) > peak * 8 / 10);

    // 快速衰减：80 ms 之后的峰值比起音低 20 dB 以上（不像陶瓷/玻璃那样长时间鸣响）。
    assert(peak_abs(80 * ms, n) * 10 < peak);

    // 音色：用过零率粗估主频，应在约 1.1 kHz 的“降调木鱼”附近（0.8–1.6 kHz）。
    size_t zc = 0;
    for (size_t i = 1; i < 60 * ms; i++) {
        if ((muyu_knock_pcm[i - 1] < 0) != (muyu_knock_pcm[i] < 0)) zc++;
    }
    const double hz = (double)zc / 2.0 / 0.060;
    assert(hz > 800 && hz < 1600);

    printf("knock samples=%zu peak=%d zero-cross-freq=%.0f Hz\n", n, peak, hz);
    printf("test_muyu_tone: PASS\n");
    return 0;
}

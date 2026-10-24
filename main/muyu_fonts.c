// main/muyu_fonts.c —— 木鱼应用字体描述符。说明见 muyu_fonts.h。
#include "muyu_fonts.h"

#include <stdbool.h>

// 生成的字体符号（定义在 assets/fonts/muyu/*.c，由 main/CMakeLists.txt 编译）。
LV_FONT_DECLARE(muyu_font_16);
LV_FONT_DECLARE(muyu_font_24);
LV_FONT_DECLARE(muyu_font_num_44);

// 可写副本：生成的字体是 const，不能直接改 fallback 字段，所以浅拷贝一份
// 与程序同寿命的描述符，再挂上 Montserrat 作为图标（LV_SYMBOL_*）的回退。
// 只在 muyu_fonts_init() 中写一次，之后只读，无需额外同步。
static lv_font_t s_text16;
static lv_font_t s_title24;
static bool s_ready;

void muyu_fonts_init(void)
{
    if (s_ready) return;
    s_text16 = muyu_font_16;
    s_text16.fallback = &lv_font_montserrat_14;
    s_title24 = muyu_font_24;
    s_title24.fallback = &lv_font_montserrat_20;
    s_ready = true;
}

const lv_font_t *muyu_font_text16(void)
{
    return s_ready ? &s_text16 : &muyu_font_16;
}

const lv_font_t *muyu_font_title24(void)
{
    return s_ready ? &s_title24 : &muyu_font_24;
}

const lv_font_t *muyu_font_number44(void)
{
    return &muyu_font_num_44;
}

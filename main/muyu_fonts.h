// main/muyu_fonts.h —— 木鱼应用的字体入口。
//
// 字体源码由 assets/fonts/muyu/generate.sh 从 ZCOOL XiaoWei（OFL）生成，只含
// assets/fonts/muyu/muyu_font_chars.txt 中的汉字与可打印 ASCII。新增中文文案前
// 必须先补字符清单并重新生成，否则会显示缺字方框。
#pragma once

#include "lvgl.h"

// 初始化带 fallback 的字体描述符。必须在 LVGL 初始化之后、创建任何使用这些
// 字体的控件之前调用一次；重复调用无副作用。线程上下文：持有 bsp_lvgl_lock()。
void muyu_fonts_init(void);

// 16 px 正文字体：中文子集 + ASCII，缺字（LVGL 图标）回退到 Montserrat 14。
const lv_font_t *muyu_font_text16(void);

// 24 px 标题/飘字字体：中文子集 + ASCII，缺字回退到 Montserrat 20。
const lv_font_t *muyu_font_title24(void);

// 44 px 数字字体：只含 0-9，用于累计功德。
const lv_font_t *muyu_font_number44(void);

#!/usr/bin/env bash
# assets/fonts/muyu/generate.sh —— 重新生成“敲木鱼”应用使用的 LVGL 字体子集。
#
# 用法：
#   assets/fonts/muyu/generate.sh /path/to/ZCOOLXiaoWei-Regular.ttf
#
# 源字体：ZCOOL XiaoWei（站酷小薇体），SIL OFL 1.1，见同目录 ZCOOLXiaoWei-OFL.txt。
# 下载地址与校验值记录在 assets/fonts/README.zh_CN.md。
# 转换器：lv_font_conv 1.5.3（通过 npx 固定版本调用，需要 Node.js）。
#
# 修改界面中文文案后：先把新增汉字加入 muyu_font_chars.txt，再运行本脚本，
# 最后运行 ./tools/validate.sh 与真机显示验收。
set -euo pipefail

font="${1:?usage: $0 /path/to/ZCOOLXiaoWei-Regular.ttf}"
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
conv=(npx --yes lv_font_conv@1.5.3)

# 汉字清单（去掉换行）。ASCII 可打印字符统一用 0x20-0x7E 覆盖。
symbols="$(tr -d '\n' < "${here}/muyu_font_chars.txt")"

# 16 px：底部按键提示、“本次 +N”、“自动”标签。
"${conv[@]}" --font "${font}" --range 0x20-0x7E --symbols "${symbols}" \
    --size 16 --bpp 4 --format lvgl --no-compress \
    --lv-font-name muyu_font_16 --lv-include lvgl.h \
    --output "${here}/muyu_font_16.c"

# 24 px：标题“功德”与敲击飘字。
"${conv[@]}" --font "${font}" --range 0x20-0x7E --symbols "${symbols}" \
    --size 24 --bpp 4 --format lvgl --no-compress \
    --lv-font-name muyu_font_24 --lv-include lvgl.h \
    --output "${here}/muyu_font_24.c"

# 44 px：只含数字，用于累计功德大数字。
"${conv[@]}" --font "${font}" --range 0x30-0x39 \
    --size 44 --bpp 4 --format lvgl --no-compress \
    --lv-font-name muyu_font_num_44 --lv-include lvgl.h \
    --output "${here}/muyu_font_num_44.c"

# lv_font_conv 会把完整命令写进文件头注释；把本机绝对路径替换为文件名，
# 让生成结果与机器无关、可复现比对。
for out in muyu_font_16.c muyu_font_24.c muyu_font_num_44.c; do
    sed -i.bak -e "s#--font [^ ]*/#--font #" -e "s#--output [^ ]*/#--output #" "${here}/${out}"
    rm -f "${here}/${out}.bak"
done

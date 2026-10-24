<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 字库资源（Fonts）

本目录存放可复用的字库源文件与生成的 LVGL 字体源码。每个字库记录来源、许可、字符范围、转换命令和集成方式。

## `muyu/`：“敲木鱼，攒功德”应用字体

| 文件 | 内容 |
| --- | --- |
| [`muyu/muyu_font_chars.txt`](muyu/muyu_font_chars.txt) | 应用界面用到的全部汉字（UTF-8，一行）。ASCII 可打印字符 `0x20-0x7E` 由生成脚本统一加入。 |
| [`muyu/generate.sh`](muyu/generate.sh) | 可复现的生成脚本：`lv_font_conv` 1.5.3、4 bpp、不压缩。 |
| `muyu/muyu_font_16.c` | 16 px：汉字子集 + ASCII，用于按键提示、“本次 +N”、“自动”标签。 |
| `muyu/muyu_font_24.c` | 24 px：汉字子集 + ASCII，用于“功德”标题与敲击飘字。 |
| `muyu/muyu_font_num_44.c` | 44 px：仅数字 `0-9`，用于累计功德大数字。 |
| [`muyu/ZCOOLXiaoWei-OFL.txt`](muyu/ZCOOLXiaoWei-OFL.txt) | 源字体许可证全文。 |

- **源字体**：ZCOOL XiaoWei（站酷小薇体）Regular，SIL Open Font License 1.1。
  下载地址 `https://raw.githubusercontent.com/google/fonts/main/ofl/zcoolxiaowei/ZCOOLXiaoWei-Regular.ttf`，
  SHA-256 `a42b620140f493db42f741351dfbf343c0936d58588ee8004b8b2a218d997ff1`。
  TTF 原文件约 6 MB，不提交到仓库，按上述地址下载后用脚本重新生成。
- **集成方式**：`main/CMakeLists.txt` 通过 `target_sources` 编译三个 `.c`；`main/muyu_fonts.c` 用 `LV_FONT_DECLARE` 声明，并为 16/24 px 建立可写描述符，缺字时回退到 Montserrat 14/20 以显示 LVGL 图标（音量、电池）。
- **覆盖检查**：生成后确认每个 `.c` 中都出现了清单里全部汉字的 `/* U+XXXX "字" */` 注释（当前 21 个汉字均已覆盖）。
- **资源影响**：三个字体合计约 32 KB Flash，只读常量，不占用 LVGL 内存池。

修改界面中文文案时：

1. 把新增汉字加入 `muyu/muyu_font_chars.txt`；
2. 运行 `assets/fonts/muyu/generate.sh /path/to/ZCOOLXiaoWei-Regular.ttf`（需要 Node.js）；
3. 运行 `./tools/validate.sh`，并在真机上逐页检查中文显示，没有缺字方框。

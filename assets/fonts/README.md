<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fonts

This directory holds reusable font sources and generated LVGL font sources. Each font records its source, license, character range, conversion command, and integration.

## `muyu/`: fonts for the "Tap the Wooden Fish" app

| File | Contents |
| --- | --- |
| [`muyu/muyu_font_chars.txt`](muyu/muyu_font_chars.txt) | Every Chinese character used by the app UI (UTF-8, one line). Printable ASCII `0x20-0x7E` is added by the generator script. |
| [`muyu/generate.sh`](muyu/generate.sh) | Reproducible generator: `lv_font_conv` 1.5.3, 4 bpp, uncompressed. |
| `muyu/muyu_font_16.c` | 16 px: CJK subset + ASCII for key hints, the session counter, and the auto-mode tag. |
| `muyu/muyu_font_24.c` | 24 px: CJK subset + ASCII for the merit title and floating strike text. |
| `muyu/muyu_font_num_44.c` | 44 px: digits `0-9` only, for the total merit counter. |
| [`muyu/ZCOOLXiaoWei-OFL.txt`](muyu/ZCOOLXiaoWei-OFL.txt) | Full license text of the source font. |

- **Source font**: ZCOOL XiaoWei Regular, SIL Open Font License 1.1.
  Download `https://raw.githubusercontent.com/google/fonts/main/ofl/zcoolxiaowei/ZCOOLXiaoWei-Regular.ttf`,
  SHA-256 `a42b620140f493db42f741351dfbf343c0936d58588ee8004b8b2a218d997ff1`.
  The TTF (about 6 MB) is not committed; download it from the URL above and rerun the script.
- **Integration**: `main/CMakeLists.txt` compiles the three `.c` files with `target_sources`; `main/muyu_fonts.c` declares them with `LV_FONT_DECLARE` and builds writable descriptors for 16/24 px that fall back to Montserrat 14/20 for LVGL symbols (volume, battery).
- **Coverage check**: after generation, confirm each `.c` file contains a `/* U+XXXX */` glyph comment for every character in the list (all 21 characters are currently covered).
- **Resource impact**: about 32 KB of Flash in total, read-only constants, no LVGL memory-pool usage.

When changing Chinese UI text:

1. Add new characters to `muyu/muyu_font_chars.txt`;
2. Run `assets/fonts/muyu/generate.sh /path/to/ZCOOLXiaoWei-Regular.ttf` (requires Node.js);
3. Run `./tools/validate.sh` and check every page on the device for missing-glyph boxes.

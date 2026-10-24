# “敲木鱼，攒功德”应用。基线硬件测试 demo（demo_*.c、ui_pixel*.c）仍保留在
# main/ 中供 host tests 与参考，但不再编译进固件，启动后直接进入木鱼页面。
idf_component_register(
    SRCS "main.c"
         "muyu_core.c"
         "muyu_fonts.c"
         "muyu_ui.c"
         "muyu_audio.c"
         "muyu_store.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 中文字体子集：由 assets/fonts/muyu/generate.sh 生成，字符清单见 muyu_font_chars.txt。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/muyu/muyu_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/muyu/muyu_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/muyu/muyu_font_num_44.c"
)

# 木鱼敲击音 PCM：由 assets/music/muyu/make_pcm.py 生成，来源与处理见 derive.py。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/music/muyu/muyu_knock_pcm.c"
)

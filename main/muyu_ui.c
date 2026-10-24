// main/muyu_ui.c —— “敲木鱼，攒功德”页面实现。说明见 muyu_ui.h。
//
// 布局（240x320 竖屏，四角 30 px 圆角由显示层统一遮罩，内容避开四角）：
//   y  16..34   状态栏：左 声音图标 + “自动”标签，右 电量
//   y  40..64   “功德”标题（24 px）
//   y  66..108  累计功德大数字（44 px 数字字体）
//   y 106..124  “本次 +N”（16 px）
//   y 126..176  飘字区域
//   y 176..280  木鱼本体（椭圆，中心 (102,228)，半轴 78x52）
//   y 272..296  红色坐垫
//   y 298..316  按键提示（16 px）
//   木槌握点在右下 (224,250)，几何见 muyu_core.h。
//
// 内存：无 PSRAM、LVGL 池仅 24 KB。这里刻意不用 transform 旋转/缩放、整体 opa 和
// 阴影——它们需要 ARGB 中间图层，容易耗尽内存。木槌用 lv_line 画线、槌头用圆形
// 对象平移，动画只改坐标、平移量和局部透明度（text_opa / border_opa）。
#include "muyu_ui.h"

#include "muyu_core.h"
#include "muyu_fonts.h"

#include "lvgl.h"
#include <stddef.h>

// ---------------------------------------------------------------------------
// 配色（RGB888，LVGL 转 RGB565 显示）
// ---------------------------------------------------------------------------
#define COLOR_BG_TOP     0x24170F   // 背景顶部：暖棕
#define COLOR_BG_BOTTOM  0x0A0604   // 背景底部：近黑，营造夜间禅房的氛围
#define COLOR_GOLD       0xE8B860   // 主强调色：金
#define COLOR_GOLD_HOT   0xFFD98A   // 里程碑飘字：更亮的金
#define COLOR_TEXT       0xF3E6D0   // 主文字：暖白
#define COLOR_MUTED      0x9C8468   // 次要文字
#define COLOR_ALERT      0xE0654F   // 低电量 / 音频不可用
#define COLOR_WOOD_LIGHT 0xD99B55   // 木鱼受光面
#define COLOR_WOOD_DARK  0x8A471C   // 木鱼背光面
#define COLOR_WOOD_EDGE  0x4A2410   // 木鱼描边
#define COLOR_SLIT       0x2A1208   // 鱼口开槽
#define COLOR_CUSHION_A  0x8E2424   // 坐垫上缘
#define COLOR_CUSHION_B  0x4C1010   // 坐垫下缘
#define COLOR_STICK      0xB98652   // 槌柄
#define COLOR_GRIP       0x5A3418   // 握把
#define COLOR_HEAD_LIGHT 0xC98A4E   // 槌头受光面
#define COLOR_HEAD_DARK  0x6E3A1A   // 槌头背光面

// ---------------------------------------------------------------------------
// 几何常量（px）
// ---------------------------------------------------------------------------
#define FISH_X        24     // 木鱼本体左上角
#define FISH_Y        176
#define FISH_W        156
#define FISH_H        104
#define HEAD_D        30     // 槌头直径
#define LINE_ORIGIN_X 100    // 木槌 lv_line 对象原点：取槌头可达范围左上方，
#define LINE_ORIGIN_Y 100    // 让线对象包围盒尽量小，减少每帧重绘面积
#define GRIP_LEN      36     // 握把长度（从握点沿槌柄方向）
#define TOTAL_Y_BIG   70     // 累计功德 44 px 数字的顶部 y
#define TOTAL_Y_SMALL 78     // 位数过多改用 24 px 字体时的顶部 y（与大字垂直居中对齐）
#define FLOAT_POOL    3      // 飘字标签池大小：连续快速敲击时循环复用（每个标签占 LVGL 池内存）
#define FLOAT_START_Y 144    // 飘字起点 y（标签顶部）
#define FLOAT_RISE    32     // 飘字上升距离
#define FLOAT_X_OFS   (-40)  // 飘字相对屏幕中线的 x 偏移：偏木鱼左侧，避开右侧落下的槌头
#define MILESTONE_X_OFS (-6) // 里程碑文字较长（约 144 px），居中显示以免被左侧圆角裁掉
#define RIPPLE_X      132    // 涟漪中心：敲击点附近（木鱼顶部偏右）
#define RIPPLE_Y      182
#define RIPPLE_MIN_D  16     // 涟漪起始直径
#define RIPPLE_MAX_D  96     // 涟漪最终直径

// 动画时长（ms）
#define MALLET_ANIM_MS   200   // 木槌回弹时长：敲击瞬间直接落到敲击位，再用这段时间抬起
#define BOUNCE_ANIM_MS   180   // 木鱼下沉回弹
#define BOUNCE_PX        3     // 木鱼下沉最大位移
#define RIPPLE_ANIM_MS   420
#define FLOAT_ANIM_MS    760

// ---------------------------------------------------------------------------
// 页面对象。全部在 muyu_ui_create() 中创建、随 screen 永久存在（本应用只有这一页，
// 不会删除 screen），因此动画回调可以安全地引用这些指针。只在持 LVGL 锁时访问。
// ---------------------------------------------------------------------------
static lv_obj_t *s_scr;
static lv_obj_t *s_sound;          // 音量图标 + 档位数字
static lv_obj_t *s_auto_tag;       // “自动N”标签（金底药丸），紧跟在音量右侧
static lv_obj_t *s_battery;        // 电量
static lv_obj_t *s_total;          // 累计功德大数字
static lv_obj_t *s_session;        // “本次 +N”
static lv_obj_t *s_fish;           // 木鱼本体（含子对象，整体下沉）
static lv_obj_t *s_stick;          // 槌柄线
static lv_obj_t *s_grip;           // 握把线
static lv_obj_t *s_head;           // 槌头
static lv_obj_t *s_ripple;         // 涟漪圆环
static lv_obj_t *s_floats[FLOAT_POOL];
static uint8_t s_float_next;       // 下一个复用的飘字标签下标
static bool s_total_small;         // 累计功德当前是否使用小号字体
// 各飘字标签的 x 抖动：轮流左右错开一点，连敲时飘字不完全重叠。
static const int8_t FLOAT_JITTER[FLOAT_POOL] = { 0, -12, 8 };

// lv_line 只保存点数组指针，不复制数据：数组必须与对象同寿命，故用静态存储。
static lv_point_precise_t s_stick_pts[2];
static lv_point_precise_t s_grip_pts[2];

// 创建一个去掉默认主题样式的空白对象，避免主题带来的白底、边框、内边距和滚动条。
static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    if (o == NULL) return NULL;
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// 创建指定字体与颜色的标签。
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    if (l == NULL) return NULL;
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

// 根据进度 0..1000 摆放木槌：更新两条线的端点与槌头位置。
static void mallet_pose(int progress)
{
    int hx, hy;
    muyu_mallet_head(progress, &hx, &hy);
    const int px = MUYU_MALLET_PIVOT_X, py = MUYU_MALLET_PIVOT_Y;

    s_stick_pts[0].x = px - LINE_ORIGIN_X;
    s_stick_pts[0].y = py - LINE_ORIGIN_Y;
    s_stick_pts[1].x = hx - LINE_ORIGIN_X;
    s_stick_pts[1].y = hy - LINE_ORIGIN_Y;
    lv_line_set_points(s_stick, s_stick_pts, 2);

    // 握把：从握点沿槌柄方向取 GRIP_LEN。
    const int gx = px + (hx - px) * GRIP_LEN / MUYU_MALLET_LEN;
    const int gy = py + (hy - py) * GRIP_LEN / MUYU_MALLET_LEN;
    s_grip_pts[0] = s_stick_pts[0];
    s_grip_pts[1].x = gx - LINE_ORIGIN_X;
    s_grip_pts[1].y = gy - LINE_ORIGIN_Y;
    lv_line_set_points(s_grip, s_grip_pts, 2);

    lv_obj_set_pos(s_head, hx - HEAD_D / 2, hy - HEAD_D / 2);
}

// 木槌动画回调（LVGL 任务）：v 0..1000 对应从敲击位抬回静止位。
// 下落不做动画：按下后下一帧就落在木鱼上，与同时触发的音效对齐。主机渲染实测
// 带下落动画时槌头约 100 ms 后才触到木鱼，会明显落后于声音。
static void mallet_anim_cb(void *var, int32_t v)
{
    (void)var;
    mallet_pose(1000 - v);
}

// 木鱼下沉回弹回调：前 25% 下沉到 BOUNCE_PX，其余时间回到 0。
static void bounce_anim_cb(void *var, int32_t v)
{
    const int32_t y = v < 250 ? v * BOUNCE_PX / 250 : (1000 - v) * BOUNCE_PX / 750;
    lv_obj_set_style_translate_y((lv_obj_t *)var, y, 0);
}

// 涟漪回调：直径线性扩大，描边透明度线性淡出。圆环以 (RIPPLE_X, RIPPLE_Y) 为中心。
static void ripple_anim_cb(void *var, int32_t v)
{
    lv_obj_t *ring = var;
    const int32_t d = RIPPLE_MIN_D + (RIPPLE_MAX_D - RIPPLE_MIN_D) * v / 1000;
    lv_obj_set_size(ring, d, d);
    lv_obj_set_pos(ring, RIPPLE_X - d / 2, RIPPLE_Y - d / 2);
    lv_obj_set_style_border_opa(ring, (lv_opa_t)(200 * (1000 - v) / 1000), 0);
}

// 飘字回调：匀速上升；后 40% 时间淡出。结束时 text_opa 为 0（隐藏）。
static void float_anim_cb(void *var, int32_t v)
{
    lv_obj_t *label = var;
    const int32_t x_ofs = (int32_t)(intptr_t)lv_obj_get_user_data(label);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x_ofs, FLOAT_START_Y - FLOAT_RISE * v / 1000);
    const int32_t opa = v < 600 ? 255 : 255 * (1000 - v) / 400;
    lv_obj_set_style_text_opa(label, (lv_opa_t)opa, 0);
}

// 以 0..1000 为值域启动（或重启）一个作用在 var 上的动画。
static void start_anim(void *var, lv_anim_exec_xcb_t cb, uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_delete(var, cb);   // 快速连敲时打断上一轮，从头开始
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

// 构建木鱼：坐垫 → 本体（含高光、鱼口开槽、顶部雕纹）。
static bool build_fish(void)
{
    lv_obj_t *cushion = plain_obj(s_scr);
    if (cushion == NULL) return false;
    lv_obj_set_pos(cushion, 12, 268);
    lv_obj_set_size(cushion, 182, 28);
    lv_obj_set_style_radius(cushion, 14, 0);
    lv_obj_set_style_bg_opa(cushion, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cushion, lv_color_hex(COLOR_CUSHION_A), 0);
    lv_obj_set_style_bg_grad_color(cushion, lv_color_hex(COLOR_CUSHION_B), 0);
    lv_obj_set_style_bg_grad_dir(cushion, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(cushion, 2, 0);
    lv_obj_set_style_border_color(cushion, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_border_opa(cushion, LV_OPA_70, 0);

    s_fish = plain_obj(s_scr);
    if (s_fish == NULL) return false;
    lv_obj_set_pos(s_fish, FISH_X, FISH_Y);
    lv_obj_set_size(s_fish, FISH_W, FISH_H);
    lv_obj_set_style_radius(s_fish, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_fish, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_fish, lv_color_hex(COLOR_WOOD_LIGHT), 0);
    lv_obj_set_style_bg_grad_color(s_fish, lv_color_hex(COLOR_WOOD_DARK), 0);
    lv_obj_set_style_bg_grad_dir(s_fish, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_fish, 3, 0);
    lv_obj_set_style_border_color(s_fish, lv_color_hex(COLOR_WOOD_EDGE), 0);

    // 顶部高光：半透明暖白椭圆，让木鱼有圆润的体积感。
    lv_obj_t *shine = plain_obj(s_fish);
    if (shine == NULL) return false;
    lv_obj_set_pos(shine, 26, 12);
    lv_obj_set_size(shine, 64, 20);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(shine, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xFFF2DC), 0);

    // 顶部雕纹：一道细长的深色刻痕（圆角条），增加木作质感。
    lv_obj_t *groove = plain_obj(s_fish);
    if (groove == NULL) return false;
    lv_obj_set_pos(groove, 44, 44);
    lv_obj_set_size(groove, 68, 3);
    lv_obj_set_style_radius(groove, 2, 0);
    lv_obj_set_style_bg_opa(groove, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(groove, lv_color_hex(COLOR_WOOD_EDGE), 0);

    // 鱼口开槽：木鱼正面横向的深色长缝，是木鱼最具辨识度的特征。
    lv_obj_t *slit = plain_obj(s_fish);
    if (slit == NULL) return false;
    lv_obj_set_pos(slit, 14, 60);
    lv_obj_set_size(slit, 122, 14);
    lv_obj_set_style_radius(slit, 7, 0);
    lv_obj_set_style_bg_opa(slit, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(slit, lv_color_hex(COLOR_SLIT), 0);
    // 开槽下沿一道浅色反光，让开槽看起来有深度。
    lv_obj_t *lip = plain_obj(s_fish);
    if (lip == NULL) return false;
    lv_obj_set_pos(lip, 26, 76);
    lv_obj_set_size(lip, 98, 2);
    lv_obj_set_style_bg_opa(lip, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(lip, lv_color_hex(0xFFE0B0), 0);
    return true;
}

// 构建木槌（两条线 + 槌头），初始处于抬起静止位。
static bool build_mallet(void)
{
    s_stick = lv_line_create(s_scr);
    s_grip = lv_line_create(s_scr);
    s_head = plain_obj(s_scr);
    if (s_stick == NULL || s_grip == NULL || s_head == NULL) return false;

    lv_obj_t *lines[2] = { s_stick, s_grip };
    const uint32_t colors[2] = { COLOR_STICK, COLOR_GRIP };
    const int widths[2] = { 7, 11 };
    for (int i = 0; i < 2; i++) {
        lv_obj_remove_style_all(lines[i]);
        lv_obj_set_pos(lines[i], LINE_ORIGIN_X, LINE_ORIGIN_Y);
        lv_obj_set_style_line_width(lines[i], widths[i], 0);
        lv_obj_set_style_line_color(lines[i], lv_color_hex(colors[i]), 0);
        lv_obj_set_style_line_rounded(lines[i], true, 0);
    }

    lv_obj_set_size(s_head, HEAD_D, HEAD_D);
    lv_obj_set_style_radius(s_head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_head, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_head, lv_color_hex(COLOR_HEAD_LIGHT), 0);
    lv_obj_set_style_bg_grad_color(s_head, lv_color_hex(COLOR_HEAD_DARK), 0);
    lv_obj_set_style_bg_grad_dir(s_head, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_head, 2, 0);
    lv_obj_set_style_border_color(s_head, lv_color_hex(COLOR_WOOD_EDGE), 0);

    mallet_pose(0);
    return true;
}

bool muyu_ui_create(void)
{
    if (s_scr != NULL) return true;

    s_scr = lv_obj_create(NULL);
    if (s_scr == NULL) return false;
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COLOR_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(COLOR_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);

    // 注：曾在木鱼后方放一个 200 px 的半透明光晕圆，但大半径圆角遮罩在绘制时要从
    // LVGL 内存池临时分配较大的连续块，24 KB 池碎片化后会断言失败，故去掉。

    // —— 状态栏 ——
    s_sound = make_label(s_scr, muyu_font_text16(), COLOR_MUTED, LV_SYMBOL_VOLUME_MAX);
    s_auto_tag = make_label(s_scr, muyu_font_text16(), COLOR_BG_BOTTOM, "自动");
    s_battery = make_label(s_scr, muyu_font_text16(), COLOR_MUTED, "");
    if (s_sound == NULL || s_auto_tag == NULL || s_battery == NULL) return false;
    lv_obj_set_pos(s_sound, 30, 16);
    lv_obj_set_style_bg_opa(s_auto_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_auto_tag, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_radius(s_auto_tag, 9, 0);
    lv_obj_set_style_pad_hor(s_auto_tag, 7, 0);
    lv_obj_set_style_pad_ver(s_auto_tag, 1, 0);
    lv_obj_add_flag(s_auto_tag, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -28, 16);

    // —— 标题与计数 ——
    lv_obj_t *title = make_label(s_scr, muyu_font_title24(), COLOR_GOLD, "功德");
    s_total = make_label(s_scr, muyu_font_number44(), COLOR_TEXT, "0");
    s_session = make_label(s_scr, muyu_font_text16(), COLOR_MUTED, "本次 +0");
    if (title == NULL || s_total == NULL || s_session == NULL) return false;
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_width(s_total, 224);
    lv_obj_set_style_text_align(s_total, LV_TEXT_ALIGN_CENTER, 0);
    // 单行显示：极端大数也不能折行压到下面的“本次”。
    lv_label_set_long_mode(s_total, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_total, LV_ALIGN_TOP_MID, 0, TOTAL_Y_BIG);
    lv_obj_align(s_session, LV_ALIGN_TOP_MID, 0, 106);

    // —— 木鱼、涟漪、木槌（绘制顺序即层级：后建的在上）——
    if (!build_fish()) return false;

    s_ripple = plain_obj(s_scr);
    if (s_ripple == NULL) return false;
    lv_obj_set_style_radius(s_ripple, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ripple, 2, 0);
    lv_obj_set_style_border_color(s_ripple, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_border_opa(s_ripple, LV_OPA_TRANSP, 0);
    lv_obj_set_size(s_ripple, RIPPLE_MIN_D, RIPPLE_MIN_D);

    if (!build_mallet()) return false;

    // —— 飘字池：初始全透明，敲击时轮流使用 ——
    for (int i = 0; i < FLOAT_POOL; i++) {
        s_floats[i] = make_label(s_scr, muyu_font_title24(), COLOR_GOLD, "");
        if (s_floats[i] == NULL) return false;
        lv_obj_set_style_text_opa(s_floats[i], LV_OPA_TRANSP, 0);
        // x 偏移存在 user_data 里给动画回调用；每次敲击时按文字类型重新设置。
        lv_obj_set_user_data(s_floats[i], (void *)(intptr_t)(FLOAT_X_OFS + FLOAT_JITTER[i]));
        lv_obj_align(s_floats[i], LV_ALIGN_TOP_MID, FLOAT_X_OFS + FLOAT_JITTER[i], FLOAT_START_Y);
    }

    // —— 按键提示 ——
    lv_obj_t *hint = make_label(s_scr, muyu_font_text16(), COLOR_MUTED, "上键自动  下键音量  确定敲");
    if (hint == NULL) return false;
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    lv_screen_load(s_scr);
    return true;
}

void muyu_ui_set_counts(uint32_t total, uint32_t session)
{
    if (s_scr == NULL) return;
    const bool small = muyu_total_needs_small_font(total);
    if (small != s_total_small) {
        s_total_small = small;
        lv_obj_set_style_text_font(s_total, small ? muyu_font_title24() : muyu_font_number44(), 0);
        lv_obj_align(s_total, LV_ALIGN_TOP_MID, 0, small ? TOTAL_Y_SMALL : TOTAL_Y_BIG);
    }
    lv_label_set_text_fmt(s_total, "%lu", (unsigned long)total);
    lv_label_set_text_fmt(s_session, "本次 +%lu", (unsigned long)session);
}

void muyu_ui_strike(const char *float_text, bool milestone)
{
    if (s_scr == NULL) return;
    mallet_pose(1000);
    start_anim(s_stick, mallet_anim_cb, MALLET_ANIM_MS, lv_anim_path_ease_in_out);
    start_anim(s_fish, bounce_anim_cb, BOUNCE_ANIM_MS, lv_anim_path_ease_out);
    start_anim(s_ripple, ripple_anim_cb, RIPPLE_ANIM_MS, lv_anim_path_ease_out);

    if (milestone) {
        // 里程碑文字较长：先收起仍在飘的普通飘字，避免两段文字叠在一起。
        for (int i = 0; i < FLOAT_POOL; i++) {
            lv_anim_delete(s_floats[i], float_anim_cb);
            lv_obj_set_style_text_opa(s_floats[i], LV_OPA_TRANSP, 0);
        }
    }
    const uint8_t idx = s_float_next;
    lv_obj_t *label = s_floats[idx];
    s_float_next = (uint8_t)((idx + 1) % FLOAT_POOL);
    const int32_t x_ofs = milestone ? MILESTONE_X_OFS : FLOAT_X_OFS + FLOAT_JITTER[idx];
    lv_obj_set_user_data(label, (void *)(intptr_t)x_ofs);
    lv_label_set_text_static(label, float_text);
    lv_obj_set_style_text_color(label, lv_color_hex(milestone ? COLOR_GOLD_HOT : COLOR_GOLD), 0);
    // 里程碑文字更长、更重要，飘得更慢一些。
    start_anim(label, float_anim_cb, milestone ? FLOAT_ANIM_MS * 2 : FLOAT_ANIM_MS, lv_anim_path_ease_out);
}

void muyu_ui_set_status(uint8_t volume, bool audio_ok, uint8_t auto_level)
{
    if (s_scr == NULL) return;
    if (volume == 0 || !audio_ok) {
        // 静音档，或音频硬件不可用（红色，提示这不是用户设置造成的）。
        lv_label_set_text_static(s_sound, LV_SYMBOL_MUTE);
    } else {
        lv_label_set_text_fmt(s_sound, LV_SYMBOL_VOLUME_MAX " %u", (unsigned)volume);
    }
    lv_obj_set_style_text_color(s_sound, lv_color_hex(audio_ok ? COLOR_MUTED : COLOR_ALERT), 0);

    if (auto_level != 0) {
        lv_label_set_text_fmt(s_auto_tag, "自动%u", (unsigned)auto_level);
        lv_obj_remove_flag(s_auto_tag, LV_OBJ_FLAG_HIDDEN);
        // 音量文字宽度随档位变化：先刷新布局拿到新宽度，再把标签贴在它右侧。
        lv_obj_update_layout(s_sound);
        lv_obj_align_to(s_auto_tag, s_sound, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    } else {
        lv_obj_add_flag(s_auto_tag, LV_OBJ_FLAG_HIDDEN);
    }
}

void muyu_ui_set_battery(int soc)
{
    if (s_scr == NULL) return;
    if (soc < 0 || soc > 100) {
        // 电量计不可用：显示占位，不隐藏，让用户知道这里本该有电量。
        lv_label_set_text_static(s_battery, LV_SYMBOL_BATTERY_EMPTY " --");
        lv_obj_set_style_text_color(s_battery, lv_color_hex(COLOR_MUTED), 0);
    } else {
        const char *icon = soc >= 80 ? LV_SYMBOL_BATTERY_FULL
                         : soc >= 55 ? LV_SYMBOL_BATTERY_3
                         : soc >= 30 ? LV_SYMBOL_BATTERY_2
                         : soc >= 10 ? LV_SYMBOL_BATTERY_1
                                     : LV_SYMBOL_BATTERY_EMPTY;
        lv_label_set_text_fmt(s_battery, "%s %d%%", icon, soc);
        lv_obj_set_style_text_color(s_battery, lv_color_hex(soc < 20 ? COLOR_ALERT : COLOR_MUTED), 0);
    }
    // 文本宽度变化后重新右对齐。
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -28, 16);
}

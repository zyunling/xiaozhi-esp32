#include "box2_clock_screen.h"
#include "box2_lunar.h"
#include "box2_custom.h"

#include "display.h"

#include <esp_log.h>
#include <ctime>
#include <cstring>

// LVGL built-in montserrat fonts — enabled via sdkconfig for the BOX2 clock-wechat
// build (CONFIG_LV_FONT_MONTSERRAT_*), used for the large clock digits and labels.
// The WiFi / battery icons are drawn with LVGL primitives (arcs / rects) so the
// clock screen has zero dependency on the project's icon-font subset.
extern const lv_font_t lv_font_montserrat_12;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_montserrat_28;
extern const lv_font_t lv_font_montserrat_44;

static const char* TAG = "Box2ClockScreen";

namespace box2 {

// ── Color palette (dark-theme matching target design) ──
static const lv_color_t kBgColor     = lv_color_hex(0x0B1626);
static const lv_color_t kCardBgColor = lv_color_hex(0x1C2742);
static const lv_color_t kCardBorder  = lv_color_hex(0x2A3A5C);
static const lv_color_t kWhite       = lv_color_hex(0xFFFFFF);
static const lv_color_t kOrange      = lv_color_hex(0xFF9F43);
static const lv_color_t kBlue        = lv_color_hex(0x4DA8DA);
static const lv_color_t kDimText     = lv_color_hex(0x8090A8);
static const lv_color_t kCyanIcon    = lv_color_hex(0x4DD4AC);
static const lv_color_t kGreen       = lv_color_hex(0x5BE05B);
static const lv_color_t kRed         = lv_color_hex(0xFF5B5B);
static const lv_color_t kYellow      = lv_color_hex(0xFFD23F);

static const char* kWeekdayZh[] = {"日", "一", "二", "三", "四", "五", "六"};

// ── Singleton ──
ClockScreen& ClockScreen::GetInstance() {
    static ClockScreen instance;
    return instance;
}

// ════════════════════════════════════════════════════════════════
//  Setup — build the entire clock UI tree once (portrait 240x320)
// ════════════════════════════════════════════════════════════════
void ClockScreen::Setup(lv_obj_t* screen, LcdDisplay* display,
                        int standby_brightness, int dim_delay_sec) {
    if (initialized_) return;
    display_ = display;
    screen_parent_ = screen;
    standby_brightness_ = standby_brightness;
    dim_delay_sec_ = dim_delay_sec;

    int sw = LV_HOR_RES;   // 240 (portrait ST7789)
    int sh = LV_VER_RES;   // 320

    // ── Root: full-screen overlay ──
    clock_screen_ = lv_obj_create(screen);
    lv_obj_set_size(clock_screen_, sw, sh);
    lv_obj_set_pos(clock_screen_, 0, 0);
    lv_obj_set_style_radius(clock_screen_, 0, 0);
    lv_obj_set_style_bg_color(clock_screen_, kBgColor, 0);
    lv_obj_set_style_bg_opa(clock_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(clock_screen_, 0, 0);
    lv_obj_set_style_pad_all(clock_screen_, 0, 0);
    lv_obj_set_style_clip_corner(clock_screen_, true, 0);
    lv_obj_add_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(clock_screen_);

    // ── Status bar (top, h=22), flex row SPACE_BETWEEN ──
    lv_obj_t* top_bar = lv_obj_create(clock_screen_);
    lv_obj_set_size(top_bar, sw, 22);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_left(top_bar, 8, 0);
    lv_obj_set_style_pad_right(top_bar, 8, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // ── Left: WiFi icon — hand-drawn arcs (no font dependency) ──
    wifi_container_ = lv_obj_create(top_bar);
    lv_obj_set_size(wifi_container_, 22, 16);
    lv_obj_set_style_bg_opa(wifi_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_container_, 0, 0);
    lv_obj_set_style_pad_all(wifi_container_, 0, 0);

    const int wcx = 11, wcy = 13;
    const int arc_diams[3] = {9, 14, 19};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* a = lv_arc_create(wifi_container_);
        int d = arc_diams[i];
        lv_obj_set_size(a, d, d);
        lv_obj_set_pos(a, wcx - d / 2, wcy - d / 2);
        lv_arc_set_range(a, 0, 100);
        lv_arc_set_value(a, 100);
        lv_arc_set_bg_angles(a, 225, 315);
        lv_arc_set_angles(a, 225, 315);
        lv_obj_set_style_arc_width(a, 2, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, kWhite, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t* dot = lv_obj_create(wifi_container_);
    lv_obj_set_size(dot, 3, 3);
    lv_obj_set_style_radius(dot, 1, 0);
    lv_obj_set_style_bg_color(dot, kWhite, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -1);

    // ── Right group: battery icon + percent + volume bar ──
    lv_obj_t* right_group = lv_obj_create(top_bar);
    lv_obj_set_size(right_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_group, 0, 0);
    lv_obj_set_style_pad_all(right_group, 0, 0);
    lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_group, 6, 0);

    volume_bar_ = lv_bar_create(right_group);
    lv_obj_set_size(volume_bar_, 32, 5);
    lv_bar_set_range(volume_bar_, 0, 100);
    lv_bar_set_value(volume_bar_, 50, LV_ANIM_OFF);
    lv_obj_set_style_radius(volume_bar_, 2, 0);
    lv_obj_set_style_bg_color(volume_bar_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(volume_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(volume_bar_, 0, 0);
    lv_obj_set_style_bg_color(volume_bar_, kBlue, LV_PART_INDICATOR);

    // Battery icon — hand-drawn outline + fill + nub
    battery_container_ = lv_obj_create(right_group);
    lv_obj_set_size(battery_container_, 28, 16);
    lv_obj_set_style_bg_opa(battery_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_container_, 0, 0);
    lv_obj_set_style_pad_all(battery_container_, 0, 0);

    lv_obj_t* body = lv_obj_create(battery_container_);
    lv_obj_set_size(body, 22, 12);
    lv_obj_set_pos(body, 0, 2);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, kCardBorder, 0);
    lv_obj_set_style_border_width(body, 1, 0);

    lv_obj_t* nub = lv_obj_create(battery_container_);
    lv_obj_set_size(nub, 2, 5);
    lv_obj_set_pos(nub, 22, 5);
    lv_obj_set_style_radius(nub, 1, 0);
    lv_obj_set_style_bg_color(nub, kCardBorder, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nub, 0, 0);

    battery_fill_ = lv_obj_create(battery_container_);
    lv_obj_set_size(battery_fill_, 18, 8);
    lv_obj_set_pos(battery_fill_, 2, 4);
    lv_obj_set_style_radius(battery_fill_, 1, 0);
    lv_obj_set_style_bg_color(battery_fill_, kGreen, 0);
    lv_obj_set_style_bg_opa(battery_fill_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_fill_, 0, 0);

    battery_pct_ = lv_label_create(right_group);
    lv_label_set_text(battery_pct_, "88%");
    lv_obj_set_style_text_font(battery_pct_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery_pct_, kWhite, 0);

    // ═══════════════════════════════════════════════════════════
    //  TIME DISPLAY — centered in available vertical space
    //  Portrait 240x320: status bar(22) + margins → ~298px usable
    //  Layout: [HH][colon][MM] on one line, large digits
    // ═══════════════════════════════════════════════════════════
    const int card_w = 72;       // hour/minute card width (fits 44pt digit + padding)
    const int card_h = 80;       // card height
    const int colon_w = 16;
    const int gap = 6;
    const int time_total_w = card_w + gap + colon_w + gap + card_w;  // 172
    const int time_x = (sw - time_total_w) / 2;                     // centered

    // Vertical centering: time block + date(24) + lunar(20) + spacing
    const int content_h = card_h + 12 + 24 + 10 + 20;
    const int time_y = 22 + ((sh - 22) - content_h) / 2;
    const int effective_y = (time_y < 30) ? 30 : time_y;  // minimum top margin after status bar

    // HH card
    card_hh_ = lv_obj_create(clock_screen_);
    lv_obj_set_size(card_hh_, card_w, card_h);
    lv_obj_set_pos(card_hh_, time_x, effective_y);
    lv_obj_set_style_radius(card_hh_, 12, 0);
    lv_obj_set_style_bg_color(card_hh_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(card_hh_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_hh_, kCardBorder, 0);
    lv_obj_set_style_border_width(card_hh_, 1, 0);
    lv_obj_set_style_shadow_width(card_hh_, 6, 0);
    lv_obj_set_style_shadow_color(card_hh_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_hh_, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card_hh_, 0, 0);
    hh_label_ = lv_label_create(card_hh_);
    lv_label_set_text(hh_label_, "08");
    lv_obj_set_style_text_font(hh_label_, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(hh_label_, kWhite, 0);
    lv_obj_align(hh_label_, LV_ALIGN_CENTER, 0, 0);

    // Colon
    colon_label_ = lv_label_create(clock_screen_);
    lv_label_set_text(colon_label_, ":");
    lv_obj_set_style_text_font(colon_label_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(colon_label_, kWhite, 0);
    lv_obj_set_style_text_opa(colon_label_, LV_OPA_60, 0);
    lv_obj_set_pos(colon_label_, time_x + card_w + gap, effective_y + card_h / 2 - 14);

    // MM card
    card_mm_ = lv_obj_create(clock_screen_);
    lv_obj_set_size(card_mm_, card_w, card_h);
    lv_obj_set_pos(card_mm_, time_x + card_w + gap * 2 + colon_w, effective_y);
    lv_obj_set_style_radius(card_mm_, 12, 0);
    lv_obj_set_style_bg_color(card_mm_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(card_mm_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_mm_, kCardBorder, 0);
    lv_obj_set_style_border_width(card_mm_, 1, 0);
    lv_obj_set_style_shadow_width(card_mm_, 6, 0);
    lv_obj_set_style_shadow_color(card_mm_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_mm_, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card_mm_, 0, 0);
    mm_label_ = lv_label_create(card_mm_);
    lv_label_set_text(mm_label_, "48");
    lv_obj_set_style_text_font(mm_label_, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(mm_label_, kOrange, 0);
    lv_obj_align(mm_label_, LV_ALIGN_CENTER, 0, 0);

    // SS label (small, below MM card right edge — no card background to save space)
    ss_label_ = lv_label_create(clock_screen_);
    lv_label_set_text(ss_label_, "31");
    lv_obj_set_style_text_font(ss_label_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ss_label_, kBlue, 0);
    lv_obj_set_pos(ss_label_,
                   time_x + card_w * 2 + gap * 2 + colon_w + card_w - 18,
                   effective_y + card_h - 18);

    // ── Date + Weekday row ──
    const int date_y = effective_y + card_h + 12;
    date_row_ = lv_obj_create(clock_screen_);
    lv_obj_set_style_bg_opa(date_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(date_row_, 0, 0);
    lv_obj_set_style_pad_all(date_row_, 0, 0);
    lv_obj_set_flex_flow(date_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_row_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_row_, 8, 0);
    lv_obj_set_width(date_row_, LV_SIZE_CONTENT);
    lv_obj_set_height(date_row_, LV_SIZE_CONTENT);
    lv_obj_align(date_row_, LV_ALIGN_TOP_MID, 0, date_y);

    date_label_ = lv_label_create(date_row_);
    lv_label_set_text(date_label_, "2026/08/02");
    lv_obj_set_style_text_font(date_label_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(date_label_, kWhite, 0);

    weekday_tag_ = lv_obj_create(date_row_);
    lv_obj_set_size(weekday_tag_, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_radius(weekday_tag_, 5, 0);
    lv_obj_set_style_bg_color(weekday_tag_, kBlue, 0);
    lv_obj_set_style_bg_opa(weekday_tag_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weekday_tag_, 0, 0);
    lv_obj_set_style_pad_left(weekday_tag_, 6, 0);
    lv_obj_set_style_pad_right(weekday_tag_, 6, 0);
    weekday_label_ = lv_label_create(weekday_tag_);
    lv_label_set_text(weekday_label_, "周六");
    lv_obj_set_style_text_font(weekday_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(weekday_label_, kWhite, 0);
    lv_obj_align(weekday_label_, LV_ALIGN_CENTER, 0, 0);

    // ── Lunar calendar row (optional) ──
#ifdef CONFIG_USE_LUNAR_STANDBY
    const int lunar_y = date_y + 26;
    lunar_row_ = lv_obj_create(clock_screen_);
    lv_obj_set_style_bg_opa(lunar_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lunar_row_, 0, 0);
    lv_obj_set_style_pad_all(lunar_row_, 0, 0);
    lv_obj_set_flex_flow(lunar_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lunar_row_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(lunar_row_, 4, 0);
    lv_obj_set_width(lunar_row_, LV_SIZE_CONTENT);
    lv_obj_set_height(lunar_row_, LV_SIZE_CONTENT);
    lv_obj_align(lunar_row_, LV_ALIGN_TOP_MID, 0, lunar_y);

    lunar_label_ = lv_label_create(lunar_row_);
    lv_label_set_text(lunar_label_, "农历：戊申年闰六月初九");
    lv_obj_set_style_text_font(lunar_label_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lunar_label_, kDimText, 0);
#else
    lunar_row_   = nullptr;
    lunar_label_ = nullptr;
#endif

    // ── Dim timer ──
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &ClockScreen::DimTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "box2_dim";
    esp_timer_create(&timer_args, &dim_timer_);

    initialized_ = true;
    ESP_LOGI(TAG, "Clock screen OK (%dx%d, bright=%d, dim=%ds, lunar=%d)",
             sw, sh, standby_brightness_, dim_delay_sec_,
#ifdef CONFIG_USE_LUNAR_STANDBY
             1);
#else
             0);
#endif
}

// ════════════════════════════════════════════════════════════════
//  Show / Hide / Poke
// ════════════════════════════════════════════════════════════════
void ClockScreen::Show() {
    if (!initialized_) return;
    visible_ = true;
    lv_obj_clear_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    Update(true);
    StartDimTimer();
    ESP_LOGI(TAG, "Shown");
}

void ClockScreen::Hide() {
    if (!initialized_) return;
    visible_ = false;
    lv_obj_add_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    StopDimTimer();
    ESP_LOGI(TAG, "Hidden");
}

void ClockScreen::Poke() {
    if (!initialized_ || !visible_) return;
    StartDimTimer();
}

void ClockScreen::Update(bool force) {
    if (!initialized_) return;
    if (!force && !visible_) return;
    UpdateTime();
}

// ════════════════════════════════════════════════════════════════
//  Setters for status-bar indicators
// ════════════════════════════════════════════════════════════════
void ClockScreen::SetBattery(int level, int charging) {
    if (!initialized_) return;

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", level);
    lv_label_set_text(battery_pct_, pct);

    // Resize fill rect (body interior is ~18px wide).
    int w = 2 + (level * 16) / 100;   // 2..18
    if (w > 18) w = 18;
    if (w < 2)  w = 2;
    lv_obj_set_width(battery_fill_, w);

    lv_color_t c = kGreen;
    if (charging)     c = kYellow;
    else if (level <= 5) c = kRed;
    lv_obj_set_style_bg_color(battery_fill_, c, 0);
}

void ClockScreen::SetWifiIcon(const char* icon) {
    // Hand-drawn WiFi arcs — only toggle opacity based on connected state.
    if (!initialized_ || !wifi_container_) return;
    bool connected = (icon != nullptr && icon[0] != '\0');
    lv_obj_set_style_opa(wifi_container_, connected ? LV_OPA_COVER : LV_OPA_40, 0);
}

void ClockScreen::SetVolume(int volume) {
    if (!initialized_ || !volume_bar_) return;
    int v = (volume < 0) ? 0 : (volume > 100 ? 100 : volume);
    lv_bar_set_value(volume_bar_, v, LV_ANIM_OFF);
}

void ClockScreen::SetDimCallback(box2_standby_cb_t cb, void* user_data) {
    dim_callback_ = cb;
    dim_cb_user_data_ = user_data;
}

// ════════════════════════════════════════════════════════════════
//  Update time/date/lunar — called every second
// ════════════════════════════════════════════════════════════════
void ClockScreen::UpdateTime() {
    time_t now = time(nullptr);
    if (now < 0) return;
    std::tm* tm = localtime(&now);
    if (!tm) return;

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", tm->tm_hour);
    lv_label_set_text(hh_label_, buf);
    snprintf(buf, sizeof(buf), "%02d", tm->tm_min);
    lv_label_set_text(mm_label_, buf);
    snprintf(buf, sizeof(buf), "%02d", tm->tm_sec);
    lv_label_set_text(ss_label_, buf);

    char date_buf[32];
    snprintf(date_buf, sizeof(date_buf), "%04d/%02d/%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    lv_label_set_text(date_label_, date_buf);

    char wd[16];
    snprintf(wd, sizeof(wd), "周%s", kWeekdayZh[tm->tm_wday]);
    lv_label_set_text(weekday_label_, wd);

#ifdef CONFIG_USE_LUNAR_STANDBY
    UpdateLunar(tm);
#endif
}

#ifdef CONFIG_USE_LUNAR_STANDBY
void ClockScreen::UpdateLunar(std::tm* tm) {
    if (!lunar_label_) return;

    int year  = tm->tm_year + 1900;
    int month = tm->tm_mon + 1;
    int day   = tm->tm_mday;

    uint32_t lunar = box2_solar_to_lunar(year, month, day);
    if (lunar == 0) {
        lv_label_set_text(lunar_label_, "");
        return;
    }

    int ly = (lunar >> 16) & 0xFFFF;
    int lm = (lunar >> 8)  & 0xFF;
    int ld = lunar & 0xFF;

    char ganzhi[16];
    box2_format_lunar_ganzhi(ly, ganzhi, sizeof(ganzhi));

    char ldate[32];
    box2_format_lunar_date(ly, lm, ld, ldate, sizeof(ldate));

    char full[64];
    snprintf(full, sizeof(full), "农历：%s%s", ganzhi, ldate);
    lv_label_set_text(lunar_label_, full);
}
#endif

// ════════════════════════════════════════════════════════════════
//  Dim timer — auto-dim after inactivity
// ════════════════════════════════════════════════════════════════
void ClockScreen::StartDimTimer() {
    if (!dim_timer_) return;
    StopDimTimer();
    esp_timer_start_once(dim_timer_, (uint64_t)dim_delay_sec_ * 1000000ULL);
}

void ClockScreen::StopDimTimer() {
    if (!dim_timer_) return;
    esp_timer_stop(dim_timer_);
}

void ClockScreen::DimTimerCallback(void* arg) {
    auto* self = static_cast<ClockScreen*>(arg);
    if (!self || !self->visible_) return;
    if (self->dim_callback_)
        self->dim_callback_(self->dim_cb_user_data_);
}

// ════════════════════════════════════════════════════════════════
//  Destroy
// ════════════════════════════════════════════════════════════════
void ClockScreen::Destroy() {
    if (dim_timer_) {
        esp_timer_stop(dim_timer_);
        esp_timer_delete(dim_timer_);
        dim_timer_ = nullptr;
    }
    if (clock_screen_) {
        lv_obj_del(clock_screen_);
        clock_screen_ = nullptr;
    }
    initialized_ = false;
    visible_ = false;
}

}  // namespace box2

// ════════════════════════════════════════════════════════════════
//  C interface (called from lcd_display.cc)
// ════════════════════════════════════════════════════════════════
extern "C" {

void box2_clock_setup(lv_obj_t* screen, void* display,
                     int standby_brightness, int dim_delay_sec) {
    auto* d = reinterpret_cast<LcdDisplay*>(display);
    box2::ClockScreen::GetInstance().Setup(screen, d,
                                           standby_brightness, dim_delay_sec);
}
void box2_clock_show(void)        { box2::ClockScreen::GetInstance().Show(); }
void box2_clock_hide(void)        { box2::ClockScreen::GetInstance().Hide(); }
void box2_clock_update(int force) { box2::ClockScreen::GetInstance().Update(force != 0); }
void box2_clock_poke(void)        { box2::ClockScreen::GetInstance().Poke(); }
void box2_clock_destroy(void)     { box2::ClockScreen::GetInstance().Destroy(); }
void box2_clock_set_battery(int level, int charging) {
    box2::ClockScreen::GetInstance().SetBattery(level, charging);
}
void box2_clock_set_wifi_icon(const char* icon) {
    box2::ClockScreen::GetInstance().SetWifiIcon(icon);
}
void box2_clock_set_volume(int volume) {
    box2::ClockScreen::GetInstance().SetVolume(volume);
}
void box2_clock_set_dim_callback(box2_standby_cb_t cb, void* user_data) {
    box2::ClockScreen::GetInstance().SetDimCallback(cb, user_data);
}

}  // extern "C"

#include "box2_clock_screen.h"
#include "box2_lunar.h"
#include "box2_custom.h"

#include "display.h"
#include "lvgl_display/lvgl_theme.h"
#include "lvgl_display/lvgl_font.h"

#include <esp_log.h>
#include <ctime>
#include <cstring>

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_4);

static const char* TAG = "Box2ClockScreen";

namespace box2 {

static const lv_color_t kBgColor = lv_color_hex(0x0B1626);
static const lv_color_t kCardBgColor = lv_color_hex(0x1C2742);
static const lv_color_t kCardBorderColor = lv_color_hex(0x2A3A5C);
static const lv_color_t kWhiteColor = lv_color_hex(0xFFFFFF);
static const lv_color_t kOrangeColor = lv_color_hex(0xFF9F43);
static const lv_color_t kBlueColor = lv_color_hex(0x4DA8DA);
static const lv_color_t kDimTextColor = lv_color_hex(0x8090A8);
static const lv_color_t kCyanColor = lv_color_hex(0x4DD4AC);
static const lv_color_t kGreenColor = lv_color_hex(0x5BE05B);
static const lv_color_t kRedColor = lv_color_hex(0xFF5B5B);
static const lv_color_t kYellowColor = lv_color_hex(0xFFD23F);

static const char* kWeekdayZh[] = {"日", "一", "二", "三", "四", "五", "六"};

ClockScreen& ClockScreen::GetInstance() {
    static ClockScreen instance;
    return instance;
}

void ClockScreen::Setup(lv_obj_t* screen, LcdDisplay* display,
                        int standby_brightness, int dim_delay_sec) {
    if (initialized_) return;
    display_ = display;
    screen_parent_ = screen;
    standby_brightness_ = standby_brightness;
    dim_delay_sec_ = dim_delay_sec;

    auto* theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (theme == nullptr) {
        auto& tm = LvglThemeManager::GetInstance();
        theme = tm.GetTheme("light");
    }
    if (theme == nullptr) {
        ESP_LOGE(TAG, "No theme available for clock screen");
        return;
    }

    auto* text_font = theme->text_font()->font();
    auto* icon_font = theme->icon_font()->font();

    clock_screen_ = lv_obj_create(screen);
    lv_obj_set_size(clock_screen_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(clock_screen_, 0, 0);
    lv_obj_set_style_radius(clock_screen_, 0, 0);
    lv_obj_set_style_bg_color(clock_screen_, kBgColor, 0);
    lv_obj_set_style_bg_opa(clock_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(clock_screen_, 0, 0);
    lv_obj_set_style_pad_all(clock_screen_, 0, 0);
    lv_obj_set_style_clip_corner(clock_screen_, true, 0);
    lv_obj_add_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(clock_screen_);

    // Top bar: WiFi (left) | Volume (center) | Battery (right)
    lv_obj_t* top_bar = lv_obj_create(clock_screen_);
    lv_obj_set_size(top_bar, LV_HOR_RES, 32);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_set_style_pad_left(top_bar, 12, 0);
    lv_obj_set_style_pad_right(top_bar, 12, 0);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    wifi_icon_ = lv_label_create(top_bar);
    lv_label_set_text(wifi_icon_, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon_, icon_font, 0);
    lv_obj_set_style_text_color(wifi_icon_, kCyanColor, 0);

    volume_icon_ = lv_label_create(top_bar);
    lv_label_set_text(volume_icon_, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(volume_icon_, icon_font, 0);
    lv_obj_set_style_text_color(volume_icon_, kWhiteColor, 0);

    volume_bar_ = lv_bar_create(top_bar);
    lv_obj_set_size(volume_bar_, 50, 6);
    lv_bar_set_range(volume_bar_, 0, 100);
    lv_bar_set_value(volume_bar_, 50, LV_ANIM_OFF);
    lv_obj_set_style_radius(volume_bar_, 3, 0);
    lv_obj_set_style_bg_color(volume_bar_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(volume_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(volume_bar_, 0, 0);
    lv_obj_set_style_radius(volume_bar_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_bar_, kBlueColor, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(volume_bar_, LV_OPA_COVER, LV_PART_INDICATOR);

    battery_icon_ = lv_label_create(top_bar);
    lv_label_set_text(battery_icon_, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(battery_icon_, icon_font, 0);
    lv_obj_set_style_text_color(battery_icon_, kGreenColor, 0);

    battery_pct_ = lv_label_create(top_bar);
    lv_label_set_text(battery_pct_, "100%");
    lv_obj_set_style_text_font(battery_pct_, text_font, 0);
    lv_obj_set_style_text_color(battery_pct_, kWhiteColor, 0);

    // Time area: HH card : MM card . SS card
    lv_obj_t* time_wrap = lv_obj_create(clock_screen_);
    lv_obj_set_size(time_wrap, LV_HOR_RES, 120);
    lv_obj_set_pos(time_wrap, 0, 40);
    lv_obj_set_style_bg_opa(time_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_wrap, 0, 0);
    lv_obj_set_style_pad_all(time_wrap, 0, 0);
    lv_obj_set_style_clip_corner(time_wrap, true, 0);
    lv_obj_set_flex_flow(time_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_wrap, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_wrap, 6, 0);

    // HH card
    card_hh_ = lv_obj_create(time_wrap);
    lv_obj_set_size(card_hh_, 70, 90);
    lv_obj_set_style_radius(card_hh_, 16, 0);
    lv_obj_set_style_bg_color(card_hh_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(card_hh_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_hh_, kCardBorderColor, 0);
    lv_obj_set_style_border_width(card_hh_, 1, 0);
    lv_obj_set_style_shadow_width(card_hh_, 8, 0);
    lv_obj_set_style_shadow_color(card_hh_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_hh_, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(card_hh_, 0, 0);
    lv_obj_set_flex_flow(card_hh_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card_hh_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    hh_label_ = lv_label_create(card_hh_);
    lv_label_set_text(hh_label_, "08");
    lv_obj_set_style_text_font(hh_label_, text_font, 0);
    lv_obj_set_style_text_color(hh_label_, kWhiteColor, 0);
    lv_obj_set_style_transform_zoom(hh_label_, 384, 0);
    lv_obj_set_style_transform_pivot_x(hh_label_, 50, 0);
    lv_obj_set_style_transform_pivot_y(hh_label_, 50, 0);

    // Colon
    colon_label_ = lv_label_create(time_wrap);
    lv_label_set_text(colon_label_, ":");
    lv_obj_set_style_text_font(colon_label_, text_font, 0);
    lv_obj_set_style_text_color(colon_label_, kWhiteColor, 0);
    lv_obj_set_style_text_opa(colon_label_, LV_OPA_60, 0);
    lv_obj_set_style_transform_zoom(colon_label_, 384, 0);
    lv_obj_set_style_transform_pivot_x(colon_label_, 50, 0);
    lv_obj_set_style_transform_pivot_y(colon_label_, 50, 0);

    // MM card
    card_mm_ = lv_obj_create(time_wrap);
    lv_obj_set_size(card_mm_, 70, 90);
    lv_obj_set_style_radius(card_mm_, 16, 0);
    lv_obj_set_style_bg_color(card_mm_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(card_mm_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_mm_, kCardBorderColor, 0);
    lv_obj_set_style_border_width(card_mm_, 1, 0);
    lv_obj_set_style_shadow_width(card_mm_, 8, 0);
    lv_obj_set_style_shadow_color(card_mm_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_mm_, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(card_mm_, 0, 0);
    lv_obj_set_flex_flow(card_mm_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card_mm_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mm_label_ = lv_label_create(card_mm_);
    lv_label_set_text(mm_label_, "48");
    lv_obj_set_style_text_font(mm_label_, text_font, 0);
    lv_obj_set_style_text_color(mm_label_, kOrangeColor, 0);
    lv_obj_set_style_transform_zoom(mm_label_, 384, 0);
    lv_obj_set_style_transform_pivot_x(mm_label_, 50, 0);
    lv_obj_set_style_transform_pivot_y(mm_label_, 50, 0);

    // SS card (smaller)
    card_ss_ = lv_obj_create(time_wrap);
    lv_obj_set_size(card_ss_, 52, 90);
    lv_obj_set_style_radius(card_ss_, 16, 0);
    lv_obj_set_style_bg_color(card_ss_, kCardBgColor, 0);
    lv_obj_set_style_bg_opa(card_ss_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_ss_, kCardBorderColor, 0);
    lv_obj_set_style_border_width(card_ss_, 1, 0);
    lv_obj_set_style_shadow_width(card_ss_, 8, 0);
    lv_obj_set_style_shadow_color(card_ss_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card_ss_, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(card_ss_, 0, 0);
    lv_obj_set_flex_flow(card_ss_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card_ss_, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ss_label_ = lv_label_create(card_ss_);
    lv_label_set_text(ss_label_, "31");
    lv_obj_set_style_text_font(ss_label_, text_font, 0);
    lv_obj_set_style_text_color(ss_label_, kBlueColor, 0);
    lv_obj_set_style_transform_zoom(ss_label_, 307, 0);
    lv_obj_set_style_transform_pivot_x(ss_label_, 50, 0);
    lv_obj_set_style_transform_pivot_y(ss_label_, 50, 0);

    // Date area: "2026/07/29" + blue tag "星期三"
    lv_obj_t* date_wrap = lv_obj_create(clock_screen_);
    lv_obj_set_size(date_wrap, LV_HOR_RES, 36);
    lv_obj_set_pos(date_wrap, 0, 160);
    lv_obj_set_style_bg_opa(date_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(date_wrap, 0, 0);
    lv_obj_set_style_pad_all(date_wrap, 0, 0);
    lv_obj_set_flex_flow(date_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_wrap, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_wrap, 12, 0);

    date_label_ = lv_label_create(date_wrap);
    lv_label_set_text(date_label_, "2026/07/29");
    lv_obj_set_style_text_font(date_label_, text_font, 0);
    lv_obj_set_style_text_color(date_label_, kWhiteColor, 0);
    lv_obj_set_style_transform_zoom(date_label_, 204, 0);
    lv_obj_set_style_transform_pivot_x(date_label_, 50, 0);
    lv_obj_set_style_transform_pivot_y(date_label_, 50, 0);

    weekday_tag_ = lv_obj_create(date_wrap);
    lv_obj_set_style_radius(weekday_tag_, 8, 0);
    lv_obj_set_style_bg_color(weekday_tag_, kBlueColor, 0);
    lv_obj_set_style_bg_opa(weekday_tag_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(weekday_tag_, 0, 0);
    lv_obj_set_style_pad_left(weekday_tag_, 10, 0);
    lv_obj_set_style_pad_right(weekday_tag_, 10, 0);
    lv_obj_set_style_pad_top(weekday_tag_, 3, 0);
    lv_obj_set_style_pad_bottom(weekday_tag_, 3, 0);

    weekday_label_ = lv_label_create(weekday_tag_);
    lv_label_set_text(weekday_label_, "星期三");
    lv_obj_set_style_text_font(weekday_label_, text_font, 0);
    lv_obj_set_style_text_color(weekday_label_, kWhiteColor, 0);

    // Lunar area: star icon + "农历：戊申年六月十五"
    lv_obj_t* lunar_wrap = lv_obj_create(clock_screen_);
    lv_obj_set_size(lunar_wrap, LV_HOR_RES, 30);
    lv_obj_set_pos(lunar_wrap, 0, 200);
    lv_obj_set_style_bg_opa(lunar_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lunar_wrap, 0, 0);
    lv_obj_set_style_pad_all(lunar_wrap, 0, 0);
    lv_obj_set_flex_flow(lunar_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lunar_wrap, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(lunar_wrap, 8, 0);

    star_icon_ = lv_label_create(lunar_wrap);
    lv_label_set_text(star_icon_, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(star_icon_, &font_material_symbols_30_4, 0);
    lv_obj_set_style_text_color(star_icon_, kYellowColor, 0);

    lunar_label_ = lv_label_create(lunar_wrap);
    lv_label_set_text(lunar_label_, "农历：戊申年六月十五");
    lv_obj_set_style_text_font(lunar_label_, text_font, 0);
    lv_obj_set_style_text_color(lunar_label_, kDimTextColor, 0);

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &ClockScreen::DimTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "box2_dim";
    esp_timer_create(&timer_args, &dim_timer_);

    initialized_ = true;
    ESP_LOGI(TAG, "Clock screen initialized (brightness=%d, delay=%ds)",
             standby_brightness_, dim_delay_sec_);
}

void ClockScreen::Show() {
    if (!initialized_) return;
    visible_ = true;
    lv_obj_clear_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    Update(true);
    StartDimTimer();
    ESP_LOGI(TAG, "Clock screen shown");
}

void ClockScreen::Hide() {
    if (!initialized_) return;
    visible_ = false;
    lv_obj_add_flag(clock_screen_, LV_OBJ_FLAG_HIDDEN);
    StopDimTimer();
    ESP_LOGI(TAG, "Clock screen hidden");
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

void ClockScreen::SetBattery(int level, int charging) {
    if (!initialized_) return;

    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d%%", level);
    lv_label_set_text(battery_pct_, pct_str);

    const char* icon = LV_SYMBOL_BATTERY_FULL;
    lv_color_t color = kGreenColor;
    if (charging) {
        icon = LV_SYMBOL_POWER;
        color = kYellowColor;
    } else if (level <= 5) {
        icon = LV_SYMBOL_BATTERY_EMPTY;
        color = kRedColor;
    } else if (level <= 20) {
        icon = LV_SYMBOL_BATTERY_1;
        color = kRedColor;
    } else if (level <= 50) {
        icon = LV_SYMBOL_BATTERY_2;
        color = kYellowColor;
    } else if (level <= 80) {
        icon = LV_SYMBOL_BATTERY_3;
        color = kGreenColor;
    }
    lv_label_set_text(battery_icon_, icon);
    lv_obj_set_style_text_color(battery_icon_, color, 0);
}

void ClockScreen::SetWifiIcon(const char* icon) {
    if (!initialized_) return;
    lv_label_set_text(wifi_icon_, icon ? icon : LV_SYMBOL_WIFI);
}

void ClockScreen::SetDimCallback(box2_standby_cb_t cb, void* user_data) {
    dim_callback_ = cb;
    dim_cb_user_data_ = user_data;
}

void ClockScreen::SetVolume(int volume) {
    if (!initialized_ || volume_bar_ == nullptr) return;

    lv_bar_set_value(volume_bar_, volume, LV_ANIM_OFF);

    if (volume == 0) {
        lv_label_set_text(volume_icon_, LV_SYMBOL_MUTE);
        lv_obj_set_style_text_color(volume_icon_, kDimTextColor, 0);
    } else if (volume < 50) {
        lv_label_set_text(volume_icon_, LV_SYMBOL_VOLUME_MID);
        lv_obj_set_style_text_color(volume_icon_, kYellowColor, 0);
    } else {
        lv_label_set_text(volume_icon_, LV_SYMBOL_VOLUME_MAX);
        lv_obj_set_style_text_color(volume_icon_, kCyanColor, 0);
    }
}

void ClockScreen::UpdateTime() {
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);

    char hh_str[8], mm_str[8], ss_str[8];
    snprintf(hh_str, sizeof(hh_str), "%02d", tm->tm_hour);
    snprintf(mm_str, sizeof(mm_str), "%02d", tm->tm_min);
    snprintf(ss_str, sizeof(ss_str), "%02d", tm->tm_sec);

    lv_label_set_text(hh_label_, hh_str);
    lv_label_set_text(mm_label_, mm_str);
    lv_label_set_text(ss_label_, ss_str);

    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    lv_label_set_text(date_label_, date_str);

    char weekday_str[16];
    snprintf(weekday_str, sizeof(weekday_str), "星期%s", kWeekdayZh[tm->tm_wday]);
    lv_label_set_text(weekday_label_, weekday_str);

    UpdateLunar();
}

void ClockScreen::UpdateLunar() {
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    int year = tm->tm_year + 1900;
    int month = tm->tm_mon + 1;
    int day = tm->tm_mday;

    uint32_t lunar = box2_solar_to_lunar(year, month, day);
    if (lunar == 0) return;

    int lunar_year = (lunar >> 16) & 0xFFFF;
    int lunar_month = (lunar >> 8) & 0xFF;
    int lunar_day = lunar & 0xFF;

    char ganzhi[16];
    box2_format_lunar_ganzhi(lunar_year, ganzhi, sizeof(ganzhi));

    char date_str[32];
    box2_format_lunar_date(lunar_year, lunar_month, lunar_day,
                           date_str, sizeof(date_str));

    char full_str[64];
    snprintf(full_str, sizeof(full_str), "农历：%s%s", ganzhi, date_str);
    lv_label_set_text(lunar_label_, full_str);
}

void ClockScreen::StartDimTimer() {
    if (dim_timer_ == nullptr) return;
    StopDimTimer();
    esp_timer_start_once(dim_timer_, dim_delay_sec_ * 1000000ULL);
}

void ClockScreen::StopDimTimer() {
    if (dim_timer_ == nullptr) return;
    esp_timer_stop(dim_timer_);
}

void ClockScreen::DimTimerCallback(void* arg) {
    auto* self = static_cast<ClockScreen*>(arg);
    if (self == nullptr || !self->visible_) return;
    if (self->dim_callback_ != nullptr) {
        self->dim_callback_(self->dim_cb_user_data_);
    }
}

void ClockScreen::Destroy() {
    if (dim_timer_ != nullptr) {
        esp_timer_stop(dim_timer_);
        esp_timer_delete(dim_timer_);
        dim_timer_ = nullptr;
    }
    if (clock_screen_ != nullptr) {
        lv_obj_del(clock_screen_);
        clock_screen_ = nullptr;
    }
    initialized_ = false;
    visible_ = false;
}

}  // namespace box2

extern "C" {

void box2_clock_setup(lv_obj_t* screen, void* display,
                     int standby_brightness, int dim_delay_sec) {
    auto* d = reinterpret_cast<LcdDisplay*>(display);
    box2::ClockScreen::GetInstance().Setup(screen, d,
                                           standby_brightness, dim_delay_sec);
}

void box2_clock_show(void) {
    box2::ClockScreen::GetInstance().Show();
}

void box2_clock_hide(void) {
    box2::ClockScreen::GetInstance().Hide();
}

void box2_clock_update(int force) {
    box2::ClockScreen::GetInstance().Update(force != 0);
}

void box2_clock_poke(void) {
    box2::ClockScreen::GetInstance().Poke();
}

void box2_clock_destroy(void) {
    box2::ClockScreen::GetInstance().Destroy();
}

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

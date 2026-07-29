#ifndef BOX2_CLOCK_SCREEN_H
#define BOX2_CLOCK_SCREEN_H

#include <lvgl.h>
#include <esp_timer.h>

typedef void (*box2_standby_cb_t)(void* user_data);

class LcdDisplay;

namespace box2 {

class ClockScreen {
public:
    static ClockScreen& GetInstance();

    void Setup(lv_obj_t* screen, LcdDisplay* display,
               int standby_brightness, int dim_delay_sec);
    void Show();
    void Hide();
    void Update(bool force);
    void Poke();
    void Destroy();

    void SetBattery(int level, int charging);
    void SetWifiIcon(const char* icon);
    void SetVolume(int volume);
    void SetDimCallback(box2_standby_cb_t cb, void* user_data);

    int GetStandbyBrightness() const { return standby_brightness_; }

    bool IsVisible() const { return visible_; }

private:
    ClockScreen() = default;
    ~ClockScreen() = default;
    ClockScreen(const ClockScreen&) = delete;
    ClockScreen& operator=(const ClockScreen&) = delete;

    void UpdateTime();
    void UpdateLunar();

    void StartDimTimer();
    void StopDimTimer();

    static void DimTimerCallback(void* arg);

    LcdDisplay* display_ = nullptr;
    lv_obj_t* screen_parent_ = nullptr;

    int standby_brightness_ = 50;
    int dim_delay_sec_ = 10;
    bool visible_ = false;
    bool initialized_ = false;

    box2_standby_cb_t dim_callback_ = nullptr;
    void* dim_cb_user_data_ = nullptr;

    // Timing
    esp_timer_handle_t dim_timer_ = nullptr;

    // Main screen
    lv_obj_t* clock_screen_ = nullptr;

    // Top bar elements
    lv_obj_t* wifi_icon_ = nullptr;
    lv_obj_t* volume_icon_ = nullptr;
    lv_obj_t* volume_bar_ = nullptr;
    lv_obj_t* battery_icon_ = nullptr;
    lv_obj_t* battery_pct_ = nullptr;

    // Time area
    lv_obj_t* card_hh_ = nullptr;
    lv_obj_t* hh_label_ = nullptr;
    lv_obj_t* colon_label_ = nullptr;
    lv_obj_t* card_mm_ = nullptr;
    lv_obj_t* mm_label_ = nullptr;
    lv_obj_t* card_ss_ = nullptr;
    lv_obj_t* ss_label_ = nullptr;

    // Date area
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* weekday_tag_ = nullptr;
    lv_obj_t* weekday_label_ = nullptr;

    // Lunar area
    lv_obj_t* lunar_label_ = nullptr;
    lv_obj_t* star_icon_ = nullptr;
};

}  // namespace box2

#endif  // BOX2_CLOCK_SCREEN_H

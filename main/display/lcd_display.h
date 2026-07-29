#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <atomic>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles

    // Clock standby screen members
    lv_obj_t* clock_screen_ = nullptr;        // Full clock standby screen container
    lv_obj_t* clock_time_label_ = nullptr;    // HH:MM time display
    lv_obj_t* clock_seconds_label_ = nullptr; // SS seconds display
    lv_obj_t* clock_weekday_label_ = nullptr; // Weekday label
    lv_obj_t* clock_date_label_ = nullptr;    // Date label (e.g. 2026/07/29)
    lv_obj_t* clock_lunar_label_ = nullptr;   // Lunar date label
    lv_obj_t* clock_battery_label_ = nullptr; // Battery icon
    lv_obj_t* clock_battery_percent_ = nullptr; // Battery percentage text
    lv_obj_t* clock_wifi_label_ = nullptr;    // WiFi status icon
    lv_obj_t* clock_volume_label_ = nullptr;  // Volume icon+value
    lv_obj_t* clock_status_label_ = nullptr;  // Center status text (muted, etc.)

    uint8_t standby_brightness_ = 50;          // Standby backlight brightness (after dim delay)
    esp_timer_handle_t standby_dim_timer_ = nullptr; // One-shot timer for delayed dimming
    int standby_dim_delay_sec_ = 10;           // Seconds before dimming in standby

    void InitializeLcdThemes();
    virtual void UpdateStatusBar(bool update_all = false) override;
#if CONFIG_USE_CLOCK_STANDBY_SCREEN
    void SetupClockStandbyScreen();
    virtual void ShowClockStandbyScreen() override;
    virtual void HideClockStandbyScreen() override;
    void UpdateClockScreen(bool force = false);
    void SetStandbyBrightness(uint8_t pct);
    void StartStandbyDimTimer();
    void StopStandbyDimTimer();
#endif
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height);

public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;

    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);

#if CONFIG_USE_CLOCK_STANDBY_SCREEN
    virtual void PokeStandbyDisplay() override;
#endif
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                  int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                  bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
};

#endif  // LCD_DISPLAY_H

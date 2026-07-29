#ifndef BOX2_CUSTOM_H
#define BOX2_CUSTOM_H

#include <lvgl.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*box2_standby_cb_t)(void* user_data);

void box2_clock_setup(lv_obj_t* screen, void* display,
                     int standby_brightness, int dim_delay_sec);

void box2_clock_show(void);
void box2_clock_hide(void);
void box2_clock_update(int force);
void box2_clock_poke(void);
void box2_clock_destroy(void);

void box2_clock_set_battery(int level, int charging);
void box2_clock_set_wifi_icon(const char* icon);
void box2_clock_set_volume(int volume);

void box2_clock_set_dim_callback(box2_standby_cb_t cb, void* user_data);

uint32_t box2_solar_to_lunar(int year, int month, int day);
void box2_format_lunar_date(int year, int month, int day,
                            char* buffer, size_t size);
void box2_format_lunar_ganzhi(int lunar_year, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // BOX2_CUSTOM_H

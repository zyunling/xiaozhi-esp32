#ifndef BOX2_LUNAR_H
#define BOX2_LUNAR_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t box2_solar_to_lunar(int year, int month, int day);
void box2_format_lunar_date(int year, int month, int day,
                             char* buffer, size_t size);
void box2_format_lunar_ganzhi(int lunar_year, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // BOX2_LUNAR_H

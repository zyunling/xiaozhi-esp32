#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Convert solar date (year, month, day) to lunar date.
// year: 1900-2100
// Returns: lunar year (high 16 bits), month (8 bits), day (8 bits)
//          or 0 on error / out of range
uint32_t SolarToLunar(int year, int month, int day);

// Get lunar date string like "六月十五"
// buffer must be at least 32 bytes
void FormatLunarDate(int year, int month, int day, char* buffer, size_t size);

// Get lunar year GanZhi (干支) string, e.g. "乙巳年"
void FormatLunarYearGanZhi(int lunar_year, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // LUNAR_CALENDAR_H

#include "lunar_calendar.h"
#include <cstring>
#include <ctime>

// Lunar data table for years 1900-2100.
// Each uint32_t entry encodes:
//   bits  0-11 : 12 normal months big/small (1=30 days, 0=29 days)
//   bits 12-15 : leap month (0=none, 1-12=leap month)
//   bit     16 : leap month size (1=30 days, 0=29 days)
//   bits 17-21 : spring festival day (1-31)
//   bits 22-25 : spring festival month (1 or 2)
static const uint32_t lunar_info[] = {
    0x007e8b52, 0x00a60752, 0x00900ea5, 0x007a5b2a, 0x00a0064b, 0x00880a9b, 0x00734aa6, 0x009a056a,
    0x00840b59, 0x006c2ba8, 0x00940752, 0x007c6d85, 0x00a40b25, 0x008c0a4b, 0x00755a4b, 0x009c02ad,
    0x0086056b, 0x006e25b5, 0x00960da9, 0x00837e92, 0x00a80e92, 0x00900d25, 0x00785d2d, 0x00a00a56,
    0x008a02b6, 0x00714ad5, 0x009a06d4, 0x00840ea9, 0x006e2f48, 0x00940e92, 0x007c6686, 0x00a2052b,
    0x008c0a57, 0x00755946, 0x009c0b5a, 0x008806d4, 0x00713761, 0x00960749, 0x007f7b13, 0x00a60a93,
    0x0090052b, 0x0077651b, 0x009e0aad, 0x008a056a, 0x00734da5, 0x009a0ba4, 0x00840b49, 0x006c2d49,
    0x00940a95, 0x007a7aad, 0x00a20536, 0x008c0aad, 0x00775aca, 0x009c05b2, 0x00860da5, 0x00713ea2,
    0x00980d4a, 0x007e8515, 0x00a40a97, 0x00900556, 0x00786555, 0x009e0ad5, 0x008a06d2, 0x00724755,
    0x009a0ea5, 0x0084064a, 0x006a364b, 0x00920a9b, 0x007c7a9a, 0x00a2056a, 0x008c0b69, 0x00765ba2,
    0x009e0b52, 0x00860b25, 0x006e4b23, 0x00960a4b, 0x007e8a2b, 0x00a402ad, 0x008e056d, 0x00796589,
    0x00a00da9, 0x008a0d92, 0x00724e95, 0x009a0d25, 0x0084ac4d, 0x00a80a56, 0x009202b6, 0x007a62d5,
    0x00a206d5, 0x008c0ea9, 0x00765f42, 0x009e0e92, 0x00880d26, 0x006e352a, 0x00940a57, 0x007e8a56,
    0x00a6035a, 0x008e06d5, 0x00785b69, 0x00a00749, 0x008a0693, 0x00704a93, 0x0098052b, 0x00820a5b,
    0x006c2aac, 0x0092056a, 0x007a7d95, 0x00a40ba4, 0x008e0b49, 0x00745d43, 0x009c0a95, 0x0086052d,
    0x006e4555, 0x00940ab5, 0x007e9aaa, 0x00a605d2, 0x00900da5, 0x00796e8a, 0x00a00d4a, 0x008a0c95,
    0x00724a96, 0x00980556, 0x00820ab5, 0x006c2ad8, 0x009406d2, 0x007a6745, 0x00a20725, 0x008c064b,
    0x00745647, 0x009a0cab, 0x0086055a, 0x006e356a, 0x00960b69, 0x007ebb52, 0x00a60b52, 0x00900b25,
    0x00796d0b, 0x009e0a4b, 0x008804ab, 0x007052ab, 0x009805ad, 0x00820b6a, 0x006c2da8, 0x00940d92,
    0x007c7ea5, 0x00a20d25, 0x008c0a55, 0x00755a4d, 0x009c04b6, 0x008405b5, 0x006f36d2, 0x00960ec9,
    0x00828f12, 0x00a60e92, 0x00900d26, 0x00796516, 0x009e0a57, 0x00880556, 0x00714365, 0x00980755,
    0x00840749, 0x006a374b, 0x00920693, 0x007a7aab, 0x00a2052b, 0x008a0a5b, 0x00745aaa, 0x009c056a,
    0x00860b65, 0x006e4ba2, 0x00960b4a, 0x007e8d15, 0x00a60a95, 0x008e052d, 0x0076654d, 0x009e0ab5,
    0x008a05aa, 0x007045d5, 0x00980da5, 0x00840d4a, 0x006c3e49, 0x00920c96, 0x007a7c8e, 0x00a20556,
    0x008c0ab5, 0x00755ac2, 0x009c06d2, 0x00860ea5, 0x00704722, 0x0094068b, 0x007c8617, 0x00a404ab,
    0x008e055b, 0x00776556, 0x009e0b6a, 0x008a0752, 0x00724b95, 0x00980b45, 0x00820a8b, 0x006a2a4d,
    0x009204ab,
};

static const char* lunar_months_cn[] = {
    "正", "二", "三", "四", "五", "六",
    "七", "八", "九", "十", "冬", "腊"
};

static const char* lunar_days_cn[] = {
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"
};

static const char* tiangan[] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"
};

static const char* dizhi[] = {
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"
};

// Days in a lunar month (29 or 30)
static inline int lunar_month_days(uint32_t info, int month, bool is_leap) {
    int leap_month = (info >> 12) & 0xF;
    if (is_leap) {
        if (month != leap_month) return 0; // not a leap month
        return ((info >> 16) & 1) ? 30 : 29;
    }
    return (info & (1U << (month - 1))) ? 30 : 29;
}

// Total days in a lunar year
static int lunar_year_days(int year) {
    uint32_t info = lunar_info[year - 1900];
    int sum = 348; // 29 * 12
    for (int i = 0; i < 12; i++) {
        if (info & (1U << i)) sum++;
    }
    int leap = (info >> 12) & 0xF;
    if (leap > 0) {
        sum += ((info >> 16) & 1) ? 30 : 29;
    }
    return sum;
}

uint32_t SolarToLunar(int year, int month, int day) {
    if (year < 1900 || year > 2100) return 0;

    // Build a simple day count since 1900-01-01
    struct tm base = {};
    base.tm_year = 1900 - 1900;
    base.tm_mon = 0;
    base.tm_mday = 1;
    time_t base_t = mktime(&base);

    struct tm target = {};
    target.tm_year = year - 1900;
    target.tm_mon = month - 1;
    target.tm_mday = day;
    time_t target_t = mktime(&target);

    int offset = static_cast<int>((target_t - base_t) / (24 * 3600));

    // Find the lunar year
    int lunar_year = 1900;
    while (lunar_year <= 2100) {
        int days = lunar_year_days(lunar_year);
        if (offset < days) break;
        offset -= days;
        lunar_year++;
    }
    if (lunar_year > 2100) return 0;

    uint32_t info = lunar_info[lunar_year - 1900];
    int leap_month = (info >> 12) & 0xF;

    // Find the lunar month
    int lunar_month = 1;
    bool is_leap = false;
    while (lunar_month <= 12) {
        int md = lunar_month_days(info, lunar_month, false);
        if (offset < md) break;
        offset -= md;
        lunar_month++;
    }

    // Check if we are in the leap month
    if (leap_month > 0 && lunar_month > leap_month) {
        // Need to adjust: the leap month sits after the normal month of same number
        // Re-calculate more carefully
        offset = static_cast<int>((target_t - base_t) / (24 * 3600));
        for (int y = 1900; y < lunar_year; y++) offset -= lunar_year_days(y);

        lunar_month = 1;
        while (lunar_month <= 12) {
            int md = lunar_month_days(info, lunar_month, false);
            if (offset < md) break;
            offset -= md;
            if (lunar_month == leap_month) {
                int leap_md = lunar_month_days(info, lunar_month, true);
                if (offset < leap_md) {
                    is_leap = true;
                    break;
                }
                offset -= leap_md;
            }
            lunar_month++;
        }
    } else if (leap_month > 0 && lunar_month == leap_month) {
        // Could be normal month or leap month
        int md = lunar_month_days(info, lunar_month, false);
        if (offset >= md) {
            offset -= md;
            is_leap = true;
        }
    }

    int lunar_day = offset + 1;
    return (static_cast<uint32_t>(lunar_year) << 16) |
           (static_cast<uint32_t>(lunar_month) << 8) |
           (static_cast<uint32_t>(lunar_day)) |
           (is_leap ? 0x80000000U : 0);
}

void FormatLunarDate(int year, int month, int day, char* buffer, size_t size) {
    if (size < 16) {
        if (size > 0) buffer[0] = '\0';
        return;
    }
    const char* leap_str = (year & 0x8000) ? "闰" : "";
    int y = year & 0x7FFF;
    int m = month;
    int d = day;
    if (m < 1 || m > 12 || d < 1 || d > 30) {
        snprintf(buffer, size, "---");
        return;
    }
    snprintf(buffer, size, "%s%s%s", leap_str, lunar_months_cn[m - 1], lunar_days_cn[d - 1]);
}

void FormatLunarYearGanZhi(int lunar_year, char* buffer, size_t size) {
    if (size < 8) {
        if (size > 0) buffer[0] = '\0';
        return;
    }
    int offset = lunar_year - 1900 + 36; // 1900 is 庚子年 (37th in cycle), adjust
    int tg = offset % 10;
    int dz = offset % 12;
    snprintf(buffer, size, "%s%s年", tiangan[tg], dizhi[dz]);
}

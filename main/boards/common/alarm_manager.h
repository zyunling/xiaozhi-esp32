#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#include <esp_timer.h>

struct Alarm {
    int id = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    std::string repeat;  // "once", "daily", "weekday"
    std::string label;
    bool enabled = true;
    bool fired_today = false;  // Prevent re-firing within same day
};

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    void Initialize();
    void SetAlarm(const Alarm& alarm);
    bool DeleteAlarm(int id);
    std::vector<Alarm> ListAlarms() const;
    void Dismiss();

    void OnAlarmFired(std::function<void(const Alarm&)> callback) {
        on_alarm_fired_ = callback;
    }

private:
    AlarmManager() = default;
    ~AlarmManager() = default;

    void LoadAlarms();
    void SaveAlarm(const Alarm& alarm);
    int NextId();
    void CheckAlarms();
    void OnTimer();

    static void TimerCallback(void* arg);

    std::vector<Alarm> alarms_;
    esp_timer_handle_t timer_ = nullptr;
    std::function<void(const Alarm&)> on_alarm_fired_;
    int next_id_ = 0;
};

#endif // ALARM_MANAGER_H
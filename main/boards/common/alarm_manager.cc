#include "alarm_manager.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>
#include <ctime>

#define TAG "AlarmMgr"

static const char* ALARM_KEY_PREFIX = "alarm_";
static const char* ALARM_COUNT_KEY = "alarm_count";
static const char* ALARM_NEXT_ID_KEY = "alarm_next_id";
static const int ALARM_CHECK_INTERVAL_MS = 30000;  // Check every 30s

void AlarmManager::Initialize() {
    LoadAlarms();

    esp_timer_create_args_t timer_args = {
        .callback = TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, ALARM_CHECK_INTERVAL_MS * 1000));

    ESP_LOGI(TAG, "Alarm manager initialized with %zu alarms", alarms_.size());
}

void AlarmManager::TimerCallback(void* arg) {
    auto* self = static_cast<AlarmManager*>(arg);
    self->OnTimer();
}

void AlarmManager::OnTimer() {
    CheckAlarms();
}

void AlarmManager::LoadAlarms() {
    alarms_.clear();
    Settings settings("alarm", false);
    int count = settings.GetInt(ALARM_COUNT_KEY, 0);
    next_id_ = settings.GetInt(ALARM_NEXT_ID_KEY, 0);

    for (int i = 0; i < count; i++) {
        std::string key = ALARM_KEY_PREFIX + std::to_string(i);
        std::string json_str = settings.GetString(key, "");
        if (json_str.empty()) continue;

        cJSON* root = cJSON_Parse(json_str.c_str());
        if (root == nullptr) continue;

        Alarm alarm;
        cJSON* item;

        item = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsNumber(item)) alarm.id = item->valueint;

        item = cJSON_GetObjectItem(root, "hour");
        if (cJSON_IsNumber(item)) alarm.hour = item->valueint;

        item = cJSON_GetObjectItem(root, "minute");
        if (cJSON_IsNumber(item)) alarm.minute = item->valueint;

        item = cJSON_GetObjectItem(root, "repeat");
        if (cJSON_IsString(item)) alarm.repeat = item->valuestring;

        item = cJSON_GetObjectItem(root, "label");
        if (cJSON_IsString(item)) alarm.label = item->valuestring;

        item = cJSON_GetObjectItem(root, "enabled");
        if (cJSON_IsBool(item)) alarm.enabled = item->valueint != 0;

        item = cJSON_GetObjectItem(root, "fired_today");
        if (cJSON_IsBool(item)) alarm.fired_today = item->valueint != 0;

        cJSON_Delete(root);
        alarms_.push_back(alarm);
    }
}

void AlarmManager::SaveAlarm(const Alarm& alarm) {
    Settings settings("alarm", true);

    // Find existing index or create new
    int index = -1;
    for (size_t i = 0; i < alarms_.size(); i++) {
        if (alarms_[i].id == alarm.id) {
            // Update in-place
            alarms_[i] = alarm;
            index = i;
            break;
        }
    }

    if (index == -1) {
        // New alarm
        alarms_.push_back(alarm);
        index = alarms_.size() - 1;
    }

    // Serialize to JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", alarm.id);
    cJSON_AddNumberToObject(root, "hour", alarm.hour);
    cJSON_AddNumberToObject(root, "minute", alarm.minute);
    cJSON_AddStringToObject(root, "repeat", alarm.repeat.c_str());
    cJSON_AddStringToObject(root, "label", alarm.label.c_str());
    cJSON_AddBoolToObject(root, "enabled", alarm.enabled);
    cJSON_AddBoolToObject(root, "fired_today", alarm.fired_today);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string key = ALARM_KEY_PREFIX + std::to_string(index);
    settings.SetString(key, json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    settings.SetInt(ALARM_COUNT_KEY, alarms_.size());
    settings.SetInt(ALARM_NEXT_ID_KEY, next_id_);
}

int AlarmManager::NextId() {
    return next_id_++;
}

void AlarmManager::SetAlarm(const Alarm& alarm) {
    Alarm new_alarm = alarm;
    if (new_alarm.id == 0) {
        new_alarm.id = NextId();
    }
    SaveAlarm(new_alarm);
    ESP_LOGI(TAG, "Alarm set: id=%d %02d:%02d repeat=%s label=%s",
             new_alarm.id, new_alarm.hour, new_alarm.minute,
             new_alarm.repeat.c_str(), new_alarm.label.c_str());
}

bool AlarmManager::DeleteAlarm(int id) {
    for (auto it = alarms_.begin(); it != alarms_.end(); ++it) {
        if (it->id == id) {
            alarms_.erase(it);
            // Rewrite all alarms to NVS
            Settings settings("alarm", true);
            settings.EraseAll();
            for (size_t i = 0; i < alarms_.size(); i++) {
                std::string key = ALARM_KEY_PREFIX + std::to_string(i);
                cJSON* root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "id", alarms_[i].id);
                cJSON_AddNumberToObject(root, "hour", alarms_[i].hour);
                cJSON_AddNumberToObject(root, "minute", alarms_[i].minute);
                cJSON_AddStringToObject(root, "repeat", alarms_[i].repeat.c_str());
                cJSON_AddStringToObject(root, "label", alarms_[i].label.c_str());
                cJSON_AddBoolToObject(root, "enabled", alarms_[i].enabled);
                cJSON_AddBoolToObject(root, "fired_today", alarms_[i].fired_today);
                char* json_str = cJSON_PrintUnformatted(root);
                settings.SetString(key, json_str);
                cJSON_free(json_str);
                cJSON_Delete(root);
            }
            settings.SetInt(ALARM_COUNT_KEY, alarms_.size());
            settings.SetInt(ALARM_NEXT_ID_KEY, next_id_);
            ESP_LOGI(TAG, "Alarm deleted: id=%d", id);
            return true;
        }
    }
    return false;
}

std::vector<Alarm> AlarmManager::ListAlarms() const {
    return alarms_;
}

void AlarmManager::Dismiss() {
    // Reset fired_today for all alarms so they can fire again
    for (auto& alarm : alarms_) {
        if (alarm.fired_today) {
            alarm.fired_today = false;
            SaveAlarm(alarm);
        }
    }
    ESP_LOGI(TAG, "Alarms dismissed");
}

void AlarmManager::CheckAlarms() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    int current_hour = timeinfo.tm_hour;
    int current_min = timeinfo.tm_min;
    int current_wday = timeinfo.tm_wday;  // 0=Sun, 1=Mon, ..., 6=Sat
    int current_yday = timeinfo.tm_yday;

    for (auto& alarm : alarms_) {
        if (!alarm.enabled) continue;

        // Check if time matches
        if (alarm.hour != current_hour || alarm.minute != current_min) continue;

        // Check if already fired today
        // Use a simple approach: check fired_today flag
        if (alarm.fired_today) continue;

        // Check repeat pattern
        bool should_fire = false;
        if (alarm.repeat == "once") {
            should_fire = true;
        } else if (alarm.repeat == "daily") {
            should_fire = true;
        } else if (alarm.repeat == "weekday") {
            should_fire = (current_wday >= 1 && current_wday <= 5);
        }

        if (should_fire) {
            alarm.fired_today = true;
            SaveAlarm(alarm);
            ESP_LOGI(TAG, "Alarm FIRED: id=%d %02d:%02d label=%s",
                     alarm.id, alarm.hour, alarm.minute, alarm.label.c_str());
            if (on_alarm_fired_) {
                on_alarm_fired_(alarm);
            }
        }
    }
}
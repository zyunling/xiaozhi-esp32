#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "audio_codec.h"
#include "board.h"

#define MUSIC_PLAYER_EVENT_STOP    (1 << 0)
#define MUSIC_PLAYER_EVENT_PAUSE   (1 << 1)
#define MUSIC_PLAYER_EVENT_RESUME  (1 << 2)
#define MUSIC_PLAYER_EVENT_PLAY    (1 << 3)

enum class MusicPlayerState {
    kIdle,
    kPlaying,
    kPaused,
    kStopped
};

class MusicPlayer {
public:
    static MusicPlayer& GetInstance() {
        static MusicPlayer instance;
        return instance;
    }

    void Initialize();
    bool Play(const std::string& url);
    void Stop();
    void Pause();
    void Resume();
    MusicPlayerState GetState() const { return state_; }
    bool IsPlaying() const { return state_ == MusicPlayerState::kPlaying; }

    void OnStatusChanged(std::function<void(MusicPlayerState)> callback) {
        on_status_changed_ = callback;
    }

private:
    MusicPlayer();
    ~MusicPlayer();

    void SetState(MusicPlayerState state);
    void PlayerTask();
    static void PlayerTaskEntry(void* arg);

    std::atomic<MusicPlayerState> state_{MusicPlayerState::kIdle};
    std::string stream_url_;
    TaskHandle_t task_handle_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    std::function<void(MusicPlayerState)> on_status_changed_;

    // Resampling
    void ResampleTo24k(const std::vector<int16_t>& input, int input_channels,
                       std::vector<int16_t>& output);
};

#endif // MUSIC_PLAYER_H
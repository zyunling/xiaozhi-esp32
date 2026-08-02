#include "music_player.h"
#include "mp3_decoder.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>

#define TAG "MusicPlayer"

// Buffer sizes
#define HTTP_READ_CHUNK       4096
#define ACCUMULATOR_SIZE      (8192 * 2)  // Large enough for multiple MP3 frames
#define PCM_FRAME_SAMPLES     1152 * 2     // Max MP3 frame is 1152 samples per channel
#define OUTPUT_SAMPLE_RATE    24000
#define MP3_SAMPLE_RATE       44100       // Most common MP3 sample rate

MusicPlayer::MusicPlayer() {
    event_group_ = xEventGroupCreate();
}

MusicPlayer::~MusicPlayer() {
    Stop();
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
}

void MusicPlayer::Initialize() {
    ESP_LOGI(TAG, "Music player initialized");
}

void MusicPlayer::SetState(MusicPlayerState state) {
    state_ = state;
    if (on_status_changed_) {
        on_status_changed_(state);
    }
}

bool MusicPlayer::Play(const std::string& url) {
    // Stop any current playback
    if (state_ != MusicPlayerState::kIdle) {
        Stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    stream_url_ = url;
    SetState(MusicPlayerState::kPlaying);

    // Clear event bits
    xEventGroupClearBits(event_group_, 
        MUSIC_PLAYER_EVENT_STOP | MUSIC_PLAYER_EVENT_PAUSE | 
        MUSIC_PLAYER_EVENT_RESUME | MUSIC_PLAYER_EVENT_PLAY);

    // Create task if not already running
    if (task_handle_ == nullptr) {
        xTaskCreate(PlayerTaskEntry, "music_player", 8192, this, 1, &task_handle_);
    } else {
        // Signal the task to start playing
        xEventGroupSetBits(event_group_, MUSIC_PLAYER_EVENT_PLAY);
    }

    ESP_LOGI(TAG, "Starting playback: %s", url.c_str());
    return true;
}

void MusicPlayer::Stop() {
    if (state_ == MusicPlayerState::kIdle) return;
    
    ESP_LOGI(TAG, "Stopping playback");
    xEventGroupSetBits(event_group_, MUSIC_PLAYER_EVENT_STOP);
    
    // Wait for task to acknowledge
    if (task_handle_ != nullptr) {
        TickType_t timeout = pdMS_TO_TICKS(2000);
        // Signal and wait
        auto start = xTaskGetTickCount();
        while (state_ != MusicPlayerState::kIdle && 
               (xTaskGetTickCount() - start) < timeout) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void MusicPlayer::Pause() {
    if (state_ != MusicPlayerState::kPlaying) return;
    ESP_LOGI(TAG, "Pausing playback");
    xEventGroupSetBits(event_group_, MUSIC_PLAYER_EVENT_PAUSE);
}

void MusicPlayer::Resume() {
    if (state_ != MusicPlayerState::kPaused) return;
    ESP_LOGI(TAG, "Resuming playback");
    xEventGroupSetBits(event_group_, MUSIC_PLAYER_EVENT_RESUME);
}

void MusicPlayer::PlayerTaskEntry(void* arg) {
    auto* player = static_cast<MusicPlayer*>(arg);
    player->PlayerTask();
    player->task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

// Linear interpolation resample from 44100Hz to 24000Hz
// input: source PCM at 44100Hz (can be mono or stereo interleaved)
// input_channels: 1 for mono, 2 for stereo
// output: resampled PCM at 24000Hz, mono
void MusicPlayer::ResampleTo24k(const std::vector<int16_t>& input, int input_channels,
                                 std::vector<int16_t>& output) {
    if (input.empty()) return;

    // Convert to mono first if stereo
    std::vector<int16_t> mono_input;
    if (input_channels == 2) {
        mono_input.resize(input.size() / 2);
        for (size_t i = 0; i < mono_input.size(); i++) {
            // Average left and right channels
            mono_input[i] = (static_cast<int>(input[i * 2]) + static_cast<int>(input[i * 2 + 1])) / 2;
        }
    } else {
        mono_input = input;
    }

    // Resample from 44100 to 24000 using linear interpolation
    // ratio = 24000 / 44100 = 0.54421768
    int input_samples = mono_input.size();
    int output_samples = static_cast<int>(input_samples * 24000LL / 44100);
    if (output_samples <= 0) return;

    output.resize(output_samples);
    for (int i = 0; i < output_samples; i++) {
        double pos = i * 44100.0 / 24000.0;
        int index = static_cast<int>(pos);
        double frac = pos - index;

        if (index >= input_samples - 1) {
            output[i] = mono_input[input_samples - 1];
        } else {
            output[i] = static_cast<int16_t>(
                mono_input[index] * (1.0 - frac) + mono_input[index + 1] * frac);
        }
    }
}

void MusicPlayer::PlayerTask() {
    ESP_LOGI(TAG, "Player task started");

    while (true) {
        // Wait for PLAY event or external stop
        EventBits_t bits = xEventGroupWaitBits(event_group_, 
            MUSIC_PLAYER_EVENT_PLAY | MUSIC_PLAYER_EVENT_STOP,
            pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MUSIC_PLAYER_EVENT_STOP) {
            SetState(MusicPlayerState::kIdle);
            continue;
        }

        if (!(bits & MUSIC_PLAYER_EVENT_PLAY)) {
            continue;
        }

        // We are now in playing state
        SetState(MusicPlayerState::kPlaying);

        // Open HTTP stream
        auto network = Board::GetInstance().GetNetwork();
        if (!network) {
            ESP_LOGE(TAG, "No network available");
            SetState(MusicPlayerState::kIdle);
            continue;
        }

        auto http = network->CreateHttp(3);
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client");
            SetState(MusicPlayerState::kIdle);
            continue;
        }

        http->SetHeader("Accept", "audio/mpeg");
        http->SetHeader("Icy-MetaData", "0");  // No ICY metadata

        if (!http->Open("GET", stream_url_)) {
            ESP_LOGE(TAG, "Failed to open stream: %s", stream_url_.c_str());
            SetState(MusicPlayerState::kIdle);
            continue;
        }

        if (http->GetStatusCode() != 200) {
            ESP_LOGE(TAG, "Stream returned status %d", http->GetStatusCode());
            http->Close();
            SetState(MusicPlayerState::kIdle);
            continue;
        }

        ESP_LOGI(TAG, "Stream opened successfully");

        // Initialize MP3 decoder
        mp3dec_t mp3_dec;
        mp3dec_init(&mp3_dec);

        // Buffers
        std::vector<uint8_t> accumulator;
        accumulator.reserve(ACCUMULATOR_SIZE);
        std::vector<int16_t> pcm_output(PCM_FRAME_SAMPLES);
        std::vector<int16_t> resampled_output;

        uint8_t http_buf[HTTP_READ_CHUNK];
        bool stream_ended = false;
        int total_frames = 0;

        // Audio codec for output
        auto codec = Board::GetInstance().GetAudioCodec();

        while (state_ == MusicPlayerState::kPlaying && !stream_ended) {
            // Check for pause
            bits = xEventGroupGetBits(event_group_);
            if (bits & MUSIC_PLAYER_EVENT_PAUSE) {
                xEventGroupClearBits(event_group_, MUSIC_PLAYER_EVENT_PAUSE);
                SetState(MusicPlayerState::kPaused);
                
                // Wait for resume or stop
                while (state_ == MusicPlayerState::kPaused) {
                    bits = xEventGroupWaitBits(event_group_,
                        MUSIC_PLAYER_EVENT_RESUME | MUSIC_PLAYER_EVENT_STOP,
                        pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
                    
                    if (bits & MUSIC_PLAYER_EVENT_STOP) {
                        break;
                    } else if (bits & MUSIC_PLAYER_EVENT_RESUME) {
                        SetState(MusicPlayerState::kPlaying);
                    }
                }
                continue;
            }

            // Check for stop
            if (bits & MUSIC_PLAYER_EVENT_STOP) {
                xEventGroupClearBits(event_group_, MUSIC_PLAYER_EVENT_STOP);
                break;
            }

            // Read more data from HTTP
            int ret = http->Read((char*)http_buf, HTTP_READ_CHUNK);
            if (ret < 0) {
                ESP_LOGE(TAG, "HTTP read error");
                break;
            }
            if (ret == 0) {
                // Stream ended, try to decode remaining data
                stream_ended = true;
            } else {
                // Add to accumulator
                accumulator.insert(accumulator.end(), http_buf, http_buf + ret);
            }

            // Decode all complete frames from accumulator
            bool decoded_any = false;
            while (true) {
                if (accumulator.size() < 4) break;  // Need at least MP3 frame header

                mp3dec_frame_info_t frame_info;
                int samples = mp3dec_decode_frame(&mp3_dec, 
                    accumulator.data(), accumulator.size(),
                    pcm_output.data(), &frame_info);

                if (samples > 0) {
                    decoded_any = true;
                    total_frames++;

                    // Remove consumed bytes from accumulator
                    accumulator.erase(accumulator.begin(), 
                        accumulator.begin() + frame_info.frame_bytes);

                    // Get PCM data (samples are per channel, channels from frame_info)
                    int channels = frame_info.channels;
                    int hz = frame_info.hz;
                    int total_samples = samples * channels;

                    // Resample to 24kHz mono
                    std::vector<int16_t> pcm_frame(pcm_output.begin(), 
                        pcm_output.begin() + total_samples);
                    resampled_output.clear();
                    ResampleTo24k(pcm_frame, channels, resampled_output);

                    if (!resampled_output.empty()) {
                        codec->OutputData(resampled_output);
                    }

                    // Yield to allow other tasks
                    vTaskDelay(1);
                } else {
                    // Not enough data for a complete frame, or no valid frame
                    break;
                }
            }

            // Prevent accumulator from growing unbounded
            if (accumulator.size() > ACCUMULATOR_SIZE) {
                // Remove oldest 4KB, keeping the tail
                size_t remove = accumulator.size() - ACCUMULATOR_SIZE;
                accumulator.erase(accumulator.begin(), accumulator.begin() + remove);
            }
        }

        http->Close();
        ESP_LOGI(TAG, "Playback finished: %d MP3 frames decoded", total_frames);

        // If we stopped naturally (stream ended), set to idle
        if (stream_ended) {
            SetState(MusicPlayerState::kIdle);
        }
    }
}
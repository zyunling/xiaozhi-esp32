#include "wifi_board.h"
#include "codecs/es8389_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_manager.h"

#include "i2c_device.h"
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>

#include "mcp_server.h"
#include "navidrome_api.h"
#include "music_player.h"
#include "alarm_manager.h"

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "esp_io_expander_tca95xx_16bit.h"

#define TAG "atk_dnesp32s3_box2_wifi"

class atk_dnesp32s3_box2_wifi : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;   
    LcdDisplay* display_;
    esp_io_expander_handle_t io_exp_handle;
    button_handle_t btns;
    button_driver_t* btn_driver_ = nullptr;
    static atk_dnesp32s3_box2_wifi* instance_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    PowerSupply power_status_;
    esp_timer_handle_t wake_timer_handle_;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    int ticks_ = 0;
    const int kChgCtrlInterval = 5;

    void InitializeBoardPowerManager() {
        instance_ = this;

        if (IoExpanderGetLevel(XIO_CHRG) == 0) {
            power_status_ = kDeviceTypecSupply;
        } else {
            power_status_ = kDeviceBatterySupply;
        }

        esp_timer_create_args_t wake_display_timer_args = {
            .callback = [](void *arg) {
                atk_dnesp32s3_box2_wifi* self = static_cast<atk_dnesp32s3_box2_wifi*>(arg);

                self->ticks_ ++;
                if (self->ticks_ % self->kChgCtrlInterval == 0) {
                    if (self->IoExpanderGetLevel(XIO_CHRG) == 0) {
                        self->power_status_ = kDeviceTypecSupply;
                    } else {
                        self->power_status_ = kDeviceBatterySupply;
                    }

                    /* 低于某个电量，会自动关机 */
                    if (self->power_manager_->low_voltage_ < 2630 && self->power_status_ == kDeviceBatterySupply) {
                        esp_timer_stop(self->power_manager_->timer_handle_);

                        esp_io_expander_set_dir(self->io_exp_handle, XIO_CHG_CTRL, IO_EXPANDER_OUTPUT);
                        esp_io_expander_set_level(self->io_exp_handle, XIO_CHG_CTRL, 0);
                        vTaskDelay(pdMS_TO_TICKS(100));

                        esp_io_expander_set_dir(self->io_exp_handle, XIO_CHG_CTRL, IO_EXPANDER_INPUT);
                        esp_io_expander_set_level(self->io_exp_handle, XIO_CHG_CTRL, 0);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wake_update_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&wake_display_timer_args, &wake_timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(wake_timer_handle_, 100000));
    }

    void InitializePowerManager() {
        power_manager_ = new PowerManager(io_exp_handle);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            power_status_ = is_charging ? kDeviceTypecSupply : kDeviceBatterySupply;
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            if (power_status_ == kDeviceTypecSupply) {
                // Charging: keep standby dim level (30%), don't go to 1%
                // The clock standby dim timer handles brightness
            } else {
                GetBacklight()->SetBrightness(1);
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            if (power_status_ == kDeviceBatterySupply) {
                GetBacklight()->SetBrightness(0);
                esp_timer_stop(power_manager_->timer_handle_);
                esp_io_expander_set_dir( io_exp_handle, XIO_CHG_CTRL, IO_EXPANDER_OUTPUT);
                esp_io_expander_set_level(io_exp_handle, XIO_CHG_CTRL, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_io_expander_set_level(io_exp_handle, XIO_SYS_POW, 0);
            }
        });

        // Only enable auto-sleep on battery power; disable when charging
        bool charging = (IoExpanderGetLevel(XIO_CHRG) == 0);
        power_save_timer_->SetEnabled(!charging);
    }

    void audio_volume_change(bool direction) {
        auto codec = GetAudioCodec();
        auto volume = codec->output_volume();

        if (direction) {
            volume += 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
        } else {
            volume -= 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
        }
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    }

    void audio_volume_minimum(){
        GetAudioCodec()->SetOutputVolume(0);
        GetDisplay()->ShowNotification(Lang::Strings::MUTED);
    }

    void audio_volume_maxmum(){
        GetAudioCodec()->SetOutputVolume(100);
        GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
    }

    esp_err_t IoExpanderSetLevel(uint16_t pin_mask, uint8_t level) {
        return esp_io_expander_set_level(io_exp_handle, pin_mask, level);
    }

    uint8_t IoExpanderGetLevel(uint16_t pin_mask) {
        uint32_t pin_val = 0;
        esp_io_expander_get_level(io_exp_handle, DRV_IO_EXP_INPUT_MASK, &pin_val);
        pin_mask &= DRV_IO_EXP_INPUT_MASK;
        return (uint8_t)((pin_val & pin_mask) ? 1 : 0);
    }

    void InitializeIoExpander() {
        esp_err_t ret = ESP_OK;
        esp_io_expander_new_i2c_tca95xx_16bit(i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &io_exp_handle);

        ret |= esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_OUTPUT_MASK, IO_EXPANDER_OUTPUT);
        ret |= esp_io_expander_set_dir(io_exp_handle, DRV_IO_EXP_INPUT_MASK, IO_EXPANDER_INPUT);

        ret |= esp_io_expander_set_level(io_exp_handle, XIO_SYS_POW, 1);
        ret |= esp_io_expander_set_level(io_exp_handle, XIO_EN_3V3A, 1);
        ret |= esp_io_expander_set_level(io_exp_handle, XIO_EN_4G, 1);
        ret |= esp_io_expander_set_level(io_exp_handle, XIO_SPK_EN, 1);
        ret |= esp_io_expander_set_level(io_exp_handle, XIO_USB_SEL, 1);
        ret |= esp_io_expander_set_level(io_exp_handle, XIO_VBUS_EN, 0);

        assert(ret == ESP_OK);
    }

    // Initialize I2C peripheral
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeButtons() {
        instance_ = this;

        button_config_t l_btn_cfg = {
            .long_press_time = 800,
            .short_press_time = 500
        };

        button_config_t m_btn_cfg = {
            .long_press_time = 800,
            .short_press_time = 500
        };

        button_config_t r_btn_cfg = {
            .long_press_time = 800,
            .short_press_time = 500
        };

        button_driver_t* xio_l_btn_driver_ = nullptr;
        button_driver_t* xio_m_btn_driver_ = nullptr;

        button_handle_t l_btn_handle = NULL;
        button_handle_t m_btn_handle = NULL;
        button_handle_t r_btn_handle = NULL;

        xio_l_btn_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        xio_l_btn_driver_->enable_power_save = false;
        xio_l_btn_driver_->get_key_level = [](button_driver_t *button_driver) -> uint8_t {
            return !instance_->IoExpanderGetLevel(XIO_KEY_L);
        };
        ESP_ERROR_CHECK(iot_button_create(&l_btn_cfg, xio_l_btn_driver_, &l_btn_handle));

        xio_m_btn_driver_ = (button_driver_t*)calloc(1, sizeof(button_driver_t));
        xio_m_btn_driver_->enable_power_save = false;
        xio_m_btn_driver_->get_key_level = [](button_driver_t *button_driver) -> uint8_t {
            return instance_->IoExpanderGetLevel(XIO_KEY_M);
        };
        ESP_ERROR_CHECK(iot_button_create(&m_btn_cfg, xio_m_btn_driver_, &m_btn_handle));

        button_gpio_config_t r_cfg = {
            .gpio_num = R_BUTTON_GPIO,
            .active_level = BUTTON_INACTIVE,
            .enable_power_save = false,
            .disable_pull = false
        };
        ESP_ERROR_CHECK(iot_button_new_gpio_device(&r_btn_cfg, &r_cfg, &r_btn_handle));

        iot_button_register_cb(l_btn_handle, BUTTON_PRESS_DOWN, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);
            self->power_save_timer_->WakeUp();
            self->GetDisplay()->PokeStandbyDisplay();
            self->audio_volume_change(false);
        }, this);

        iot_button_register_cb(l_btn_handle, BUTTON_LONG_PRESS_START, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);
            self->power_save_timer_->WakeUp();
            self->GetDisplay()->PokeStandbyDisplay();
            self->audio_volume_minimum();
        }, this);

        iot_button_register_cb(m_btn_handle, BUTTON_PRESS_DOWN, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);
            self->power_save_timer_->WakeUp();
            self->GetDisplay()->PokeStandbyDisplay();
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        }, this);

        iot_button_register_cb(m_btn_handle, BUTTON_LONG_PRESS_START, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);

            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                self->EnterWifiConfigMode();
                return;
            }

            self->GetDisplay()->PokeStandbyDisplay();

            if (self->power_status_ == kDeviceBatterySupply) {
                auto backlight = self->GetBacklight();
                backlight->SetBrightness(0);
                esp_timer_stop(self->power_manager_->timer_handle_);
                esp_io_expander_set_dir(self->io_exp_handle, XIO_CHG_CTRL, IO_EXPANDER_OUTPUT);
                esp_io_expander_set_level(self->io_exp_handle, XIO_CHG_CTRL, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_io_expander_set_level(self->io_exp_handle, XIO_SYS_POW, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }, this);

        iot_button_register_cb(r_btn_handle, BUTTON_PRESS_DOWN, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);
            self->power_save_timer_->WakeUp();
            self->GetDisplay()->PokeStandbyDisplay();
            self->audio_volume_change(true);
        }, this);

        iot_button_register_cb(r_btn_handle, BUTTON_LONG_PRESS_START, nullptr, [](void* button_handle, void* usr_data) {
            auto self = static_cast<atk_dnesp32s3_box2_wifi*>(usr_data);
            self->power_save_timer_->WakeUp();
            self->GetDisplay()->PokeStandbyDisplay();
            self->audio_volume_maxmum();
        }, this);
    }

    void InitializeSt7789Display() {
        ESP_LOGI(TAG, "Install panel IO");

        /* RD PIN */
        gpio_config_t gpio_init_struct;
        gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
        gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;
        gpio_init_struct.pin_bit_mask = 1ull << LCD_PIN_RD;
        gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&gpio_init_struct);
        gpio_set_level(LCD_PIN_RD, 1);

        /* BL PIN */
        gpio_init_struct.pin_bit_mask = 1ull << DISPLAY_BACKLIGHT_PIN;
        gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&gpio_init_struct);

        esp_lcd_i80_bus_handle_t i80_bus = NULL;
        esp_lcd_i80_bus_config_t bus_config = {
            .dc_gpio_num = LCD_PIN_DC,
            .wr_gpio_num = LCD_PIN_WR,
            .clk_src = LCD_CLK_SRC_DEFAULT,
            .data_gpio_nums = {
                LCD_PIN_D0,
                LCD_PIN_D1,
                LCD_PIN_D2,
                LCD_PIN_D3,
                LCD_PIN_D4,
                LCD_PIN_D5,
                LCD_PIN_D6,
                LCD_PIN_D7,
            },
            .bus_width = 8,
            .max_transfer_bytes = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
            .dma_burst_size = 64,
        };
        ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

        esp_lcd_panel_io_i80_config_t io_config = {
            .cs_gpio_num = LCD_PIN_CS,
            .pclk_hz = (20 * 1000 * 1000),
            .trans_queue_depth = 7,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .dc_levels = {
                .dc_idle_level = 1,
                .dc_cmd_level = 0,
                .dc_dummy_level = 0,
                .dc_data_level = 1,
            },
            .flags = {
                .cs_active_high = 0,        
                .pclk_active_neg = 0,       
                .pclk_idle_low = 0,           
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.reset_gpio_num = LCD_PIN_RST;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_set_gap(panel, 0, 0);
        esp_lcd_panel_io_tx_param(panel_io, 0xCF, (uint8_t[]) {0x00,0x83,0x30}, 3);
        esp_lcd_panel_io_tx_param(panel_io, 0xED, (uint8_t[]) {0x64,0x03,0x12,0x81}, 4);
        esp_lcd_panel_io_tx_param(panel_io, 0xE8, (uint8_t[]) {0x85,0x01,0x79}, 3);
        esp_lcd_panel_io_tx_param(panel_io, 0xCB, (uint8_t[]) {0x39,0x2C,0x00,0x34,0x02}, 5);
        esp_lcd_panel_io_tx_param(panel_io, 0xF7, (uint8_t[]) {0x20}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xEA, (uint8_t[]) {0x00,0x00}, 2);
        esp_lcd_panel_io_tx_param(panel_io, 0xbb, (uint8_t[]) {0x20}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xc3, (uint8_t[]) {0x00}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xC4, (uint8_t[]) {0x20}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xC5, (uint8_t[]) {0x20}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xC6, (uint8_t[]) {0x10}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xC7, (uint8_t[]) {0xB0}, 1);
        // MADCTL is configured by esp_lcd_panel_swap_xy / esp_lcd_panel_mirror below
        esp_lcd_panel_io_tx_param(panel_io, 0x3A, (uint8_t[]) {0x55}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xB1, (uint8_t[]) {0x00,0x1B}, 2);
        esp_lcd_panel_io_tx_param(panel_io, 0xF2, (uint8_t[]) {0x08}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0x26, (uint8_t[]) {0x01}, 1);
        esp_lcd_panel_io_tx_param(panel_io, 0xE0, (uint8_t[]) {0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17}, 14);
        esp_lcd_panel_io_tx_param(panel_io, 0xE1, (uint8_t[]) {0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E}, 14);
        esp_lcd_panel_io_tx_param(panel_io, 0xB7, (uint8_t[]) {0x07}, 1);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();

        // ============ Music Control Tools ============
        mcp_server.AddTool(
            "music.navidrome.configure",
            "配置Navidrome音乐服务器连接信息。参数：\n"
            "  `url`: Navidrome服务器地址，例如 http://192.168.1.100:4533\n"
            "  `username`: 用户名\n"
            "  `password`: 密码\n"
            "返回值：配置状态信息",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("username", kPropertyTypeString),
                Property("password", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                NavidromeApi::Config cfg;
                cfg.url = properties["url"].value<std::string>();
                cfg.username = properties["username"].value<std::string>();
                cfg.password = properties["password"].value<std::string>();
                NavidromeApi::GetInstance().SaveConfig(cfg);
                return "{\"success\": true, \"message\": \"Navidrome服务器配置已保存\"}";
            }
        );

        mcp_server.AddTool(
            "music.navidrome.status",
            "获取Navidrome服务器连接状态。无参数。\n"
            "返回值：服务器连接状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& api = NavidromeApi::GetInstance();
                if (!api.IsConfigured()) {
                    return "{\"configured\": false, \"message\": \"Navidrome服务器未配置\"}";
                }
                bool connected = api.Ping();
                return connected ?
                    "{\"configured\": true, \"connected\": true, \"message\": \"Navidrome服务器已连接\"}" :
                    "{\"configured\": true, \"connected\": false, \"message\": \"Navidrome服务器无法连接\"}";
            }
        );

        mcp_server.AddTool(
            "music.search",
            "在Navidrome中搜索音乐。参数：\n"
            "  `query`: 搜索关键词（歌曲名、歌手或专辑名）\n"
            "返回值：搜索到的歌曲列表",
            PropertyList({
                Property("query", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto& api = NavidromeApi::GetInstance();
                if (!api.IsConfigured()) {
                    return "{\"error\": \"Navidrome服务器未配置\"}";
                }
                auto query = properties["query"].value<std::string>();
                auto songs = api.Search(query, 10);
                cJSON* arr = cJSON_CreateArray();
                for (const auto& song : songs) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "id", song.id.c_str());
                    cJSON_AddStringToObject(item, "title", song.title.c_str());
                    cJSON_AddStringToObject(item, "artist", song.artist.c_str());
                    cJSON_AddStringToObject(item, "album", song.album.c_str());
                    cJSON_AddNumberToObject(item, "duration", song.duration);
                    cJSON_AddItemToArray(arr, item);
                }
                cJSON* result = cJSON_CreateObject();
                cJSON_AddItemToObject(result, "songs", arr);
                cJSON_AddNumberToObject(result, "count", songs.size());
                return result;
            }
        );

        mcp_server.AddTool(
            "music.play",
            "播放音乐。参数：\n"
            "  `id`: 歌曲ID（从music.search获取）\n"
            "返回值：播放状态信息",
            PropertyList({
                Property("id", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto& api = NavidromeApi::GetInstance();
                if (!api.IsConfigured()) {
                    return "{\"error\": \"Navidrome服务器未配置\"}";
                }
                auto song_id = properties["id"].value<std::string>();
                auto song = api.GetSong(song_id);
                if (song.id.empty()) {
                    return "{\"error\": \"未找到该歌曲\"}";
                }
                std::string stream_url = api.GetStreamUrl(song_id);
                auto& player = MusicPlayer::GetInstance();
                if (!player.Play(stream_url)) {
                    return "{\"error\": \"播放失败\"}";
                }
                cJSON* result = cJSON_CreateObject();
                cJSON_AddStringToObject(result, "title", song.title.c_str());
                cJSON_AddStringToObject(result, "artist", song.artist.c_str());
                cJSON_AddStringToObject(result, "album", song.album.c_str());
                cJSON_AddBoolToObject(result, "playing", true);
                return result;
            }
        );

        mcp_server.AddTool(
            "music.stop",
            "停止播放当前音乐。无参数。\n"
            "返回值：操作状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& player = MusicPlayer::GetInstance();
                player.Stop();
                return "{\"success\": true, \"message\": \"音乐已停止\"}";
            }
        );

        mcp_server.AddTool(
            "music.pause",
            "暂停播放当前音乐。无参数。\n"
            "返回值：操作状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& player = MusicPlayer::GetInstance();
                if (!player.IsPlaying()) {
                    return "{\"error\": \"当前没有正在播放的音乐\"}";
                }
                player.Pause();
                return "{\"success\": true, \"message\": \"音乐已暂停\"}";
            }
        );

        mcp_server.AddTool(
            "music.resume",
            "继续播放已暂停的音乐。无参数。\n"
            "返回值：操作状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& player = MusicPlayer::GetInstance();
                if (player.GetState() != MusicPlayerState::kPaused) {
                    return "{\"error\": \"当前没有已暂停的音乐\"}";
                }
                player.Resume();
                return "{\"success\": true, \"message\": \"音乐已继续播放\"}";
            }
        );

        mcp_server.AddTool(
            "music.status",
            "获取当前音乐播放状态。无参数。\n"
            "返回值：当前播放状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& player = MusicPlayer::GetInstance();
                cJSON* result = cJSON_CreateObject();
                switch (player.GetState()) {
                    case MusicPlayerState::kIdle:
                        cJSON_AddStringToObject(result, "state", "idle");
                        break;
                    case MusicPlayerState::kPlaying:
                        cJSON_AddStringToObject(result, "state", "playing");
                        break;
                    case MusicPlayerState::kPaused:
                        cJSON_AddStringToObject(result, "state", "paused");
                        break;
                    case MusicPlayerState::kStopped:
                        cJSON_AddStringToObject(result, "state", "stopped");
                        break;
                }
                return result;
            }
        );

        // ============ Alarm Tools ============
        mcp_server.AddTool(
            "alarm.set",
            "设置闹钟。参数：\n"
            "  `hour`: 小时（0-23）\n"
            "  `minute`: 分钟（0-59）\n"
            "  `repeat`: 重复模式（\"once\"-单次, \"daily\"-每天, \"weekday\"-工作日）\n"
            "  `label`: 闹钟标签（可选，例如\"起床\"、\"吃药\"）\n"
            "返回值：设置的闹钟信息",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 59),
                Property("repeat", kPropertyTypeString, std::string("once")),
                Property("label", kPropertyTypeString, std::string(""))
            }),
            [](const PropertyList& properties) -> ReturnValue {
                Alarm alarm;
                alarm.hour = static_cast<uint8_t>(properties["hour"].value<int>());
                alarm.minute = static_cast<uint8_t>(properties["minute"].value<int>());
                alarm.repeat = properties["repeat"].value<std::string>();
                alarm.label = properties["label"].value<std::string>();
                alarm.enabled = true;

                auto& mgr = AlarmManager::GetInstance();
                mgr.SetAlarm(alarm);

                cJSON* result = cJSON_CreateObject();
                cJSON_AddNumberToObject(result, "id", alarm.id);
                cJSON_AddNumberToObject(result, "hour", alarm.hour);
                cJSON_AddNumberToObject(result, "minute", alarm.minute);
                cJSON_AddStringToObject(result, "repeat", alarm.repeat.c_str());
                cJSON_AddStringToObject(result, "label", alarm.label.c_str());
                cJSON_AddBoolToObject(result, "enabled", alarm.enabled);
                return result;
            }
        );

        mcp_server.AddTool(
            "alarm.list",
            "列出所有已设置的闹钟。无参数。\n"
            "返回值：闹钟列表",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                auto& mgr = AlarmManager::GetInstance();
                auto alarms = mgr.ListAlarms();
                cJSON* arr = cJSON_CreateArray();
                for (const auto& alarm : alarms) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddNumberToObject(item, "id", alarm.id);
                    cJSON_AddNumberToObject(item, "hour", alarm.hour);
                    cJSON_AddNumberToObject(item, "minute", alarm.minute);
                    cJSON_AddStringToObject(item, "repeat", alarm.repeat.c_str());
                    cJSON_AddStringToObject(item, "label", alarm.label.c_str());
                    cJSON_AddBoolToObject(item, "enabled", alarm.enabled);
                    cJSON_AddItemToArray(arr, item);
                }
                cJSON* result = cJSON_CreateObject();
                cJSON_AddItemToObject(result, "alarms", arr);
                cJSON_AddNumberToObject(result, "count", alarms.size());
                return result;
            }
        );

        mcp_server.AddTool(
            "alarm.delete",
            "删除指定ID的闹钟。参数：\n"
            "  `id`: 闹钟ID（从alarm.list获取）\n"
            "返回值：操作状态信息",
            PropertyList({
                Property("id", kPropertyTypeInteger)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                int id = properties["id"].value<int>();
                auto& mgr = AlarmManager::GetInstance();
                if (mgr.DeleteAlarm(id)) {
                    return "{\"success\": true, \"message\": \"闹钟已删除\"}";
                }
                return "{\"error\": \"未找到指定ID的闹钟\"}";
            }
        );

        mcp_server.AddTool(
            "alarm.dismiss",
            "解除所有已触发的闹钟。无参数。\n"
            "返回值：操作状态信息",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                AlarmManager::GetInstance().Dismiss();
                return "{\"success\": true, \"message\": \"闹钟已解除\"}";
            }
        );

        // Initialize the alarm manager
        AlarmManager::GetInstance().Initialize();
        AlarmManager::GetInstance().OnAlarmFired([](const Alarm& alarm) {
            ESP_LOGI(TAG, "Alarm fired: %s at %02d:%02d",
                     alarm.label.c_str(), alarm.hour, alarm.minute);
            // TODO: Play alarm sound or notification via audio system
        });

        ESP_LOGI(TAG, "MCP tools registered (music + alarm)");
    }

public:
    atk_dnesp32s3_box2_wifi()  {
        InitializeI2c();
        InitializeIoExpander();
        InitializePowerSaveTimer();
        InitializePowerManager();
        InitializeSt7789Display();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
        InitializeBoardPowerManager();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8389AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8389_ADDR,
            AUDIO_CODEC_USE_MCLK);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(atk_dnesp32s3_box2_wifi);

// 定义静态成员变量
atk_dnesp32s3_box2_wifi* atk_dnesp32s3_box2_wifi::instance_ = nullptr;

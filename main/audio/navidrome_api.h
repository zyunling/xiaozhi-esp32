#ifndef NAVIDROME_API_H
#define NAVIDROME_API_H

#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include "settings.h"
#include "board.h"

#define TAG_Navidrome "NavidromeApi"

struct NavidromeSong {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    int duration = 0;  // seconds
    std::string cover_art_id;
};

struct NavidromeAlbum {
    std::string id;
    std::string name;
    std::string artist;
    int song_count = 0;
    int duration = 0;  // seconds
    std::string cover_art_id;
};

struct NavidromePlaylist {
    std::string id;
    std::string name;
    int song_count = 0;
};

class NavidromeApi {
public:
    static NavidromeApi& GetInstance() {
        static NavidromeApi instance;
        return instance;
    }

    struct Config {
        std::string url;
        std::string username;
        std::string password;
    };

    void LoadConfig() {
        Settings settings("navidrome", false);
        config_.url = settings.GetString("url", "");
        config_.username = settings.GetString("username", "");
        config_.password = settings.GetString("password", "");
    }

    void SaveConfig(const Config& cfg) {
        Settings settings("navidrome", true);
        if (!cfg.url.empty()) {
            config_.url = cfg.url;
            settings.SetString("url", cfg.url);
        }
        if (!cfg.username.empty()) {
            config_.username = cfg.username;
            settings.SetString("username", cfg.username);
        }
        if (!cfg.password.empty()) {
            config_.password = cfg.password;
            settings.SetString("password", cfg.password);
        }
    }

    const Config& GetConfig() const { return config_; }
    bool IsConfigured() const {
        return !config_.url.empty() && !config_.username.empty();
    }

    std::string BuildUrl(const std::string& endpoint, const std::string& params = "") const {
        std::string url = config_.url;
        if (url.back() == '/') url.pop_back();
        url += "/rest/" + endpoint + "?u=" + config_.username + "&p=" + config_.password + "&v=1.16.0&c=xiaozhi";
        if (!params.empty()) {
            url += "&" + params;
        }
        return url;
    }

    bool Ping() {
        auto http = CreateHttp();
        if (!http) return false;
        std::string url = BuildUrl("ping");
        if (!http->Open("GET", url)) {
            ESP_LOGE(TAG_Navidrome, "Ping failed: connection error");
            return false;
        }
        bool ok = (http->GetStatusCode() == 200);
        http->Close();
        return ok;
    }

    std::vector<NavidromeSong> Search(const std::string& query, int count = 10) {
        std::vector<NavidromeSong> results;
        auto http = CreateHttp();
        if (!http) return results;

        std::string url = BuildUrl("search3", "query=" + UrlEncode(query) + "&songCount=" + std::to_string(count));
        if (!http->Open("GET", url)) return results;
        if (http->GetStatusCode() != 200) { http->Close(); return results; }

        std::string body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(body.c_str());
        if (!root) return results;

        cJSON* sr = cJSON_GetObjectItem(root, "subsonic-response");
        cJSON* srch = sr ? cJSON_GetObjectItem(sr, "searchResult3") : nullptr;
        cJSON* songs = srch ? cJSON_GetObjectItem(srch, "song") : nullptr;

        if (cJSON_IsArray(songs)) {
            int size = cJSON_GetArraySize(songs);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(songs, i);
                if (!item) continue;
                NavidromeSong song;
                auto id = cJSON_GetObjectItem(item, "id");
                if (cJSON_IsString(id)) song.id = id->valuestring;
                auto title = cJSON_GetObjectItem(item, "title");
                if (cJSON_IsString(title)) song.title = title->valuestring;
                auto artist = cJSON_GetObjectItem(item, "artist");
                if (cJSON_IsString(artist)) song.artist = artist->valuestring;
                auto album = cJSON_GetObjectItem(item, "album");
                if (cJSON_IsString(album)) song.album = album->valuestring;
                auto dur = cJSON_GetObjectItem(item, "duration");
                if (cJSON_IsNumber(dur)) song.duration = dur->valueint;
                auto cover = cJSON_GetObjectItem(item, "coverArt");
                if (cJSON_IsString(cover)) song.cover_art_id = cover->valuestring;
                results.push_back(song);
            }
        }
        cJSON_Delete(root);
        return results;
    }

    std::vector<NavidromeAlbum> GetAlbumList(int count = 20) {
        std::vector<NavidromeAlbum> results;
        auto http = CreateHttp();
        if (!http) return results;

        std::string url = BuildUrl("getAlbumList2", "type=newest&size=" + std::to_string(count));
        if (!http->Open("GET", url)) return results;
        if (http->GetStatusCode() != 200) { http->Close(); return results; }

        std::string body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(body.c_str());
        if (!root) return results;

        cJSON* sr = cJSON_GetObjectItem(root, "subsonic-response");
        cJSON* al = sr ? cJSON_GetObjectItem(sr, "albumList2") : nullptr;
        cJSON* albums = al ? cJSON_GetObjectItem(al, "album") : nullptr;

        if (cJSON_IsArray(albums)) {
            int size = cJSON_GetArraySize(albums);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(albums, i);
                if (!item) continue;
                NavidromeAlbum album;
                auto id = cJSON_GetObjectItem(item, "id");
                if (cJSON_IsString(id)) album.id = id->valuestring;
                auto name = cJSON_GetObjectItem(item, "name");
                if (cJSON_IsString(name)) album.name = name->valuestring;
                auto artist = cJSON_GetObjectItem(item, "artist");
                if (cJSON_IsString(artist)) album.artist = artist->valuestring;
                auto sc = cJSON_GetObjectItem(item, "songCount");
                if (cJSON_IsNumber(sc)) album.song_count = sc->valueint;
                auto dur = cJSON_GetObjectItem(item, "duration");
                if (cJSON_IsNumber(dur)) album.duration = dur->valueint;
                auto cover = cJSON_GetObjectItem(item, "coverArt");
                if (cJSON_IsString(cover)) album.cover_art_id = cover->valuestring;
                results.push_back(album);
            }
        }
        cJSON_Delete(root);
        return results;
    }

    std::vector<NavidromePlaylist> GetPlaylists() {
        std::vector<NavidromePlaylist> results;
        auto http = CreateHttp();
        if (!http) return results;

        std::string url = BuildUrl("getPlaylists");
        if (!http->Open("GET", url)) return results;
        if (http->GetStatusCode() != 200) { http->Close(); return results; }

        std::string body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(body.c_str());
        if (!root) return results;

        cJSON* sr = cJSON_GetObjectItem(root, "subsonic-response");
        cJSON* pls = sr ? cJSON_GetObjectItem(sr, "playlists") : nullptr;
        cJSON* pl = pls ? cJSON_GetObjectItem(pls, "playlist") : nullptr;

        if (cJSON_IsArray(pl)) {
            int size = cJSON_GetArraySize(pl);
            for (int i = 0; i < size; i++) {
                cJSON* item = cJSON_GetArrayItem(pl, i);
                if (!item) continue;
                NavidromePlaylist playlist;
                auto id = cJSON_GetObjectItem(item, "id");
                if (cJSON_IsString(id)) playlist.id = id->valuestring;
                auto name = cJSON_GetObjectItem(item, "name");
                if (cJSON_IsString(name)) playlist.name = name->valuestring;
                auto sc = cJSON_GetObjectItem(item, "songCount");
                if (cJSON_IsNumber(sc)) playlist.song_count = sc->valueint;
                results.push_back(playlist);
            }
        }
        cJSON_Delete(root);
        return results;
    }

    std::string GetStreamUrl(const std::string& song_id) const {
        return BuildUrl("stream", "id=" + song_id);
    }

    NavidromeSong GetSong(const std::string& song_id) {
        NavidromeSong song;
        auto http = CreateHttp();
        if (!http) return song;

        std::string url = BuildUrl("getSong", "id=" + song_id);
        if (!http->Open("GET", url)) return song;
        if (http->GetStatusCode() != 200) { http->Close(); return song; }

        std::string body = http->ReadAll();
        http->Close();

        cJSON* root = cJSON_Parse(body.c_str());
        if (!root) return song;

        cJSON* sr = cJSON_GetObjectItem(root, "subsonic-response");
        cJSON* s = sr ? cJSON_GetObjectItem(sr, "song") : nullptr;
        if (s) {
            auto id = cJSON_GetObjectItem(s, "id");
            if (cJSON_IsString(id)) song.id = id->valuestring;
            auto title = cJSON_GetObjectItem(s, "title");
            if (cJSON_IsString(title)) song.title = title->valuestring;
            auto artist = cJSON_GetObjectItem(s, "artist");
            if (cJSON_IsString(artist)) song.artist = artist->valuestring;
            auto album = cJSON_GetObjectItem(s, "album");
            if (cJSON_IsString(album)) song.album = album->valuestring;
            auto dur = cJSON_GetObjectItem(s, "duration");
            if (cJSON_IsNumber(dur)) song.duration = dur->valueint;
            auto cover = cJSON_GetObjectItem(s, "coverArt");
            if (cJSON_IsString(cover)) song.cover_art_id = cover->valuestring;
        }
        cJSON_Delete(root);
        return song;
    }

private:
    NavidromeApi() { LoadConfig(); }

    std::unique_ptr<Http> CreateHttp() {
        auto network = Board::GetInstance().GetNetwork();
        if (!network) return nullptr;
        auto http = network->CreateHttp(2);
        if (!http) return nullptr;
        http->SetHeader("Accept", "audio/mpeg,application/json,*/*");
        // Set a short timeout for API calls
        return http;
    }

    static std::string UrlEncode(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                result += c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                result += buf;
            }
        }
        return result;
    }

    Config config_;
};

#endif // NAVIDROME_API_H
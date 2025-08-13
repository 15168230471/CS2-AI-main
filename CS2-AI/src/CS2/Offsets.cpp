#include "CS2/Offsets.h"
#include "CS2/Constants.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>
#include <QTimer>
#include <QString>
#include <QByteArray>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr int REQUEST_TIMEOUT_MS = 10000; // 10s
static constexpr char BASE_URL[] = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/";

static const fs::path kConfigDir = fs::path("Configuration");
static const fs::path kOffsetsPath = kConfigDir / "offsets.json";
static const fs::path kClientPath = kConfigDir / "client_dll.json";
static const fs::path kEngine2Path = kConfigDir / "engine2_dll.json";
static const fs::path kButtonsPath = kConfigDir / "buttons.json";

// ------- 工具函数 -------

static std::optional<QByteArray> http_get_sync(const std::string& url, int timeout_ms)
{
    QNetworkAccessManager manager;
    QNetworkRequest req(QUrl(QString::fromStdString(url)));
    // 一些服务端（含 GitHub Raw）对 UA/Accept 比较敏感，显式设置更稳
    req.setRawHeader("User-Agent", "QtNetwork/CS2-Offsets-Updater");
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = manager.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (reply) reply->abort();
        loop.quit();
        });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(timeout_ms > 0 ? timeout_ms : REQUEST_TIMEOUT_MS);
    loop.exec();

    if (!reply) {
        Logging::log_error("HTTP: reply is null for " + url);
        return std::nullopt;
    }

    if (reply->error() != QNetworkReply::NoError) {
        Logging::log_error("HTTP GET failed: " + url + " , error: " + reply->errorString().toStdString());
        reply->deleteLater();
        return std::nullopt;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}

static bool write_atomic_file(const fs::path& dst, const std::string& content)
{
    try {
        fs::create_directories(dst.parent_path());
        fs::path tmp = dst;
        tmp += ".part";

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs) return false;
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            ofs.flush();
            if (!ofs) return false;
        }

        // 原子替换
        fs::rename(tmp, dst);
        return true;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("write_atomic_file failed: ") + e.what());
        return false;
    }
}

static bool download_json_and_save(const std::string& url, const fs::path& save_path)
{
    auto data_opt = http_get_sync(url, REQUEST_TIMEOUT_MS);
    if (!data_opt) {
        Logging::log_error("Download skipped (request failed): " + url);
        return false;
    }

    // 先验证 JSON，避免把坏数据写入本地
    try {
        json j = json::parse(data_opt->constData(), data_opt->constData() + data_opt->size());
        // 规范化存储（也可以直接落原始 data；这里用 pretty 便于查看）
        std::string normalized = j.dump(2);
        if (!write_atomic_file(save_path, normalized)) {
            Logging::log_error("Failed to write file: " + save_path.string());
            return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("JSON validate failed for URL: ") + url + " , err: " + e.what());
        return false;
    }
}

static std::optional<json> load_json_file(const fs::path& p)
{
    try {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) {
            Logging::log_error("Open file failed: " + p.string());
            return std::nullopt;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        json j = json::parse(content);
        return j;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("Parse file failed: ") + p.string() + " , err: " + e.what());
        return std::nullopt;
    }
}

static void try_update_all_remote_jsons()
{
    // 逐个尝试下载；失败不影响后续，也不影响本地读取
    (void)download_json_and_save(std::string(BASE_URL) + "offsets.json", kOffsetsPath);
    (void)download_json_and_save(std::string(BASE_URL) + "client_dll.json", kClientPath);
    (void)download_json_and_save(std::string(BASE_URL) + "engine2_dll.json", kEngine2Path);
    (void)download_json_and_save(std::string(BASE_URL) + "buttons.json", kButtonsPath);
}

// ------- 业务入口 -------

std::optional<Offsets> load_offsets_from_files()
{
    // 1) 先尝试远程更新（失败也继续执行）
    try {
        try_update_all_remote_jsons();
    }
    catch (const std::exception& e) {
        // 极端情况下网络模块抛异常，这里也吞掉，避免影响后续本地读取
        Logging::log_error(std::string("Update remote jsons threw: ") + e.what());
    }

    // 2) 一律从本地读取（保持你原有“本地为准”的策略）
    auto general_json_opt = load_json_file(kOffsetsPath);
    auto client_json_opt = load_json_file(kClientPath);
    auto engine2_json_opt = load_json_file(kEngine2Path);
    auto buttons_json_opt = load_json_file(kButtonsPath);

    if (!general_json_opt || !client_json_opt || !engine2_json_opt || !buttons_json_opt) {
        Logging::log_error("One or more local JSON files missing or invalid under Configuration/.");
        return std::nullopt;
    }

    const auto& general_offsets_json = *general_json_opt;
    const auto& client_offsets_json = *client_json_opt;
    const auto& engine2_offsets_json = *engine2_json_opt; // 这里若你需要从 engine2_dll.json 取字段可直接用
    const auto& buttons_offsets_json = *buttons_json_opt;

    try {
        Offsets offsets{};

        offsets.entity_listelement_size = 0x10;
        offsets.local_player_controller_offset =
            static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwLocalPlayerController"]);
        offsets.entity_list_start_offset =
            static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwEntityList"]);
        offsets.local_player_pawn =
            static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwLocalPlayerPawn"]);
        offsets.global_vars =
            static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwGlobalVars"]);
        offsets.client_state_view_angle =
            static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwViewAngles"]);

        offsets.player_health_offset =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iHealth"]);
        offsets.player_pawn_handle =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CCSPlayerController"]["fields"]["m_hPlayerPawn"]);
        offsets.team_offset =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iTeamNum"]);
        offsets.sceneNode =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_pGameSceneNode"]);
        offsets.position =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_vOldOrigin"]);
        offsets.model_state =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CSkeletonInstance"]["fields"]["m_modelState"]);

        offsets.gun_game_immunity =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_bGunGameImmunity"]);

        offsets.m_pWeaponServices =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_pWeaponServices"]);
        offsets.m_hActiveWeapon =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CPlayer_WeaponServices"]["fields"]["m_hActiveWeapon"]);
        offsets.m_iClip1 =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerWeapon"]["fields"]["m_iClip1"]);

        offsets.shots_fired_offset =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_iShotsFired"]);

        offsets.m_pInventoryServices =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CCSPlayerController"]["fields"]["m_pInventoryServices"]);

        offsets.m_rank =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CCSPlayerController_InventoryServices"]["fields"]["m_nPersonaDataPublicLevel"]);

        // 按键
        offsets.force_attack = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["attack"]);
        offsets.force_forward = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["forward"]);
        offsets.force_backward = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["back"]);
        offsets.force_left = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["left"]);
        offsets.force_right = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["right"]);

        offsets.crosshair_offset =
            static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawnBase"]["fields"]["m_iIDEntIndex"]);

        // 从 offsets.json 里读取 engine2.dll 的两个字段（按你原先逻辑）
        const auto& engine_offsets = general_offsets_json["engine2.dll"];
        offsets.network_game_client =
            static_cast<uintptr_t>(engine_offsets["dwNetworkGameClient"]);
        offsets.network_game_client_is_background_map =
            static_cast<uintptr_t>(engine_offsets["dwNetworkGameClient_isBackgroundMap"]);

        return offsets;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("Assemble Offsets failed: ") + e.what());
        return std::nullopt;
    }
}

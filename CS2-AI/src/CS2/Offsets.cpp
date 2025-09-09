#include "CS2/Offsets.h"
#include "CS2/Constants.h"
#include "Utility/json.hpp"
#include "Utility/Logging.h"

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
#include <cerrno>
#include <cstdlib>
#include <QDebug>   // 新增：为 qInfo()

using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr int REQUEST_TIMEOUT_MS = 10000; // 10s
//static constexpr char BASE_URL[] = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/";
static constexpr char BASE_URL[] = "";  //未更新就用本地的

static const fs::path kConfigDir = fs::path("Configuration");
static const fs::path kOffsetsPath = kConfigDir / "offsets.json";
static const fs::path kClientPath = kConfigDir / "client_dll.json";
static const fs::path kEngine2Path = kConfigDir / "engine2_dll.json";
static const fs::path kButtonsPath = kConfigDir / "buttons.json";

// ---------- 小工具 ----------

static const char* jtype(const json& v) {
    using t = nlohmann::json::value_t;
    switch (v.type()) {
    case t::null:            return "null";
    case t::object:          return "object";
    case t::array:           return "array";
    case t::string:          return "string";
    case t::boolean:         return "boolean";
    case t::number_integer:  return "number_integer";
    case t::number_unsigned: return "number_unsigned";
    case t::number_float:    return "number_float";
    default:                 return "unknown";
    }
}

// 允许：数字(整/无符号/浮点) 或 字符串("0x1234"/"1234")
// 失败会把 path_for_log + 实际类型/原始值 打进日志，并抛异常给上层
static uint64_t require_u64(const json& v, const char* path_for_log) {
    try {
        if (v.is_number_unsigned() || v.is_number_integer())
            return v.get<uint64_t>();
        if (v.is_number_float())
            return static_cast<uint64_t>(v.get<double>());

        if (v.is_string()) {
            std::string s = v.get<std::string>();
            int base = 10;
            if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) { base = 16; s = s.substr(2); }
            errno = 0;
            char* end = nullptr;
            unsigned long long val = std::strtoull(s.c_str(), &end, base);
            if (errno == 0 && end && *end == '\0')
                return static_cast<uint64_t>(val);
            throw std::runtime_error(std::string("bad numeric string value=") + v.dump());
        }

        throw std::runtime_error(std::string("type=") + jtype(v) + " value=" + v.dump());
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("[Offsets] key '") + path_for_log + "' invalid: " + e.what());
        throw;
    }
}

static std::optional<QByteArray> http_get_sync(const std::string& url, int timeout_ms) {
    QNetworkAccessManager manager;
    QNetworkRequest req(QUrl(QString::fromStdString(url)));
    req.setRawHeader("User-Agent", "QtNetwork/Offsets-Updater");
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

static bool write_atomic_file(const fs::path& dst, const std::string& content) {
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
        fs::rename(tmp, dst);
        return true;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("write_atomic_file failed: ") + e.what());
        return false;
    }
}

static bool download_json_and_save(const std::string& url, const fs::path& save_path) {
    auto data_opt = http_get_sync(url, REQUEST_TIMEOUT_MS);
    if (!data_opt) {
        Logging::log_error("Download skipped (request failed): " + url);
        return false;
    }
    try {
        json j = json::parse(data_opt->constData(), data_opt->constData() + data_opt->size());
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

static std::optional<json> load_json_file(const fs::path& p) {
    try {
        const auto abs = fs::absolute(p).string();
        qInfo().noquote() << "[JSON] Reading:" << QString::fromStdString(abs);


        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) {
            Logging::log_error("Open file failed: " + p.string());
            return std::nullopt;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        // 忽略注释，提高容错
        json j = json::parse(content, nullptr, /*allow_exceptions*/ true, /*ignore_comments*/ true);
        return j;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("Parse file failed: ") + p.string() + " , err: " + e.what());
        return std::nullopt;
    }
}

static void try_update_all_remote_jsons() {
    (void)download_json_and_save(std::string(BASE_URL) + "offsets.json", kOffsetsPath);
    (void)download_json_and_save(std::string(BASE_URL) + "client_dll.json", kClientPath);
    (void)download_json_and_save(std::string(BASE_URL) + "engine2_dll.json", kEngine2Path);
    (void)download_json_and_save(std::string(BASE_URL) + "buttons.json", kButtonsPath);
}

// ---------- 业务入口 ----------

std::optional<Offsets> load_offsets_from_files() {
    // 1) 先尝试远程更新（失败也继续执行）
    try {
        try_update_all_remote_jsons();
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("Update remote jsons threw: ") + e.what());
    }

    // 2) 统一本地读取
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
    const auto& engine2_offsets_json = *engine2_json_opt;
    const auto& buttons_offsets_json = *buttons_json_opt;

    try {
        Offsets offsets{};

        // 固定常量
        offsets.entity_listelement_size = 0x10;

        // ---- offsets.json -> client.dll ----
        offsets.local_player_controller_offset =
            static_cast<uintptr_t>(require_u64(general_offsets_json["client.dll"]["dwLocalPlayerController"],
                "offsets.json: client.dll.dwLocalPlayerController"));

        offsets.entity_list_start_offset =
            static_cast<uintptr_t>(require_u64(general_offsets_json["client.dll"]["dwEntityList"],
                "offsets.json: client.dll.dwEntityList"));

        offsets.local_player_pawn =
            static_cast<uintptr_t>(require_u64(general_offsets_json["client.dll"]["dwLocalPlayerPawn"],
                "offsets.json: client.dll.dwLocalPlayerPawn"));

        offsets.global_vars =
            static_cast<uintptr_t>(require_u64(general_offsets_json["client.dll"]["dwGlobalVars"],
                "offsets.json: client.dll.dwGlobalVars"));

        offsets.client_state_view_angle =
            static_cast<uintptr_t>(require_u64(general_offsets_json["client.dll"]["dwViewAngles"],
                "offsets.json: client.dll.dwViewAngles"));

        // ---- client_dll.json -> classes/fields ----
        offsets.player_health_offset =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iHealth"],
                "client_dll.json: C_BaseEntity.fields.m_iHealth"));

        offsets.player_pawn_handle =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["CCSPlayerController"]["fields"]["m_hPlayerPawn"],
                "client_dll.json: CCSPlayerController.fields.m_hPlayerPawn"));

        offsets.team_offset =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iTeamNum"],
                "client_dll.json: C_BaseEntity.fields.m_iTeamNum"));

        offsets.sceneNode =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_pGameSceneNode"],
                "client_dll.json: C_BaseEntity.fields.m_pGameSceneNode"));

        offsets.position =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_vOldOrigin"],
                "client_dll.json: C_BasePlayerPawn.fields.m_vOldOrigin"));

        offsets.model_state =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["CSkeletonInstance"]["fields"]["m_modelState"],
                "client_dll.json: CSkeletonInstance.fields.m_modelState"));

        offsets.gun_game_immunity =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_bGunGameImmunity"],
                "client_dll.json: C_CSPlayerPawn.fields.m_bGunGameImmunity"));

        offsets.m_pWeaponServices =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_pWeaponServices"],
                "client_dll.json: C_BasePlayerPawn.fields.m_pWeaponServices"));

        offsets.m_hActiveWeapon =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["CPlayer_WeaponServices"]["fields"]["m_hActiveWeapon"],
                "client_dll.json: CPlayer_WeaponServices.fields.m_hActiveWeapon"));

        offsets.m_iClip1 =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_BasePlayerWeapon"]["fields"]["m_iClip1"],
                "client_dll.json: C_BasePlayerWeapon.fields.m_iClip1"));

        offsets.shots_fired_offset =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_iShotsFired"],
                "client_dll.json: C_CSPlayerPawn.fields.m_iShotsFired"));
        offsets.crosshair_offset =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_iIDEntIndex"],
                "client_dll.json: C_CSPlayerPawn.fields.m_iIDEntIndex"));
        offsets.m_pInventoryServices =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["CCSPlayerController"]["fields"]["m_pInventoryServices"],
                "client_dll.json: CCSPlayerController.fields.m_pInventoryServices"));

        offsets.m_rank =
            static_cast<uintptr_t>(require_u64(
                client_offsets_json["client.dll"]["classes"]["CCSPlayerController_InventoryServices"]["fields"]["m_nPersonaDataPublicLevel"],
                "client_dll.json: CCSPlayerController_InventoryServices.fields.m_nPersonaDataPublicLevel"));

        offsets.entity_spotted_state = static_cast<uintptr_t>(
            client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_entitySpottedState"]);
        // 可选：如果希望从 JSON 获取内部偏移，也可以读 EntitySpottedState_t 中的 m_bSpotted（值为 8）
        offsets.entity_spotted_state_bSpotted = static_cast<uintptr_t>(
            client_offsets_json["client.dll"]["classes"]["EntitySpottedState_t"]["fields"]["m_bSpotted"]);


        // ---- buttons.json ----
        offsets.force_attack =
            static_cast<uintptr_t>(require_u64(buttons_offsets_json["client.dll"]["attack"],
                "buttons.json: client.dll.attack"));
        offsets.force_forward =
            static_cast<uintptr_t>(require_u64(buttons_offsets_json["client.dll"]["forward"],
                "buttons.json: client.dll.forward"));
        offsets.force_backward =
            static_cast<uintptr_t>(require_u64(buttons_offsets_json["client.dll"]["back"],
                "buttons.json: client.dll.back"));
        offsets.force_left =
            static_cast<uintptr_t>(require_u64(buttons_offsets_json["client.dll"]["left"],
                "buttons.json: client.dll.left"));
        offsets.force_right =
            static_cast<uintptr_t>(require_u64(buttons_offsets_json["client.dll"]["right"],
                "buttons.json: client.dll.right"));

        // ---- offsets.json -> engine2.dll（按你原有逻辑读取两个字段） ----
        const auto& engine_offsets = general_offsets_json["engine2.dll"];
        offsets.network_game_client =
            static_cast<uintptr_t>(require_u64(engine_offsets["dwNetworkGameClient"],
                "offsets.json: engine2.dll.dwNetworkGameClient"));
        offsets.network_game_client_is_background_map =
            static_cast<uintptr_t>(require_u64(engine_offsets["dwNetworkGameClient_isBackgroundMap"],
                "offsets.json: engine2.dll.dwNetworkGameClient_isBackgroundMap"));

        return offsets;
    }
    catch (const std::exception& e) {
        Logging::log_error(std::string("Assemble Offsets failed: ") + e.what());
        return std::nullopt;
    }
}
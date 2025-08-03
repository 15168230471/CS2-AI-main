#include "CS2/Offsets.h"

#include "CS2/Constants.h"

// 引入 Qt 网络模块用于 HTTP 请求
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>
#include <QObject>
#include <QString>

using json = nlohmann::json;
static constexpr int HEX = 16;
static json offsets_json;

/**
 * @brief 同步从远程 URL 下载并解析 JSON。
 *
 * 使用 QNetworkAccessManager 发送 GET 请求并等待完成。若请求失败或解析错误，
 * 返回 std::nullopt。
 */
static std::optional<json> fetch_remote_json(const std::string& url)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        Logging::log_error(std::string("Failed to fetch URL: ") + url + ", error: " + reply->errorString().toStdString());
        reply->deleteLater();
        return std::nullopt;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    try {
        return json::parse(data.constData());
    }
    catch (std::exception const&) {
        Logging::log_error(std::string("Failed to parse JSON from URL: ") + url);
        return std::nullopt;
    }
}


std::optional<Offsets> load_offsets_from_files()
{
    auto offsets = Offsets{};
    try
    {
        // 构建远程 URL
        constexpr char BASE_URL[] = "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/";
        const std::string general_url = std::string(BASE_URL) + "offsets.json";
        const std::string client_url = std::string(BASE_URL) + "client_dll.json";
        const std::string engine_url = std::string(BASE_URL) + "engine2_dll.json";
        const std::string buttons_url = std::string(BASE_URL) + "buttons.json";

        // 下载偏移 JSON
        auto general_json_opt = fetch_remote_json(general_url);
        auto client_json_opt = fetch_remote_json(client_url);
        auto engine_json_opt = fetch_remote_json(engine_url);
        auto buttons_json_opt = fetch_remote_json(buttons_url);

        if (!general_json_opt || !client_json_opt || !buttons_json_opt)
        {
            Logging::log_error("One or more offset JSON files could not be downloaded.");
            return {};
        }

        const auto& general_offsets_json = general_json_opt.value();
        const auto& client_offsets_json = client_json_opt.value();
        const auto& buttons_offsets_json = buttons_json_opt.value();

        offsets.entity_listelement_size = 0x10;
        offsets.local_player_controller_offset = static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwLocalPlayerController"]);
        offsets.entity_list_start_offset = static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwEntityList"]);
        offsets.local_player_pawn = static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwLocalPlayerPawn"]);
        offsets.global_vars = static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwGlobalVars"]);
        offsets.client_state_view_angle = static_cast<uintptr_t>(general_offsets_json["client.dll"]["dwViewAngles"]);


        offsets.player_health_offset = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iHealth"]);
        offsets.player_pawn_handle = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CCSPlayerController"]["fields"]["m_hPlayerPawn"]);
        offsets.team_offset = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_iTeamNum"]);
        offsets.sceneNode = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BaseEntity"]["fields"]["m_pGameSceneNode"]);
        offsets.position = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_vOldOrigin"]);
        offsets.model_state = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CSkeletonInstance"]["fields"]["m_modelState"]);

        offsets.gun_game_immunity = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_bGunGameImmunity"]);

        offsets.m_pWeaponServices = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerPawn"]["fields"]["m_pWeaponServices"]);
        offsets.m_hActiveWeapon = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["CPlayer_WeaponServices"]["fields"]["m_hActiveWeapon"]);
        offsets.m_iClip1 = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_BasePlayerWeapon"]["fields"]["m_iClip1"]);

        offsets.shots_fired_offset = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawn"]["fields"]["m_iShotsFired"]);

        // 按键偏移量
        offsets.force_attack = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["attack"]);
        offsets.force_forward = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["forward"]);
        offsets.force_backward = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["back"]);
        offsets.force_left = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["left"]);
        offsets.force_right = static_cast<uintptr_t>(buttons_offsets_json["client.dll"]["right"]);

        offsets.crosshair_offset = static_cast<uintptr_t>(client_offsets_json["client.dll"]["classes"]["C_CSPlayerPawnBase"]["fields"]["m_iIDEntIndex"]);
        
        
        // engine2.dll 的 dwNetworkGameClient 和 dwNetworkGameClient_isBackgroundMap
        const auto& engine_offsets = general_offsets_json["engine2.dll"];
        offsets.network_game_client = static_cast<uintptr_t>(engine_offsets["dwNetworkGameClient"]);
        offsets.network_game_client_is_background_map =
            static_cast<uintptr_t>(engine_offsets["dwNetworkGameClient_isBackgroundMap"]);

    }
    catch (std::exception const& e)
    {
        Logging::log_error(std::string(e.what()));
        return {};
    }

    return offsets;
}

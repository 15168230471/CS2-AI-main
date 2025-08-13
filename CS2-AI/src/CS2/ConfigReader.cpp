#include "CS2/ConfigReader.h"
#include "CS2/Constants.h"

#include <QCoreApplication>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>

using json = nlohmann::json;

std::optional<Config> Config::read_in_config_data()
{
    namespace fs = std::filesystem;

    // 1) 取得可执行文件所在目录
    QString exeDirQ = QCoreApplication::applicationDirPath();
    fs::path exeDir = exeDirQ.toStdString();

    // 2) 构造 <exeDir>/Configuration/config.json 的绝对路径
    fs::path configPath = exeDir / "Configuration" / "config.json";


    // 3) 最多尝试读取 50 次（≈5 秒）
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        // 文件必须存在并且大小 > 0
        if (fs::exists(configPath) && fs::file_size(configPath) > 0)
        {
            std::ifstream ifs(configPath);
            try
            {
                auto config_json = json::parse(ifs);

                Config result;
                result.client_dll_name = config_json.at("client_dll_name").get<std::string>();
                result.engine_dll_name = config_json.at("engine_dll_name").get<std::string>();
                result.windowname = config_json.at("window_name").get<std::string>();
                result.trigger_button = static_cast<DWORD>(config_json.at("trigger_button").get<int>());
                result.delay = config_json.at("delay").get<int>();

                return result;
            }
            catch (const nlohmann::json::parse_error& e)
            {
                Logging::log_error(std::string("[Config] parse error, retrying: ") + e.what());
            }
            catch (const nlohmann::json::type_error& e)
            {
                Logging::log_error(std::string("[Config] type error, retrying: ") + e.what());
            }
            catch (const std::exception& e)
            {
                Logging::log_error(std::string("[Config] unexpected error: ") + e.what());
                // 如果是关键字段缺失、你确认不会动态变化，也可以 break;
            }
        }

        // 等待 100 ms 后再试
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 所有重试都失败
    Logging::log_error("Config couldn't be read after retries, make sure you have a valid config file");
    return std::nullopt;
}

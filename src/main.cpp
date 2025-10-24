#include "App.h"
#include "argparse.hpp"
#include "flags.h"
#include "modules/DemoModule.h"
#include "modules/GraphingDataModule.h"
#include "modules/NumericDataModule.h"
#include <memory>
#include <print>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

bool flags::enableHardwareMock = false;
int flags::logLevel = 2;

int main(int argc, char* argv[])
{
    /////////////////////////////////////////////////////////////////
    // Argument parsing
    /////////////////////////////////////////////////////////////////
    std::string versionInfo = "Workbench Screens App\nVersion: ";
    versionInfo += "1.0";
    versionInfo += "\n";
    versionInfo += "Built on: " + std::string(__DATE__) + " " + std::string(__TIME__) + "\n";
    versionInfo += "Built on: ";
    versionInfo += "Linux";
    versionInfo += "\nCompiler: ";
#ifdef __clang__
    versionInfo += "Clang";
#endif
#ifdef __GNUC__
    versionInfo += "GCC";
#endif
    versionInfo += "\nC++ Standard: ";
#ifdef __cplusplus
    if (__cplusplus == 202300L)
        versionInfo += "C++23";
    else if (__cplusplus == 202100L)
        versionInfo += "C++21";
    else if (__cplusplus == 202002L)
        versionInfo += "C++20";
    else if (__cplusplus == 201703L)
        versionInfo += "C++17";
    else if (__cplusplus == 201402L)
        versionInfo += "C++14";
    else if (__cplusplus == 201103L)
        versionInfo += "C++11";
    else
        versionInfo += std::to_string(__cplusplus);
#endif
    versionInfo += "\nAuthor: Andrew McDaniel\nCopyright: 2025\n";
    argparse::ArgumentParser argumentParser("Workbench Screens App", versionInfo);

    argumentParser.add_argument("--enable-hardware-mock")
        .help("Enable hardware mock for testing without real hardware")
        .default_value(false)
        .implicit_value(true);
    argumentParser.add_argument("--log-level")
        .help("Set log verbosity level (0=error, 1=warning, 2=info, 3=debug, 4=trace)")
        .default_value(2)
        .action([&](const std::string& value) {
            int valueInt = 2;
            try{
                valueInt = std::stoi(value);
            }catch(...) {
                throw std::invalid_argument("Log level must be an integer between 0 and 4");
            }
            if(valueInt < 0 || valueInt > 4) {
                throw std::invalid_argument("Log level must be between 0 and 4");
            }
            return std::stoi(value);
        });
    try {
        argumentParser.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    
    flags::enableHardwareMock = argumentParser.get<bool>("--enable-hardware-mock");
    flags::logLevel = argumentParser.get<int>("--log-level");
    
    // Initialize spdlog rotating file logger
    try {
        namespace fs = std::filesystem;
        fs::create_directories("logs");
        auto logger = spdlog::rotating_logger_mt("workbench", "logs/workbench.log", 5 * 1024 * 1024, 3);
        // Map numeric log level to spdlog level
        spdlog::level::level_enum level = spdlog::level::info;
        switch (flags::logLevel) {
        case 0:
            level = spdlog::level::err;
            break;
        case 1:
            level = spdlog::level::warn;
            break;
        case 2:
            level = spdlog::level::info;
            break;
        case 3:
            level = spdlog::level::debug;
            break;
        case 4:
            level = spdlog::level::trace;
            break;
        default:
            level = spdlog::level::info;
            break;
        }
        spdlog::set_default_logger(logger);
        spdlog::set_level(level);
        spdlog::info("Starting Workbench Screens (log level {})", flags::logLevel);
    } catch (const std::exception& ex) {
        std::cerr << "Failed to initialize logging: " << ex.what() << std::endl;
    }

    App app;
    app.setHardwareMockEnabled(flags::enableHardwareMock);
    app.registerModule(std::make_unique<DemoModule>());
    app.registerModule(std::make_unique<NumericDataModule>());
    app.registerModule(std::make_unique<GraphingDataModule>());
    return app.run();
}

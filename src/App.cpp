#include "App.h"

#include <algorithm>
#include <utility>

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/color.hpp>
#include <spdlog/spdlog.h>

App::App()
    : hardwareService_ { dataRegistry_ }
    , moduleContext_ { dataRegistry_, hardwareService_ }
    , pluginManager_(moduleContext_)
    , dashboard_(moduleContext_)
{
}

void App::setHardwareMockEnabled(bool enabled)
{
    auto opts = hardware::HardwareServiceClient::Options();
    opts.enableMock = enabled;
    hardwareService_.configure(opts);

    // If mock mode is requested, register the mock source metadata synchronously so
    // UI code that queries the DataRegistry during bootstrap can discover it.
    if (enabled) {
        core::SourceMetadata meta;
        meta.id = "mock.12v";
        meta.name = "12V Supply";
        meta.kind = core::DataKind::Numeric;
        meta.unit = std::string("V");
        dataRegistry_.registerSource(meta);
        spdlog::info("Hardware mock enabled; registered mock source '{}'", meta.id);
    }
}

void App::registerModule(core::ModulePtr module)
{
    pluginManager_.registerModule(std::move(module));
    modulesBootstrapped_ = false;
}

int App::run()
{
    spdlog::info("Starting hardware service");
    hardwareService_.start();
    bootstrapModules();

    auto component = dashboard_.build();
    if (component) {
        ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
        auto screen = ftxui::ScreenInteractive::Fullscreen();
        screen.Loop(component);
    }

    spdlog::info("Shutting down modules and hardware service");
    pluginManager_.shutdownModules();
    hardwareService_.stop();
    modulesBootstrapped_ = false;
    return 0;
}

core::DataRegistry& App::dataRegistry()
{
    return dataRegistry_;
}

hardware::HardwareServiceClient& App::hardwareService()
{
    return hardwareService_;
}

void App::bootstrapModules()
{
    if (modulesBootstrapped_) {
        return;
    }

    pluginManager_.initializeModules();

    registeredWindows_.clear();
    for (const auto& module : pluginManager_.modules()) {
        auto windows = module->createDefaultWindows(moduleContext_);
        registeredWindows_.insert(registeredWindows_.end(), windows.begin(), windows.end());
    }

    dashboard_.setAvailableWindows(registeredWindows_);
    openDefaultWindows();
    modulesBootstrapped_ = true;
}

void App::openDefaultWindows()
{
    for (const auto& spec : registeredWindows_) {
        if (spec.openByDefault) {
            dashboard_.addWindow(spec);
        }
    }
}

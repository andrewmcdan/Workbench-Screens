#include "App.h"

#include <algorithm>
#include <utility>

#include <ftxui/component/screen_interactive.hpp>

App::App()
    : hardwareService_{dataRegistry_},
      moduleContext_{dataRegistry_, hardwareService_},
      pluginManager_(moduleContext_),
      dashboard_(moduleContext_) {}

void App::registerModule(core::ModulePtr module) {
    pluginManager_.registerModule(std::move(module));
    modulesBootstrapped_ = false;
}

int App::run() {
    hardwareService_.start();
    bootstrapModules();

    auto component = dashboard_.build();
    if (component) {
        auto screen = ftxui::ScreenInteractive::Fullscreen();
        screen.Loop(component);
    }

    pluginManager_.shutdownModules();
    hardwareService_.stop();
    modulesBootstrapped_ = false;
    return 0;
}

core::DataRegistry& App::dataRegistry() {
    return dataRegistry_;
}

hardware::HardwareServiceClient& App::hardwareService() {
    return hardwareService_;
}

void App::bootstrapModules() {
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

void App::openDefaultWindows() {
    for (const auto& spec : registeredWindows_) {
        if (spec.openByDefault) {
            dashboard_.addWindow(spec);
        }
    }
}

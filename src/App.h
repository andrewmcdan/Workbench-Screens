#pragma once

#pragma once

#include "core/DataRegistry.h"
#include "core/Module.h"
#include "core/ModuleContext.h"
#include "core/PluginManager.h"
#include "hardware/HardwareServiceClient.h"
#include "ui/Dashboard.h"

#include <string>
#include <vector>

class App {
public:
    App();

    void registerModule(core::ModulePtr module);
    void setHardwareMockEnabled(bool enabled);
    int run();

    core::DataRegistry& dataRegistry();
    hardware::HardwareServiceClient& hardwareService();

private:
    void bootstrapModules();
    void openDefaultWindows();

    core::DataRegistry dataRegistry_;
    hardware::HardwareServiceClient hardwareService_;
    core::ModuleContext moduleContext_;
    core::PluginManager pluginManager_;
    ui::Dashboard dashboard_;
    std::vector<ui::WindowSpec> registeredWindows_;
    bool modulesBootstrapped_{false};
};

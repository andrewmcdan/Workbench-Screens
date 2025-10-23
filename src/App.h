#pragma once

#pragma once

#include "core/DataRegistry.h"
#include "core/Module.h"
#include "core/ModuleContext.h"
#include "core/PluginManager.h"
#include "hardware/TeensyLink.h"
#include "ui/Dashboard.h"

#include <string>
#include <vector>

class App {
public:
    App();

    void registerModule(core::ModulePtr module);
    int run();

    core::DataRegistry& dataRegistry();
    hardware::TeensyLink& teensyLink();

private:
    void bootstrapModules();
    void openDefaultWindows();

    core::DataRegistry dataRegistry_;
    hardware::TeensyLink teensyLink_;
    core::ModuleContext moduleContext_;
    core::PluginManager pluginManager_;
    ui::Dashboard dashboard_;
    std::vector<ui::WindowSpec> registeredWindows_;
    bool modulesBootstrapped_{false};
};

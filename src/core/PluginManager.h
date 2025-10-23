#pragma once

#include "Module.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

class PluginManager {
public:
    explicit PluginManager(ModuleContext& context);
    ~PluginManager();

    void registerModule(ModulePtr module);

    void initializeModules();
    void shutdownModules();
    void tickModules(std::chrono::milliseconds delta);

    [[nodiscard]] const std::vector<ModulePtr>& modules() const;

private:
    ModuleContext& context_;
    std::vector<ModulePtr> modules_;
    std::unordered_map<std::string, std::vector<std::string>> moduleSources_;
    bool initialized_{false};
};

}  // namespace core

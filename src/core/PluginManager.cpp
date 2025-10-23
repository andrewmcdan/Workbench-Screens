#include "PluginManager.h"

#include "DataRegistry.h"

#include <utility>

namespace core {

PluginManager::PluginManager(ModuleContext& context)
    : context_(context) {}

PluginManager::~PluginManager() {
    shutdownModules();
}

void PluginManager::registerModule(ModulePtr module) {
    if (!module) {
        return;
    }

    const std::string moduleId = module->id();
    auto* modulePtr = module.get();
    moduleSources_.try_emplace(moduleId);
    modules_.push_back(std::move(module));

    if (initialized_) {
        auto declared = modulePtr->declareSources();
        auto& ids = moduleSources_[moduleId];
        ids.clear();
        for (auto& meta : declared) {
            ids.push_back(meta.id);
            context_.dataRegistry.registerSource(std::move(meta));
        }
        modulePtr->initialize(context_);
    }
}

void PluginManager::initializeModules() {
    if (initialized_) {
        return;
    }

    for (auto& module : modules_) {
        auto& ids = moduleSources_[module->id()];
        ids.clear();
        for (auto& meta : module->declareSources()) {
            ids.push_back(meta.id);
            context_.dataRegistry.registerSource(std::move(meta));
        }
    }

    for (auto& module : modules_) {
        module->initialize(context_);
    }

    initialized_ = true;
}

void PluginManager::shutdownModules() {
    if (!initialized_) {
        return;
    }

    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
        auto& module = *it;
        module->shutdown(context_);
        auto mapIt = moduleSources_.find(module->id());
        if (mapIt != moduleSources_.end()) {
            for (const auto& sourceId : mapIt->second) {
                context_.dataRegistry.unregisterSource(sourceId);
            }
        }
    }

    initialized_ = false;
}

void PluginManager::tickModules(std::chrono::milliseconds delta) {
    if (!initialized_) {
        return;
    }

    for (auto& module : modules_) {
        module->tick(context_, delta);
    }
}

const std::vector<ModulePtr>& PluginManager::modules() const {
    return modules_;
}

}  // namespace core

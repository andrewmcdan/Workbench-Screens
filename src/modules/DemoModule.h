#pragma once

#include "core/Module.h"

#include <chrono>

class DemoModule : public core::Module {
public:
    DemoModule();

    std::string id() const override;
    std::string displayName() const override;

    void initialize(core::ModuleContext& context) override;
    void shutdown(core::ModuleContext& context) override;

    std::vector<core::SourceMetadata> declareSources() override;
    std::vector<ui::WindowSpec> createDefaultWindows(core::ModuleContext& context) override;

    void tick(core::ModuleContext& context, std::chrono::milliseconds delta) override;

private:
    double voltage_{0.0};
    std::chrono::milliseconds accumulator_{0};
};

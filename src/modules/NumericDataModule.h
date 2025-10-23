#pragma once

#include "core/Module.h"
#include "ui/WindowSpec.h"

#include <chrono>

class NumericDataModule : public core::Module {
public:
    NumericDataModule();
    ~NumericDataModule() override = default;

    std::string id() const override;
    std::string displayName() const override;

    void initialize(core::ModuleContext& context) override;
    void shutdown(core::ModuleContext& context) override;

    std::vector<core::SourceMetadata> declareSources() override;
    std::vector<ui::WindowSpec> createDefaultWindows(core::ModuleContext& context) override;

private:
    core::ModuleContext* context_{nullptr};
};

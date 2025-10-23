#pragma once

#include "ModuleContext.h"
#include "Types.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace ui {
struct WindowSpec;
}

namespace core {

class Module {
public:
    virtual ~Module() = default;

    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;

    virtual void initialize(ModuleContext& context) = 0;
    virtual void shutdown(ModuleContext& context) = 0;

    virtual std::vector<SourceMetadata> declareSources() = 0;
    virtual std::vector<ui::WindowSpec> createDefaultWindows(ModuleContext& context) = 0;

    virtual void tick(ModuleContext& context, std::chrono::milliseconds delta) {
        (void)context;
        (void)delta;
    }
};

using ModulePtr = std::unique_ptr<Module>;

}  // namespace core

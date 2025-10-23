#pragma once

#include "core/ModuleContext.h"

#include <cassert>
#include <functional>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>

namespace ui {

struct WindowContext {
    core::ModuleContext* moduleContext{nullptr};
    std::string windowId;

    [[nodiscard]] core::ModuleContext& module() const {
        assert(moduleContext != nullptr);
        return *moduleContext;
    }
};

using ComponentFactory = std::function<ftxui::Component(WindowContext&)>;

struct WindowSpec {
    std::string id;
    std::string title;
    ComponentFactory componentFactory;
    bool closable{true};
    bool cloneable{true};
    bool openByDefault{false};
    int defaultLeft{8};
    int defaultTop{4};
    int defaultWidth{40};
    int defaultHeight{14};
    bool resizeLeft{true};
    bool resizeRight{true};
    bool resizeTop{true};
    bool resizeBottom{true};
};

}  // namespace ui

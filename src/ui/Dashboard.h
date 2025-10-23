#pragma once

#include "ui/WindowSpec.h"

#include <vector>

namespace ui {

class Dashboard {
public:
    explicit Dashboard(core::ModuleContext& moduleContext);

    void setAvailableWindows(std::vector<WindowSpec> specs);

    std::string addWindow(const WindowSpec& spec);
    std::string addWindowById(const std::string& specId);
    std::string addWindowByIndex(int index);

    bool cloneWindow(const std::string& instanceId);
    bool closeWindow(const std::string& instanceId);

    [[nodiscard]] ftxui::Component build();

    [[nodiscard]] const std::vector<WindowSpec>& availableWindows() const;
    [[nodiscard]] std::vector<std::string> activeWindowIds() const;

private:
    struct WindowInstance {
        std::string instanceId;
        WindowSpec spec;
        WindowContext context;
        ftxui::Component component;
        int left{8};
        int top{4};
        int width{40};
        int height{14};
        bool resizeLeft{true};
        bool resizeRight{true};
        bool resizeTop{true};
        bool resizeBottom{true};
    };

    WindowSpec* findSpec(const std::string& specId);
    WindowInstance* findInstance(const std::string& instanceId);
    std::string generateInstanceId(const WindowSpec& spec);
    void ensureComponent(WindowInstance& instance);
    void ensureRootInitialized();
    void refreshWindowComponents();
    void markLayoutDirty();
    void updateAvailableWindowTitles();
    void clampSelectedWindowIndex();
    ftxui::Component buildHeader();
    ftxui::Component buildWindowArea();
    ftxui::Component buildWindowComponent(WindowInstance& instance);

    core::ModuleContext& moduleContext_;
    std::vector<WindowSpec> availableWindows_;
    std::vector<WindowInstance> activeWindows_;
    std::vector<std::string> availableWindowTitles_;
    int selectedWindowIndex_{0};
    int nextWindowIndex_{1};
    int cascadeOffset_{0};
    bool layoutDirty_{true};
    ftxui::Component root_;
};

}  // namespace ui

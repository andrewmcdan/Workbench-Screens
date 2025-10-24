#include "Dashboard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/util/ref.hpp>

namespace ui {

Dashboard::Dashboard(core::ModuleContext& moduleContext)
    : moduleContext_(moduleContext)
{
}

void Dashboard::setAvailableWindows(std::vector<WindowSpec> specs)
{
    availableWindows_ = std::move(specs);
    updateAvailableWindowTitles();
    clampSelectedWindowIndex();
    markLayoutDirty();
}

std::string Dashboard::addWindow(const WindowSpec& spec)
{
    const std::string id = generateInstanceId(spec);
    WindowInstance instance;
    instance.instanceId = id;
    instance.spec = spec;
    instance.context.moduleContext = &moduleContext_;
    instance.context.windowId = id;
    instance.left = spec.defaultLeft + cascadeOffset_;
    instance.top = spec.defaultTop + cascadeOffset_;
    instance.width = std::max(10, spec.defaultWidth);
    instance.height = std::max(6, spec.defaultHeight);
    instance.resizeLeft = spec.resizeLeft;
    instance.resizeRight = spec.resizeRight;
    instance.resizeTop = spec.resizeTop;
    instance.resizeBottom = spec.resizeBottom;
    instance.renameLines = { spec.title, std::string {}, std::string {} };
    activeWindows_.insert(activeWindows_.begin(), std::move(instance));
    auto& stored = activeWindows_.front();
    ensureComponent(stored);
    cascadeOffset_ = (cascadeOffset_ + 2) % 20;
    markLayoutDirty();
    return id;
}

std::string Dashboard::addWindowById(const std::string& specId)
{
    if (auto* spec = findSpec(specId)) {
        return addWindow(*spec);
    }
    return {};
}

std::string Dashboard::addWindowByIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(availableWindows_.size())) {
        return {};
    }
    return addWindow(availableWindows_[static_cast<std::size_t>(index)]);
}

bool Dashboard::cloneWindow(const std::string& instanceId)
{
    if (auto* instance = findInstance(instanceId)) {
        if (!instance->spec.cloneable) {
            return false;
        }
        WindowSpec specCopy = instance->spec;
        const std::string id = generateInstanceId(specCopy);
        WindowInstance clone;
        clone.instanceId = id;
        clone.spec = std::move(specCopy);
        clone.context.moduleContext = &moduleContext_;
        clone.context.windowId = id;
        clone.left = instance->left + 4;
        clone.top = instance->top + 2;
        clone.width = instance->width;
        clone.height = instance->height;
        clone.resizeLeft = instance->resizeLeft;
        clone.resizeRight = instance->resizeRight;
        clone.resizeTop = instance->resizeTop;
        clone.resizeBottom = instance->resizeBottom;
        clone.renameLines = instance->renameLines;
        activeWindows_.insert(activeWindows_.begin(), std::move(clone));
        auto& stored = activeWindows_.front();
        ensureComponent(stored);
        cascadeOffset_ = (cascadeOffset_ + 2) % 20;
        markLayoutDirty();
        return true;
    }
    return false;
}

bool Dashboard::closeWindow(const std::string& instanceId)
{
    const auto it = std::remove_if(activeWindows_.begin(), activeWindows_.end(), [&](const WindowInstance& instance) {
        return instance.instanceId == instanceId;
    });
    if (it != activeWindows_.end()) {
        activeWindows_.erase(it, activeWindows_.end());
        if (activeWindows_.empty()) {
            cascadeOffset_ = 0;
        }
        clampSelectedWindowIndex();
        markLayoutDirty();
        return true;
    }
    return false;
}

ftxui::Component Dashboard::build()
{
    ensureRootInitialized();
    if (layoutDirty_) {
        refreshWindowComponents();
    }
    return root_;
}

const std::vector<WindowSpec>& Dashboard::availableWindows() const
{
    return availableWindows_;
}

std::vector<std::string> Dashboard::activeWindowIds() const
{
    std::vector<std::string> ids;
    ids.reserve(activeWindows_.size());
    for (const auto& window : activeWindows_) {
        ids.push_back(window.instanceId);
    }
    return ids;
}

ui::WindowSpec* Dashboard::findSpec(const std::string& specId)
{
    auto it = std::find_if(availableWindows_.begin(), availableWindows_.end(), [&](const WindowSpec& spec) {
        return spec.id == specId;
    });
    if (it != availableWindows_.end()) {
        return &*it;
    }
    return nullptr;
}

Dashboard::WindowInstance* Dashboard::findInstance(const std::string& instanceId)
{
    auto it = std::find_if(activeWindows_.begin(), activeWindows_.end(), [&](const WindowInstance& instance) {
        return instance.instanceId == instanceId;
    });
    if (it != activeWindows_.end()) {
        return &*it;
    }
    return nullptr;
}

std::string Dashboard::generateInstanceId(const WindowSpec& spec)
{
    return spec.id + "#" + std::to_string(nextWindowIndex_++);
}

void Dashboard::ensureComponent(WindowInstance& instance)
{
    if (!instance.component && instance.spec.componentFactory) {
        instance.component = instance.spec.componentFactory(instance.context);
    }
}

void Dashboard::ensureRootInitialized()
{
    if (root_) {
        return;
    }
    root_ = ftxui::Container::Vertical({});
    layoutDirty_ = true;
}

void Dashboard::refreshWindowComponents()
{
    if (!root_) {
        return;
    }

    auto header = buildHeader();
    auto windowArea = buildWindowArea();
    auto separatorComponent = ftxui::Renderer([]() {
        using namespace ftxui;
        return separator();
    });

    root_->DetachAllChildren();
    root_->Add(header);
    root_->Add(separatorComponent);
    root_->Add(windowArea);
    layoutDirty_ = false;
}

void Dashboard::markLayoutDirty()
{
    layoutDirty_ = true;
    if (auto* screen = ftxui::ScreenInteractive::Active()) {
        screen->Post([this]() {
            if (layoutDirty_) {
                refreshWindowComponents();
            }
        });
    } else if (root_) {
        refreshWindowComponents();
    }
}

void Dashboard::updateAvailableWindowTitles()
{
    availableWindowTitles_.clear();
    availableWindowTitles_.reserve(availableWindows_.size());
    for (const auto& spec : availableWindows_) {
        if (!spec.title.empty()) {
            availableWindowTitles_.push_back(spec.title);
        } else {
            availableWindowTitles_.push_back(spec.id);
        }
    }
}

void Dashboard::clampSelectedWindowIndex()
{
    if (availableWindows_.empty()) {
        selectedWindowIndex_ = 0;
        return;
    }
    if (selectedWindowIndex_ < 0) {
        selectedWindowIndex_ = 0;
    } else if (selectedWindowIndex_ >= static_cast<int>(availableWindows_.size())) {
        selectedWindowIndex_ = static_cast<int>(availableWindows_.size()) - 1;
    }
}

ftxui::Component Dashboard::buildHeader()
{
    using namespace ftxui;

    if (availableWindows_.empty()) {
        return Renderer([this]() {
            return hbox({
                       color(ftxui::Color::RGB(255, 1, 1), text("Workbench Dashboard") | bold),
                       filler(),
                       text("No modules registered yet.") | dim,
                   })
                | border;
        });
    }

    clampSelectedWindowIndex();

    auto menu = Menu(&availableWindowTitles_, &selectedWindowIndex_);
    auto menuFrame = Renderer(menu, [menu]() {
        using namespace ftxui;
        return menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 10);
    });
    auto addButton = Button("Create", [this]() {
        clampSelectedWindowIndex();
        addWindowByIndex(selectedWindowIndex_);
    });
    auto headerControls = Container::Vertical({
        menuFrame,
        addButton,
    });

    return Renderer(headerControls, [this, menuFrame, addButton]() {
        using namespace ftxui;
        return hbox({
                   vbox({
                       color(ftxui::Color::RGB(255, 255, 0), text("Workbench") | bold),
                       color(ftxui::Color::RGB(255, 255, 0), text("Dashboard") | bold),
                   }) | size(WIDTH, EQUAL, 10),
                   separator(),
                   vbox({
                       text("Available Windows") | dim,
                       menuFrame->Render(),
                       text(" "),
                       addButton->Render(),
                   }) | border
                       | flex,
                   filler(),
                   text("Open: ") | dim,
                   text(std::to_string(activeWindows_.size())) | dim,
               })
            | border;
    });
}

ftxui::Component Dashboard::buildWindowArea()
{
    using namespace ftxui;

    if (activeWindows_.empty()) {
        return Renderer([]() {
            return vbox({
                       text("No windows open.") | dim,
                       text("Use the header above to add one.") | dim,
                   })
                | border;
        });
    }

    Components windowComponents;
    windowComponents.reserve(activeWindows_.size());
    for (auto& instance : activeWindows_) {
        windowComponents.push_back(buildWindowComponent(instance));
    }

    auto stack = Container::Stacked(std::move(windowComponents));
    return Renderer(stack, [stack]() {
        using namespace ftxui;
        return stack->Render() | flex;
    });
}

ftxui::Component Dashboard::buildWindowComponent(WindowInstance& instance)
{
    using namespace ftxui;

    ensureComponent(instance);

    const std::string title = instance.spec.title.empty() ? instance.instanceId : instance.spec.title;

    Components controlComponents;
    if (instance.spec.cloneable) {
        controlComponents.push_back(Button("Clone", [this, id = instance.instanceId]() {
            cloneWindow(id);
        }));
    }
    if (instance.spec.closable) {
        controlComponents.push_back(Button("Close", [this, id = instance.instanceId]() {
            closeWindow(id);
        }));
    }

    Component controlsContainer;
    if (controlComponents.empty()) {
        controlsContainer = Renderer([]() {
            using namespace ftxui;
            return text("");
        });
    } else {
        controlsContainer = Container::Horizontal(std::move(controlComponents));
    }
    auto titleRenderer = Renderer([title]() {
        using namespace ftxui;
        return text(title) | bold;
    });

    Component contentComponent = instance.component;
    if (!contentComponent) {
        contentComponent = Renderer([]() {
            using namespace ftxui;
            return text("Component factory not provided.") | dim;
        });
    }

    auto renameInputs = std::make_shared<std::array<Component, 3>>();
    const char* placeholders[3] = { "Window label", "...", "..." };
    for (std::size_t i = 0; i < renameInputs->size(); ++i) {
        auto option = ftxui::InputOption::Default();
        option.placeholder = placeholders[i];
        option.multiline = false;
        (*renameInputs)[i] = Input(&instance.renameLines[i], option);
    }
    auto renameContainer = Container::Vertical({
        (*renameInputs)[0],
        (*renameInputs)[1],
        (*renameInputs)[2],
    });
    auto renameRenderer = Renderer(renameContainer, [renameInputs, renameContainer]() {
        using namespace ftxui;
        return vbox({
            (*renameInputs)[0] ? (*renameInputs)[0]->Render() : text(""),
            (*renameInputs)[1] ? (*renameInputs)[1]->Render() : text(""),
            (*renameInputs)[2] ? (*renameInputs)[2]->Render() : text(""),
        });
    });

    auto windowContainer = Container::Vertical({
        Container::Horizontal({
            titleRenderer,
            controlsContainer,
        }),
        renameRenderer,
        contentComponent,
    });
    auto innerRenderer = Renderer(windowContainer, [titleRenderer, controlsContainer, renameRenderer, contentComponent]() {
        using namespace ftxui;
        return vbox({
            hbox({
                renameRenderer->Render(),
                // titleRenderer->Render(),
                filler(),
                controlsContainer->Render(),
            }),
            // renameRenderer->Render(),
            separator(),
            contentComponent->Render() | flex,
        });
    });

    WindowOptions options;
    options.inner = innerRenderer;
    options.title = title;
    options.left = &instance.left;
    options.top = &instance.top;
    options.width = &instance.width;
    options.height = &instance.height;
    options.resize_left = &instance.resizeLeft;
    options.resize_right = &instance.resizeRight;
    options.resize_top = &instance.resizeTop;
    options.resize_down = &instance.resizeBottom;
    return Window(std::move(options));
}

} // namespace ui

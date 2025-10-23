#include "modules/NumericDataModule.h"

#include "core/DataRegistry.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace {

struct MetricStats {
    std::string channelId;
    std::string unit;
    double current{0.0};
    double min{0.0};
    double max{0.0};
    bool hasCurrent{false};
    bool hasMin{false};
    bool hasMax{false};
};

struct NumericDataState : std::enable_shared_from_this<NumericDataState> {
    explicit NumericDataState(core::ModuleContext& moduleContext)
        : moduleContext(moduleContext) {}

    ~NumericDataState() {
        unsubscribe();
    }

    void selectSource(int index, bool force) {
        std::lock_guard lock(mutex);
        if (sources.empty()) {
            return;
        }
        if (index < 0 || index >= static_cast<int>(sources.size())) {
            return;
        }
        selectedIndex = index;
        const std::string newSource = sources[static_cast<std::size_t>(index)].id;
        if (!force && newSource == currentSourceId) {
            return;
        }
        subscribe(newSource);
    }

    void subscribe(const std::string& sourceId) {
        unsubscribe();
        metrics.clear();
        currentSourceId = sourceId;

        auto self = weak_from_this();
        observerToken = moduleContext.dataRegistry.addObserver(sourceId, [self](const core::DataFrame& frame) {
            if (auto state = self.lock()) {
                state->handleFrame(frame);
            }
        });

        if (auto latest = moduleContext.dataRegistry.latest(sourceId)) {
            handleFrame(*latest);
        }
    }

    void unsubscribe() {
        if (observerToken != 0 && !currentSourceId.empty()) {
            moduleContext.dataRegistry.removeObserver(currentSourceId, observerToken);
        }
        observerToken = 0;
        currentSourceId.clear();
    }

    void handleFrame(const core::DataFrame& frame) {
        {
            std::lock_guard lock(mutex);
            for (const auto& point : frame.points) {
                if (const auto* numeric = std::get_if<core::NumericSample>(&point.payload)) {
                    auto& entry = metrics[point.channelId];
                    entry.channelId = point.channelId;
                    entry.unit = numeric->unit;
                    entry.current = numeric->value;
                    entry.hasCurrent = true;
                    if (!entry.hasMin || numeric->value < entry.min) {
                        entry.min = numeric->value;
                        entry.hasMin = true;
                    }
                    if (!entry.hasMax || numeric->value > entry.max) {
                        entry.max = numeric->value;
                        entry.hasMax = true;
                    }
                }
            }
        }
        requestRebuild();
    }

    void resetMin(const std::string& channelId) {
        {
            std::lock_guard lock(mutex);
            if (auto it = metrics.find(channelId); it != metrics.end()) {
                it->second.hasMin = it->second.hasCurrent;
                if (it->second.hasCurrent) {
                    it->second.min = it->second.current;
                }
            }
        }
        requestRebuild();
    }

    void resetMax(const std::string& channelId) {
        {
            std::lock_guard lock(mutex);
            if (auto it = metrics.find(channelId); it != metrics.end()) {
                it->second.hasMax = it->second.hasCurrent;
                if (it->second.hasCurrent) {
                    it->second.max = it->second.current;
                }
            }
        }
        requestRebuild();
    }

    std::vector<std::string> buildMetricStrings() const {
        std::lock_guard lock(mutex);
        std::vector<std::string> lines;
        lines.reserve(metrics.size() * 3);
        auto keys = collectSortedKeys();
        for (const auto& key : keys) {
            const auto& entry = metrics.at(key);
            if (entry.hasCurrent) {
                lines.push_back(formatValue(entry.channelId, entry.current, entry.unit, "Value"));
            }
            if (entry.hasMin) {
                lines.push_back(formatValue(entry.channelId, entry.min, entry.unit, "Min"));
            }
            if (entry.hasMax) {
                lines.push_back(formatValue(entry.channelId, entry.max, entry.unit, "Max"));
            }
        }
        return lines;
    }

    std::vector<std::string> collectSortedKeys() const {
        std::vector<std::string> keys;
        keys.reserve(metrics.size());
        for (const auto& [key, _] : metrics) {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    static std::string formatValue(const std::string& channelId, double value, const std::string& unit, const std::string& kind) {
        std::string label = channelId;
        if (!kind.empty()) {
            label = kind + " " + label;
        }
        return label + ": " + formatNumeric(value) + (unit.empty() ? "" : " " + unit);
    }

    static std::string formatNumeric(double value) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        return std::string(buffer);
    }

    void requestRebuild() {
        auto self = shared_from_this();
        if (auto* screen = ftxui::ScreenInteractive::Active()) {
            screen->Post([self]() {
                if (self->metricsPane) {
                    self->rebuildMetricsPane();
                }
            });
        }
    }

    void rebuildMetricsPane() {
        if (!metricsPane) {
            return;
        }
        metricsPane->DetachAllChildren();

        auto keys = collectSortedKeys();
        auto weakSelf = weak_from_this();
        for (const auto& key : keys) {
            MetricStats statsCopy;
            {
                std::lock_guard lock(mutex);
                statsCopy = metrics.at(key);
            }

            if (statsCopy.hasCurrent) {
                const std::string line = formatValue(key, statsCopy.current, statsCopy.unit, "");
                auto row = ftxui::Renderer([line]() {
                    using namespace ftxui;
                    return hbox({
                        text(line),
                    });
                });
                metricsPane->Add(row);
            }
            if (statsCopy.hasMin) {
                const std::string line = formatValue(key, statsCopy.min, statsCopy.unit, "Min");
                auto resetButton = ftxui::Button("Reset", [weakSelf, key]() {
                    if (auto self = weakSelf.lock()) {
                        self->resetMin(key);
                    }
                },ftxui::ButtonOption::Ascii());
                auto row = ftxui::Renderer(resetButton, [line, resetButton]() {
                    using namespace ftxui;
                    return hbox({
                        text(line),
                        filler(),
                        resetButton->Render(),
                    });
                });
                metricsPane->Add(row);
            }
            if (statsCopy.hasMax) {
                const std::string line = formatValue(key, statsCopy.max, statsCopy.unit, "Max");
                auto resetButton = ftxui::Button("Reset", [weakSelf, key]() {
                    if (auto self = weakSelf.lock()) {
                        self->resetMax(key);
                    }
                },ftxui::ButtonOption::Ascii());
                auto row = ftxui::Renderer(resetButton, [line, resetButton]() {
                    using namespace ftxui;
                    return hbox({
                        text(line),
                        filler(),
                        resetButton->Render(),
                    });
                });
                metricsPane->Add(row);
            }
        }

        if (metricsPane->ChildCount() == 0) {
            auto empty = ftxui::Renderer([]() {
                using namespace ftxui;
                return text("No numeric data available.") | dim;
            });
            metricsPane->Add(empty);
        }
    }

    core::ModuleContext& moduleContext;
    std::vector<core::SourceMetadata> sources;
    std::vector<std::string> sourceTitles;
    int selectedIndex{0};
    std::string currentSourceId;
    int observerToken{0};
    std::map<std::string, MetricStats> metrics;
    mutable std::recursive_mutex mutex;

    ftxui::Component menuComponent;
    std::shared_ptr<ftxui::ComponentBase> metricsPane;
};

class NumericDataComponent : public ftxui::ComponentBase {
public:
    explicit NumericDataComponent(std::shared_ptr<NumericDataState> state)
        : state_(std::move(state)) {
        buildSourceList();

        ftxui::MenuOption menuOption;
        menuOption.on_change = [weak = std::weak_ptr(state_), this]() {
            if (auto state = weak.lock()) {
                state->selectSource(state->selectedIndex, false);
            }
        };

        menuComponent_ = ftxui::Menu(&state_->sourceTitles, &state_->selectedIndex, menuOption);
        auto menuFrame = ftxui::Renderer(menuComponent_, [menuComponent = menuComponent_]() {
            using namespace ftxui;
            return menuComponent->Render() | vscroll_indicator | frame | size(WIDTH, LESS_THAN, 30);
        });

        state_->metricsPane = ftxui::Container::Vertical({});
        auto metricsFrame = ftxui::Renderer(state_->metricsPane, [state = state_]() {
            using namespace ftxui;
            return state->metricsPane->Render() | vscroll_indicator | frame | flex;
        });

        auto layout = ftxui::Container::Horizontal({
            menuFrame,
            ftxui::Renderer([] { return ftxui::separator(); }),
            metricsFrame,
        });

        Add(layout);

        if (!state_->sourceTitles.empty()) {
            state_->selectSource(state_->selectedIndex, true);
        } else {
            state_->rebuildMetricsPane();
        }
    }

    ~NumericDataComponent() override {
        if (state_) {
            state_->unsubscribe();
        }
    }

private:
    void buildSourceList() {
        auto metadata = state_->moduleContext.dataRegistry.listSources();
        for (const auto& meta : metadata) {
            if (meta.kind == core::DataKind::Numeric) {
                state_->sources.push_back(meta);
                state_->sourceTitles.push_back(meta.name);
            }
        }

        if (state_->sources.empty()) {
            state_->sourceTitles = {"No numeric sources available"};
        }
    }

    std::shared_ptr<NumericDataState> state_;
    ftxui::Component menuComponent_;
};

}  // namespace

NumericDataModule::NumericDataModule() = default;

std::string NumericDataModule::id() const {
    return "ui.numeric_data";
}

std::string NumericDataModule::displayName() const {
    return "Numeric Data Viewer";
}

void NumericDataModule::initialize(core::ModuleContext& context) {
    context_ = &context;
}

void NumericDataModule::shutdown(core::ModuleContext& context) {
    (void)context;
    context_ = nullptr;
}

std::vector<core::SourceMetadata> NumericDataModule::declareSources() {
    return {};
}

std::vector<ui::WindowSpec> NumericDataModule::createDefaultWindows(core::ModuleContext& context) {
    ui::WindowSpec spec;
    spec.id = "ui.numeric_data.window";
    spec.title = "Numeric Data";
    spec.cloneable = true;
    spec.openByDefault = true;
    spec.componentFactory = [&context](ui::WindowContext&) -> ftxui::Component {
        auto state = std::make_shared<NumericDataState>(context);
        return std::make_shared<NumericDataComponent>(std::move(state));
    };

    return {spec};
}

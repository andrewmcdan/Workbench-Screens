#include "GraphingDataModule.h"

#include "core/DataRegistry.h"
#include "core/ModuleContext.h"
#include "hardware/HardwareServiceClient.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace {

struct ChannelHistory {
    std::string channelId;
    std::string unit;
    std::vector<double> samples;
    double current { 0.0 };
    double min { 0.0 };
    double max { 0.0 };
    bool hasCurrent { false };
    bool hasMin { false };
    bool hasMax { false };
};

struct GraphingState : std::enable_shared_from_this<GraphingState> {
    explicit GraphingState(core::ModuleContext& moduleContext)
        : moduleContext(moduleContext)
    {
    }

    ~GraphingState()
    {
        unsubscribe();
    }

    void selectSource(int index, bool force)
    {
        std::lock_guard lock(mutex);
        if (sources.empty())
            return;
        if (index < 0 || index >= static_cast<int>(sources.size()))
            return;
        selectedIndex = index;
        const std::string newSource = sources[static_cast<std::size_t>(index)].id;
        if (!force && newSource == currentSourceId)
            return;
        subscribe(newSource);
    }

    void subscribe(const std::string& sourceId)
    {
        unsubscribe();
        histories.clear();
        currentSourceId = sourceId;

        moduleContext.hardwareService.subscribeSource(sourceId);

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

    void unsubscribe()
    {
        if (observerToken != 0 && !currentSourceId.empty()) {
            moduleContext.dataRegistry.removeObserver(currentSourceId, observerToken);
        }
        if (!currentSourceId.empty()) {
            moduleContext.hardwareService.unsubscribeSource(currentSourceId);
        }
        observerToken = 0;
        currentSourceId.clear();
    }

    void handleFrame(const core::DataFrame& frame)
    {
        {
            std::lock_guard lock(mutex);
            for (const auto& point : frame.points) {
                if (const auto* numeric = std::get_if<core::NumericSample>(&point.payload)) {
                    auto& h = histories[point.channelId];
                    h.channelId = point.channelId;
                    h.unit = numeric->unit;
                    h.current = numeric->value;
                    h.hasCurrent = true;
                    if (!h.hasMin || numeric->value < h.min) {
                        h.min = numeric->value;
                        h.hasMin = true;
                    }
                    if (!h.hasMax || numeric->value > h.max) {
                        h.max = numeric->value;
                        h.hasMax = true;
                    }
                    h.samples.push_back(numeric->value);
                    if (static_cast<int>(h.samples.size()) > maxSamples) {
                        h.samples.erase(h.samples.begin(), h.samples.begin() + (h.samples.size() - maxSamples));
                    }
                }
            }
        }
        requestRebuild();
    }

    void clearHistory(const std::string& channelId)
    {
        {
            std::lock_guard lock(mutex);
            if (auto it = histories.find(channelId); it != histories.end()) {
                it->second.samples.clear();
                it->second.hasMin = it->second.hasCurrent;
                it->second.hasMax = it->second.hasCurrent;
                if (it->second.hasCurrent) {
                    it->second.min = it->second.current;
                    it->second.max = it->second.current;
                }
            }
        }
        requestRebuild();
    }

    void requestRebuild()
    {
        auto self = shared_from_this();
        if (auto* screen = ftxui::ScreenInteractive::Active()) {
            screen->Post([self]() {
                if (self->graphPane) {
                    self->rebuildGraphPane();
                }
            });
        }
    }

    static std::string sparkline(const std::vector<double>& samples)
    {
        // Unicode blocks from low to high
        static const std::string blocks = "▁▂▃▄▅▆▇█";
        if (samples.empty())
            return "";
        double mn = samples.front();
        double mx = samples.front();
        for (double v : samples) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (mn == mx) {
            // flat line
            return std::string(samples.size(), blocks.front());
        }
        std::string out;
        out.reserve(samples.size());
        for (double v : samples) {
            double t = (v - mn) / (mx - mn);
            int idx = static_cast<int>(t * (blocks.size() - 1) + 0.5);
            idx = std::clamp(idx, 0, static_cast<int>(blocks.size() - 1));
            out += blocks[idx];
        }
        return out;
    }

    void rebuildGraphPane()
    {
        if (!graphPane)
            return;
        graphPane->DetachAllChildren();

        std::vector<ChannelHistory> items;
        {
            std::lock_guard lock(mutex);
            for (auto& [k, v] : histories)
                items.push_back(v);
        }

        std::sort(items.begin(), items.end(), [](const ChannelHistory& a, const ChannelHistory& b) {
            return a.channelId < b.channelId;
        });

        auto weakSelf = weak_from_this();
        for (const auto& h : items) {
            ChannelHistory copy = h;
            auto row = ftxui::Renderer([copy]() {
                using namespace ftxui;
                Element e;
                if (!copy.hasCurrent) {
                    e = text(copy.channelId + ": no data") | dim;
                } else if (copy.samples.empty()) {
                    e = vbox({ text(copy.channelId + ": " + formatNumeric(copy.current) + (copy.unit.empty() ? "" : " " + copy.unit)), text("(no history)") | dim });
                } else {
                    std::string spark = sparkline(copy.samples);
                    e = vbox({
                        hbox({ text(copy.channelId), filler(), text(formatNumeric(copy.current) + (copy.unit.empty() ? "" : " " + copy.unit)) | bold }),
                        separator(),
                        text(spark) | color(Color::Green),
                        hbox({ text("min: " + formatNumeric(copy.min)), filler(), text("max: " + formatNumeric(copy.max)) }),
                    });
                }
                return e;
            });

            // Add clear button row when we have data
            if (copy.hasCurrent) {
                auto clearButton = ftxui::Button("Clear", [weakSelf, id = copy.channelId]() {
					if (auto self = weakSelf.lock()) self->clearHistory(id); }, ftxui::ButtonOption::Ascii());

                auto rowWithButton = ftxui::Renderer(clearButton, [row, clearButton]() {
                    using namespace ftxui;
                    return hbox({ row->Render(), filler(), clearButton->Render() });
                });
                graphPane->Add(rowWithButton);
            } else {
                graphPane->Add(row);
            }
        }

        if (graphPane->ChildCount() == 0) {
            auto empty = ftxui::Renderer([]() {
                using namespace ftxui;
                return text("No numeric data available.") | dim;
            });
            graphPane->Add(empty);
        }
    }

    static std::string formatNumeric(double value)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", value);
        return std::string(buf);
    }

    core::ModuleContext& moduleContext;
    std::vector<core::SourceMetadata> sources;
    std::vector<std::string> sourceTitles;
    int selectedIndex { 0 };
    std::string currentSourceId;
    int observerToken { 0 };
    std::map<std::string, ChannelHistory> histories;
    mutable std::recursive_mutex mutex;

    ftxui::Component menuComponent;
    std::shared_ptr<ftxui::ComponentBase> graphPane;

    const size_t maxSamples { 80 };
};

class GraphingComponent : public ftxui::ComponentBase {
public:
    explicit GraphingComponent(std::shared_ptr<GraphingState> state)
        : state_(std::move(state))
    {
        buildSourceList();

        ftxui::MenuOption menuOption;
        menuOption.on_change = [weak = std::weak_ptr(state_), this]() {
            if (auto state = weak.lock())
                state->selectSource(state->selectedIndex, false);
        };

        menuComponent_ = ftxui::Menu(&state_->sourceTitles, &state_->selectedIndex, menuOption);
        auto menuFrame = ftxui::Renderer(menuComponent_, [menuComponent = menuComponent_]() {
            using namespace ftxui;
            return menuComponent->Render() | vscroll_indicator | frame | size(WIDTH, LESS_THAN, 30);
        });

        state_->graphPane = ftxui::Container::Vertical({});
        auto graphFrame = ftxui::Renderer(state_->graphPane, [state = state_]() {
            using namespace ftxui;
            return state->graphPane->Render() | vscroll_indicator | frame | flex;
        });

        auto layout = ftxui::Container::Horizontal({ menuFrame, ftxui::Renderer([] { return ftxui::separator(); }), graphFrame });
        Add(layout);

        if (!state_->sourceTitles.empty()) {
            state_->selectSource(state_->selectedIndex, true);
        } else {
            state_->rebuildGraphPane();
        }
    }

    ~GraphingComponent() override
    {
        if (state_)
            state_->unsubscribe();
    }

private:
    void buildSourceList()
    {
        auto metadata = state_->moduleContext.dataRegistry.listSources();
        for (const auto& meta : metadata) {
            if (meta.kind == core::DataKind::Numeric) {
                state_->sources.push_back(meta);
                state_->sourceTitles.push_back(meta.name);
            }
        }

        if (state_->sources.empty())
            state_->sourceTitles = { "No numeric sources available" };
    }

    std::shared_ptr<GraphingState> state_;
    ftxui::Component menuComponent_;
};

} // namespace

GraphingDataModule::GraphingDataModule() = default;

std::string GraphingDataModule::id() const { return "ui.graphing"; }

std::string GraphingDataModule::displayName() const { return "Graphing"; }

void GraphingDataModule::initialize(core::ModuleContext& context) { context_ = &context; }

void GraphingDataModule::shutdown(core::ModuleContext& context)
{
    (void)context;
    context_ = nullptr;
}

std::vector<core::SourceMetadata> GraphingDataModule::declareSources() { return {}; }

std::vector<ui::WindowSpec> GraphingDataModule::createDefaultWindows(core::ModuleContext& context)
{
    ui::WindowSpec spec;
    spec.id = "ui.graphing.window";
    spec.title = "Graphing";
    spec.cloneable = true;
    spec.openByDefault = true;
    spec.componentFactory = [&context](ui::WindowContext&) -> ftxui::Component {
        auto state = std::make_shared<GraphingState>(context);
        return std::make_shared<GraphingComponent>(std::move(state));
    };

    return { spec };
}

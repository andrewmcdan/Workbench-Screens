#include "GraphingDataModule.h"

#include "core/DataRegistry.h"
#include "core/ModuleContext.h"
#include "hardware/HardwareServiceClient.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "flags.h"
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

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
        // Debug: indicate selection in console
        if (flags::logLevel >= 3) {
            spdlog::debug("Graphing: selecting source '{}' (index={})", newSource, index);
        }
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
        if (flags::logLevel >= 4) {
            spdlog::trace("Graphing: received frame for source '{}' with {} points", frame.sourceId, frame.points.size());
        }
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
        notifyNewData();
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
        notifyNewData();
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

            // Create a persistent function object for the graph callback and keep it alive
            // by capturing a shared_ptr to it in the Renderer.
            auto graphFunc = std::make_shared<std::function<std::vector<int>(int, int)>>(
                [samples = copy.samples](int width, int height) -> std::vector<int> {
                    std::vector<int> out(width, 0);
                    if (width <= 0 || height <= 0)
                        return out;
                    if (samples.empty())
                        return out;

                    // find min/max
                    double mn = samples.front();
                    double mx = samples.front();
                    for (double v : samples) {
                        mn = std::min(mn, v);
                        mx = std::max(mx, v);
                    }
                    if (mn == mx) {
                        // flat line in middle
                        int mid = height / 2;
                        for (int x = 0; x < width; ++x)
                            out[x] = mid;
                        return out;
                    }

                    const double scale = (height - 1) / (mx - mn);
                    const double srcSize = static_cast<double>(samples.size());
                    for (int x = 0; x < width; ++x) {
                        double srcPos = (srcSize - 1) * (static_cast<double>(x) / static_cast<double>(std::max(1, width - 1)));
                        int i0 = static_cast<int>(std::floor(srcPos));
                        int i1 = static_cast<int>(std::ceil(srcPos));
                        double v = 0.0;
                        if (i0 == i1)
                            v = samples[std::clamp(i0, 0, static_cast<int>(samples.size()) - 1)];
                        else {
                            double t = srcPos - static_cast<double>(i0);
                            double a = samples[std::clamp(i0, 0, static_cast<int>(samples.size()) - 1)];
                            double b = samples[std::clamp(i1, 0, static_cast<int>(samples.size()) - 1)];
                            v = a + (b - a) * t;
                        }
                        int y = static_cast<int>(std::round((v - mn) * scale));
                        y = std::clamp(y, 0, height - 1);
                        out[x] = y;
                    }
                    return out;
                });

            auto row = ftxui::Renderer([copy, graphFunc]() {
                using namespace ftxui;
                Element e;
                if (!copy.hasCurrent) {
                    e = text(copy.channelId + ": no data") | dim;
                } else {
                    e = vbox({
                            hbox({ text(copy.channelId), filler(), text(formatNumeric(copy.current) + (copy.unit.empty() ? "" : " " + copy.unit)) | bold }),
                            separator(),
                            // pass reference to the function object kept alive by graphFunc
                            graph(std::ref(*graphFunc)) | color(Color::Green) | flex,
                            hbox({ text("min: " + formatNumeric(copy.min)), filler(), text("max: " + formatNumeric(copy.max)) }),
                        })
                        | flex;
                }
                return e;
            });

            // Add the row directly (no manual clear button for streaming data)
            graphPane->Add(row);
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
    // No per-module refresher any more. Use the centralized postRedraw callback on moduleContext
    // to request UI redraws from other threads.
    void notifyNewData()
    {
        if (moduleContext.postRedraw) {
            auto self = shared_from_this();
            moduleContext.postRedraw([self]() {
                if (self->graphPane)
                    self->rebuildGraphPane();
            });
        }
        // Always request a local rebuild as a secondary trigger.
        requestRebuild();
    }
};

class GraphingComponent : public ftxui::ComponentBase {
public:
    explicit GraphingComponent(std::shared_ptr<GraphingState> state)
        : state_(std::move(state))
    {
        buildSourceList();

        ftxui::MenuOption menuOption;
        auto triggerSelect = [weak = std::weak_ptr(state_), this]() {
            if (auto state = weak.lock()) {
                if (flags::logLevel >= 3) {
                    spdlog::debug("Graphing menu on_change: index={} source_count={}", state->selectedIndex, state->sources.size());
                }
                state->selectSource(state->selectedIndex, false);
            }
        };
        menuOption.on_change = triggerSelect;
        // menuOption.on_enter = triggerSelect;

        menuComponent_ = ftxui::Menu(&state_->sourceTitles, &state_->selectedIndex, menuOption);
        auto menuFrame = ftxui::Renderer(menuComponent_, [menuComponent = menuComponent_]() {
            using namespace ftxui;
            return menuComponent->Render() | vscroll_indicator;
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
        if (state_) {
            state_->unsubscribe();
        }
    }

private:
    void buildSourceList()
    {
        auto metadata = state_->moduleContext.dataRegistry.listSources();
        if (flags::logLevel >= 3) {
            std::vector<std::string> ids;
            ids.reserve(metadata.size());
            for (const auto& meta : metadata) {
                ids.push_back(meta.id);
            }
            spdlog::debug("Graphing: buildSourceList saw {} sources: {}", ids.size(), fmt::join(ids, ", "));
        }
        for (const auto& meta : metadata) {
            if (meta.kind == core::DataKind::Numeric) {
                state_->sources.push_back(meta);
                // Try to show a quick preview of the latest numeric value for this source, if available
                auto latest = state_->moduleContext.dataRegistry.latest(meta.id);
                if (latest && !latest->points.empty()) {
                    const auto& p = latest->points.front();
                    if (const auto* numeric = std::get_if<core::NumericSample>(&p.payload)) {
                        state_->sourceTitles.push_back(meta.name + " (" + std::to_string(numeric->value) + " " + numeric->unit + ")");
                        continue;
                    }
                }
                state_->sourceTitles.push_back(meta.name);
            }
        }

        if (state_->sources.empty()) {
            state_->sourceTitles = { "No numeric sources available" };
            return;
        }

        // If hardware mock is enabled, prefer the mock source by default (if present).
        if (flags::enableHardwareMock) {
            for (size_t i = 0; i < state_->sources.size(); ++i) {
                if (state_->sources[i].id == "mock.12v") {
                    state_->selectedIndex = static_cast<int>(i);
                    break;
                }
            }
        }
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

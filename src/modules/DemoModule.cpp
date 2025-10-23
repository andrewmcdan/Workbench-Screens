#include "modules/DemoModule.h"

#include "core/DataRegistry.h"
#include "ui/WindowSpec.h"

#include <chrono>
#include <functional>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

namespace {

constexpr char kSourceId[] = "demo.metrics";
constexpr char kVoltageChannelId[] = "demo.voltage";

void PublishVoltage(core::ModuleContext& context, double voltage) {
    const auto now = std::chrono::system_clock::now();

    core::NumericSample sample;
    sample.value = voltage;
    sample.unit = "V";
    sample.timestamp = now;

    core::DataPoint point;
    point.channelId = kVoltageChannelId;
    point.payload = sample;

    core::DataFrame frame;
    frame.sourceId = kSourceId;
    frame.sourceName = "Demo Metrics";
    frame.timestamp = now;
    frame.points.push_back(std::move(point));

    context.dataRegistry.update(frame);
}

}  // namespace

DemoModule::DemoModule() = default;

std::string DemoModule::id() const {
    return "demo.module";
}

std::string DemoModule::displayName() const {
    return "Demo Module";
}

void DemoModule::initialize(core::ModuleContext& context) {
    voltage_ = 3.30;
    accumulator_ = std::chrono::milliseconds{0};
    PublishVoltage(context, voltage_);
}

void DemoModule::shutdown(core::ModuleContext& context) {
    (void)context;
}

std::vector<core::SourceMetadata> DemoModule::declareSources() {
    return {
        core::SourceMetadata{
            kSourceId,
            "Demo Metrics",
            core::DataKind::Numeric,
            "Mock voltage readings for UI testing.",
            std::string("V")}  // unit
    };
}

std::vector<ui::WindowSpec> DemoModule::createDefaultWindows(core::ModuleContext& context) {
    (void)context;
    ui::WindowSpec spec;
    spec.id = kSourceId;
    spec.title = "Demo Voltage";
    spec.cloneable = true;
    spec.openByDefault = true;
    spec.defaultLeft = 12;
    spec.defaultTop = 6;
    spec.defaultWidth = 36;
    spec.defaultHeight = 12;
    spec.componentFactory = [](ui::WindowContext& windowContext) -> ftxui::Component {
        return ftxui::Renderer([registry = std::ref(windowContext.module().dataRegistry)]() {
            using namespace ftxui;
            auto latest = registry.get().latest(kSourceId);
            if (!latest || latest->points.empty()) {
                return vbox({
                    text("No data yet.") | dim,
                });
            }

            const auto& payload = latest->points.front().payload;
            if (const auto* numeric = std::get_if<core::NumericSample>(&payload)) {
                return vbox({
                    text("Voltage"),
                    separator(),
                    text(std::to_string(numeric->value) + " " + numeric->unit) | bold,
                });
            }

            return vbox({
                text("Unsupported payload type.") | dim,
            });
        });
    };

    return {spec};
}

void DemoModule::tick(core::ModuleContext& context, std::chrono::milliseconds delta) {
    accumulator_ += delta;
    if (accumulator_ < std::chrono::milliseconds(1000)) {
        return;
    }

    accumulator_ -= std::chrono::milliseconds(1000);
    voltage_ += 0.05;
    if (voltage_ > 5.00) {
        voltage_ = 3.30;
    }
    PublishVoltage(context, voltage_);
}

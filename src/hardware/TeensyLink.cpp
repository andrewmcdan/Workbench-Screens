#include "TeensyLink.h"

#include "core/DataRegistry.h"

#include <chrono>
#include <utility>

namespace hardware {

TeensyLink::TeensyLink() = default;

void TeensyLink::setPortName(std::string port) {
    std::lock_guard lock(mutex_);
    portName_ = std::move(port);
}

void TeensyLink::connect() {
    std::lock_guard lock(mutex_);
    connected_ = true;
}

void TeensyLink::disconnect() {
    std::lock_guard lock(mutex_);
    connected_ = false;
    while (!incoming_.empty()) {
        incoming_.pop();
    }
}

bool TeensyLink::isConnected() const {
    std::lock_guard lock(mutex_);
    return connected_;
}

void TeensyLink::send(const teensy::Message& message) {
    (void)message;
    // TODO: Implement serial transmission.
}

void TeensyLink::pushIncoming(std::vector<std::uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    incoming_.push(std::move(bytes));
}

void TeensyLink::processIncoming(core::DataRegistry& registry) {
    for (;;) {
        std::vector<std::uint8_t> buffer;
        {
            std::lock_guard lock(mutex_);
            if (incoming_.empty()) {
                break;
            }
            buffer = std::move(incoming_.front());
            incoming_.pop();
        }

        if (auto message = teensy::Decode(buffer)) {
            handleMessage(*message, registry);
        }
    }
}

void TeensyLink::handleMessage(const teensy::Message& message, core::DataRegistry& registry) {
    using teensy::MessageType;
    switch (message.type) {
    case MessageType::MeasurementUpdate: {
        const auto now = std::chrono::system_clock::now();
        core::DataFrame frame;
        frame.sourceId = message.measurementUpdate.sourceId;
        frame.sourceName = message.measurementUpdate.sourceId;
        frame.timestamp = now;
        for (const auto& channel : message.measurementUpdate.channels) {
            core::NumericSample sample;
            sample.timestamp = now;
            sample.value = channel.value;
            sample.unit = channel.unit;

            core::DataPoint point;
            point.channelId = channel.channelId;
            point.payload = sample;
            frame.points.push_back(std::move(point));
        }
        registry.update(frame);
        break;
    }
    case MessageType::GpioStateResponse: {
        const auto now = std::chrono::system_clock::now();
        core::DataFrame frame;
        frame.sourceId = "teensy.gpio";
        frame.sourceName = "Teensy GPIO";
        frame.timestamp = now;

        core::DataPoint point;
        point.channelId = "gpio";
        core::GpioState gpio;
        gpio.pins = message.gpioState.pins;
        gpio.timestamp = now;
        point.payload = gpio;
        frame.points.push_back(std::move(point));
        registry.update(frame);
        break;
    }
    case MessageType::SerialData: {
        const auto now = std::chrono::system_clock::now();
        core::DataFrame frame;
        frame.sourceId = message.serialPayload.sourceId;
        frame.sourceName = message.serialPayload.sourceId;
        frame.timestamp = now;

        core::SerialSample serial;
        serial.timestamp = now;
        serial.text.assign(message.serialPayload.bytes.begin(), message.serialPayload.bytes.end());

        core::DataPoint point;
        point.channelId = "serial";
        point.payload = serial;
        frame.points.push_back(std::move(point));
        registry.update(frame);
        break;
    }
    default:
        break;
    }
}

}  // namespace hardware

#pragma once

#include "core/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hardware::teensy {

enum class MessageType : std::uint8_t {
    HandshakeRequest = 0x01,
    HandshakeResponse = 0x02,
    MeasurementUpdate = 0x10,
    LogicFrame = 0x11,
    SerialData = 0x12,
    SetGpioState = 0x20,
    QueryGpioState = 0x21,
    GpioStateResponse = 0x22,
    Heartbeat = 0x30,
    Ack = 0x31,
    Nack = 0x32
};

struct HandshakeRequest {
    std::string firmwareVersion;
    std::string deviceId;
};

struct HandshakeResponse {
    bool accepted{false};
    std::string reason;
    std::uint8_t protocolVersion{1};
};

struct NumericChannelUpdate {
    std::string channelId;
    double value{0.0};
    std::string unit;
};

struct MeasurementUpdate {
    std::string sourceId;
    std::vector<NumericChannelUpdate> channels;
};

struct SerialPayload {
    std::string sourceId;
    std::vector<std::uint8_t> bytes;
};

struct LogicFrame {
    std::string sourceId;
    std::vector<std::uint8_t> packedBits;
    std::uint32_t sampleRateHz{0};
};

struct GpioCommand {
    std::uint8_t pin{0};
    bool level{false};
};

struct GpioStateResponse {
    std::vector<bool> pins;
};

struct Heartbeat {
    std::uint64_t sequence{0};
};

struct Message {
    MessageType type{MessageType::Heartbeat};
    HandshakeRequest handshakeRequest;
    HandshakeResponse handshakeResponse;
    MeasurementUpdate measurementUpdate;
    SerialPayload serialPayload;
    LogicFrame logicFrame;
    GpioCommand gpioCommand;
    GpioStateResponse gpioState;
    Heartbeat heartbeat;
};

std::vector<std::uint8_t> Encode(const Message& message);
std::optional<Message> Decode(const std::vector<std::uint8_t>& buffer);

}  // namespace hardware::teensy

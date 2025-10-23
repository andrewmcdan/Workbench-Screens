#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace core {

enum class DataKind {
    Numeric,
    Waveform,
    Serial,
    Logic,
    GpioState,
    Custom
};

struct NumericSample {
    double value{0.0};
    std::string unit;
    std::chrono::system_clock::time_point timestamp;
};

struct WaveformSample {
    std::vector<double> samples;
    double sampleRateHz{0.0};
    std::chrono::system_clock::time_point timestamp;
};

struct SerialSample {
    std::string text;
    std::chrono::system_clock::time_point timestamp;
};

struct LogicSample {
    std::vector<bool> channels;
    std::chrono::nanoseconds samplePeriod{};
    std::chrono::system_clock::time_point timestamp;
};

struct GpioState {
    std::vector<bool> pins;
    std::chrono::system_clock::time_point timestamp;
};

using DataPayload = std::variant<std::monostate, NumericSample, WaveformSample, SerialSample, LogicSample, GpioState>;

struct DataPoint {
    std::string channelId;
    DataPayload payload;
};

struct DataFrame {
    std::string sourceId;
    std::string sourceName;
    std::vector<DataPoint> points;
    std::chrono::system_clock::time_point timestamp;
};

struct SourceMetadata {
    std::string id;
    std::string name;
    DataKind kind{DataKind::Custom};
    std::string description;
    std::optional<std::string> unit;
};

}  // namespace core

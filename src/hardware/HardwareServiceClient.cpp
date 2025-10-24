#include "hardware/HardwareServiceClient.h"

#include "core/DataRegistry.h"
#include "core/Types.h"
#include <spdlog/spdlog.h>

#include "flags.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

// #ifndef _WIN32
// #include <jsonrpccpp/common/exception.h>
// #else
namespace jsonrpc {
class JsonRpcException : public std::runtime_error {
public:
    explicit JsonRpcException(const std::string& message)
        : std::runtime_error(message)
    {
    }
};
} // namespace jsonrpc
// #endif

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

core::DataKind ParseKind(const std::string& text)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "numeric") {
        return core::DataKind::Numeric;
    }
    if (lower == "waveform") {
        return core::DataKind::Waveform;
    }
    if (lower == "serial") {
        return core::DataKind::Serial;
    }
    if (lower == "logic") {
        return core::DataKind::Logic;
    }
    if (lower == "gpiostate" || lower == "gpio") {
        return core::DataKind::GpioState;
    }
    return core::DataKind::Custom;
}

std::chrono::system_clock::time_point ParseTimestamp(const nlohmann::json& value)
{
    if (value.is_number()) {
        const double seconds = value.get<double>();
        auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(seconds));
        return std::chrono::system_clock::time_point(duration);
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        try {
            double seconds = std::stod(text);
            auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(seconds));
            return std::chrono::system_clock::time_point(duration);
        } catch (...) {
            // fall through
        }
    }
    return std::chrono::system_clock::now();
}

std::string ToJsonRpcId(uint64_t counter)
{
    return "ui-" + std::to_string(counter);
}

} // namespace

namespace hardware {

HardwareServiceClient::HardwareServiceClient(core::DataRegistry& registry)
    : registry_(registry)
{
}

HardwareServiceClient::~HardwareServiceClient()
{
    stop();
}

void HardwareServiceClient::configure(Options options)
{
    options_ = std::move(options);
}

void HardwareServiceClient::start()
{
#ifdef _WIN32
    // Unix domain sockets are not supported on Windows. The client remains dormant.
    (void)running_;
#else
    if (running_) {
        return;
    }
    running_ = true;
    // If mock mode is enabled, register the mock source synchronously so it is visible
    // during UI bootstrap, then start a mock publisher thread.
    if (options_.enableMock) {
        const std::string sourceId = "mock.12v";
        // Register metadata for mock source synchronously so the UI can discover it.
        core::SourceMetadata meta;
        meta.id = sourceId;
        meta.name = "12V Supply";
        meta.kind = core::DataKind::Numeric;
        meta.unit = std::string("V");
        registry_.registerSource(meta);
        spdlog::info("HardwareServiceClient: registered mock source '{}'", meta.id);

        // Start a light-weight mock worker that publishes a 1Hz sine wave
        worker_ = std::thread([this, sourceId]() {
            using namespace std::chrono_literals;
            const std::string channelId = "12v";

            const double amplitude = 0.5; // +/-0.5V
            const double offset = 12.0; // center 12V
            const double freqHz = 1.0; // 1 Hz
            const auto period = std::chrono::milliseconds(20); // 50 Hz update
            auto start = std::chrono::steady_clock::now();
            while (running_) {
                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<double> t = now - start;
                double angle = 2.0 * M_PI * freqHz * t.count();
                double value = offset + amplitude * std::sin(angle);

                core::NumericSample sample;
                sample.value = value; // ensure double
                sample.unit = "V";
                sample.timestamp = std::chrono::system_clock::now();

                core::DataPoint point;
                point.channelId = channelId;
                point.payload = sample;

                core::DataFrame frame;
                frame.sourceId = sourceId;
                frame.sourceName = "12V Supply";
                frame.timestamp = sample.timestamp;
                frame.points.push_back(std::move(point));

                registry_.update(frame);
                spdlog::trace("HardwareServiceClient: published mock frame {} -> {}", frame.sourceId, value);

                std::this_thread::sleep_for(period);
            }
        });
    } else {
        worker_ = std::thread(&HardwareServiceClient::run, this);
    }
#endif
}

void HardwareServiceClient::stop()
{
#ifdef _WIN32
    running_ = false;
#else
    if (!running_) {
        return;
    }
    running_ = false;
    closeSocket();
    if (worker_.joinable()) {
        worker_.join();
    }
#endif
}

void HardwareServiceClient::subscribeSource(const std::string& sourceId)
{
    if (sourceId.empty()) {
        return;
    }
    bool shouldSend = false;
    {
        std::lock_guard lock(subscriptionsMutex_);
        if (std::find(subscribedSources_.begin(), subscribedSources_.end(), sourceId) == subscribedSources_.end()) {
            subscribedSources_.push_back(sourceId);
            shouldSend = true;
        }
    }
    if (shouldSend) {
        sendSubscriptionMessage(sourceId);
    }
}

void HardwareServiceClient::unsubscribeSource(const std::string& sourceId)
{
    if (sourceId.empty()) {
        return;
    }
    bool shouldSend = false;
    {
        std::lock_guard lock(subscriptionsMutex_);
        const auto it = std::remove(subscribedSources_.begin(), subscribedSources_.end(), sourceId);
        if (it != subscribedSources_.end()) {
            subscribedSources_.erase(it, subscribedSources_.end());
            shouldSend = true;
        }
    }
    if (shouldSend) {
        sendUnsubscribeMessage(sourceId);
    }
}

void HardwareServiceClient::requestMetricReset(const std::string& sourceId,
    const std::string& channelId,
    const std::string& metric)
{
    if (sourceId.empty() || channelId.empty() || metric.empty()) {
        return;
    }
    nlohmann::json request {
        { "jsonrpc", "2.0" },
        { "id", nextRequestId() },
        { "method", "workbench.resetMetric" },
        { "params",
            {
                { "sourceId", sourceId },
                { "channelId", channelId },
                { "metric", metric },
            } }
    };
    sendJson(request);
}

void HardwareServiceClient::run()
{
#ifndef _WIN32
    while (running_) {
        try {
            connectSocket();
            sendRegisterClient();
            resendSubscriptions();
            readLoop();
        } catch (const std::exception& ex) {
            // For now, swallow the exception. Detailed logging will be added later.
            (void)ex;
        }

        if (!running_) {
            break;
        }
        std::this_thread::sleep_for(options_.reconnectDelay);
    }
#endif
}

void HardwareServiceClient::connectSocket()
{
#ifndef _WIN32
    closeSocket();

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        throw std::runtime_error("HardwareServiceClient: failed to create socket");
    }

    sockaddr_un addr {};
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (options_.socketPath.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        throw std::runtime_error("HardwareServiceClient: socket path is too long");
    }
    std::strncpy(addr.sun_path, options_.socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_un)) == -1) {
        const int err = errno;
        ::close(fd);
        throw std::runtime_error("HardwareServiceClient: connect() failed with errno " + std::to_string(err));
    }

    socketFd_.store(fd);
    readBuffer_.clear();
#endif
}

void HardwareServiceClient::closeSocket()
{
#ifndef _WIN32
    const int fd = socketFd_.exchange(-1);
    if (fd >= 0) {
        ::close(fd);
    }
#endif
}

void HardwareServiceClient::readLoop()
{
#ifndef _WIN32
    const int fd = socketFd_.load();
    if (fd < 0) {
        return;
    }

    constexpr std::size_t kChunkSize = 4096;
    std::vector<char> buffer(kChunkSize);
    while (running_) {
        const ssize_t bytesRead = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (bytesRead > 0) {
            readBuffer_.append(buffer.data(), static_cast<std::size_t>(bytesRead));

            std::size_t newlinePos = std::string::npos;
            while ((newlinePos = readBuffer_.find('\n')) != std::string::npos) {
                std::string message = readBuffer_.substr(0, newlinePos);
                readBuffer_.erase(0, newlinePos + 1);
                if (!message.empty()) {
                    handleIncomingMessage(message);
                }
            }
        } else if (bytesRead == 0) {
            break; // connection closed cleanly
        } else {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    closeSocket();
#else
    // No-op on Windows.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
}

void HardwareServiceClient::handleIncomingMessage(const std::string& message)
{
    try {
        auto json = nlohmann::json::parse(message);

        if (json.contains("method")) {
            const std::string method = json.at("method").get<std::string>();
            const auto& params = json.contains("params") ? json.at("params") : nlohmann::json::object();
            handleRelayNotification(method, params);
        } else if (json.contains("result") || json.contains("error")) {
            handleResponse(json);
        }
    } catch (const nlohmann::json::exception& ex) {
        (void)ex;
    } catch (const jsonrpc::JsonRpcException& ex) {
        (void)ex;
    }
}

void HardwareServiceClient::handleResponse(const nlohmann::json& response)
{
    // Currently, we have no stateful request tracking. This is a placeholder for
    // future acknowledgement handling.
    (void)response;
}

void HardwareServiceClient::handleRelayNotification(const std::string& method,
    const nlohmann::json& params)
{
    if (method == "workbench.dataFrame") {
        publishFrameFromJson(params);
        return;
    }

    if (method == "workbench.metadata") {
        if (params.is_array()) {
            for (const auto& entry : params) {
                registerMetadataFromJson(entry);
            }
        } else if (params.contains("sources")) {
            for (const auto& entry : params.at("sources")) {
                registerMetadataFromJson(entry);
            }
        } else {
            registerMetadataFromJson(params);
        }
        return;
    }

    // Additional notifications (GPIO updates, serial streams, etc.) will be
    // handled here once the relay exposes them.
}

void HardwareServiceClient::publishFrameFromJson(const nlohmann::json& params)
{
    if (!params.contains("frame")) {
        return;
    }

    core::SourceMetadata metadata;
    if (params.contains("source")) {
        const auto& sourceJson = params.at("source");
        metadata.id = sourceJson.value("id", "");
        metadata.name = sourceJson.value("name", metadata.id);
        metadata.description = sourceJson.value("description", "");
        metadata.kind = ParseKind(sourceJson.value("kind", "custom"));
        if (sourceJson.contains("unit") && !sourceJson.at("unit").is_null()) {
            metadata.unit = sourceJson.at("unit").get<std::string>();
        }
        if (!metadata.id.empty()) {
            registry_.registerSource(metadata);
        }
    }

    const auto& frameJson = params.at("frame");
    const std::string sourceId = frameJson.value("sourceId", metadata.id);
    if (sourceId.empty()) {
        return;
    }

    core::DataFrame frame;
    frame.sourceId = sourceId;
    frame.sourceName = frameJson.value("sourceName", metadata.name.empty() ? sourceId : metadata.name);
    frame.timestamp = ParseTimestamp(frameJson.value("timestamp", nlohmann::json {}));

    const auto& pointsJson = frameJson.value("points", nlohmann::json::array());
    for (const auto& pointJson : pointsJson) {
        core::DataPoint point;
        point.channelId = pointJson.value("channelId", "");

        if (pointJson.contains("numeric")) {
            const auto& numeric = pointJson.at("numeric");
            core::NumericSample sample;
            sample.value = numeric.value("value", 0.0);
            sample.unit = numeric.value("unit", "");
            sample.timestamp = frame.timestamp;
            point.payload = sample;
        } else if (pointJson.contains("waveform")) {
            const auto& waveform = pointJson.at("waveform");
            core::WaveformSample sample;
            sample.samples = waveform.value("samples", std::vector<double> {});
            sample.sampleRateHz = waveform.value("sampleRate", 0.0);
            sample.timestamp = frame.timestamp;
            point.payload = sample;
        } else if (pointJson.contains("serial")) {
            const auto& serial = pointJson.at("serial");
            core::SerialSample sample;
            sample.text = serial.value("text", "");
            sample.timestamp = frame.timestamp;
            point.payload = sample;
        } else if (pointJson.contains("logic")) {
            const auto& logic = pointJson.at("logic");
            core::LogicSample sample;
            sample.channels = logic.value("channels", std::vector<bool> {});
            const auto periodNs = logic.value("periodNs", 0LL);
            sample.samplePeriod = std::chrono::nanoseconds(periodNs);
            sample.timestamp = frame.timestamp;
            point.payload = sample;
        } else if (pointJson.contains("gpio")) {
            const auto& gpio = pointJson.at("gpio");
            core::GpioState state;
            state.pins = gpio.value("pins", std::vector<bool> {});
            state.timestamp = frame.timestamp;
            point.payload = state;
        } else {
            point.payload = std::monostate {};
        }

        frame.points.push_back(std::move(point));
    }

    registry_.update(frame);
}

void HardwareServiceClient::registerMetadataFromJson(const nlohmann::json& meta)
{
    if (!meta.contains("id")) {
        return;
    }

    core::SourceMetadata metadata;
    metadata.id = meta.value("id", "");
    metadata.name = meta.value("name", metadata.id);
    metadata.description = meta.value("description", "");
    metadata.kind = ParseKind(meta.value("kind", "custom"));
    if (meta.contains("unit") && !meta.at("unit").is_null()) {
        metadata.unit = meta.at("unit").get<std::string>();
    }

    registry_.registerSource(std::move(metadata));
}

void HardwareServiceClient::resendSubscriptions()
{
    std::lock_guard lock(subscriptionsMutex_);
    for (const auto& sourceId : subscribedSources_) {
        sendSubscriptionMessage(sourceId);
    }
}

void HardwareServiceClient::sendJson(const nlohmann::json& message)
{
#ifndef _WIN32
    const int fd = socketFd_.load();
    if (fd < 0) {
        return;
    }

    const std::string serialized = message.dump() + "\n";
    std::lock_guard lock(sendMutex_);
    ssize_t total = 0;
    const char* data = serialized.data();
    const ssize_t size = static_cast<ssize_t>(serialized.size());
    while (total < size) {
        const ssize_t sent = ::send(fd, data + total, static_cast<size_t>(size - total), 0);
        if (sent <= 0) {
            break;
        }
        total += sent;
    }
#else
    (void)message;
#endif
}

void HardwareServiceClient::sendSubscriptionMessage(const std::string& sourceId)
{
    if (sourceId.empty()) {
        return;
    }
    nlohmann::json request {
        { "jsonrpc", "2.0" },
        { "id", nextRequestId() },
        { "method", "workbench.subscribe" },
        { "params",
            {
                { "sourceId", sourceId },
            } }
    };
    sendJson(request);
}

void HardwareServiceClient::sendUnsubscribeMessage(const std::string& sourceId)
{
    if (sourceId.empty()) {
        return;
    }
    nlohmann::json request {
        { "jsonrpc", "2.0" },
        { "id", nextRequestId() },
        { "method", "workbench.unsubscribe" },
        { "params",
            {
                { "sourceId", sourceId },
            } }
    };
    sendJson(request);
}

void HardwareServiceClient::sendRegisterClient()
{
    nlohmann::json request {
        { "jsonrpc", "2.0" },
        { "id", nextRequestId() },
        { "method", "workbench.registerClient" },
        { "params",
            {
                { "protocol", 1 },
            } }
    };
    sendJson(request);
}

std::string HardwareServiceClient::nextRequestId()
{
    return ToJsonRpcId(++requestCounter_);
}

} // namespace hardware

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace core {
class DataRegistry;
}  // namespace core

namespace hardware {

/**
 * @brief Client responsible for talking to the external hardware relay service.
 *
 * The relay process exposes a JSON-RPC 2.0 endpoint over a Unix domain socket.
 * This client keeps a persistent connection, forwards control requests, and
 * converts inbound notifications into calls to `DataRegistry::update()`.
 */
class HardwareServiceClient {
public:
    struct Options {
        std::string socketPath{"/var/run/workbench/hardware-relay.sock"};
        std::chrono::milliseconds reconnectDelay{std::chrono::seconds(2)};
    };

    explicit HardwareServiceClient(core::DataRegistry& registry);
    ~HardwareServiceClient();

    void configure(Options options);
    void start();
    void stop();

    void subscribeSource(const std::string& sourceId);
    void unsubscribeSource(const std::string& sourceId);

    void requestMetricReset(const std::string& sourceId,
                            const std::string& channelId,
                            const std::string& metric);

private:
    void run();
    void connectSocket();
    void closeSocket();
    void readLoop();
    void handleIncomingMessage(const std::string& message);
    void handleResponse(const nlohmann::json& response);
    void handleRelayNotification(const std::string& method, const nlohmann::json& params);
    void publishFrameFromJson(const nlohmann::json& jsonParams);
    void registerMetadataFromJson(const nlohmann::json& meta);
    void resendSubscriptions();

    void sendJson(const nlohmann::json& message);
    void sendSubscriptionMessage(const std::string& sourceId);
    void sendUnsubscribeMessage(const std::string& sourceId);
    void sendRegisterClient();

    std::string nextRequestId();

    core::DataRegistry& registry_;
    Options options_;

    std::thread worker_;
    std::atomic<bool> running_{false};

    std::atomic<int> socketFd_{-1};
    std::mutex sendMutex_;
    std::string readBuffer_;

    std::mutex subscriptionsMutex_;
    std::vector<std::string> subscribedSources_;

    std::atomic<uint64_t> requestCounter_{0};
};

}  // namespace hardware


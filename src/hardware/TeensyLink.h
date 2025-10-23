#pragma once

#include "hardware/TeensyProtocol.h"

#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace core {
class DataRegistry;
}

namespace hardware {

class TeensyLink {
public:
    TeensyLink();

    void setPortName(std::string port);
    void connect();
    void disconnect();
    [[nodiscard]] bool isConnected() const;

    void send(const teensy::Message& message);
    void pushIncoming(std::vector<std::uint8_t> bytes);
    void processIncoming(core::DataRegistry& registry);

private:
    void handleMessage(const teensy::Message& message, core::DataRegistry& registry);

    mutable std::mutex mutex_;
    std::string portName_;
    bool connected_{false};
    std::queue<std::vector<std::uint8_t>> incoming_;
};

}  // namespace hardware

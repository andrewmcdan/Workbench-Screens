#include "TeensyProtocol.h"

#include <stdexcept>

namespace hardware::teensy {

std::vector<std::uint8_t> Encode(const Message& message) {
    std::vector<std::uint8_t> buffer;
    buffer.push_back(static_cast<std::uint8_t>(message.type));
    // TODO: append serialized payload based on message.type.
    return buffer;
}

std::optional<Message> Decode(const std::vector<std::uint8_t>& buffer) {
    if (buffer.empty()) {
        return std::nullopt;
    }

    Message message;
    message.type = static_cast<MessageType>(buffer[0]);
    // TODO: parse payload bytes based on message.type.
    return message;
}

}  // namespace hardware::teensy

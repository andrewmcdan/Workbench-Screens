#pragma once

namespace core {
class DataRegistry;
}

namespace hardware {
class TeensyLink;
}

namespace core {

struct ModuleContext {
    DataRegistry& dataRegistry;
    hardware::TeensyLink& teensyLink;
};

}  // namespace core

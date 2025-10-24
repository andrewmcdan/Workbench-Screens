#pragma once

namespace core {
class DataRegistry;
}

namespace hardware {
class HardwareServiceClient;
}

namespace core {

struct ModuleContext {
    DataRegistry& dataRegistry;
    hardware::HardwareServiceClient& hardwareService;
};

}  // namespace core

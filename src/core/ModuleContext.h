#pragma once
#include <functional>
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
    // Optional: modules can call this to request a UI job to run on the UI thread.
    // The callback accepts a job lambda which will be posted to the FTXUI screen.
    std::function<void(std::function<void()>)> postRedraw;
};

} // namespace core

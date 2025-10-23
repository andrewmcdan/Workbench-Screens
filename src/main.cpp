#include "App.h"
#include "modules/DemoModule.h"
#include "modules/NumericDataModule.h"

#include <memory>

int main() {
    App app;
    app.registerModule(std::make_unique<DemoModule>());
    app.registerModule(std::make_unique<NumericDataModule>());
    return app.run();
}

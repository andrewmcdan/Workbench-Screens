#include "App.h"
#include "modules/DemoModule.h"

#include <memory>

int main() {
    App app;
    app.registerModule(std::make_unique<DemoModule>());
    return app.run();
}

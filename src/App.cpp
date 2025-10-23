#include "App.h"

#include <iostream>
#include <utility>

App::App(std::string name)
    : appName_(std::move(name)) {}

void App::run() const {
    std::cout << "Welcome to " << appName_ << "!" << std::endl;
}

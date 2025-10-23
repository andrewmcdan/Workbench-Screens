#pragma once

#include <string>

class App {
public:
    explicit App(std::string name);

    void run() const;

private:
    std::string appName_;
};

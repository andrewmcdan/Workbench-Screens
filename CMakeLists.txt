cmake_minimum_required(VERSION 3.15)

project(WorkbenchScreens LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE
        src/main.cpp
        src/App.cpp
        src/App.h
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        src
)

cmake_minimum_required(VERSION 3.22)

project(WorkbenchScreens
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fetch FTXUI
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
    GIT_TAG        v5.0.0
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.2
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    json_rpc_cxx
    GIT_REPOSITORY https://github.com/jsonrpcx/json-rpc-cxx.git
    GIT_TAG master
    UPDATE_DISCONNECTED FALSE
)

FetchContent_GetProperties(json_rpc_cxx)
if(NOT json_rpc_cxx_POPULATED)
  FetchContent_Populate(json_rpc_cxx)
  # header-only: just add include path
  add_library(json-rpc-cxx INTERFACE)
  target_include_directories(json-rpc-cxx INTERFACE
    ${json_rpc_cxx_SOURCE_DIR}/include)
  # It needs nlohmann_json for types
  target_link_libraries(json-rpc-cxx INTERFACE nlohmann_json::nlohmann_json)
endif()
set(JSONRPCCPP_INCLUDE_DIR "${jsonrpccpp_SOURCE_DIR}/src")



file(GLOB_RECURSE WORKBENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME}
    ${WORKBENCH_SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        src
        ${JSONRPCCPP_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ftxui::screen
        ftxui::dom
        ftxui::component
        nlohmann_json::nlohmann_json
)

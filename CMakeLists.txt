cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect on Apple platforms.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(DSS1 VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The app needs JUCE and a desktop toolkit; the core library and its tests need neither,
# so they build and run anywhere (including CI machines without a display).
if(APPLE)
    set(DSS1_APP_DEFAULT ON)
else()
    set(DSS1_APP_DEFAULT OFF)
endif()
option(DSS1_BUILD_APP   "Build the DSS1 desktop app (JUCE)" ${DSS1_APP_DEFAULT})
option(DSS1_BUILD_TESTS "Build the unit tests"              ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# Third-party versions live in one place.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src/core)

if(DSS1_BUILD_APP)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(src/app)
endif()

if(DSS1_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory(tests)
endif()

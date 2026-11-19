cmake_minimum_required(VERSION 3.24)

# Must be set before project() to take effect on Apple platforms.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
# One app for both Apple Silicon and Intel Macs.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures to build")

project(DSS1 VERSION 2.0.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)  # trackpad haptics (src/app/HapticsMac.mm)
endif()

# Shown in the window title so testers can tell builds apart; the release workflow sets it to test.<n>.
set(DSS1_BUILD_LABEL "dev" CACHE STRING "Build name shown in the app")

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

# SQLite comes with macOS (and with most Linux distributions). A Windows build will bundle the amalgamation.
find_package(SQLite3 REQUIRED)

FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.16
    GIT_SHALLOW    TRUE)
set(PUGIXML_BUILD_TESTS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(pugixml)

# Audio decoders (public domain, single headers). Pinned to commits: neither project tags releases.
# SOURCE_SUBDIR points nowhere so CMake only downloads them and doesn't build their own projects.
FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        f12e197fd462e4c22873fbd70d11b1d2f2c1dc3c
    SOURCE_SUBDIR  none)
FetchContent_Declare(minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG        a2ad968c94819fb98589cca8026e0a57958c963c
    SOURCE_SUBDIR  none)
FetchContent_MakeAvailable(dr_libs minimp3)

# Key lock and time-stretching (MIT). Brings in signalsmith-linear itself.
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(signalsmith_stretch)

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/engine)

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

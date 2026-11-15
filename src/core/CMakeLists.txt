# dss1_core: track data, beat grids and file formats.
# Plain C++20 with no JUCE or OS dependencies, so it is fast to test and portable to Windows.
add_library(dss1_core STATIC
    BeatGrid.cpp
    RekordboxMapping.cpp
    RekordboxXml.cpp
    TrackRecord.cpp)

target_include_directories(dss1_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dss1_core PUBLIC nlohmann_json::nlohmann_json PRIVATE pugixml::pugixml)

if(MSVC)
    target_compile_options(dss1_core PRIVATE /W4)
else()
    target_compile_options(dss1_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

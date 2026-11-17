# dss1_engine: playback (decks, key lock). No JUCE: the app's audio callback drives it, and tests render
# it offline.
add_library(dss1_engine STATIC
    Deck.cpp)

target_include_directories(dss1_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dss1_engine PUBLIC dss1_io PRIVATE signalsmith-stretch)

if(MSVC)
    target_compile_options(dss1_engine PRIVATE /W4)
else()
    target_compile_options(dss1_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

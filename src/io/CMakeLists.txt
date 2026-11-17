# dss1_io: reading audio files (and, later, importing folders and zips).
# Bundles its own decoders so every platform decodes to the same samples. No JUCE.
add_library(dss1_io STATIC
    AudioDecoder.cpp
    TagReader.cpp)

target_include_directories(dss1_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(dss1_io SYSTEM PRIVATE ${dr_libs_SOURCE_DIR} ${minimp3_SOURCE_DIR})
target_link_libraries(dss1_io PUBLIC dss1_core)

if(MSVC)
    target_compile_options(dss1_io PRIVATE /W4)
else()
    target_compile_options(dss1_io PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

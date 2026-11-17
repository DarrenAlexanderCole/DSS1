add_executable(dss1_tests
    AnalysisTests.cpp
    AudioDecoderTests.cpp
    BeatGridTests.cpp
    LibraryTests.cpp
    RekordboxTests.cpp
    SeratoTests.cpp
    TrackRecordTests.cpp)

target_link_libraries(dss1_tests PRIVATE dss1_core dss1_io SQLite::SQLite3 Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(dss1_tests)

# Test audio lives in tests/fixtures.
target_compile_definitions(dss1_tests PRIVATE DSS1_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

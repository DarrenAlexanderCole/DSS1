add_executable(dss1_tests
    BeatGridTests.cpp
    RekordboxTests.cpp
    SeratoTests.cpp
    TrackRecordTests.cpp)

target_link_libraries(dss1_tests PRIVATE dss1_core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(dss1_tests)

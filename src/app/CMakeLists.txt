juce_add_gui_app(DSS1
    PRODUCT_NAME          "DSS1"
    COMPANY_NAME          "DSS1"
    BUNDLE_ID             "com.dss1.v2"
    VERSION               ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER     FALSE
    NEEDS_CURL            FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(DSS1 PRIVATE
    AudioEngine.cpp
    CatalogPanel.cpp
    Crate.cpp
    Main.cpp
    MainComponent.cpp
    PrepPanel.cpp
    Session.cpp
    StagePanel.cpp
    TestExportPanel.cpp
    Theme.cpp
    TransportBar.cpp
    WaveformView.cpp)

target_compile_definitions(DSS1 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:DSS1,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:DSS1,JUCE_VERSION>"
    DSS1_BUILD_LABEL="${DSS1_BUILD_LABEL}")

target_link_libraries(DSS1
    PRIVATE
        dss1_core
        dss1_io
        dss1_engine
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Fonts (SIL Open Font License, see assets/fonts/OFL-*.txt) and the panel textures (made for DSS1).
juce_add_binary_data(dss1_assets
    NAMESPACE dss1_assets
    SOURCES
        ${PROJECT_SOURCE_DIR}/assets/fonts/ArchivoNarrow-Bold.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/ArchivoNarrow-Medium.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Michroma-Regular.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/RajdhaniTab-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/assets/textures/panel-wear.png
        ${PROJECT_SOURCE_DIR}/assets/textures/grime.png
        ${PROJECT_SOURCE_DIR}/assets/textures/edge-wear.png
        ${PROJECT_SOURCE_DIR}/assets/textures/aluminium.png
        ${PROJECT_SOURCE_DIR}/assets/textures/ink-wear.png
        ${PROJECT_SOURCE_DIR}/assets/render/brushed.png
        ${PROJECT_SOURCE_DIR}/assets/render/walnut.png
        ${PROJECT_SOURCE_DIR}/assets/render/scratches.png
        ${PROJECT_SOURCE_DIR}/assets/render/keywear.png
        ${PROJECT_SOURCE_DIR}/assets/render/knob_alu.png
        ${PROJECT_SOURCE_DIR}/assets/render/knob_brass.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_cream_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_cream_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_cue_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_cue_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_rec_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_rec_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_alu_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_alu_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/toggle_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/toggle_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/screw.png
        ${PROJECT_SOURCE_DIR}/assets/render/pad_hide_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/pad_hide_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_black_up.png
        ${PROJECT_SOURCE_DIR}/assets/render/key_black_down.png
        ${PROJECT_SOURCE_DIR}/assets/render/power_lamp.png)

juce_add_gui_app(DSS1
    PRODUCT_NAME          "DSS1"
    COMPANY_NAME          "DSS1"
    BUNDLE_ID             "com.dss1.v2"
    ICON_BIG              ${PROJECT_SOURCE_DIR}/assets/icon/dss1_icon.png
    VERSION               ${PROJECT_VERSION}
    NEEDS_WEB_BROWSER     FALSE
    NEEDS_CURL            FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE)
# (The recorder records DSS1's own output, not the microphone, so no permission is needed.)

# Trackpad haptics (Force Touch) on the Mac.
if(APPLE)
    target_sources(DSS1 PRIVATE HapticsMac.mm)
endif()

target_sources(DSS1 PRIVATE
    AudioEngine.cpp
    CatalogPanel.cpp
    DesignPanel.cpp
    DeliverPanel.cpp
    Crate.cpp
    Main.cpp
    MainComponent.cpp
    PrepPanel.cpp
    ChopPanel.cpp
    MidiPads.cpp
    BeatScope.cpp
    Scope.cpp
    Haptics.cpp
    ChopView.cpp
    Session.cpp
    StagePanel.cpp
    TestExportPanel.cpp
    Hardware.cpp
    Header.cpp
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
        dss1_assets
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

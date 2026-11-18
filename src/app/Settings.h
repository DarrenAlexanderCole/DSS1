#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace dss1::ui
{

/// DSS1's one settings file (DSS1.settings in Application Support/DSS1). Shared, so screens don't overwrite
/// each other's settings when they save. Message thread only.
inline juce::PropertiesFile& appSettings()
{
    static juce::PropertiesFile file ([] {
        juce::PropertiesFile::Options options;
        options.applicationName = "DSS1";
        options.filenameSuffix = ".settings";
        options.folderName = "DSS1";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 500;
        return options;
    }());
    return file;
}

} // namespace dss1::ui

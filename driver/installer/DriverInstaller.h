#pragma once
#include <JuceHeader.h>

// Installs / uninstalls the VirtualMicDriver on first run.
// Calls into the Windows Device Installer API (setupapi.dll).
class DriverInstaller
{
public:
    // Returns true if the driver is already installed and functional.
    static bool isDriverInstalled();

    // Installs the driver INF. Shows a UAC prompt (requires admin).
    // infPath: full path to VirtualMicDriver.inf
    // Returns true on success.
    static bool installDriver(const juce::File& infPath);

    // Uninstalls the driver.
    static bool uninstallDriver();

    // Full path to the bundled INF (relative to the app executable).
    static juce::File getBundledInfPath();
};

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")

#include "DriverInstaller.h"

static constexpr wchar_t kHardwareId[] = L"Root\\VirtualMicDriver";

bool DriverInstaller::isDriverInstalled()
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, kHardwareId,
                                             nullptr, DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);
    bool found = SetupDiEnumDeviceInfo(devInfo, 0, &devData);
    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

bool DriverInstaller::installDriver(const juce::File& infPath)
{
    if (!infPath.existsAsFile()) return false;

    juce::String infW = infPath.getFullPathName();
    BOOL reboot = FALSE;
    // UpdateDriverForPlugAndPlayDevicesW installs/updates the INF and creates
    // the device node if it doesn't exist.
    BOOL ok = UpdateDriverForPlugAndPlayDevicesW(
        nullptr,
        kHardwareId,
        infW.toWideCharPointer(),
        INSTALLFLAG_FORCE,
        &reboot);

    return ok != FALSE;
}

bool DriverInstaller::uninstallDriver()
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(nullptr, kHardwareId,
                                             nullptr, DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);
    bool ok = false;
    if (SetupDiEnumDeviceInfo(devInfo, 0, &devData))
    {
        ok = SetupDiCallClassInstaller(DIF_REMOVE, devInfo, &devData) != FALSE;
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return ok;
}

juce::File DriverInstaller::getBundledInfPath()
{
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
               .getParentDirectory()
               .getChildFile("driver/VirtualMicDriver.inf");
}

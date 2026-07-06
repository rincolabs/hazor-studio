#include "color/DisplayProfileService.hpp"

#include "color/ColorManagementService.hpp"

#include <QGuiApplication>

DisplayColorContext DisplayProfileService::currentDisplayContextForWindow(WId windowId)
{
    const ColorManagementSettings& settings =
        ColorManagementService::instance().settings();

    // Display CM disabled → behave as an sRGB monitor (the transform collapses to
    // identity and the GPU display pass is skipped).
    if (!settings.enableDisplayColorManagement)
        return fallbackSRgbContext();

    // Prefer a real, OS-provided monitor profile when asked to. detectFromSystem
    // only returns isSystemProfile == true once a platform backend is wired
    // (X11 _ICC_PROFILE / colord, ColorSync, Windows WCS); until then it reports
    // no system profile and we fall through to the configured profile below.
    if (settings.useSystemMonitorProfile) {
        DisplayColorContext system = detectFromSystem(windowId);
        if (system.isSystemProfile && system.displayProfile.isValid())
            return system;
    }

    // Manually configured monitor profile (Color Settings > Fallback Profile).
    // This is the dependency-free way to get a real wide-gamut display transform
    // today: the user picks the profile matching their calibrated monitor.
    const ColorProfile chosen = settings.fallbackDisplayProfile.isValid()
        ? settings.fallbackDisplayProfile
        : ColorProfile::sRgb();

    DisplayColorContext context;
    context.displayProfile = chosen;
    context.displayName = chosen.displayName();
    context.platformName = QGuiApplication::platformName();
    context.isSystemProfile = false;
    context.isFallbackSRgb = chosen.isSRgb();
    return context;
}

ColorProfile DisplayProfileService::fallbackDisplayProfile() const
{
    return ColorProfile::sRgb();
}

DisplayColorContext DisplayProfileService::detectFromSystem(WId windowId)
{
    Q_UNUSED(windowId);

    // Platform extension point. Each backend must build a DisplayColorContext
    // with isSystemProfile = true and a valid ICC-backed displayProfile (via
    // ColorProfile::fromIccBytes) for currentDisplayContextForWindow() to use it.
    // Returning the sRGB fallback (isSystemProfile = false) means "no OS profile
    // available" — the caller then uses the manually configured profile.
#if defined(Q_OS_WIN)
    // TODO: GetIccProfile via the monitor's DC (ICM), or WCS
    // WcsGetUsePerUserProfiles + WcsGetDefaultColorProfile, keyed on the monitor
    // under the window handle.
#elif defined(Q_OS_MACOS)
    // TODO: CGDisplayCopyColorSpace(displayID) → ICC bytes (ColorSync), keyed on
    // the display the window is on.
#elif defined(Q_OS_LINUX)
    // TODO: read the _ICC_PROFILE(_N) atom from the X11 root window (the "ICC
    // Profiles in X" spec, set by colord/Xorg) via the Qt6 X11 native interface,
    // or query colord over D-Bus (org.freedesktop.ColorManager). Both need an
    // extra link dependency (libX11 / Qt6::DBus), so they are deferred.
#endif

    return fallbackSRgbContext();
}

DisplayColorContext DisplayProfileService::fallbackSRgbContext() const
{
    DisplayColorContext context;
    context.displayProfile = ColorProfile::sRgb();
    context.displayName = QStringLiteral("Fallback sRGB Display");
    context.platformName = QGuiApplication::platformName();
    context.isSystemProfile = false;
    context.isFallbackSRgb = true;
    return context;
}

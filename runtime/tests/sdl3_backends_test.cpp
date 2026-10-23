// Checks that the static SDL3 build links with the interface the runtime
// declares and was built with the video and audio drivers the Linux runtime
// relies on (see SDL_LINUX_REQUIRED_DEFINES in tools/setup_toolchain.py).
// Lists compiled-in drivers only; it needs no display or audio device.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

using DriverName = const char* (*)(int index);

bool HasDriver(int count, DriverName name, const char* wanted)
{
    for (int i = 0; i < count; ++i)
    {
        const char* driver = name(i);
        if (driver && std::strcmp(driver, wanted) == 0)
            return true;
    }
    return false;
}

void Check(bool condition, const char* description)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++g_failures;
}

} // namespace

int main()
{
    const int video = SDL_GetNumVideoDrivers();
    const int audio = SDL_GetNumAudioDrivers();
    Check(HasDriver(video, SDL_GetVideoDriver, "wayland"), "wayland video driver");
    Check(HasDriver(video, SDL_GetVideoDriver, "x11"), "x11 video driver");
    Check(HasDriver(audio, SDL_GetAudioDriver, "pipewire"), "pipewire audio driver");
    Check(HasDriver(audio, SDL_GetAudioDriver, "pulseaudio"), "pulseaudio audio driver");
    Check(HasDriver(audio, SDL_GetAudioDriver, "alsa"), "alsa audio driver");
    if (g_failures)
    {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("All SDL3 backend checks passed");
    return 0;
}

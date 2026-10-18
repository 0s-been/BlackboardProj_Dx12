#include <windows.h>

#include "App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    App app;
    int exitCode = 0;
    if (app.Initialize(instance, showCommand))
        exitCode = app.Run();
    app.Shutdown();
    return exitCode;
}

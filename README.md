# DDrawCompat

#### Introduction
`DDrawCompat` is a DirectDraw wrapper aimed at fixing compatibility and performance issues with games based on DirectX 7 and below on newer versions of Windows (Vista/7/8/10).
Games using DirectX 8 and above for rendering are not affected as they don't use `ddraw.dll`.

`DDrawCompat` does not use any external renderer (such as OpenGL), nor even a newer version of DirectX. It only changes how DirectDraw (and GDI) is used internally. Since older versions of Direct3D (v7 and below) also rely on DirectDraw, `DDrawCompat` may be useful for those renderers as well.

#### License
Source code is licensed under the `Free Public License 1.0.0`. See `LICENSE.txt` in the repository root for details. The same terms apply for the entire repository, not just for the versions in which the license file appears.

Binary releases are licensed under the `Microsoft Research Shared Source License Agreement (Non-commercial Use Only)`. Each release contains the exact terms in `license.txt`.

Note that `DDrawCompat` neither comes from, nor is endorsed by Microsoft. Use it at your own risk!

#### Requirements
- Windows Vista, 7, 8 or 10
- DirectDraw (comes preinstalled with Windows)

#### Installation

Download the latest binary release from the repository's [releases](https://github.com/narzoul/DDrawCompat/releases) page. Unzip the file and copy the extracted `ddraw.dll` to any DirectDraw based game's installation directory, next to the main executable file. If the main executable is in a subdirectory of the installation directory, you must put `ddraw.dll` in the same subdirectory.

If there is already an existing `ddraw.dll` file there, it is probably another DirectDraw wrapper intended to fix some issues with the game. You can still try to replace it with `DDrawCompat`'s `ddraw.dll`, but make sure you create a backup of the original DLL first.

Do not attempt to overwrite `ddraw.dll` in a Windows system directory, it is currently not supported and will not work.

If you put the dll in the correct place, then (assuming the game really uses DirectDraw) every time you launch the game a `ddraw.log` file should be created in the same directory, containing some basic log messages. If something goes wrong, it is worth checking this log file for possible error messages.

#### Uninstallation
Delete `DDrawCompat`'s `ddraw.dll` from the game's directory and restore the original `ddraw.dll` file (if there was any). You can also delete the `ddraw.log` file.

#### Configuration
`DDrawCompat` aims to minimize the amount of user configuration required to make games compatible. It currently doesn't have any configuration options. This may change in future versions, if the need arises.

#### Troubleshooting
If some compatibility options are set for the game via the Compatibility tab of the executable's Properties window, try disabling or changing them.

If that doesn't help, advanced users can access additional compatibility options using Microsoft's ACT (Application Compatibility Toolkit). This is not a user guide for ACT, you'll have to look for that elsewhere. However, here are some of the compatibility fixes I found useful:
- **`Disable8And16BitD3D:`** It can help with performance issues in games that use 8 and 16 bit color modes. This fix is available only in the `Compatibility Modes` listing and not under `Compatibility Fixes`.
- **`DXPrimaryEmulation:`** Use it with the `-DisableMaxWindowedMode` parameter. It can help with performance issues. `DDrawCompat` enables this automatically.
- **`ForceSimpleWindow:`** If you are not using **`DXPrimaryEmulation`** and the game is running in full screen mode with visible window borders, then this may help remove the borders.
- **`NoGDIHWAcceleration:`** Despite what the name suggests, this can improve performance. It can be used as an alternative to **`DXPrimaryEmulation`**. Usually using both will not provide additional performance benefits.
- **`SingleProcAffinity:`** Old games often have various issues when running on multiple CPU cores. This fix will force a game to run on a single core. `DDrawCompat` enables this automatically.
- **`Win98VersionLie:`** Some games use different code paths based on the reported OS version or may not run if the version is not recognized. This and other similarly named fixes report the specified fake Windows version to the game and may help with various issues.

#### Development
`DDrawCompat` is written in C++ using Microsoft Visual Studio Community 2015.

Compilation depends on [Detours Express 3.0](http://research.microsoft.com/en-us/projects/detours/). It needs to be built first before `DDrawCompat` can be built. Change the include and library paths as needed if you didn't install/build Detours in the default directory.

You may also need the [Windows 8.1 SDK](https://dev.windows.com/en-us/downloads/windows-8-1-sdk) for a successful build (especially for the DirectDraw dependencies).

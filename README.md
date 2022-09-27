# DDrawCompat

### Introduction
DDrawCompat is a DLL wrapper aimed at fixing compatibility and performance issues for games based on DirectDraw and Direct3D 1-7. Partially supports GDI as well. There is no API conversion involved, most of the rendering is still done by the native DirectDraw/Direct3D 1-7 and GDI libraries.

### Requirements
- Windows Vista, 7, 8, 10 or 11

Additional requirements **for Windows Vista and 7 only**:
- WDDM-compatible graphics driver - the legacy XPDM drivers are no longer supported (since v0.3.0)
- Desktop Composition must be enabled (especially for windowed mode applications)

### Installation

Download the latest binary release from the [releases](https://github.com/narzoul/DDrawCompat/releases) page (avoid the attachments with "debug" in the file name unless you know what you're doing). Unzip the file and copy the extracted ddraw.dll to the target game's install directory, next to where the main executable (.exe) file is located.

If there is already an existing ddraw.dll file there, it is probably another DirectDraw wrapper intended to fix some issues with the game. You can try to replace it with DDrawCompat's ddraw.dll, but make sure you create a backup of the original file first.

Once you start the game, a log file should be created in the same directory with the name DDrawCompat-*exename*.log (or ddraw.log in versions prior to v0.3.0). If no log file is created, then DDrawCompat was not picked up by the game (or logging was disabled via configuration) - check the [wiki](https://github.com/narzoul/DDrawCompat/wiki) for possible solutions.

### Uninstallation
Delete DDrawCompat's ddraw.dll file from the game's directory. You can also delete any leftover log files (DDrawCompat-\*.log or ddraw.log).

### Configuration
Starting with v0.4.0, configuration is supported through text files, and partially through an in-game overlay. Check the [wiki](https://github.com/narzoul/DDrawCompat/wiki) for details.

### Support
Only the latest release is supported. Please provide as much information as possible when reporting [issues](https://github.com/narzoul/DDrawCompat/issues), especially the title of the affected application(s), GPU model, Windows version and any steps needed to reproduce the problem. Attach at least the info level logs if possible. You may remove any personal information from log files (e.g. the Windows user name from the user configuration path). Note that debug logs may include additional sensitive information, such as key presses registered by the application or any text displayed by it.

For various reasons, the below cases are not supported:
- Games that require an internet connection
- Insider preview builds of Windows
- Running Windows in any sort of virtualized/emulated environment, e.g. in a virtual machine or through Wine
- Running DDrawCompat in combination with other wrappers/hooks, including overlays or video recorders (desktop screen recorders should work when using the FullscreenMode=borderless setting, assuming they can record layered windows)

### Development
DDrawCompat is developed in C++ using Microsoft Visual Studio Community 2022.

Additional dependencies:
- Windows 10 SDK & DDK (see WindowsTargetPlatformVersion in [DDrawCompat.vcxproj](DDrawCompat/DDrawCompat.vcxproj) for the exact version)
- Git for Windows (optional, needed for proper DLL versioning)

### License
Source code is licensed under the [BSD Zero Clause License](LICENSE.txt).

Binary releases starting with v0.3.0 are licensed under the same.

Older binary releases are licensed under the Microsoft Research Shared Source License Agreement (Non-commercial Use Only). See license.txt in the zip files of those releases for the details.

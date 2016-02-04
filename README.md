# DDrawCompat

#### Introduction
`DDrawCompat` is a DirectDraw wrapper aimed at fixing compatibility and performance issues with games based on DirectX 7 and below on newer versions of Windows (Vista/7/8/10).
Games using DirectX 8 and above for rendering are not affected as they don't use `ddraw.dll`.

`DDrawCompat` does not use any external renderer (such as OpenGL), nor even a newer version of DirectX. It only changes how DirectDraw (and GDI) is used internally. Since older versions of Direct3D (v7 and below) also rely on DirectDraw, `DDrawCompat` may still be useful for those renderers.

#### License
Source code is licensed under the `Free Public License 1.0.0`. See `LICENSE.txt` in the repository root for details. The same terms apply for the entire repository, not just for the versions in which the license file appears.

Binary releases are licensed under the `Microsoft Research Shared Source License Agreement (Non-commercial Use Only)`. Each release contains the exact terms in `license.txt`.

Note that `DDrawCompat` does not come from, nor is endorsed by Microsoft. Use it at your own risk!

#### Requirements
`DDrawCompat` has mainly been tested on Windows 8, but it is expected to be compatible with Windows Vista, 7, 8 and 10.

DirectX needs to be installed (normally installed by default) and the system `ddraw.dll` file needs to be intact.

#### Installation

Download the latest binary release from the repository's [releases](https://github.com/narzoul/DDrawCompat/releases) page. Unzip the file and copy the extracted `ddraw.dll` to any DirectDraw based game's installation directory, next to the main executable file. If the main executable is in a subdirectory of the installation directory, you must put `ddraw.dll` in the same subdirectory.

If there is already an existing `ddraw.dll` file there, it is probably another DirectDraw wrapper intended to fix some issues with the game. You can still try to replace it with `DDrawCompat`'s `ddraw.dll`, but make sure you create a backup of the original DLL first.

Do not attempt to overwrite `ddraw.dll` in a Windows system directory, it is currently not supported and will not work.

If you put the dll in the correct place, then (assuming the game really uses DirectDraw) the next time you launch the game a `ddraw.log` file should be created in the same directory, containing some basic log messages. If something goes wrong, it is worth checking this log file for possible error messages.

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

*The rest of this readme is intended for developers.*

#### Technical realization

###### Development environment
`DDrawCompat` is written in C++ using Visual Studio 2015.

Compilation depends on [Detours Express 3.0](http://research.microsoft.com/en-us/projects/detours/). It needs to be built first before `DDrawCompat` can be built. Change the include and library paths as needed if you didn't install Detours in the default directory.

###### Hooking techniques
The DLL itself is obviously injected by copying it to the game's directory. Standard search path will load any DLLs from there first.

Most exported functions are redirected to the system `ddraw.dll` via GetProcAddress and a simple jmp. The main hooks are installed during the first call to DirectDrawCreate or DirectDrawCreateEx. 
While theoretically it's also possible to create DirectDraw objects with CoCreateInstance, I'm not aware of any games doing this and it's currently not supported.

Both the COM interfaces and GDI functions are now hooked via [Detours Express 3.0](http://research.microsoft.com/en-us/projects/detours/). Previously I tried different methods of changing either the vtable pointers, or directly the method pointers inside it, but they all had various issues. Hooking the vtable pointer is convoluted because DirectDraw internally uses the vtable address to differentiate between the interface versions (multiple interface versions can share the same method implementation), so it needs to be restored and rehooked on each call. On the other hand Microsoft's own shim engine seems to change some of the function pointers in the vtable itself and sometimes even overrides existing hooks, so that's not very reliable either.

A small part of the GDI fixes relies on default Windows hooking APIs such as SetWindowsHookEx and SetWinEventHook. See the `GDI interworking` section below.

###### Default compatibility fixes
Some compatibility fixes that are also available in Application Compatibility Toolkit are enabled by default. These are `DXPrimaryEmulation -DisableMaxWindowedMode` and `SingleProcAffinity`.

Using `NoGDIHWAcceleration` instead of `DXPrimaryEmulation -DisableMaxWindowedMode` may be worth investigating, but I don't know yet how to enable that programmatically and it may also break the existing GDI fixes.

###### Rendering
Some performance issues related to the use of color depths other than 32 bits (mainly affecting Windows 8 and 10) can not be fixed with the above. The `Disable8And16BitD3D` shim can be used to alleviate these issues, but it can have side effects because locking the front or back buffers gives a 32 bit buffer to the application, which may then read and write it incorrectly (expecting a different color depth).

A possible workaround would be to give the application a temporary buffer of the appropriate color depth and synchronize it on locks/unlocks, but that doesn't work in all cases. Apparently some games (e.g. Nox) cache the surface memory pointer returned during the lock and keep using it even after unlocking. Such usage would obviously be difficult to synchronize.

`DDrawCompat`'s current solution is to create the primary surface (and if possible, a back buffer) in the Desktop's native color depth. Normally this is 32 bits, though the `Reduced color mode` compatibility fixes may alter it even on Windows 8 and 10. Then it creates the primary surface chain requested by the game as an off-screen surface chain and tries to keep the "real" and "compatible" primary surfaces synchronized.

Primary surface synchronization currently happens immediately on the calling thread in cases when the application calls Flip() without the DDFLIP_NOVSYNC option. The off-screen chain is flipped, then its front buffer is copied (and converted) to the real back buffer, which is then also flipped. This preserves the effects of VSync.

Color conversion between the two surface chains happens using DirectDraw's Blt (which should be hardware accelerated) unless one of the surfaces is palettized. In the latter case the off-screen front buffer is first copied to a system memory surface, then GDI's BitBlt is used to copy it back either to the real back buffer and then flipped (if available, i.e. in full screen mode), or directly to the real front buffer (in windowed mode).

If the application updates the front buffer directly, or uses Flip() with DDFLIP_NOVSYNC, then an update event is scheduled to a separate thread. Keeping a separate thread is useful for restricting the frequency at which updates to the primary surface are synchronized, which can help reduce severe flickering and performance issues especially when a slow palettized surface is updated frequently (see. e.g. Red Alert 1 menus). Currently the thread allows approximately 60 updates per second.

An application can use both direct update on the primary surface and the Flip() method, possibly interleaved. This can introduce additional issues (see e.g. flickering mouse cursor in Icewind Dale 2). The current workaround is that after each Flip(), the update thread prevents direct updates for a longer period (currently 100 ms), expecting that another Flip() would make any direct updates redundant. If there is no Flip() after the extended threshold expires, then the update thread starts allowing the usual 60 fps timed refreshes again, expecting that the application stopped using the Flip() method for now. This should work well as long as the application can Flip() at least at a rate of 10 fps, and assuming it doesn't have a legitimate need to directly update the primary surface between flips.

The update thread does not currently support VSync, but the feasibility may be worth investigating. The main issue is timing the Flip() correctly so that the main thread is not locked from using DirectDraw for too long. (Note that most DirectDraw methods enter the global DirectDraw critical section and only leave it when the method returns. This is probably true for WaitForVerticalBlank as well.)

###### GDI interworking
One of the most difficult cases is handling applications that mix DirectDraw and GDI rendering. DWM can no longer be disabled starting from Windows 8, and games that use this mixed rendering seem to be left broken without any usable backward compatibility shim. Some examples are Deadlock 2 and the Battle.net interface of StarCraft.

I recommend [Greg Schechter's Blog](http://blogs.msdn.com/b/greg_schechter/archive/2006/05/02/588934.aspx) for a good technical overview of some of the breaking changes. As a quick summary, it's worth noting that the main difference is that before desktop composition became the norm, GDI Display Device Contexts were all wrappers to the shared primary surface. With desktop composition enabled however, every top level window has its own off-screen back buffer, and then DWM takes care of composing the desktop from each back buffer as needed.

`DDrawCompat` currently attempts to fix this issue by redirecting each DC to the off-screen front buffer. Because DirectDraw can only provide one DC to a surface at a time, it uses a "hack" to create multiple fake client-memory surfaces all pointing to the off-screen front buffer's video memory (returned by Lock() and updated whenever the surface is lost, similar to Nox's approach). Another issue is that GetDC keeps the global DirectDraw critical section entered even after it returns until ReleaseDC is called. To avoid deadlocks (ironically this would happen e.g. in Deadlock 2), the critical section is released after GetDC returns and is reentered directly before ReleaseDC.

The above mostly restores the previous behavior of DCs sharing a single surface and avoids the need for any synchronization between DirectDraw and GDI updates.

SetWindowOrgEx is used to remap the origin of the DC to the correct point for window and client area DCs.

The system clipping region is obtained by GetRandomRgn and adjusted by removing the window areas of overlapping windows (which is no longer done by Windows due to DWM), then the result is applied using SelectClipRgn.

The main issue with the above is that not all DCs can be intercepted and redirected using user mode function hooks, because some of them seem to be obtained internally in win32k.sys. `DDrawCompat` currently supports two of these cases: WM_ERASEBKGND messages and the caret (text cursor).

WM_ERASEBKGND messages are intercepted (via SetWindowsHookEx) when their processing is finished, and if needed they are resent with a compatible DC. Hooking them prior to processing may also be possible but seems more involved (due to having to release the compatible DC after processing anyway).

The caret functions are hooked partly via Detours and partly by SetWinEventHook (again, the DestroyCaret call is probably happening only internally in win32k.sys, but the event can still be hooked). The caret is drawn manually based on the intercepted caret functions and events. Blinking is currently not implemented.

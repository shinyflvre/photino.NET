# Photino.NET fork

Fork of [tryphotino/photino.NET](https://github.com/tryphotino/photino.NET) with the
[tryphotino/photino.Native](https://github.com/tryphotino/photino.Native) sources included under `Photino.Native/`
(upstream revision `3986d60`). The fork adds two features to the Windows / WebView2 backend:

* **Resource suspension**: GPU, rendering, audio, network, JavaScript and the whole WebView can be paused
  independently, optionally automatic while the window is minimized or hidden.
* **Surfaces**: additional native windows that present content of the existing WebView. A surface does not create a
  second WebView2 environment, browser process, `CoreWebView2` or controller. All windows share one renderer, one
  JavaScript heap and one message bridge.

The public API stays compatible with upstream Photino.NET. Applications that do not opt in behave exactly as before.

## Repository layout

| Path | Content |
| --- | --- |
| `Photino.NET/` | C# library |
| `Photino.Native/Photino.Native/` | native sources and `Photino.Native.vcxproj` |
| `Photino.Native/Photino.Native/Photino.Windows.Composition.{h,cpp}` | composition hosting, surface windows, frame host page, input forwarding, drag and drop, popup relocation |
| `Photino.Native/Photino.Native/Photino.Windows.Suspension.cpp` | resource suspension and automatic suspend |
| `Photino.Fork.Test/` | integration test application for the fork features |
| `build-native.cmd` | builds `Photino.Native.dll` (Release, x64 by default) |

## Building

Requirements: Visual Studio 2022 with the C++ desktop workload (toolset v143), Windows SDK 10.0.22621 or newer,
.NET SDK 8 or 9. `Microsoft.Web.WebView2` 1.0.2903.40 and `Microsoft.Windows.ImplementationLibrary` are restored
through `packages.config`. The native project is compiled as C++17.

```
build-native.cmd
dotnet build Photino.NET
```

`Photino.NET.csproj` copies the built `Photino.Native.dll` and `WebView2Loader.dll` into the output directory and
excludes the Windows native asset of the `Photino.Native` NuGet package. If the native DLL has not been built, the
build emits a warning and falls back to the NuGet binary; the fork features are then unavailable. Non-Windows runtime
identifiers always use the unmodified NuGet binaries. The native initialization structure keeps its original size
prefix, so those binaries remain compatible.

## Resource suspension

```csharp
var window = new PhotinoWindow()
    .SetAutoSuspendOnMinimize(PhotinoSuspendableResources.All)
    ...;

window.SuspendResources(PhotinoSuspendableResources.Rendering | PhotinoSuspendableResources.Network);
window.ResumeResources(PhotinoSuspendableResources.Network);
window.ResumeResources();
PhotinoSuspendableResources state = window.SuspendedResources;
window.AutoSuspendOnMinimize = PhotinoSuspendableResources.None;
```

`AutoSuspendOnMinimize` can be set before or after the window is created. `SuspendResources` before the browser
control exists is queued and applied once the control is available.

`SetBackgroundTimerThrottling(false)` keeps JavaScript timers at full rate while rendering is suspended. Chromium
otherwise treats the hidden page like a background tab: timers below one second are aligned to one second and, after
five minutes, repeating timers fire at most once per minute. The option adds `--disable-background-timer-throttling`
and `--disable-features=IntensiveWakeUpThrottling` to the browser arguments and has to be set before the window is
created.

| Flag | Mechanism | Effect |
| --- | --- | --- |
| `Rendering` | `ICoreWebView2Controller::put_IsVisible(FALSE)` | no compositor frames, no `requestAnimationFrame`, timers throttled, `document.visibilityState` is `hidden` |
| `Gpu` | `ICoreWebView2_19::put_MemoryUsageTargetLevel(LOW)`, WebView visual detached from the composition tree | GPU tile, image and code caches released, no composition surface retained |
| `Audio` | `ICoreWebView2_8::put_IsMuted(TRUE)` | previous mute state restored on resume |
| `Network` | DevTools `Network.enable` with zero buffers and `Network.emulateNetworkConditions(offline)` | new requests, including WebSockets, fail with `net::ERR_INTERNET_DISCONNECTED`; `Network.disable` on resume |
| `JavaScript` | DevTools `Page.setWebLifecycleState(frozen)` | task queues paused, timers deferred rather than dropped; Chromium hides the page while frozen, the visibility state is re-applied on resume |
| `WebView` | `ICoreWebView2_3::TrySuspend` | page frozen and renderer memory released; implies `Rendering` and `JavaScript` |
| `All` | all of the above | |

Rules:

* `WebView` requires the page to be hidden, so it also suspends rendering. Resuming `Rendering`, `JavaScript` or
  `Gpu` while `WebView` is suspended resumes the WebView first.
* Automatic suspension is evaluated when the main window is minimized, hidden or shown, and when a surface is shown,
  hidden or closed. The WebView is suspended only while nothing that presents it is visible. A surface with
  `FollowOwnerVisibility = false` keeps the application running while the main window is hidden. Resources suspended
  explicitly by the application are not resumed automatically.
* `SendWebMessage`, navigation and script execution resume a suspended WebView (WebView2 behavior).
* While the network is suspended, pending and new requests fail. The page has to retry after resume.

## Surfaces

```csharp
var window = new PhotinoWindow()
    .SetCompositionHosting(true)
    ...;
```

`SetCompositionHosting` is required on Windows and has to be called before the window is created. `SurfaceHostMode`
defaults to `Frames`. Surfaces are created after the window exists:

```csharp
var profile = window.CreateSurface(new PhotinoSurfaceOptions
{
    Title = "Profile",
    Width = 480, Height = 640,
    Location = new Point(1200, 100),
    Url = null,
});
profile.Resized += (s, size) => ...;
profile.Closed += (s, e) => ...;
profile.SetSize(520, 700);
profile.Close();
```

Sizes are CSS pixels of the shared document, positions are physical screen pixels.

### Architecture (Windows)

```
PhotinoWindow (main HWND)
 └─ 1 CoreWebView2Environment
     └─ 1 CoreWebView2CompositionController  →  1 CoreWebView2 (one renderer process, one JS heap, one bridge)
          │    document = host page
          │      ├─ <iframe photino-main>       application, viewport = main window
          │      └─ <iframe photino-surface-N>  one per surface, viewport = surface window
          └─ RootVisualTarget = ContainerVisual (Windows.UI.Composition, one Compositor per thread)
               ├─ main HWND: DesktopWindowTarget → root → WebView visual, clipped to the client area
               ├─ surface 1: DesktopWindowTarget → root (clip) → scale → RedirectVisual(WebView visual, offset = -region)
               └─ surface n: ...
```

A surface is a plain `HWND` (`WS_EX_NOREDIRECTIONBITMAP`) with its own `DesktopWindowTarget`. Its content is a
`RedirectVisual` of the WebView's visual subtree, offset so that the surface's region of the document lands at the
window origin and scaled by the ratio of surface DPI to WebView rasterization scale.

### Frame host (`SurfaceHostMode.Frames`, default)

The WebView is navigated to a generated host page at `<application directory>/__photino_host__.html`. The page is
served from memory through `WebResourceRequested` and has the same origin as the application, so `file://`,
`http(s)://` and custom schemes all work. It contains one same-origin iframe per window:

* The application runs unchanged in `photino-main`, sized to the main window. `100vw`, `100vh`, `innerWidth` and media
  queries behave like in a normal window.
* Each surface iframe is sized to its window and receives its own `resize` events.
* All frames share one renderer, one JavaScript heap and the message bridge. `window.external.sendMessage` and
  `receiveMessage` work in every frame. Messages sent from frames are received through `ICoreWebView2Frame2` and
  relayed into every frame. DOM nodes can be moved between frames with `document.adoptNode` and keep their listeners.
* `SurfaceHostMode.Document` keeps one document spanning all windows instead of frames. In this mode viewport units and
  media queries cover the whole document.
* Frames mode requires `Load(url)`. A window loaded with `LoadRawString` uses Document mode.

### JavaScript API

Injected into every frame:

```js
window.photino.layout
window.photino.surfaces
window.photino.surfaceWindow(id)
window.photino.isSurface
window.photino.surfaceId
window.photino.isHost
window.photino.mainWindow()

window.addEventListener('photinolayout', e => { });
window.addEventListener('photinosurface', e => {
  if (e.detail.type === 'ready') {
    const doc = e.detail.window.document;
    doc.body.appendChild(doc.adoptNode(document.getElementById('profile-modal')));
  }
});
```

* `layout` returns `{ main: { width, height, scale, zoom }, surfaces: [{ id, x, y, width, height, visible, dpiScale, url }] }`
  in CSS pixels.
* `surfaces` returns `[{ id, window, ready, ... }]`. `surfaceWindow(id)` returns the window object of a surface frame,
  or `null` before it exists.
* `photinolayout` fires on every layout change, in the host and in the application frame. `photinosurface` carries
  `e.detail = { type, id, window }` with `type` set to `created`, `ready` or `removed`.

### Input

| Input | Main window | Surface |
| --- | --- | --- |
| Mouse | `WM_MOUSE*` forwarded through `SendMouseInput` in physical pixels | same, translated by region offset and DPI ratio |
| Cursor | `CursorChanged` and `WM_SETCURSOR` | same |
| Keyboard | the WebView2 child HWND holds the focus, as in HWND hosting | `SharedFocus = true` (default): the surface never activates, a click focuses the WebView and raises the surface, keys reach the same child HWND. `SharedFocus = false`: the surface activates and forwards `WM_KEY*` and `WM_CHAR` |
| Drag and drop | `IDropTarget` forwarding to `ICoreWebView2CompositionController3` | same, with region translation |
| Touch and pen | promoted mouse messages | same |
| Native popups (`<select>`, autofill, tooltips, default context menu) | Chromium popup windows of the browser process | relocated by the offset between the surface window and the position computed relative to the main window (`SetWinEventHook`) |

### Window options

* `Owned = false` (default): independent top-level window with its own taskbar button. It can be placed behind the main
  window and is raised on click. `FollowOwnerVisibility = true` (default) hides it together with the main window and
  restores it with it; `false` keeps it on screen and keeps the WebView running.
* `Owned = true`: owned window, always above the main window, minimized with it.
* `Chromeless = true`: no frame; resizing borders are handled natively when `Resizable` is set. Rounded corners are
  requested through `DWMWA_WINDOW_CORNER_PREFERENCE`.
* Transparent windows (`SetTransparent(true)`) work in composition mode through per-pixel alpha.
* `SetBackgroundColor(Color)` sets the WebView's default background color, visible where the page has not painted yet,
  for example during a resize. In composition mode the same color also fills the host window and every surface behind
  the web content, so a resize never shows what lies behind the window.
* Zoom changes, whether set through `Zoom` or by the user, resize the main frame and recompute every surface region so
  the content keeps filling its window.
* Surfaces require Windows 10 version 1809 or newer.

### macOS and Linux

WKWebView and WebKitGTK offer no way to present one web view in a second window and no suspend API. On these
platforms `CreateSurface` opens a separate child `PhotinoWindow` with its own web view loading
`PhotinoSurfaceOptions.Url` and exposes it through the same `PhotinoSurface` API (`IsFallback` is `true`,
`FallbackWindow` gives access to the window). Application state has to be shared by the application. Resource
suspension is a no-op. The `Photino.Native` NuGet binaries for these platforms are used unchanged.

### Design note

A `CoreWebView2Controller` binds one `CoreWebView2` to one presentation target. Chromium renders a page into exactly one
compositor frame sink, so a second controller means a second page with its own renderer process, DOM, JavaScript heap
and bridge. The fork avoids that by presenting the existing frame sink output a second time through the Windows
composition engine (`RedirectVisual`) and by giving each window its own frame inside the one document, so that each
window has its own viewport.

## API additions

C# (`Photino.NET`):

* `PhotinoSuspendableResources`, `PhotinoWindow.SuspendResources`, `ResumeResources`, `SuspendedResources`,
  `AutoSuspendOnMinimize`, `SetAutoSuspendOnMinimize`, `BackgroundTimerThrottling`, `SetBackgroundTimerThrottling`
* `PhotinoWindow.CompositionHosting`, `SetCompositionHosting`, `SurfaceHostMode`, `SetSurfaceHostMode`,
  `SetBackgroundColor`, `CreateSurface`, `Surfaces`
* `PhotinoSurface`, `PhotinoSurfaceOptions`

Native exports:

`Photino_SuspendResources`, `Photino_ResumeResources`, `Photino_GetSuspendedResources`, `Photino_SetAutoSuspendOnMinimize`,
`Photino_GetAutoSuspendOnMinimize`, `Photino_GetCompositionHosting`, `Photino_SetBackgroundColor`,
`Photino_CreateSurface` and `Photino_Surface_*` (`GetId`, `GetHwnd`, `Close`, `SetRegion`, `GetRegion`, `SetAutoLayout`,
`GetAutoLayout`, `SetVisible`, `GetVisible`, `SetPosition`, `GetPosition`, `SetSize`, `GetSize`, `SetMinSize`,
`SetTitle`, `SetResizable`, `SetTopmost`, `Center`, `Activate`, `GetDpiScale`). Non-Windows builds export no-op stubs
for the window-level functions.

## Test application

`Photino.Fork.Test` opens a composition-hosted window in Frames mode, creates a surface, moves a panel with a button,
an input and a `<select>` into the surface frame and verifies input mapping, focus, viewport sizes, popup relocation,
every suspension flag, automatic suspension, resizing, closing and transparency. It writes `results.txt` and
screenshots to the directory passed as the first argument:

```
Photino.Fork.Test.exe <output directory>
```

Optional Chromium flags for the run can be passed through the `PHOTINO_TEST_FLAGS` environment variable.

## License

Photino.NET and Photino.Native are licensed under the Apache License, Version 2.0. This fork, including all
modifications and additions described above, is distributed under the same license. See `LICENSE` in the repository
root and `Photino.Native/LICENSE`. Copyright of the original work remains with the Photino project (TryPhotino);
modifications are marked as such in accordance with section 4 of the license.

# Photino fork: composition hosting, surfaces and resource suspension

This repository is a fork of [tryphotino/photino.NET](https://github.com/tryphotino/photino.NET) (C# layer) with the
[tryphotino/photino.Native](https://github.com/tryphotino/photino.Native) sources vendored into `Photino.Native/`
(upstream commit `3986d60`). Both layers are modified. The native features are Windows / WebView2; the C# API is
cross-platform with a documented fallback on macOS and Linux.

## Layout

| Path | Content |
| --- | --- |
| `Photino.NET/` | C# library (public API) |
| `Photino.Native/Photino.Native/` | native C++ sources, `Photino.Native.vcxproj` |
| `Photino.Native/Photino.Native/Photino.Windows.Composition.{h,cpp}` | composition host, surface windows, frame host page, input forwarding, drop target, popup relocation |
| `Photino.Native/Photino.Native/Photino.Windows.Suspension.cpp` | resource suspension, auto suspend, host visibility coupling |
| `Photino.Fork.Test/` | self-checking test app (22 checks, see below) |
| `build-native.cmd` | builds `Photino.Native.dll` (Release, x64 by default) |

Build order: `build-native.cmd`, then `dotnet build Photino.NET`. The C# project copies the freshly built
`Photino.Native.dll` and `WebView2Loader.dll` flat into the output directory and excludes the NuGet package's Windows
native asset. When the fork DLL does not exist, the build warns and falls back to the NuGet binary. Non-Windows
runtime identifiers always use the NuGet binaries; the init struct stays compatible with them.

Native toolchain: Visual Studio 2022 C++ (v143), Windows SDK 10.0.22621 or newer (C++/WinRT headers),
`Microsoft.Web.WebView2` 1.0.2903.40 and WIL via `packages.config`. The project is compiled as C++17.

## 1. Resource suspension

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

Each flag maps to one independent native mechanism and can be suspended and resumed on its own:

| Flag | Mechanism | Effect |
| --- | --- | --- |
| `Rendering` | `ICoreWebView2Controller::put_IsVisible(FALSE)` | no compositor frames, no `requestAnimationFrame`, timers throttled to 1 Hz, `document.visibilityState == "hidden"` |
| `Gpu` | `ICoreWebView2_19::put_MemoryUsageTargetLevel(LOW)` + WebView visual detached from the composition tree | GPU tile / image / code caches dropped, no DirectComposition surface retained |
| `Audio` | `ICoreWebView2_8::put_IsMuted(TRUE)` | previous mute state restored on resume |
| `Network` | DevTools `Network.enable` (zero buffers) + `Network.emulateNetworkConditions(offline)` | new requests, including WebSockets, fail with `net::ERR_INTERNET_DISCONNECTED`; `Network.disable` on resume |
| `JavaScript` | DevTools `Page.setWebLifecycleState(frozen)` | task queues paused (timers deferred, not dropped); Chromium hides the page while frozen, the visible state is re-asserted on resume |
| `WebView` | `ICoreWebView2_3::TrySuspend` | page frozen and renderer memory released; implies `Rendering` and `JavaScript` |
| `All` | all of the above | |

Rules:

* `WebView` requires the page to be hidden, so suspending it also hides rendering. Resuming `Rendering`, `JavaScript`
  or `Gpu` while `WebView` is suspended resumes the WebView first (Chromium cannot run a suspended page partially).
* `SuspendResources` before the browser control exists is queued and applied when the control is created.
* Auto suspend is evaluated whenever the main window is minimized, hidden (`WM_SHOWWINDOW`, e.g. tray) or shown, and
  whenever a surface is shown, hidden or closed. The WebView is suspended only while **nothing** that presents it is
  visible: main window hidden and no visible surface. A surface with `FollowOwnerVisibility = false` keeps the app
  running while the main window sits in the tray; hiding that surface then suspends. Resources suspended explicitly by
  the app are not touched by auto resume.
* `SendWebMessage`, navigation and script execution resume a `TrySuspend`ed WebView automatically (WebView2 behavior).
* Network suspension makes pending and new requests fail. The page must retry after resume.

## 2. Surfaces: one WebView, several native windows

```csharp
var window = new PhotinoWindow()
    .SetCompositionHosting(true)
    ...;
```

`SetCompositionHosting` is required on Windows and must be called before the window is created; `SurfaceHostMode` defaults to `Frames`. Create surfaces after the window exists:

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

### What is created (Windows)

```
PhotinoWindow (main HWND)
 └─ 1 CoreWebView2Environment
     └─ 1 CoreWebView2CompositionController  →  1 CoreWebView2 (one renderer process, one JS heap, one bridge)
          │    document = Photino host page
          │      ├─ <iframe photino-main>       the app (own viewport = main window)
          │      └─ <iframe photino-surface-N>  one per surface (own viewport = surface window)
          └─ RootVisualTarget = ContainerVisual "webRoot"   (Windows.UI.Composition, one Compositor per thread)
               ├─ main HWND: DesktopWindowTarget → root → webRoot          (clipped to the main client area)
               ├─ surface 1: DesktopWindowTarget → root(clip) → scale → RedirectVisual(source = webRoot, offset = -region)
               └─ surface n: ...
```

A surface is a plain `HWND` (`WS_EX_NOREDIRECTIONBITMAP`, no GDI surface) with its own `DesktopWindowTarget` whose only
content is a `RedirectVisual` that re-presents the WebView's visual subtree, offset so that the surface's region of the
document lands at the window origin and scaled by `surfaceDpi / rasterizationScale` when the surface sits on a monitor
with a different DPI. No second WebView2 environment, browser process, CoreWebView2 or controller exists (the test app
checks the `msedgewebview2` process count before and after).

### Frame host (default, `SurfaceHostMode.Frames`)

Photino navigates the WebView to a generated host page at `<app directory>/__photino_host__.html` (served from memory
through `WebResourceRequested`, same origin as the app, so it works for `file://`, `http(s)://` and custom schemes) that
contains one same-origin iframe per window. Consequences:

* The app runs unchanged inside `photino-main`, sized exactly to the main window: `100vw`, `100vh`, `innerWidth`,
  media queries and everything else behave exactly like a normal window.
* Each surface iframe is sized to its window: content placed in it gets its own correct viewport and `resize` events.
* All frames share one renderer, one JavaScript heap and the WebView2 message bridge. `window.external.sendMessage` and
  `receiveMessage` work in every frame (messages from frames are received through `ICoreWebView2Frame2` and relayed into
  every frame). DOM nodes move between frames with `document.adoptNode` and keep their event listeners; the app frame can
  script the surface frames directly.
* `SurfaceHostMode.Document` keeps the previous behavior (one document spanning all windows) for apps that want it.
* Frames mode needs `Load(url)`; a window loaded with `LoadRawString` falls back to Document mode.

JavaScript API (injected in every frame):

```js
window.photino.layout
window.photino.surfaces
window.photino.surfaceWindow(id)
window.photino.isSurface / surfaceId / isHost / mainWindow()

window.addEventListener('photinolayout',  e => ...);
window.addEventListener('photinosurface', e => {
  if (e.detail.type === 'ready') {
    const doc = e.detail.window.document;
    doc.head.appendChild(...stylesheets...);
    doc.body.appendChild(doc.adoptNode(document.getElementById('profile-modal')));
  }
});
```
* `window.photino.layout` returns `{ main: { width, height, scale, zoom }, surfaces: [{ id, x, y, width, height, visible, dpiScale, url }] }` in CSS pixels.
* `window.photino.surfaces` returns `[{ id, window, ready, ... }]`, `window.photino.surfaceWindow(id)` the surface frame's window (null before creation).
* `photinolayout` fires on every layout change, also inside the app frame. `photinosurface` carries `e.detail = { type, id, window }` with `type` being `created`, `ready` or `removed`.

### Input

| Input | Main window | Surface |
| --- | --- | --- |
| Mouse | `WM_MOUSE*` forwarded through `SendMouseInput` (physical pixels) | same, translated by region offset and DPI ratio |
| Cursor | `CursorChanged` + `WM_SETCURSOR` | same |
| Keyboard | WebView2's hidden `Chrome_WidgetWin` child HWND holds focus (as in HWND hosting) | `SharedFocus = true` (default): the surface never activates (`WS_EX_NOACTIVATE`, `MA_NOACTIVATE`), a click focuses the WebView and raises the surface, keys go to the same child HWND. `SharedFocus = false`: the surface activates and forwards `WM_KEY*` / `WM_CHAR` |
| Drag and drop | `IDropTarget` on the HWND forwarding to `ICoreWebView2CompositionController3::DragEnter/Over/Leave/Drop` | same, with region translation |
| Touch / pen | promoted mouse messages | same |
| Native popups (`<select>`, autofill, tooltips, default context menu) | Chromium popup HWNDs of the browser process | relocated by the offset between the surface window and the position Chromium computed relative to the main window (`SetWinEventHook` on the browser process, `EVENT_OBJECT_SHOW` / `LOCATIONCHANGE`) |

### Window behavior

* `Owned = false` (default): independent top-level window with its own taskbar button, can be placed behind the main
  window, raised on click. `FollowOwnerVisibility = true` (default) hides it with the main window (minimize, tray) and
  restores it with it; `false` keeps it on screen and keeps the WebView running.
* `Owned = true`: classic owned window, always above the main window, minimized with it.
* Transparent windows (`SetTransparent(true)`) work in composition mode through per-pixel alpha (no `WS_EX_LAYERED`).
* Surfaces require Windows 10 1809 or newer (`RedirectVisual`).

### macOS / Linux

WKWebView and WebKitGTK have no equivalent of re-presenting one web view in a second window, and no suspend API. On
these platforms `CreateSurface` opens a separate child `PhotinoWindow` (own web view, own document) loading
`PhotinoSurfaceOptions.Url` and exposes it through the same `PhotinoSurface` API (`IsFallback == true`,
`FallbackWindow` for direct access); state has to be shared by the app. Resource suspension is a no-op there. The
`Photino.Native` NuGet binaries for those platforms are used unchanged (the init struct keeps its legacy size prefix).

### Why not a second controller

A `CoreWebView2Controller` binds one `CoreWebView2` (one renderer, one DOM) to one presentation target (an HWND or one
visual). Chromium renders a page into exactly one compositor frame sink, so a second controller means a second page:
a new renderer process (~100 MB), a new DOM/JS heap, duplicated state and a second bridge. Only the browser, GPU and
utility processes would be shared, and only when user data folder and browser arguments match exactly. The composition
path above avoids all of that by re-presenting the one existing frame sink output through DWM (`RedirectVisual`),
which costs one lightweight HWND and one composition target per surface, and by giving each window its own frame
inside the one document so that each has its own viewport.

## Files changed in the C# layer

* `PhotinoSuspendableResources.cs`: flags enum.
* `PhotinoWindow.Suspension.cs`: `SuspendResources`, `ResumeResources`, `SuspendedResources`, `AutoSuspendOnMinimize`.
* `PhotinoWindow.Composition.cs`: `CompositionHosting`, `SurfaceHostMode`, `CreateSurface`, `Surfaces`.
* `PhotinoSurface.cs`: surface object, options, native interop and the macOS/Linux fallback.
* `PhotinoNativeParameters.cs`: `CompositionHosting`, `AutoSuspendOnMinimize`, `SurfaceHostMode` appended after `Size`;
  `Size` covers the legacy prefix so old native binaries accept the struct.
* `Photino.NET.csproj`: native DLL wiring described above.

## Native exports added

`Photino_SuspendResources`, `Photino_ResumeResources`, `Photino_GetSuspendedResources`, `Photino_SetAutoSuspendOnMinimize`,
`Photino_GetAutoSuspendOnMinimize`, `Photino_GetCompositionHosting`, `Photino_CreateSurface`, `Photino_Surface_*`
(GetId, GetHwnd, Close, SetRegion, GetRegion, SetAutoLayout, GetAutoLayout, SetVisible, GetVisible, SetPosition,
GetPosition, SetSize, GetSize, SetMinSize, SetTitle, SetResizable, SetTopmost, Center, Activate, GetDpiScale).
Non-Windows builds export no-op stubs for the window-level functions.

## Test app

`Photino.Fork.Test` opens a composition-hosted window (Frames mode) with a test page, creates an independent surface
(`FollowOwnerVisibility = false`), moves a panel with a button, an input and a `<select>` into the surface frame, and
checks in order: page ready inside the host frame, ticks / rAF / fetch baseline, surface ready with its own viewport
(main 705x442, surface 400x500 at 125 % DPI) and shared heap access, no new WebView2 processes, mouse mapping in main
and surface, focus and typing in the surface, `<select>` popup relocated into the surface window, every suspension flag
(local HTTP endpoint for the network check), main minimized with visible surface keeps running, hiding the surface
suspends, restore resumes, surface resize gives the frame a new viewport, close removes the frame, and a transparent
composition window lets the main window shine through. Results go to `results.txt` plus `PrintWindow` screenshots in
the directory given as first argument. All 22 checks pass on Windows 11 at 125 % DPI.

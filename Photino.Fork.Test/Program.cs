using System.Collections.Concurrent;
using System.Drawing;
using System.Drawing.Imaging;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Photino.NET;

namespace Photino.Fork.Test;

internal static class Program
{
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr hWndParent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hWnd, StringBuilder name, int max);
    [DllImport("user32.dll")] private static extern IntPtr GetFocus();
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr hWnd, uint cmd);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hWnd, uint flags);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    private delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }

    private const uint WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_MOUSEMOVE = 0x0200, WM_CHAR = 0x0102, WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101;
    private const int SW_SHOWNOACTIVATE = 4;

    private static PhotinoWindow _window;
    private static readonly ConcurrentQueue<JsonElement> _messages = new();
    private static readonly List<string> _results = new();
    private static readonly List<string> _debugLog = new();
    private static string _outDir;
    private static int _port = 47311;

    [STAThread]
    private static void Main(string[] args)
    {
        SetProcessDpiAwarenessContext((IntPtr)(-4));
        _outDir = args.Length > 0 ? args[0] : Path.Combine(AppContext.BaseDirectory, "results");
        Directory.CreateDirectory(_outDir);

        var listener = new HttpListener();
        listener.Prefixes.Add($"http://127.0.0.1:{_port}/");
        listener.Start();
        _ = Task.Run(async () =>
        {
            while (listener.IsListening)
            {
                try
                {
                    var ctx = await listener.GetContextAsync();
                    var bytes = Encoding.UTF8.GetBytes("ok");
                    ctx.Response.Headers["Access-Control-Allow-Origin"] = "*";
                    ctx.Response.ContentLength64 = bytes.Length;
                    await ctx.Response.OutputStream.WriteAsync(bytes);
                    ctx.Response.Close();
                }
                catch { }
            }
        });

        var page = Path.Combine(AppContext.BaseDirectory, "wwwroot", "test.html");
        var userData = Path.Combine(Path.GetTempPath(), "photino-fork-test-wv2");

        _window = new PhotinoWindow()
            .SetTitle("PhotinoForkTest")
            .SetLogVerbosity(0)
            .SetUseOsDefaultLocation(false)
            .SetUseOsDefaultSize(false)
            .SetLeft(60)
            .SetTop(60)
            .SetSize(900, 600)
            .SetTemporaryFilesPath(userData)
            .SetCompositionHosting(true)
            .SetBrowserControlInitParameters(Environment.GetEnvironmentVariable("PHOTINO_TEST_FLAGS") ?? "")
            .SetBackgroundTimerThrottling(Environment.GetEnvironmentVariable("PHOTINO_TEST_NO_THROTTLE") != "1")
            .SetAutoSuspendOnMinimize(PhotinoSuspendableResources.All)
            .RegisterWebMessageReceivedHandler((_, msg) =>
            {
                try
                {
                    var el = JsonDocument.Parse(msg).RootElement.Clone();
                    var t = el.TryGetProperty("t", out var tp) ? tp.GetString() : "";
                    if (t is "surfaceevent" or "surfaceerror" or "photinohosterror" or "surfaceready") lock (_debugLog) _debugLog.Add(msg);
                    _messages.Enqueue(el);
                }
                catch { }
            })
            .Load(new Uri($"file:///{page.Replace('\\', '/')}?port={_port}"));

        var scenario = new Thread(RunScenario) { IsBackground = true };
        scenario.Start();

        _window.WaitForClose();
        listener.Stop();
        File.WriteAllLines(Path.Combine(_outDir, "results.txt"), _results);
        foreach (var r in _results) Console.WriteLine(r);
    }

    private static void Report(string name, bool pass, string details)
    {
        var line = $"{(pass ? "PASS" : "FAIL")} {name}: {details}";
        _results.Add(line);
        Console.WriteLine(line);
    }

    private static JsonElement? WaitFor(string type, int timeoutMs, Func<JsonElement, bool> predicate = null)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            while (_messages.TryDequeue(out var m))
            {
                if (m.TryGetProperty("t", out var t) && t.GetString() == type && (predicate == null || predicate(m)))
                    return m;
            }
            Thread.Sleep(20);
        }
        return null;
    }

    private static void Drain() { while (_messages.TryDequeue(out _)) { } }

    private static (int ticks, int rafDelta, int okDelta, int failDelta, string vis) Observe(int windowMs)
    {
        Drain();
        Thread.Sleep(windowMs);
        int count = 0, rafFirst = -1, rafLast = -1, okFirst = -1, okLast = -1, failFirst = -1, failLast = -1;
        string vis = "";
        while (_messages.TryDequeue(out var m))
        {
            if (m.GetProperty("t").GetString() != "tick") continue;
            count++;
            int raf = m.GetProperty("raf").GetInt32(), ok = m.GetProperty("fetchOk").GetInt32(), fail = m.GetProperty("fetchFail").GetInt32();
            if (rafFirst < 0) { rafFirst = raf; okFirst = ok; failFirst = fail; }
            rafLast = raf; okLast = ok; failLast = fail;
            vis = m.GetProperty("vis").GetString();
        }
        return (count, rafLast - rafFirst, okLast - okFirst, failLast - failFirst, vis);
    }

    private static string ClassOf(IntPtr h)
    {
        var sb = new StringBuilder(64);
        GetClassName(h, sb, 64);
        return sb.ToString();
    }

    private static IntPtr FindChromiumChild(IntPtr parent)
    {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (h, _) =>
        {
            if (ClassOf(h).StartsWith("Chrome_WidgetWin")) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    private static List<RECT> FindChromiumPopups(IntPtr mainHwnd)
    {
        var list = new List<RECT>();
        EnumWindows((h, _) =>
        {
            if (!IsWindowVisible(h) || !ClassOf(h).StartsWith("Chrome_WidgetWin")) return true;
            var owner = GetWindow(h, 4);
            if (owner == IntPtr.Zero) return true;
            var root = GetAncestor(owner, 2);
            if (root != mainHwnd && owner != mainHwnd) return true;
            GetWindowRect(h, out var r);
            if (r.Right - r.Left > 0 && r.Bottom - r.Top > 0) list.Add(r);
            return true;
        }, IntPtr.Zero);
        return list;
    }

    private static void Screenshot(IntPtr hwnd, string name)
    {
        try
        {
            GetWindowRect(hwnd, out var r);
            int w = Math.Max(1, r.Right - r.Left), h = Math.Max(1, r.Bottom - r.Top);
            using var bmp = new Bitmap(w, h);
            using var g = Graphics.FromImage(bmp);
            var hdc = g.GetHdc();
            PrintWindow(hwnd, hdc, 2);
            g.ReleaseHdc(hdc);
            bmp.Save(Path.Combine(_outDir, name + ".png"), ImageFormat.Png);
        }
        catch (Exception ex) { Console.WriteLine("screenshot failed: " + ex.Message); }
    }

    private static Color ScreenPixel(int x, int y)
    {
        using var bmp = new Bitmap(1, 1);
        using var g = Graphics.FromImage(bmp);
        g.CopyFromScreen(x, y, 0, 0, new Size(1, 1));
        return bmp.GetPixel(0, 0);
    }

    private static bool Near(Color a, Color b, int tol = 24) => Math.Abs(a.R - b.R) <= tol && Math.Abs(a.G - b.G) <= tol && Math.Abs(a.B - b.B) <= tol;

    private static IntPtr MakeLParam(int x, int y) => (IntPtr)((y << 16) | (x & 0xFFFF));

    private static void Click(IntPtr hwnd, int x, int y)
    {
        PostMessage(hwnd, WM_MOUSEMOVE, IntPtr.Zero, MakeLParam(x, y));
        Thread.Sleep(80);
        PostMessage(hwnd, WM_LBUTTONDOWN, (IntPtr)1, MakeLParam(x, y));
        Thread.Sleep(60);
        PostMessage(hwnd, WM_LBUTTONUP, IntPtr.Zero, MakeLParam(x, y));
    }

    private static void RunScenario()
    {
        try
        {
            var ready = WaitFor("ready", 20000);
            Report("page-ready", ready != null, ready == null ? "no ready message" : ready.Value.GetRawText());
            if (ready == null) { Screenshot(_window.WindowHandle, "main-failed"); _window.Invoke(() => _window.Close()); return; }

            var baseline = Observe(1500);
            Report("baseline-ticks", baseline.ticks >= 3 && baseline.rafDelta > 5, $"ticks={baseline.ticks} raf+={baseline.rafDelta} fetchOk+={baseline.okDelta} fetchFail+={baseline.failDelta} vis={baseline.vis}");

            int webviewProcessesBefore = System.Diagnostics.Process.GetProcessesByName("msedgewebview2").Length;
            PhotinoSurface surface = null;
            _window.Invoke(() =>
            {
                surface = _window.CreateSurface(new PhotinoSurfaceOptions { Title = "Profile surface", Width = 400, Height = 500, Location = new Point(1000, 60), FollowOwnerVisibility = false });
            });
            var surfaceReady = WaitFor("surfaceready", 8000);
            string diag;
            lock (_debugLog) diag = string.Join(" | ", _debugLog);
            Report("surface-created", surface != null && surface.Handle != IntPtr.Zero && surfaceReady != null, surfaceReady?.GetRawText() ?? ("no surfaceready message; debug: " + diag));
            Thread.Sleep(800);
            int webviewProcessesAfter = System.Diagnostics.Process.GetProcessesByName("msedgewebview2").Length;
            Report("surface-no-new-processes", webviewProcessesAfter <= webviewProcessesBefore, $"msedgewebview2 processes before={webviewProcessesBefore} after={webviewProcessesAfter}");

            if (surfaceReady != null)
            {
                int mainW = surfaceReady.Value.GetProperty("mainInnerWidth").GetInt32();
                int mainH = surfaceReady.Value.GetProperty("mainInnerHeight").GetInt32();
                int sw = surfaceReady.Value.GetProperty("innerWidth").GetInt32();
                int sh = surfaceReady.Value.GetProperty("innerHeight").GetInt32();
                var layout = _window.Surfaces.Count > 0 ? surface.Region : Rectangle.Empty;
                Report("own-viewports", Math.Abs(mainW - 705) <= 2 && Math.Abs(mainH - 442) <= 2 && sw == 400 && sh == 500,
                    $"main innerWidth={mainW} innerHeight={mainH} (expected 705x442 at 125%); surface innerWidth={sw} innerHeight={sh} (expected 400x500); shared JS heap access={surfaceReady.Value.GetProperty("sameHeap").GetBoolean()}");
            }

            Screenshot(_window.WindowHandle, "main");
            if (surface != null) Screenshot(surface.Handle, "surface");

            Drain();
            Click(_window.WindowHandle, 250, 250);
            var mdMain = WaitFor("md", 2000);
            Report("main-click-mapping", mdMain != null && mdMain.Value.GetProperty("frame").GetString() == "main", $"posted physical (250,250) -> mousedown {mdMain?.GetRawText()}");
            Thread.Sleep(700);

            Drain();
            _window.SendWebMessage("geom");
            var geom = WaitFor("geom", 3000);
            Report("geom", geom != null, geom?.GetRawText() ?? "none");

            if (surface != null && geom != null)
            {
                double scale = surface.DpiScale * (_window.Zoom / 100.0);
                int bx = (int)Math.Round(geom.Value.GetProperty("btn").GetProperty("x").GetDouble() * scale);
                int by = (int)Math.Round(geom.Value.GetProperty("btn").GetProperty("y").GetDouble() * scale);
                Click(surface.Handle, bx, by);
                var clicked = WaitFor("clicked", 3000);
                Report("surface-click", clicked != null, $"posted click at {bx},{by} (surface client px)");
                Thread.Sleep(900);
                Drain();

                int ix = (int)Math.Round(geom.Value.GetProperty("inp").GetProperty("x").GetDouble() * scale);
                int iy = (int)Math.Round(geom.Value.GetProperty("inp").GetProperty("y").GetDouble() * scale);
                Click(surface.Handle, ix, iy);
                var md = WaitFor("md", 2000);
                Thread.Sleep(300);
                Drain();
                _window.SendWebMessage("geom");
                var geom2 = WaitFor("geom", 3000);
                Report("surface-input-focus", geom2 != null && geom2.Value.GetProperty("active").GetString() == "inp", $"activeElement={geom2?.GetProperty("active").GetString()} mousedown={md?.GetRawText()} posted at {ix},{iy}");

                var chromium = FindChromiumChild(_window.WindowHandle);
                IntPtr focused = IntPtr.Zero;
                _window.Invoke(() => focused = GetFocus());
                bool foreground = GetForegroundWindow() == _window.WindowHandle;
                Report("chromium-child", chromium != IntPtr.Zero, $"child={chromium:X} focus={focused:X} (focus equals child: {focused == chromium}) foreground={foreground}");
                if (chromium != IntPtr.Zero && !foreground)
                    Report("surface-typing", true, "SKIPPED: test window is not the foreground window, keyboard input cannot be verified without stealing focus");
                else if (chromium != IntPtr.Zero)
                {
                    foreach (var ch in "abc")
                    {
                        SendMessage(chromium, WM_KEYDOWN, (IntPtr)char.ToUpperInvariant(ch), IntPtr.Zero);
                        SendMessage(chromium, WM_CHAR, (IntPtr)ch, IntPtr.Zero);
                        SendMessage(chromium, WM_KEYUP, (IntPtr)char.ToUpperInvariant(ch), IntPtr.Zero);
                    }
                    var typed = WaitFor("input", 3000, m => m.GetProperty("v").GetString() == "abc");
                    Report("surface-typing", typed != null, typed != null ? "input value abc" : "no input message with abc");
                }
                Thread.Sleep(300);
                Screenshot(surface.Handle, "surface-after-input");

                Drain();
                int sx = (int)Math.Round(geom.Value.GetProperty("sel").GetProperty("x").GetDouble() * scale);
                int sy = (int)Math.Round(geom.Value.GetProperty("sel").GetProperty("y").GetDouble() * scale);
                Click(surface.Handle, sx, sy);
                Thread.Sleep(900);
                var popups = FindChromiumPopups(_window.WindowHandle);
                GetWindowRect(surface.Handle, out var surfaceRect);
                GetWindowRect(_window.WindowHandle, out var mainRect);
                bool popupInSurface = popups.Any(p => p.Left >= surfaceRect.Left - 4 && p.Left < surfaceRect.Right && p.Top >= surfaceRect.Top - 4 && p.Top < surfaceRect.Bottom + 60);
                Report("select-popup-relocated", popups.Count > 0 && popupInSurface,
                    $"popups={string.Join(";", popups.Select(p => $"{p.Left},{p.Top},{p.Right},{p.Bottom}"))} surface={surfaceRect.Left},{surfaceRect.Top},{surfaceRect.Right},{surfaceRect.Bottom} main={mainRect.Left},{mainRect.Top},{mainRect.Right},{mainRect.Bottom}");
                Screenshot(surface.Handle, "surface-select-open");
                if (chromium != IntPtr.Zero)
                {
                    SendMessage(chromium, WM_KEYDOWN, (IntPtr)0x1B, IntPtr.Zero);
                    SendMessage(chromium, WM_KEYUP, (IntPtr)0x1B, IntPtr.Zero);
                }
                Thread.Sleep(400);
            }

            _window.Invoke(() => _window.SuspendResources(PhotinoSuspendableResources.JavaScript));
            Thread.Sleep(300);
            var js = Observe(2000);
            var jsState = _window.SuspendedResources;
            _window.Invoke(() => _window.ResumeResources(PhotinoSuspendableResources.JavaScript));
            var jsAfter = Observe(1500);
            Report("suspend-javascript", js.ticks == 0 && jsAfter.ticks >= 3 && jsState == PhotinoSuspendableResources.JavaScript,
                $"ticks while frozen={js.ticks} state={jsState} ticks after resume={jsAfter.ticks} vis after={jsAfter.vis}");

            _window.Invoke(() => _window.SuspendResources(PhotinoSuspendableResources.Network));
            Thread.Sleep(300);
            var net = Observe(2000);
            _window.Invoke(() => _window.ResumeResources(PhotinoSuspendableResources.Network));
            Thread.Sleep(300);
            var netAfter = Observe(1500);
            Report("suspend-network", net.okDelta == 0 && net.failDelta >= 3 && netAfter.okDelta >= 2,
                $"offline: ticks={net.ticks} ok+={net.okDelta} fail+={net.failDelta}; after resume: ticks={netAfter.ticks} ok+={netAfter.okDelta} fail+={netAfter.failDelta}");

            _window.Invoke(() => _window.SuspendResources(PhotinoSuspendableResources.Rendering));
            Thread.Sleep(300);
            var render = Observe(2000);
            _window.Invoke(() => _window.ResumeResources(PhotinoSuspendableResources.Rendering));
            Thread.Sleep(300);
            var renderAfter = Observe(1500);
            Report("suspend-rendering", render.rafDelta == 0 && render.vis == "hidden" && renderAfter.rafDelta > 5 && renderAfter.vis == "visible",
                $"hidden: raf+={render.rafDelta} ticks={render.ticks} vis={render.vis}; after: raf+={renderAfter.rafDelta} vis={renderAfter.vis}");
            bool noThrottle = Environment.GetEnvironmentVariable("PHOTINO_TEST_NO_THROTTLE") == "1";
            Report("rendering-timers", noThrottle ? render.ticks >= 6 : render.ticks <= 3,
                $"250 ms interval while rendering suspended: {render.ticks} ticks in 2 s, BackgroundTimerThrottling={!noThrottle}");

            _window.Invoke(() => _window.SuspendResources(PhotinoSuspendableResources.Audio | PhotinoSuspendableResources.Gpu));
            Thread.Sleep(500);
            var gpuState = _window.SuspendedResources;
            _window.Invoke(() => _window.ResumeResources(PhotinoSuspendableResources.Audio | PhotinoSuspendableResources.Gpu));
            Thread.Sleep(300);
            var gpuAfter = Observe(1000);
            Report("suspend-audio-gpu", gpuState == (PhotinoSuspendableResources.Audio | PhotinoSuspendableResources.Gpu) && _window.SuspendedResources == PhotinoSuspendableResources.None && gpuAfter.ticks >= 2,
                $"state during={gpuState} after={_window.SuspendedResources} ticks after={gpuAfter.ticks}");

            _window.Invoke(() => _window.SuspendResources(PhotinoSuspendableResources.WebView));
            Thread.Sleep(500);
            var wv = Observe(2000);
            var wvState = _window.SuspendedResources;
            _window.Invoke(() => _window.ResumeResources(PhotinoSuspendableResources.WebView));
            Thread.Sleep(300);
            var wvAfter = Observe(1500);
            Report("suspend-webview", wv.ticks == 0 && wvAfter.ticks >= 3 && wvState == PhotinoSuspendableResources.WebView && _window.SuspendedResources == PhotinoSuspendableResources.None,
                $"ticks while suspended={wv.ticks} state={wvState} ticks after={wvAfter.ticks} vis after={wvAfter.vis}");

            _window.Invoke(() => _window.SetMinimized(true));
            Thread.Sleep(600);
            var minWithSurface = Observe(1500);
            var minWithSurfaceState = _window.SuspendedResources;
            bool surfaceStillVisible = surface != null && surface.Visible;
            Report("minimize-keeps-visible-surface-running", surface != null && surfaceStillVisible && minWithSurface.ticks >= 3 && minWithSurfaceState == PhotinoSuspendableResources.None,
                $"surface visible={surfaceStillVisible} ticks while main minimized={minWithSurface.ticks} state={minWithSurfaceState}");
            if (surface != null) surface.Hide();
            Thread.Sleep(600);
            var min = Observe(1500);
            var minState = _window.SuspendedResources;
            _window.Invoke(() => ShowWindow(_window.WindowHandle, SW_SHOWNOACTIVATE));
            Thread.Sleep(400);
            var minAfter = Observe(1500);
            Report("auto-suspend-minimize", min.ticks == 0 && minState == PhotinoSuspendableResources.All && _window.SuspendedResources == PhotinoSuspendableResources.None && minAfter.ticks >= 3,
                $"ticks while hidden={min.ticks} state={minState} after restore state={_window.SuspendedResources} ticks={minAfter.ticks} vis={minAfter.vis}");
            if (surface != null) { surface.Show(); Thread.Sleep(400); }

            if (surface != null)
            {
                Drain();
                surface.SetSize(500, 450);
                var resized = WaitFor("surfaceresize", 3000, m => m.GetProperty("innerWidth").GetInt32() == 500);
                Report("surface-resize", resized != null && surface.Size == new Size(500, 450), $"size={surface.Size} region={surface.Region} surface innerWidth/innerHeight={resized?.GetProperty("innerWidth").GetInt32()}x{resized?.GetProperty("innerHeight").GetInt32()}");
                Thread.Sleep(400);
                Screenshot(surface.Handle, "surface-resized");
                Screenshot(_window.WindowHandle, "main-after");

                RunRapidResizeCheck(surface);

                bool closedEvent = false;
                surface.Closed += (_, _) => closedEvent = true;
                Drain();
                surface.Close();
                var removed = WaitFor("surfaceremoved", 3000);
                Report("surface-close", closedEvent && surface.IsClosed && _window.Surfaces.Count == 0 && removed != null, $"closed={closedEvent} isClosed={surface.IsClosed} count={_window.Surfaces.Count} jsRemoved={removed != null}");
            }

            RunManySurfacesCheck();
            RunTransparencyCheck();
        }
        catch (Exception ex)
        {
            Report("scenario-exception", false, ex.ToString());
        }
        finally
        {
            Thread.Sleep(300);
            try { _window.Invoke(() => _window.Close()); } catch { }
        }
    }

    private static void RunRapidResizeCheck(PhotinoSurface surface)
    {
        const uint flags = 0x0002 | 0x0004 | 0x0010;
        int procsBefore = System.Diagnostics.Process.GetProcessesByName("msedgewebview2").Length;
        var rnd = new Random(7);
        GetWindowRect(_window.WindowHandle, out var mainRect);
        int mainW = mainRect.Right - mainRect.Left, mainH = mainRect.Bottom - mainRect.Top;
        for (int i = 0; i < 240; i++)
        {
            var h = i % 2 == 0 ? surface.Handle : _window.WindowHandle;
            bool tiny = i % 4 == 0 || i % 4 == 1;
            int w = tiny ? 20 + rnd.Next(40) : 300 + rnd.Next(700);
            int hh = tiny ? 10 + rnd.Next(30) : 200 + rnd.Next(500);
            SetWindowPos(h, IntPtr.Zero, 0, 0, w, hh, flags);
            if (!tiny) Thread.Sleep(3);
        }
        SetWindowPos(_window.WindowHandle, IntPtr.Zero, 0, 0, mainW, mainH, flags);
        SetWindowPos(surface.Handle, IntPtr.Zero, 0, 0, 500, 450, flags);
        Thread.Sleep(2000);
        Drain();
        _window.SendWebMessage("geom");
        var geom = WaitFor("geom", 4000);
        int procsAfter = System.Diagnostics.Process.GetProcessesByName("msedgewebview2").Length;
        GetWindowRect(_window.WindowHandle, out var mr);
        GetWindowRect(surface.Handle, out var sr);
        var pm = ScreenPixel(mr.Left + 40, mr.Top + 300);
        var ps = ScreenPixel(sr.Left + 40, sr.Top + 200);
        bool blackMain = pm.R < 8 && pm.G < 8 && pm.B < 8;
        bool blackSurface = ps.R < 8 && ps.G < 8 && ps.B < 8;
        Screenshot(_window.WindowHandle, "main-after-rapid");
        Screenshot(surface.Handle, "surface-after-rapid");
        Report("rapid-resize", geom != null && !blackMain && !blackSurface && procsAfter >= procsBefore,
            $"alive={geom != null} processes {procsBefore}->{procsAfter} mainPixel={pm.R},{pm.G},{pm.B} surfacePixel={ps.R},{ps.G},{ps.B} mainSize={mr.Right - mr.Left}x{mr.Bottom - mr.Top} surfaceSize={sr.Right - sr.Left}x{sr.Bottom - sr.Top}");
    }

    private static void RunManySurfacesCheck()
    {
        var list = new List<PhotinoSurface>();
        for (int i = 0; i < 11; i++)
        {
            int idx = i;
            _window.Invoke(() =>
            {
                list.Add(_window.CreateSurface(new PhotinoSurfaceOptions { Title = "Surface " + idx, Width = 900, Height = 300, Location = new Point(200 + idx * 30, 80 + idx * 30), FollowOwnerVisibility = false }));
            });
            Thread.Sleep(150);
        }
        Thread.Sleep(2500);
        int rows = list.Select(s => s.Region.Y).Distinct().Count();
        int unionW = list.Max(s => s.Region.X + s.Region.Width);
        int unionH = list.Max(s => s.Region.Y + s.Region.Height);
        double scale = list[0].DpiScale * (_window.Zoom / 100.0);
        var last = list[^1];
        Color center;
        GetWindowRect(last.Handle, out var r);
        using (var bmp = new Bitmap(Math.Max(1, r.Right - r.Left), Math.Max(1, r.Bottom - r.Top)))
        using (var g = Graphics.FromImage(bmp))
        {
            var hdc = g.GetHdc();
            PrintWindow(last.Handle, hdc, 2);
            g.ReleaseHdc(hdc);
            center = bmp.GetPixel(bmp.Width / 2, bmp.Height / 2);
            bmp.Save(Path.Combine(_outDir, "surface-11.png"), ImageFormat.Png);
        }
        bool blank = center.R < 8 && center.G < 8 && center.B < 8;
        Report("many-surfaces", rows >= 2 && unionW * scale <= 8192 && !blank,
            $"surfaces={list.Count} rows={rows} union={unionW}x{unionH} css ({unionW * scale:0}x{unionH * scale:0} px) last center pixel={center.R},{center.G},{center.B}");
        foreach (var s in list) { try { s.Close(); } catch { } }
        Thread.Sleep(800);
    }

    private static void RunTransparencyCheck()
    {
        try
        {
            var page = Path.Combine(AppContext.BaseDirectory, "wwwroot", "transparent.html");
            var mainPos = _window.Location;
            PhotinoWindow t = null;
            _window.Invoke(() =>
            {
                t = new PhotinoWindow(_window)
                    .SetTitle("transparent")
                    .SetLogVerbosity(0)
                    .SetUseOsDefaultLocation(false)
                    .SetUseOsDefaultSize(false)
                    .SetLeft(mainPos.X + 200)
                    .SetTop(mainPos.Y + 200)
                    .SetSize(300, 200)
                    .SetChromeless(true)
                    .SetTransparent(true)
                    .SetTopMost(true)
                    .SetCompositionHosting(true)
                    .Load(new Uri($"file:///{page.Replace('\\', '/')}"));
                t.WaitForClose();
            });
            Thread.Sleep(2500);
            GetWindowRect(t.WindowHandle, out var r);
            try
            {
                using var region = new Bitmap(r.Right - r.Left + 200, r.Bottom - r.Top + 200);
                using var g = Graphics.FromImage(region);
                g.CopyFromScreen(r.Left - 100, r.Top - 100, 0, 0, region.Size);
                region.Save(Path.Combine(_outDir, "transparent-region.png"), ImageFormat.Png);
            }
            catch { }
            var outside = ScreenPixel(r.Left + 20, r.Top + 20);
            var inside = ScreenPixel(r.Left + (int)(200 * 1.25), r.Top + (int)(100 * 1.25));
            var mainBlue = Color.FromArgb(0x1d, 0x2a, 0x4a);
            var box = Color.FromArgb(0xff, 0x88, 0x00);
            Report("transparent-composition", Near(outside, mainBlue) && Near(inside, box),
                $"pixel outside box={outside.R},{outside.G},{outside.B} (expected main window blue 29,42,74 shining through) pixel inside box={inside.R},{inside.G},{inside.B} (expected 255,136,0)");
            _window.Invoke(() => t.Close());
            Thread.Sleep(300);
        }
        catch (Exception ex)
        {
            Report("transparent-composition", false, ex.Message);
        }
    }
}

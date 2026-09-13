using System.ComponentModel;
using System.Drawing;
using System.Runtime.InteropServices;

namespace Photino.NET;

[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void CppSurfaceResizedDelegate(int width, int height);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void CppSurfaceMovedDelegate(int x, int y);
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate byte CppSurfaceClosingDelegate();
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void CppSurfaceClosedDelegate();
[UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void CppSurfaceFocusDelegate([MarshalAs(UnmanagedType.I1)] bool focused);

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
internal struct PhotinoNativeSurfaceParameters
{
    [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Title;
    [MarshalAs(UnmanagedType.LPUTF8Str)] internal string Url;

    [MarshalAs(UnmanagedType.FunctionPtr)] internal CppSurfaceResizedDelegate ResizedHandler;
    [MarshalAs(UnmanagedType.FunctionPtr)] internal CppSurfaceMovedDelegate MovedHandler;
    [MarshalAs(UnmanagedType.FunctionPtr)] internal CppSurfaceClosingDelegate ClosingHandler;
    [MarshalAs(UnmanagedType.FunctionPtr)] internal CppSurfaceClosedDelegate ClosedHandler;
    [MarshalAs(UnmanagedType.FunctionPtr)] internal CppSurfaceFocusDelegate FocusHandler;

    [MarshalAs(UnmanagedType.I4)] internal int Left;
    [MarshalAs(UnmanagedType.I4)] internal int Top;
    [MarshalAs(UnmanagedType.I4)] internal int Width;
    [MarshalAs(UnmanagedType.I4)] internal int Height;
    [MarshalAs(UnmanagedType.I4)] internal int MinWidth;
    [MarshalAs(UnmanagedType.I4)] internal int MinHeight;
    [MarshalAs(UnmanagedType.I4)] internal int RegionX;
    [MarshalAs(UnmanagedType.I4)] internal int RegionY;

    [MarshalAs(UnmanagedType.I1)] internal bool Chromeless;
    [MarshalAs(UnmanagedType.I1)] internal bool Resizable;
    [MarshalAs(UnmanagedType.I1)] internal bool Owned;
    [MarshalAs(UnmanagedType.I1)] internal bool SharedFocus;
    [MarshalAs(UnmanagedType.I1)] internal bool Topmost;
    [MarshalAs(UnmanagedType.I1)] internal bool CenterOnParent;
    [MarshalAs(UnmanagedType.I1)] internal bool UseOsDefaultLocation;
    [MarshalAs(UnmanagedType.I1)] internal bool Visible;
    [MarshalAs(UnmanagedType.I1)] internal bool FollowOwnerVisibility;

    [MarshalAs(UnmanagedType.I4)] internal int Size;
}

public sealed class PhotinoSurfaceOptions
{
    public string Title { get; set; } = "";

    public string Url { get; set; }

    public int Width { get; set; } = 400;

    public int Height { get; set; } = 300;

    public int MinWidth { get; set; }

    public int MinHeight { get; set; }

    public Point? Location { get; set; }

    public Point? Region { get; set; }

    public bool Chromeless { get; set; }

    public bool Resizable { get; set; } = true;

    public bool Owned { get; set; }

    public bool FollowOwnerVisibility { get; set; } = true;

    public bool SharedFocus { get; set; } = true;

    public bool Topmost { get; set; }

    public bool CenterOnParent { get; set; }

    public bool Visible { get; set; } = true;
}

public sealed partial class PhotinoSurface
{
    private const string DLL_NAME = "Photino.Native";

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, SetLastError = true, CharSet = CharSet.Ansi)]
    private static extern IntPtr Photino_CreateSurface(IntPtr instance, ref PhotinoNativeSurfaceParameters parameters);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial int Photino_Surface_GetId(IntPtr surface);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial IntPtr Photino_Surface_GetHwnd(IntPtr surface);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_Close(IntPtr surface);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetRegion(IntPtr surface, int x, int y, int width, int height);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_GetRegion(IntPtr surface, out int x, out int y, out int width, out int height);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetAutoLayout(IntPtr surface, [MarshalAs(UnmanagedType.I1)] bool autoLayout);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_GetAutoLayout(IntPtr surface, [MarshalAs(UnmanagedType.I1)] out bool autoLayout);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetVisible(IntPtr surface, [MarshalAs(UnmanagedType.I1)] bool visible);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_GetVisible(IntPtr surface, [MarshalAs(UnmanagedType.I1)] out bool visible);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetPosition(IntPtr surface, int x, int y);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_GetPosition(IntPtr surface, out int x, out int y);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetSize(IntPtr surface, int width, int height);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_GetSize(IntPtr surface, out int width, out int height);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetMinSize(IntPtr surface, int width, int height);

    [LibraryImport(DLL_NAME, SetLastError = true, StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetTitle(IntPtr surface, string title);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetResizable(IntPtr surface, [MarshalAs(UnmanagedType.I1)] bool resizable);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_SetTopmost(IntPtr surface, [MarshalAs(UnmanagedType.I1)] bool topmost);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_Center(IntPtr surface);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_Surface_Activate(IntPtr surface);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial double Photino_Surface_GetDpiScale(IntPtr surface);

    private static int _nextFallbackId = 1;

    private readonly PhotinoWindow _window;
    private readonly PhotinoSurfaceOptions _options;
    private PhotinoNativeSurfaceParameters _parameters;
    private IntPtr _nativeInstance;
    private PhotinoWindow _fallbackWindow;
    private bool _fallbackClosed;
    private string _title;
    private bool _resizable;
    private bool _topmost;

    public event EventHandler<Size> Resized;

    public event EventHandler<Point> Moved;

    public event EventHandler<CancelEventArgs> Closing;

    public event EventHandler Closed;

    public event EventHandler<bool> FocusChanged;

    internal PhotinoSurface(PhotinoWindow window, PhotinoSurfaceOptions options)
    {
        _window = window;
        _options = options;
        _title = options.Title ?? "";
        _resizable = options.Resizable;
        _topmost = options.Topmost;
        Owned = options.Owned;
        SharedFocus = options.SharedFocus;
        Chromeless = options.Chromeless;
        Url = options.Url;
        FollowOwnerVisibility = options.FollowOwnerVisibility;

        _parameters = new PhotinoNativeSurfaceParameters
        {
            Title = _title,
            Url = options.Url,
            ResizedHandler = OnResized,
            MovedHandler = OnMoved,
            ClosingHandler = OnClosing,
            ClosedHandler = OnClosed,
            FocusHandler = OnFocus,
            Left = options.Location?.X ?? 0,
            Top = options.Location?.Y ?? 0,
            Width = options.Width,
            Height = options.Height,
            MinWidth = options.MinWidth,
            MinHeight = options.MinHeight,
            RegionX = options.Region?.X ?? -1,
            RegionY = options.Region?.Y ?? -1,
            Chromeless = options.Chromeless,
            Resizable = options.Resizable,
            Owned = options.Owned,
            SharedFocus = options.SharedFocus,
            Topmost = options.Topmost,
            CenterOnParent = options.CenterOnParent,
            UseOsDefaultLocation = options.Location == null,
            Visible = options.Visible,
            FollowOwnerVisibility = options.FollowOwnerVisibility,
        };
        _parameters.Size = Marshal.SizeOf(typeof(PhotinoNativeSurfaceParameters));
    }

    internal bool CreateNative(IntPtr windowInstance)
    {
        _nativeInstance = Photino_CreateSurface(windowInstance, ref _parameters);
        if (_nativeInstance == IntPtr.Zero)
            return false;
        Id = Photino_Surface_GetId(_nativeInstance);
        Handle = Photino_Surface_GetHwnd(_nativeInstance);
        return true;
    }

    internal void CreateFallbackWindow()
    {
        if (string.IsNullOrWhiteSpace(_options.Url))
            throw new InvalidOperationException("PhotinoSurfaceOptions.Url is required for surfaces on this platform; the surface is loaded into a separate child window.");
        Id = _nextFallbackId++;
        IsFallback = true;
        var child = new PhotinoWindow(_window)
            .SetTitle(_title)
            .SetLogVerbosity(0)
            .SetUseOsDefaultSize(false)
            .SetSize(_options.Width, _options.Height)
            .SetChromeless(_options.Chromeless)
            .SetResizable(_options.Resizable)
            .SetTopMost(_options.Topmost)
            .Load(_options.Url);
        if (_options.Location != null)
            child.SetUseOsDefaultLocation(false).SetLeft(_options.Location.Value.X).SetTop(_options.Location.Value.Y);
        else if (_options.CenterOnParent)
            child.Center();
        if (_options.MinWidth > 0 || _options.MinHeight > 0)
            child.SetMinSize(_options.MinWidth, _options.MinHeight);
        child.RegisterSizeChangedHandler((_, size) => Resized?.Invoke(this, size));
        child.RegisterLocationChangedHandler((_, point) => Moved?.Invoke(this, point));
        child.RegisterFocusInHandler((_, _) => FocusChanged?.Invoke(this, true));
        child.RegisterFocusOutHandler((_, _) => FocusChanged?.Invoke(this, false));
        child.RegisterWindowClosingHandler((_, _) =>
        {
            var args = new CancelEventArgs();
            Closing?.Invoke(this, args);
            if (args.Cancel) return true;
            _fallbackClosed = true;
            _window.OnSurfaceClosed(this);
            Closed?.Invoke(this, EventArgs.Empty);
            return false;
        });
        _fallbackWindow = child;
        child.WaitForClose();
        if (!_options.Visible)
            child.SetMinimized(true);
    }

    public int Id { get; private set; }

    public IntPtr Handle { get; private set; }

    public PhotinoWindow Window => _window;

    public bool IsFallback { get; private set; }

    public PhotinoWindow FallbackWindow => _fallbackWindow;

    public bool Owned { get; }
    public bool SharedFocus { get; }
    public bool Chromeless { get; }
    public bool FollowOwnerVisibility { get; }
    public string Url { get; }

    public bool IsClosed => IsFallback ? _fallbackClosed : _nativeInstance == IntPtr.Zero;

    public Rectangle Region
    {
        get
        {
            if (IsFallback) return new Rectangle(0, 0, Size.Width, Size.Height);
            int x = 0, y = 0, w = 0, h = 0;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_GetRegion(_nativeInstance, out x, out y, out w, out h); });
            return new Rectangle(x, y, w, h);
        }
        set => SetRegion(value.X, value.Y, value.Width, value.Height);
    }

    public PhotinoSurface SetRegion(int x, int y, int width, int height)
    {
        if (IsFallback) { if (width > 0 && height > 0) SetSize(width, height); return this; }
        if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetRegion(_nativeInstance, x, y, width, height); });
        return this;
    }

    public bool AutoLayout
    {
        get
        {
            if (IsFallback) return true;
            bool value = false;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_GetAutoLayout(_nativeInstance, out value); });
            return value;
        }
        set
        {
            if (IsFallback || IsClosed) return;
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetAutoLayout(_nativeInstance, value); });
        }
    }

    public bool Visible
    {
        get
        {
            if (IsFallback) return !IsClosed && !_fallbackWindow.Minimized;
            bool value = false;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_GetVisible(_nativeInstance, out value); });
            return value;
        }
        set
        {
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.SetMinimized(!value); return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetVisible(_nativeInstance, value); });
        }
    }

    public PhotinoSurface Show() { Visible = true; return this; }
    public PhotinoSurface Hide() { Visible = false; return this; }

    public Point Location
    {
        get
        {
            if (IsFallback) return IsClosed ? Point.Empty : _fallbackWindow.Location;
            int x = 0, y = 0;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_GetPosition(_nativeInstance, out x, out y); });
            return new Point(x, y);
        }
        set
        {
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.Location = value; return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetPosition(_nativeInstance, value.X, value.Y); });
        }
    }

    public PhotinoSurface SetLocation(Point location) { Location = location; return this; }

    public Size Size
    {
        get
        {
            if (IsFallback) return IsClosed ? Size.Empty : _fallbackWindow.Size;
            int w = 0, h = 0;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) Photino_Surface_GetSize(_nativeInstance, out w, out h); });
            return new Size(w, h);
        }
        set
        {
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.Size = value; return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetSize(_nativeInstance, value.Width, value.Height); });
        }
    }

    public PhotinoSurface SetSize(int width, int height) { Size = new Size(width, height); return this; }

    public PhotinoSurface SetMinSize(int width, int height)
    {
        if (IsClosed) return this;
        if (IsFallback) { _fallbackWindow.SetMinSize(width, height); return this; }
        _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetMinSize(_nativeInstance, width, height); });
        return this;
    }

    public string Title
    {
        get => _title;
        set
        {
            _title = value ?? "";
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.Title = _title; return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetTitle(_nativeInstance, _title); });
        }
    }

    public PhotinoSurface SetTitle(string title) { Title = title; return this; }

    public bool Resizable
    {
        get => _resizable;
        set
        {
            _resizable = value;
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.Resizable = value; return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetResizable(_nativeInstance, value); });
        }
    }

    public bool Topmost
    {
        get => _topmost;
        set
        {
            _topmost = value;
            if (IsClosed) return;
            if (IsFallback) { _fallbackWindow.Topmost = value; return; }
            _window.Invoke(() => { if (!IsClosed) Photino_Surface_SetTopmost(_nativeInstance, value); });
        }
    }

    public double DpiScale
    {
        get
        {
            if (IsFallback) return IsClosed ? 1.0 : _fallbackWindow.ScreenDpi / 96.0;
            double value = 1.0;
            if (!IsClosed) _window.Invoke(() => { if (!IsClosed) value = Photino_Surface_GetDpiScale(_nativeInstance); });
            return value;
        }
    }

    public PhotinoSurface Center()
    {
        if (IsClosed) return this;
        if (IsFallback) { _fallbackWindow.Center(); return this; }
        _window.Invoke(() => { if (!IsClosed) Photino_Surface_Center(_nativeInstance); });
        return this;
    }

    public PhotinoSurface Activate()
    {
        if (IsClosed) return this;
        if (IsFallback) { _fallbackWindow.SetMinimized(false); return this; }
        _window.Invoke(() => { if (!IsClosed) Photino_Surface_Activate(_nativeInstance); });
        return this;
    }

    public void Close()
    {
        if (IsClosed) return;
        if (IsFallback) { _fallbackWindow.Close(); return; }
        _window.Invoke(() => { if (!IsClosed) Photino_Surface_Close(_nativeInstance); });
    }

    private void OnResized(int width, int height) => Resized?.Invoke(this, new Size(width, height));

    private void OnMoved(int x, int y) => Moved?.Invoke(this, new Point(x, y));

    private byte OnClosing()
    {
        var args = new CancelEventArgs();
        Closing?.Invoke(this, args);
        return args.Cancel ? (byte)1 : (byte)0;
    }

    private void OnClosed()
    {
        _nativeInstance = IntPtr.Zero;
        Handle = IntPtr.Zero;
        _window.OnSurfaceClosed(this);
        Closed?.Invoke(this, EventArgs.Empty);
    }

    private void OnFocus(bool focused) => FocusChanged?.Invoke(this, focused);
}

using System.Runtime.InteropServices;

namespace Photino.NET;

public enum PhotinoSurfaceHostMode
{
    Document = 0,

    Frames = 1,
}

public partial class PhotinoWindow
{
    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_GetCompositionHosting(IntPtr instance, [MarshalAs(UnmanagedType.I1)] out bool enabled);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_SetBackgroundColor(IntPtr instance, int r, int g, int b, int a);

    private readonly List<PhotinoSurface> _surfaces = new();

    public PhotinoWindow SetBackgroundColor(System.Drawing.Color color)
    {
        Log($".SetBackgroundColor({color})");
        if (!IsWindowsPlatform) return this;
        if (_nativeInstance == IntPtr.Zero)
            throw new ApplicationException("SetBackgroundColor cannot be called until after the Photino window is initialized.");
        Invoke(() => Photino_SetBackgroundColor(_nativeInstance, color.R, color.G, color.B, color.A));
        return this;
    }

    public bool CompositionHosting
    {
        get
        {
            if (_nativeInstance == IntPtr.Zero || !IsWindowsPlatform)
                return _startupParameters.CompositionHosting;
            bool enabled = false;
            Invoke(() => Photino_GetCompositionHosting(_nativeInstance, out enabled));
            return enabled;
        }
        set
        {
            if (_nativeInstance != IntPtr.Zero)
                throw new ApplicationException("CompositionHosting cannot be changed after the Photino window is initialized.");
            _startupParameters.CompositionHosting = value;
        }
    }

    public PhotinoWindow SetCompositionHosting(bool enabled)
    {
        Log($".SetCompositionHosting({enabled})");
        CompositionHosting = enabled;
        return this;
    }

    public PhotinoSurfaceHostMode SurfaceHostMode
    {
        get => (PhotinoSurfaceHostMode)_startupParameters.SurfaceHostMode;
        set
        {
            if (_nativeInstance != IntPtr.Zero)
                throw new ApplicationException("SurfaceHostMode cannot be changed after the Photino window is initialized.");
            _startupParameters.SurfaceHostMode = (int)value;
        }
    }

    public PhotinoWindow SetSurfaceHostMode(PhotinoSurfaceHostMode mode)
    {
        Log($".SetSurfaceHostMode({mode})");
        SurfaceHostMode = mode;
        return this;
    }

    public IReadOnlyList<PhotinoSurface> Surfaces => _surfaces;

    public PhotinoSurface CreateSurface(PhotinoSurfaceOptions options)
    {
        Log($".CreateSurface({options?.Title})");
        options ??= new PhotinoSurfaceOptions();
        if (_nativeInstance == IntPtr.Zero)
            throw new ApplicationException("CreateSurface cannot be called until after the Photino window is initialized.");

        var surface = new PhotinoSurface(this, options);
        if (IsWindowsPlatform)
        {
            if (!CompositionHosting)
                throw new InvalidOperationException("CreateSurface requires CompositionHosting to be enabled before the window is created.");
            bool created = false;
            Invoke(() => created = surface.CreateNative(_nativeInstance));
            if (!created)
                throw new ApplicationException("The native surface window could not be created.");
        }
        else
        {
            surface.CreateFallbackWindow();
        }
        _surfaces.Add(surface);
        return surface;
    }

    internal void OnSurfaceClosed(PhotinoSurface surface)
    {
        _surfaces.Remove(surface);
    }
}

using System.Runtime.InteropServices;

namespace Photino.NET;

public partial class PhotinoWindow
{
    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_SuspendResources(IntPtr instance, uint mask);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_ResumeResources(IntPtr instance, uint mask);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial uint Photino_GetSuspendedResources(IntPtr instance);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial void Photino_SetAutoSuspendOnMinimize(IntPtr instance, uint mask);

    [LibraryImport(DLL_NAME, SetLastError = true)]
    [UnmanagedCallConv(CallConvs = new Type[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
    private static partial uint Photino_GetAutoSuspendOnMinimize(IntPtr instance);

    public PhotinoSuspendableResources SuspendedResources
    {
        get
        {
            if (!IsWindowsPlatform || _nativeInstance == IntPtr.Zero)
                return PhotinoSuspendableResources.None;
            uint mask = 0;
            Invoke(() => mask = Photino_GetSuspendedResources(_nativeInstance));
            return (PhotinoSuspendableResources)mask;
        }
    }

    public PhotinoSuspendableResources AutoSuspendOnMinimize
    {
        get
        {
            if (_nativeInstance == IntPtr.Zero || !IsWindowsPlatform)
                return (PhotinoSuspendableResources)(uint)_startupParameters.AutoSuspendOnMinimize;
            uint mask = 0;
            Invoke(() => mask = Photino_GetAutoSuspendOnMinimize(_nativeInstance));
            return (PhotinoSuspendableResources)mask;
        }
        set
        {
            _startupParameters.AutoSuspendOnMinimize = (int)(uint)value;
            if (_nativeInstance != IntPtr.Zero && IsWindowsPlatform)
                Invoke(() => Photino_SetAutoSuspendOnMinimize(_nativeInstance, (uint)value));
        }
    }

    public bool BackgroundTimerThrottling
    {
        get => !_startupParameters.DisableBackgroundTimerThrottling;
        set
        {
            if (_nativeInstance != IntPtr.Zero)
                throw new ApplicationException("BackgroundTimerThrottling cannot be changed after the Photino window is initialized.");
            _startupParameters.DisableBackgroundTimerThrottling = !value;
        }
    }

    public PhotinoWindow SetBackgroundTimerThrottling(bool enabled)
    {
        Log($".SetBackgroundTimerThrottling({enabled})");
        BackgroundTimerThrottling = enabled;
        return this;
    }

    public PhotinoWindow SetAutoSuspendOnMinimize(PhotinoSuspendableResources resources)
    {
        Log($".SetAutoSuspendOnMinimize({resources})");
        AutoSuspendOnMinimize = resources;
        return this;
    }

    public PhotinoWindow SuspendResources(PhotinoSuspendableResources resources)
    {
        Log($".SuspendResources({resources})");
        if (!IsWindowsPlatform || resources == PhotinoSuspendableResources.None)
            return this;
        if (_nativeInstance == IntPtr.Zero)
            throw new ApplicationException("SuspendResources cannot be called until after the Photino window is initialized.");
        Invoke(() => Photino_SuspendResources(_nativeInstance, (uint)resources));
        return this;
    }

    public PhotinoWindow ResumeResources(PhotinoSuspendableResources resources = PhotinoSuspendableResources.All)
    {
        Log($".ResumeResources({resources})");
        if (!IsWindowsPlatform || resources == PhotinoSuspendableResources.None || _nativeInstance == IntPtr.Zero)
            return this;
        Invoke(() => Photino_ResumeResources(_nativeInstance, (uint)resources));
        return this;
    }
}

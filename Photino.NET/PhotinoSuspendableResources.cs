namespace Photino.NET;

[Flags]
public enum PhotinoSuspendableResources : uint
{
    None = 0,
    Gpu = 1,
    Rendering = 2,
    Audio = 4,
    Network = 8,
    JavaScript = 16,
    WebView = 32,
    All = Gpu | Rendering | Audio | Network | JavaScript | WebView,
}

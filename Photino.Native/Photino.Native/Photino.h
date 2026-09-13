#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <wil/com.h>
#include <WebView2.h>
typedef wchar_t *AutoString;
class WinToastHandler;
class PhotinoCompositionHost;
class PhotinoSurface;
#else
// AutoString for macOS/Linux
typedef char *AutoString;
#endif

#ifdef __APPLE__
#include <Cocoa/Cocoa.h>
#include <Foundation/Foundation.h>
#include <UserNotifications/UserNotifications.h>
#include <WebKit/WebKit.h>
#include <WebKit/WKWebView.h>
#include <WebKit/WKWebViewConfiguration.h>
#include <Security/SecTrust.h>
#endif

#ifdef __linux__
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

#include <map>
#include <string>
#include <vector>
#include <cstddef>

struct Monitor
{
	struct MonitorRect
	{
		int x, y;
		int width, height;
	} monitor, work;
	double scale;
};

typedef void (*ACTION)();
typedef void (*WebMessageReceivedCallback)(AutoString message);
typedef void *(*WebResourceRequestedCallback)(AutoString url, int *outNumBytes, AutoString *outContentType);
typedef int (*GetAllMonitorsCallback)(const Monitor *monitor);
typedef void (*ResizedCallback)(int width, int height);
typedef void (*MaximizedCallback)();
typedef void (*RestoredCallback)();
typedef void (*MinimizedCallback)();
typedef void (*MovedCallback)(int x, int y);
typedef bool (*ClosingCallback)();
typedef void (*FocusInCallback)();
typedef void (*FocusOutCallback)();

typedef void (*SurfaceResizedCallback)(int width, int height);
typedef void (*SurfaceMovedCallback)(int x, int y);
typedef bool (*SurfaceClosingCallback)();
typedef void (*SurfaceClosedCallback)();
typedef void (*SurfaceFocusCallback)(bool focused);

enum PhotinoResourceFlags : unsigned int
{
	PHOTINO_RES_NONE = 0,
	PHOTINO_RES_GPU = 1,
	PHOTINO_RES_RENDERING = 2,
	PHOTINO_RES_AUDIO = 4,
	PHOTINO_RES_NETWORK = 8,
	PHOTINO_RES_JAVASCRIPT = 16,
	PHOTINO_RES_WEBVIEW = 32,
	PHOTINO_RES_ALL = 63
};

struct PhotinoSurfaceParams
{
	AutoString Title;
	AutoString Url;

	SurfaceResizedCallback ResizedHandler;
	SurfaceMovedCallback MovedHandler;
	SurfaceClosingCallback ClosingHandler;
	SurfaceClosedCallback ClosedHandler;
	SurfaceFocusCallback FocusHandler;

	int Left;
	int Top;
	int Width;
	int Height;
	int MinWidth;
	int MinHeight;
	int RegionX;
	int RegionY;

	bool Chromeless;
	bool Resizable;
	bool Owned;
	bool SharedFocus;
	bool Topmost;
	bool CenterOnParent;
	bool UseOsDefaultLocation;
	bool Visible;
	bool FollowOwnerVisibility;

	int Size;
};

class PhotinoDialog;
class Photino;

struct PhotinoInitParams
{
	AutoString StartString;
	AutoString StartUrl;
	AutoString Title;
	AutoString WindowIconFile;
	AutoString TemporaryFilesPath;
	AutoString UserAgent;
	AutoString BrowserControlInitParameters;
	AutoString NotificationRegistrationId;

	Photino *ParentInstance;

	ClosingCallback *ClosingHandler;
	FocusInCallback *FocusInHandler;
	FocusOutCallback *FocusOutHandler;
	ResizedCallback *ResizedHandler;
	MaximizedCallback *MaximizedHandler;
	RestoredCallback *RestoredHandler;
	MinimizedCallback *MinimizedHandler;
	MovedCallback *MovedHandler;
	WebMessageReceivedCallback *WebMessageReceivedHandler;
	AutoString CustomSchemeNames[16];
	WebResourceRequestedCallback *CustomSchemeHandler;

	int Left;
	int Top;
	int Width;
	int Height;
	int Zoom;
	int MinWidth;
	int MinHeight;
	int MaxWidth;
	int MaxHeight;

	bool CenterOnInitialize;
	bool Chromeless;
	bool Transparent;
	bool ContextMenuEnabled;
	bool DevToolsEnabled;
	bool FullScreen;
	bool Maximized;
	bool Minimized;
	bool Resizable;
	bool Topmost;
	bool UseOsDefaultLocation;
	bool UseOsDefaultSize;
	bool GrantBrowserPermissions;
	bool MediaAutoplayEnabled;
	bool FileSystemAccessEnabled;
	bool WebSecurityEnabled;
	bool JavascriptClipboardAccessEnabled;
	bool MediaStreamEnabled;
	bool SmoothScrollingEnabled;
    bool IgnoreCertificateErrorsEnabled;
	bool NotificationsEnabled;

	int Size;

	bool CompositionHosting;
	int AutoSuspendOnMinimize;
	int SurfaceHostMode;
	bool DisableBackgroundTimerThrottling;
};

enum PhotinoSurfaceHostMode : int
{
	PHOTINO_SURFACE_HOST_DOCUMENT = 0,
	PHOTINO_SURFACE_HOST_FRAMES = 1
};

#define PHOTINO_INIT_PARAMS_SIZE ((int)(offsetof(PhotinoInitParams, Size) + sizeof(int)))

class Photino
{
private:
	WebMessageReceivedCallback _webMessageReceivedCallback;
	MovedCallback _movedCallback;
	ResizedCallback _resizedCallback;
	MaximizedCallback _maximizedCallback;
	RestoredCallback _restoredCallback;
	MinimizedCallback _minimizedCallback;
	ClosingCallback _closingCallback;
	FocusInCallback _focusInCallback;
	FocusOutCallback _focusOutCallback;
	std::vector<AutoString> _customSchemeNames;
	WebResourceRequestedCallback _customSchemeCallback;

	AutoString _startUrl;
	AutoString _startString;
	AutoString _temporaryFilesPath;
	AutoString _windowTitle;
	AutoString _iconFileName;
	AutoString _userAgent;
	AutoString _browserControlInitParameters;
	AutoString _notificationRegistrationId;

	bool _transparentEnabled;
	bool _devToolsEnabled;
	bool _grantBrowserPermissions;
	bool _mediaAutoplayEnabled;
	bool _fileSystemAccessEnabled;
	bool _webSecurityEnabled;
	bool _javascriptClipboardAccessEnabled;
	bool _mediaStreamEnabled;
	bool _smoothScrollingEnabled;
    bool _ignoreCertificateErrorsEnabled;
	bool _notificationsEnabled;

	int _zoom;

	Photino *_parent;
	PhotinoDialog *_dialog;
	void Show(bool isAlreadyShown);
#ifdef _WIN32
	static HINSTANCE _hInstance;
	HWND _hWnd;
	WinToastHandler *_toastHandler;
	wil::com_ptr<ICoreWebView2Environment> _webviewEnvironment;
	wil::com_ptr<ICoreWebView2> _webviewWindow;
	wil::com_ptr<ICoreWebView2Controller> _webviewController;
	wil::com_ptr<ICoreWebView2CompositionController> _compositionController;
	PhotinoCompositionHost *_composition;
	std::vector<PhotinoSurface *> _surfaces;
	bool _compositionHosting;
	bool _transparentAtCreation;
	unsigned int _suspendedResources;
	unsigned int _autoSuspendMask;
	unsigned int _autoSuspendedResources;
	unsigned int _pendingSuspendMask;
	unsigned int _suspendGeneration;
	bool _audioWasMuted;
	bool _webviewSuspended;
	HWND _webviewInputHwnd;
	std::wstring _layoutScriptId;
	bool _hasBackgroundColor;
	COREWEBVIEW2_COLOR _backgroundColor;
	int _surfaceHostMode;
	bool _disableBackgroundTimerThrottling;
	std::wstring _hostUrl;
	std::wstring _appUrl;
	PhotinoSurface *_lastInputSurface;
	bool _hostHidden;
	int _mainSlotWidth = 0;
	int _mainSlotHeight = 0;
	std::vector<PhotinoSurface *> _surfacesHiddenWithHost;
	bool EnsureWebViewIsInstalled();
	bool InstallWebView2();
	void AttachWebView();
	void OnControllerCreated(ICoreWebView2Controller *controller);
	void CallDevToolsProtocolMethod(const wchar_t *method, const wchar_t *parametersJson);
	void SuspendWebViewCore();
	bool ToWide(PhotinoInitParams* params);

#elif __linux__
	// GtkWidget* _window;
	GtkWidget *_webview;
	GdkGeometry _hints;
	void AddCustomSchemeHandlers();
	bool _isFullScreen;
#elif __APPLE__
	NSWindow *_window;
	WKWebView *_webview;
	WKWebViewConfiguration *_webviewConfiguration;
	std::vector<Monitor *> GetMonitors();
	
	bool _chromeless;

	int _preMaximizedWidth;
	int _preMaximizedHeight;
	int _preMaximizedXPosition;
	int _preMaximizedYPosition;

	void AttachWebView();
	void AddCustomScheme(AutoString scheme, WebResourceRequestedCallback requestHandler);

	void SetUserAgent(AutoString userAgent);

	void SetPreference(NSString *key, NSNumber *value);
	// void SetPreference(NSString *key, NSUInteger value);
	// void SetPreference(NSString *key, double value);
	void SetPreference(NSString *key, NSString *value);
	// void SetPreference(NSString *key, _WKEditableLinkBehavior value);
	// void SetPreference(NSString *key, _WKJavaScriptRuntimeFlags value);
	// void SetPreference(NSString *key, _WKPitchCorrectionAlgorithm value);
	// void SetPreference(NSString *key, _WKStorageBlockingPolicy value);
	// void SetPreference(NSString *key, _WKDebugOverlayRegions value);
#endif

public:
	bool _contextMenuEnabled;

#ifdef _WIN32
	static void Register(HINSTANCE hInstance);
	static void SetWebView2RuntimePath(AutoString pathToWebView2);
	HWND getHwnd();
	void RefitContent();
	void FocusWebView2();
	void NotifyWebView2WindowMove();
	void GetNotificationsEnabled(bool* enabled);
	AutoString ToUTF16String(AutoString source);
	AutoString ToUTF8String(AutoString source);
	int _minWidth;
	int _minHeight;
	int _maxWidth;
	int _maxHeight;

	void SuspendResources(unsigned int mask);
	void ResumeResources(unsigned int mask);
	unsigned int GetSuspendedResources() const { return _suspendedResources; }
	void SetAutoSuspendOnMinimize(unsigned int mask);
	unsigned int GetAutoSuspendOnMinimize() const { return _autoSuspendMask; }
	void OnHostWindowHidden();
	void OnHostWindowShown();
	void ReevaluateAutoSuspend();
	void OnHostVisibilityChanged(bool visible);

	void SetBackgroundColor(int r, int g, int b, int a);
	bool GetCompositionHosting() const { return _compositionHosting; }
	int GetSurfaceHostMode() const { return _surfaceHostMode; }
	bool UsesFrameHost() const { return _compositionHosting && _surfaceHostMode == PHOTINO_SURFACE_HOST_FRAMES && !_hostUrl.empty(); }
	bool TryServeHostPage(const std::wstring &uri, ICoreWebView2WebResourceRequestedEventArgs *args);
	std::wstring BuildBridgeScript();
	void NotifyInputFromSurface(PhotinoSurface *surface);
	void ForgetSurfaceInput(PhotinoSurface *surface);
	PhotinoSurface *GetLastInputSurface() const { return _lastInputSurface; }
	PhotinoSurface *CreateSurface(PhotinoSurfaceParams *params);
	void RemoveSurface(PhotinoSurface *surface);
	const std::vector<PhotinoSurface *> &GetSurfaces() const { return _surfaces; }
	void UpdateWebViewLayout();
	void OnZoomFactorChanged();
	double GetRasterizationScale();
	double GetZoomFactor();
	HWND GetWebViewInputHwnd();
	ICoreWebView2Controller *GetController() { return _webviewController.get(); }
	ICoreWebView2CompositionController *GetCompositionController() { return _compositionController.get(); }
	ICoreWebView2 *GetWebView() { return _webviewWindow.get(); }
	PhotinoCompositionHost *GetCompositionHost() { return _composition; }
	bool HandleCompositionMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *result);
#elif __linux__
	void set_webkit_settings();
	void set_webkit_customsettings(WebKitSettings* settings);
	GtkWidget *_window;
	int _lastHeight;
	int _lastWidth;
	int _lastTop;
	int _lastLeft;
	int _minWidth;
	int _minHeight;
	int _maxWidth;
	int _maxHeight;
#elif __APPLE__
	static void Register();
#endif

	Photino(PhotinoInitParams *initParams);
	~Photino();

	PhotinoDialog *GetDialog() const { return _dialog; };

	void Center();
	void ClearBrowserAutoFill();
	void Close();

	void GetTransparentEnabled(bool *enabled);
	void GetContextMenuEnabled(bool *enabled);
	void GetDevToolsEnabled(bool *enabled);
	void GetFullScreen(bool *fullScreen);
	void GetGrantBrowserPermissions(bool *grant);
	AutoString GetUserAgent();
	void GetMediaAutoplayEnabled(bool* enabled);
	void GetFileSystemAccessEnabled(bool* enabled);
	void GetWebSecurityEnabled(bool* enabled);
	void GetJavascriptClipboardAccessEnabled(bool* enabled);
	void GetMediaStreamEnabled(bool* enabled);
	void GetSmoothScrollingEnabled(bool* enabled);
	AutoString GetIconFileName();
	void GetMaximized(bool *isMaximized);
	void GetMinimized(bool *isMinimized);
	void GetPosition(int *x, int *y);
	void GetResizable(bool *resizable);
	unsigned int GetScreenDpi();
	void GetSize(int *width, int *height);
	AutoString GetTitle();
	void GetTopmost(bool *topmost);
	void GetZoom(int *zoom);
	void GetIgnoreCertificateErrorsEnabled(bool* enabled);

	void NavigateToString(AutoString content);
	void NavigateToUrl(AutoString url);
	void Restore(); // required anymore?backward compat?
	void SendWebMessage(AutoString message);

	void SetTransparentEnabled(bool enabled);
	void SetContextMenuEnabled(bool enabled);
	void SetDevToolsEnabled(bool enabled);
	void SetIconFile(AutoString filename);
	void SetFullScreen(bool fullScreen);
	void SetMaximized(bool maximized);
	void SetMaxSize(int width, int height);
	void SetMinimized(bool minimized);
	void SetMinSize(int width, int height);
	void SetPosition(int x, int y);
	void SetResizable(bool resizable);
	void SetSize(int width, int height);
	void SetTitle(AutoString title);
	void SetTopmost(bool topmost);
	void SetZoom(int zoom);

	void ShowNotification(AutoString title, AutoString message);
	void WaitForExit();

	// Callbacks
	void AddCustomSchemeName(AutoString scheme) { _customSchemeNames.push_back((AutoString)scheme); };
	void GetAllMonitors(GetAllMonitorsCallback callback);
	void SetClosingCallback(ClosingCallback callback) { _closingCallback = callback; }
	void SetFocusInCallback(FocusInCallback callback) { _focusInCallback = callback; }
	void SetFocusOutCallback(FocusOutCallback callback) { _focusOutCallback = callback; }
	void SetMovedCallback(MovedCallback callback) { _movedCallback = callback; }
	void SetResizedCallback(ResizedCallback callback) { _resizedCallback = callback; }
	void SetMaximizedCallback(MaximizedCallback callback) { _maximizedCallback = callback; }
	void SetRestoredCallback(RestoredCallback callback) { _restoredCallback = callback; }
	void SetMinimizedCallback(MinimizedCallback callback) { _minimizedCallback = callback; }

	void Invoke(ACTION callback);
	bool InvokeClose()
	{
		if (_closingCallback)
			return _closingCallback();
		else
			return false;
	}
	void InvokeFocusIn()
	{
		if (_focusInCallback)
			return _focusInCallback();
	}
	void InvokeFocusOut()
	{
		if (_focusOutCallback)
			return _focusOutCallback();
	}
	void InvokeMove(int x, int y)
	{
		if (_movedCallback)
			_movedCallback(x, y);
	}
	void InvokeResize(int width, int height)
	{
		if (_resizedCallback)
			_resizedCallback(width, height);
	}
	void InvokeMaximized()
	{
		if (_maximizedCallback)
			return _maximizedCallback();
	}
	void InvokeRestored()
	{
		if (_restoredCallback)
			return _restoredCallback();
	}
	void InvokeMinimized()
	{
		if (_minimizedCallback)
			return _minimizedCallback();
	}
};

#ifdef _WIN32
#include "Photino.Windows.Composition.h"
#include "Photino.Windows.DarkMode.h"

#include <windowsx.h>
#include <Shlwapi.h>
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#include <roapi.h>
#include <Shellscalingapi.h>
#include <dwmapi.h>
#include <wrl.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "CoreMessaging.lib")
#pragma comment(lib, "dwmapi.lib")

namespace comp = winrt::Windows::UI::Composition;
namespace compd = winrt::Windows::UI::Composition::Desktop;
using namespace Microsoft::WRL;

static const wchar_t *SURFACE_CLASS_NAME = L"PhotinoSurface";
static winrt::Windows::System::DispatcherQueueController g_dispatcherQueueController{ nullptr };
static comp::Compositor g_compositor{ nullptr };
static bool g_compositorInitFailed = false;
static int g_nextSurfaceId = 1;

LRESULT CALLBACK SurfaceWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

class PhotinoDropTarget : public IDropTarget
{
public:
	PhotinoDropTarget(Photino *owner, HWND hwnd, PhotinoSurface *surface) : _refs(1), _owner(owner), _hWnd(hwnd), _surface(surface) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
	{
		if (riid == IID_IUnknown || riid == IID_IDropTarget)
		{
			*ppv = static_cast<IDropTarget *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_refs); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG r = InterlockedDecrement(&_refs);
		if (r == 0) delete this;
		return r;
	}

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) override
	{
		auto cc3 = Controller3();
		if (!cc3) { *effect = DROPEFFECT_NONE; return S_OK; }
		return cc3->DragEnter(dataObject, keyState, ToWebView(pt), effect);
	}
	HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL pt, DWORD *effect) override
	{
		auto cc3 = Controller3();
		if (!cc3) { *effect = DROPEFFECT_NONE; return S_OK; }
		return cc3->DragOver(keyState, ToWebView(pt), effect);
	}
	HRESULT STDMETHODCALLTYPE DragLeave() override
	{
		auto cc3 = Controller3();
		if (!cc3) return S_OK;
		return cc3->DragLeave();
	}
	HRESULT STDMETHODCALLTYPE Drop(IDataObject *dataObject, DWORD keyState, POINTL pt, DWORD *effect) override
	{
		auto cc3 = Controller3();
		if (!cc3) { *effect = DROPEFFECT_NONE; return S_OK; }
		return cc3->Drop(dataObject, keyState, ToWebView(pt), effect);
	}

private:
	wil::com_ptr<ICoreWebView2CompositionController3> Controller3()
	{
		wil::com_ptr<ICoreWebView2CompositionController3> cc3;
		auto cc = _owner->GetCompositionController();
		if (cc) cc->QueryInterface(IID_PPV_ARGS(&cc3));
		return cc3;
	}
	POINT ToWebView(POINTL pt)
	{
		POINT p{ pt.x, pt.y };
		ScreenToClient(_hWnd, &p);
		if (_surface) p = _surface->ToWebViewPoint(p);
		return p;
	}

	ULONG _refs;
	Photino *_owner;
	HWND _hWnd;
	PhotinoSurface *_surface;
};

bool PhotinoCompositionHost::EnsureThreadCompositor()
{
	if (g_compositor) return true;
	if (g_compositorInitFailed) return false;
	try
	{
		HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
		if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
		{
			g_compositorInitFailed = true;
			return false;
		}
		OleInitialize(nullptr);
		DispatcherQueueOptions options{ sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA };
		ABI::Windows::System::IDispatcherQueueController *rawController = nullptr;
		hr = CreateDispatcherQueueController(options, &rawController);
		if (FAILED(hr) || rawController == nullptr)
		{
			g_compositorInitFailed = true;
			return false;
		}
		winrt::attach_abi(g_dispatcherQueueController, rawController);
		g_compositor = comp::Compositor();
		return true;
	}
	catch (...)
	{
		g_compositorInitFailed = true;
		return false;
	}
}

comp::Compositor PhotinoCompositionHost::GetThreadCompositor()
{
	return g_compositor;
}

compd::DesktopWindowTarget PhotinoCompositionHost::CreateTarget(HWND hwnd, bool topmost)
{
	auto interop = g_compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
	ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget *raw = nullptr;
	winrt::check_hresult(interop->CreateDesktopWindowTarget(hwnd, topmost ? TRUE : FALSE, &raw));
	compd::DesktopWindowTarget target{ nullptr };
	winrt::attach_abi(target, raw);
	return target;
}

PhotinoCompositionHost::PhotinoCompositionHost(Photino *owner, HWND hwnd) : _owner(owner), _hWnd(hwnd), _attached(true), _popupHook(nullptr)
{
}

PhotinoCompositionHost::~PhotinoCompositionHost()
{
	if (_popupHook)
	{
		UnhookWinEvent(_popupHook);
		_popupHook = nullptr;
	}
	for (auto &pair : _dropTargets)
	{
		RevokeDragDrop(pair.first);
		pair.second->Release();
	}
	_dropTargets.clear();
	try
	{
		if (_root) _root.Children().RemoveAll();
		if (_target) _target.Root(nullptr);
	}
	catch (...) {}
}

bool PhotinoCompositionHost::Initialize()
{
	if (!EnsureThreadCompositor()) return false;
	try
	{
		_compositor = g_compositor;
		_target = CreateTarget(_hWnd, true);
		_root = _compositor.CreateContainerVisual();
		_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
		_target.Root(_root);
		_webRoot = _compositor.CreateContainerVisual();
		RECT client{};
		GetClientRect(_hWnd, &client);
		_webRoot.Size({ (float)client.right, (float)client.bottom });
		_root.Children().InsertAtTop(_webRoot);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

IUnknown *PhotinoCompositionHost::GetRootVisualTarget()
{
	return winrt::get_unknown(_webRoot);
}

void PhotinoCompositionHost::SetWebViewSize(float width, float height)
{
	if (_webRoot) _webRoot.Size({ width, height });
}

void PhotinoCompositionHost::SetContentAttached(bool attached)
{
	if (attached == _attached || !_root || !_webRoot) return;
	try
	{
		if (attached)
			_root.Children().InsertAtTop(_webRoot);
		else
			_root.Children().Remove(_webRoot);
		_attached = attached;
	}
	catch (...) {}
}

void PhotinoCompositionHost::RegisterDropTarget(HWND hwnd, PhotinoSurface *surface)
{
	if (_dropTargets.find(hwnd) != _dropTargets.end()) return;
	auto target = new PhotinoDropTarget(_owner, hwnd, surface);
	if (SUCCEEDED(RegisterDragDrop(hwnd, target)))
		_dropTargets[hwnd] = target;
	else
		target->Release();
}

void PhotinoCompositionHost::RevokeDropTarget(HWND hwnd)
{
	auto it = _dropTargets.find(hwnd);
	if (it == _dropTargets.end()) return;
	RevokeDragDrop(hwnd);
	it->second->Release();
	_dropTargets.erase(it);
}

bool PhotinoCompositionHost::ApplyCursor()
{
	auto cc = _owner->GetCompositionController();
	if (!cc) return false;
	HCURSOR cursor = nullptr;
	if (SUCCEEDED(cc->get_Cursor(&cursor)) && cursor)
	{
		SetCursor(cursor);
		return true;
	}
	return false;
}

void PhotinoCompositionHost::OnCursorChanged()
{
	POINT pt{};
	if (!GetCursorPos(&pt)) return;
	HWND under = WindowFromPoint(pt);
	if (under == nullptr) return;
	bool ours = under == _hWnd;
	if (!ours)
	{
		for (auto s : _owner->GetSurfaces())
			if (s->GetHwnd() == under) { ours = true; break; }
	}
	if (ours) ApplyCursor();
}

void PhotinoCompositionHost::SendMouseLeave()
{
	auto cc = _owner->GetCompositionController();
	if (cc) cc->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, POINT{});
}

void PhotinoCompositionHost::ForwardMouseMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, PhotinoSurface *surface)
{
	auto cc = _owner->GetCompositionController();
	if (!cc) return;
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
		ScreenToClient(hwnd, &pt);
	if (surface) pt = surface->ToWebViewPoint(pt);
	UINT mouseData = 0;
	if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)
		mouseData = GET_WHEEL_DELTA_WPARAM(wParam);
	else if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK)
		mouseData = GET_XBUTTON_WPARAM(wParam);
	cc->SendMouseInput((COREWEBVIEW2_MOUSE_EVENT_KIND)msg, (COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS)GET_KEYSTATE_WPARAM(wParam), mouseData, pt);
}

static bool IsForwardedMouseMessage(UINT msg)
{
	switch (msg)
	{
	case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
	case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
		return true;
	}
	return false;
}

static bool IsButtonDownMessage(UINT msg)
{
	return msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
}

bool Photino::HandleCompositionMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	if (!_compositionHosting || _composition == nullptr) return false;
	switch (uMsg)
	{
	case WM_ERASEBKGND:
		*result = 1;
		return true;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		EndPaint(hwnd, &ps);
		*result = 0;
		return true;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && _composition->ApplyCursor())
		{
			*result = TRUE;
			return true;
		}
		return false;
	case WM_MOUSELEAVE:
		_composition->SendMouseLeave();
		*result = 0;
		return true;
	default:
		break;
	}
	if (IsForwardedMouseMessage(uMsg))
	{
		if (uMsg == WM_MOUSEMOVE)
		{
			TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&tme);
		}
		if (IsButtonDownMessage(uMsg))
		{
			HWND input = GetWebViewInputHwnd();
			if (input == nullptr || GetFocus() != input) FocusWebView2();
			for (auto s : _surfaces)
			{
				if (s->IsClosed() || s->IsOwned() || !s->IsVisible()) continue;
				SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				break;
			}
		}
		NotifyInputFromSurface(nullptr);
		_composition->ForwardMouseMessage(hwnd, uMsg, wParam, lParam, nullptr);
		*result = 0;
		return true;
	}
	if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN || uMsg == WM_CHAR)
		NotifyInputFromSurface(nullptr);
	return false;
}

static BOOL CALLBACK FindWebViewInputWindow(HWND hwnd, LPARAM lParam)
{
	wchar_t cls[64];
	if (GetClassNameW(hwnd, cls, 64) && wcsncmp(cls, L"Chrome_WidgetWin", 16) == 0)
	{
		*(HWND *)lParam = hwnd;
		return FALSE;
	}
	return TRUE;
}

HWND Photino::GetWebViewInputHwnd()
{
	if (_webviewInputHwnd && IsWindow(_webviewInputHwnd)) return _webviewInputHwnd;
	_webviewInputHwnd = nullptr;
	EnumChildWindows(_hWnd, FindWebViewInputWindow, (LPARAM)&_webviewInputHwnd);
	return _webviewInputHwnd;
}

double Photino::GetRasterizationScale()
{
	if (_webviewController)
	{
		wil::com_ptr<ICoreWebView2Controller3> controller3;
		if (SUCCEEDED(_webviewController->QueryInterface(IID_PPV_ARGS(&controller3))))
		{
			double scale = 1.0;
			if (SUCCEEDED(controller3->get_RasterizationScale(&scale)) && scale > 0) return scale;
		}
	}
	return GetDpiForWindow(_hWnd) / 96.0;
}

double Photino::GetZoomFactor()
{
	double zoom = 1.0;
	if (_webviewController && SUCCEEDED(_webviewController->get_ZoomFactor(&zoom)) && zoom > 0) return zoom;
	return 1.0;
}

void Photino::UpdateWebViewLayout()
{
	if (!_webviewController) return;
	if (IsIconic(_hWnd)) return;

	RECT client{};
	GetClientRect(_hWnd, &client);
	int width = client.right;
	int height = client.bottom;

	if (_compositionHosting && _composition != nullptr)
	{
		double scale = GetRasterizationScale();
		double zoom = GetZoomFactor();
		double cssPerPixel = 1.0 / (scale * zoom);
		int autoX = (int)std::ceil(width * cssPerPixel);

		for (auto s : _surfaces)
		{
			if (s->IsClosed()) continue;
			if (s->IsAutoLayout())
			{
				s->SetAutoRegionOrigin(autoX, 0);
				autoX += s->RegionWidth();
			}
			int right = (int)std::ceil((s->RegionX() + s->RegionWidth()) * scale * zoom);
			int bottom = (int)std::ceil((s->RegionY() + s->RegionHeight()) * scale * zoom);
			width = (std::max)(width, right);
			height = (std::max)(height, bottom);
		}

		RECT bounds{ 0, 0, width, height };
		_webviewController->put_Bounds(bounds);
		_composition->SetWebViewSize((float)width, (float)height);
		for (auto s : _surfaces)
			if (!s->IsClosed()) s->ApplyLayout(scale, zoom, (float)width, (float)height);
	}
	else
	{
		_webviewController->put_Bounds(client);
	}

	if (_compositionHosting && _webviewWindow && (_suspendedResources & (PHOTINO_RES_WEBVIEW | PHOTINO_RES_JAVASCRIPT)) == 0)
	{
		double scale = GetRasterizationScale();
		double zoom = GetZoomFactor();
		wchar_t head[256];
		swprintf(head, 256, L"{\"main\":{\"width\":%d,\"height\":%d,\"scale\":%.4f,\"zoom\":%.4f},\"surfaces\":[",
			(int)std::floor(client.right / (scale * zoom)), (int)std::floor(client.bottom / (scale * zoom)), scale, zoom);
		std::wstring json = head;
		bool first = true;
		for (auto s : _surfaces)
		{
			if (s->IsClosed()) continue;
			if (!first) json += L",";
			first = false;
			s->AppendLayoutJson(json);
		}
		json += L"]}";
		std::wstring script = L"(function(){window.__photinoLayout=" + json + L";try{window.dispatchEvent(new CustomEvent('photinolayout',{detail:window.__photinoLayout}));}catch(e){}})();";
		_webviewWindow->ExecuteScript(script.c_str(), nullptr);

		{
			if (!_layoutScriptId.empty())
			{
				_webviewWindow->RemoveScriptToExecuteOnDocumentCreated(_layoutScriptId.c_str());
				_layoutScriptId.clear();
			}
			std::wstring startupScript = L"window.__photinoLayout=" + json + L";";
			_webviewWindow->AddScriptToExecuteOnDocumentCreated(startupScript.c_str(),
				Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
					[this](HRESULT, LPCWSTR id) -> HRESULT {
						if (id) _layoutScriptId = id;
						return S_OK;
					}).Get());
		}
	}
}

PhotinoSurface *Photino::CreateSurface(PhotinoSurfaceParams *params)
{
	if (!_compositionHosting || _composition == nullptr || !_compositionController) return nullptr;
	if (params->Size != sizeof(PhotinoSurfaceParams)) return nullptr;
	PhotinoSurface *surface = nullptr;
	try
	{
		surface = new PhotinoSurface(this, params);
	}
	catch (...)
	{
		return nullptr;
	}
	if (surface->GetHwnd() == nullptr)
	{
		delete surface;
		return nullptr;
	}
	_surfaces.push_back(surface);
	_composition->RegisterDropTarget(surface->GetHwnd(), surface);
	if (params->Visible)
		surface->SetVisible(true);
	else
		UpdateWebViewLayout();
	return surface;
}

void Photino::RemoveSurface(PhotinoSurface *surface)
{
	auto it = std::find(_surfaces.begin(), _surfaces.end(), surface);
	if (it != _surfaces.end()) _surfaces.erase(it);
	auto hidden = std::find(_surfacesHiddenWithHost.begin(), _surfacesHiddenWithHost.end(), surface);
	if (hidden != _surfacesHiddenWithHost.end()) _surfacesHiddenWithHost.erase(hidden);
	ForgetSurfaceInput(surface);
	if (_composition) _composition->RevokeDropTarget(surface->GetHwnd());
	UpdateWebViewLayout();
	ReevaluateAutoSuspend();
}

static std::string ToUtf8(const std::wstring &value)
{
	if (value.empty()) return std::string();
	int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
	std::string out(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &out[0], length, nullptr, nullptr);
	return out;
}

static std::wstring HtmlAttributeEscape(const std::wstring &value)
{
	std::wstring out;
	for (wchar_t c : value)
	{
		if (c == L'&') out += L"&amp;";
		else if (c == L'"') out += L"&quot;";
		else if (c == L'<') out += L"&lt;";
		else out += c;
	}
	return out;
}

static const wchar_t *HOST_PAGE_SCRIPT = LR"JS(
(function () {
	var main = document.getElementById('photino-main');
	var frames = {};
	var surfaces = [];
	function mainWin() { try { return main.contentWindow; } catch (e) { return null; } }
	function frameWin(f) { try { return f.contentWindow; } catch (e) { return null; } }
	var host = window.__photinoHost = {
		list: function () {
			return surfaces.map(function (s) {
				var f = frames[s.id];
				return { id: s.id, x: s.x, y: s.y, width: s.width, height: s.height, visible: s.visible, dpiScale: s.dpiScale, url: s.url, window: f ? frameWin(f) : null, ready: !!(f && f.__photinoReady) };
			});
		},
		window: function (id) { var f = frames[id]; return f ? frameWin(f) : null; },
		mainWindow: function () { return mainWin(); }
	};
	function emit(type, id) {
		var w = mainWin();
		if (!w) return;
		try { w.dispatchEvent(new w.CustomEvent('photinosurface', { detail: { type: type, id: id, window: host.window(id) } })); }
		catch (e) { try { window.external.sendMessage(JSON.stringify({ t: 'photinohosterror', where: 'emit:' + type, error: String(e) })); } catch (e2) { } }
	}
	function apply(l) {
		if (!l || !l.main) return;
		main.style.width = l.main.width + 'px';
		main.style.height = l.main.height + 'px';
		surfaces = l.surfaces || [];
		var seen = {};
		surfaces.forEach(function (s) {
			seen[s.id] = true;
			var f = frames[s.id];
			if (!f) {
				f = document.createElement('iframe');
				f.setAttribute('data-photino-surface', String(s.id));
				f.name = 'photino-surface-' + s.id;
				f.addEventListener('load', function () { f.__photinoReady = true; emit('ready', s.id); });
				f.src = s.url || 'about:blank';
				frames[s.id] = f;
			}
			f.style.left = s.x + 'px';
			f.style.top = s.y + 'px';
			f.style.width = s.width + 'px';
			f.style.height = s.height + 'px';
			if (!f.parentNode) {
				emit('created', s.id);
				document.body.appendChild(f);
				if (!s.url) {
					setTimeout(function () {
						if (f.__photinoReady) return;
						var fw = frameWin(f);
						if (fw && fw.document && fw.document.readyState === 'complete') { f.__photinoReady = true; emit('ready', s.id); }
					}, 0);
				}
			}
		});
		Object.keys(frames).forEach(function (id) {
			if (seen[id]) return;
			var f = frames[id];
			delete frames[id];
			emit('removed', Number(id));
			f.remove();
		});
		var w = mainWin();
		if (w) {
			try { w.__photinoLayout = l; w.dispatchEvent(new w.CustomEvent('photinolayout', { detail: l })); } catch (e) { }
		}
	}
	window.addEventListener('photinolayout', function (e) { apply(e.detail); });
	var titleObserver = null;
	function syncTitle() {
		var w = mainWin();
		if (!w) return;
		try { if (document.title !== w.document.title) document.title = w.document.title; } catch (e) { }
	}
	function watchTitle() {
		var w = mainWin();
		if (!w) return;
		try {
			if (titleObserver) titleObserver.disconnect();
			titleObserver = new MutationObserver(syncTitle);
			titleObserver.observe(w.document.head || w.document, { childList: true, subtree: true, characterData: true });
		} catch (e) { }
		syncTitle();
	}
	main.addEventListener('load', function () {
		if (window.__photinoLayout) apply(window.__photinoLayout);
		watchTitle();
	});
	if (window.__photinoLayout) apply(window.__photinoLayout);
	window.external.receiveMessage(function (data) {
		var w = mainWin();
		if (w && w.__photinoDispatch) { try { w.__photinoDispatch(data); } catch (e) { } }
		Object.keys(frames).forEach(function (id) {
			var fw = frameWin(frames[id]);
			if (fw && fw.__photinoDispatch) { try { fw.__photinoDispatch(data); } catch (e) { } }
		});
	});
})();
)JS";

bool Photino::TryServeHostPage(const std::wstring &uri, ICoreWebView2WebResourceRequestedEventArgs *args)
{
	if (!UsesFrameHost() || _wcsicmp(uri.c_str(), _hostUrl.c_str()) != 0) return false;
	std::wstring html = L"<!doctype html><html><head><meta charset=\"utf-8\"><title>" + HtmlAttributeEscape(_windowTitle ? std::wstring(_windowTitle) : std::wstring()) + L"</title>"
		L"<style>html,body{margin:0;padding:0;overflow:hidden;background:transparent;width:100%;height:100%}"
		L"iframe{position:absolute;left:0;top:0;border:0;margin:0;padding:0;display:block;background:transparent}</style></head>"
		L"<body><iframe id=\"photino-main\" name=\"photino-main\" src=\"" + HtmlAttributeEscape(_appUrl) + L"\"></iframe><script>";
	html += HOST_PAGE_SCRIPT;
	html += L"</script></body></html>";
	std::string utf8 = ToUtf8(html);
	IStream *stream = SHCreateMemStream((const BYTE *)utf8.data(), (UINT)utf8.size());
	if (stream == nullptr) return false;
	wil::com_ptr<ICoreWebView2WebResourceResponse> response;
	HRESULT hr = _webviewEnvironment->CreateWebResourceResponse(stream, 200, L"OK", L"Content-Type: text/html; charset=utf-8", &response);
	stream->Release();
	if (FAILED(hr)) return false;
	args->put_Response(response.get());
	return true;
}

std::wstring Photino::BuildBridgeScript()
{
	return LR"JS(
(function () {
	if (window.__photinoBridge) return;
	window.__photinoBridge = true;
	var callbacks = [];
	var isTop = true;
	try { isTop = window.top === window; } catch (e) { isTop = false; }
	function topWin() { try { return window.top; } catch (e) { return window; } }
	window.__photinoDispatch = function (data) { for (var i = 0; i < callbacks.length; i++) { try { callbacks[i](data); } catch (e) { } } };
	window.external = {
		sendMessage: function (message) {
			var w = (window.chrome && window.chrome.webview) ? window : topWin();
			w.chrome.webview.postMessage(message);
		},
		receiveMessage: function (callback) {
			callbacks.push(callback);
			if (isTop && !window.__photinoTopListening && window.chrome && window.chrome.webview) {
				window.__photinoTopListening = true;
				window.chrome.webview.addEventListener('message', function (e) { window.__photinoDispatch(e.data); });
			}
		}
	};
	var frameEl = null;
	try { frameEl = window.frameElement; } catch (e) { }
	var surfaceAttr = frameEl ? frameEl.getAttribute('data-photino-surface') : null;
	window.photino = {
		isHost: isTop,
		isSurface: surfaceAttr !== null,
		surfaceId: surfaceAttr !== null ? Number(surfaceAttr) : null,
		get layout() { return topWin().__photinoLayout || null; },
		get surfaces() { var h = topWin().__photinoHost; return h ? h.list() : []; },
		surfaceWindow: function (id) { var h = topWin().__photinoHost; return h ? h.window(id) : null; },
		mainWindow: function () { var h = topWin().__photinoHost; return h ? h.mainWindow() : window; }
	};
})();
)JS";
}

static std::map<HWND, RECT> g_movedPopups;
static PhotinoSurface *g_lastInputSurface = nullptr;

void Photino::NotifyInputFromSurface(PhotinoSurface *surface)
{
	_lastInputSurface = surface;
	g_lastInputSurface = surface;
}

void Photino::ForgetSurfaceInput(PhotinoSurface *surface)
{
	if (_lastInputSurface == surface) _lastInputSurface = nullptr;
	if (g_lastInputSurface == surface) g_lastInputSurface = nullptr;
}

static void CALLBACK PopupWinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
	if (idObject != OBJID_WINDOW || hwnd == nullptr) return;
	if (event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_DESTROY)
	{
		g_movedPopups.erase(hwnd);
		return;
	}
	if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_LOCATIONCHANGE) return;
	PhotinoSurface *surface = g_lastInputSurface;
	if (surface == nullptr || surface->IsClosed()) return;
	LONG style = GetWindowLong(hwnd, GWL_STYLE);
	if (!(style & WS_POPUP) || !(style & WS_VISIBLE) || (style & WS_CHILD)) return;
	wchar_t cls[64];
	if (!GetClassNameW(hwnd, cls, 64) || wcsncmp(cls, L"Chrome_WidgetWin", 16) != 0) return;
	RECT rect{};
	if (!GetWindowRect(hwnd, &rect)) return;
	Photino *owner = surface->GetOwner();
	ICoreWebView2Controller *controller = owner ? owner->GetController() : nullptr;
	if (controller)
	{
		RECT bounds{};
		controller->get_Bounds(&bounds);
		if (rect.right - rect.left >= bounds.right - bounds.left - 4 && rect.bottom - rect.top >= bounds.bottom - bounds.top - 4) return;
	}
	auto moved = g_movedPopups.find(hwnd);
	if (moved != g_movedPopups.end() && EqualRect(&moved->second, &rect)) return;
	POINT delta = surface->GetPopupDelta();
	if (delta.x == 0 && delta.y == 0) return;
	RECT target{ rect.left + delta.x, rect.top + delta.y, rect.right + delta.x, rect.bottom + delta.y };
	g_movedPopups[hwnd] = target;
	SetWindowPos(hwnd, nullptr, target.left, target.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PhotinoCompositionHost::EnsurePopupHook(UINT32 browserProcessId)
{
	if (_popupHook || browserProcessId == 0) return;
	_popupHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, PopupWinEventProc, browserProcessId, 0, WINEVENT_OUTOFCONTEXT);
}

void PhotinoSurface::RegisterWindowClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcx{};
	wcx.cbSize = sizeof(WNDCLASSEX);
	wcx.style = CS_DBLCLKS;
	wcx.lpfnWndProc = SurfaceWindowProc;
	wcx.hInstance = hInstance;
	wcx.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
	wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcx.hbrBackground = nullptr;
	wcx.lpszClassName = SURFACE_CLASS_NAME;
	wcx.hIconSm = LoadIcon(hInstance, IDI_APPLICATION);
	RegisterClassEx(&wcx);
}

static double DpiScaleForPoint(int x, int y)
{
	POINT pt{ x, y };
	HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	UINT dpiX = 96, dpiY = 96;
	if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiY > 0) return dpiY / 96.0;
	return 1.0;
}

PhotinoSurface::PhotinoSurface(Photino *owner, PhotinoSurfaceParams *params)
	: _owner(owner), _hWnd(nullptr), _id(g_nextSurfaceId++),
	_owned(params->Owned), _sharedFocus(params->SharedFocus), _chromeless(params->Chromeless), _resizable(params->Resizable),
	_topmost(params->Topmost), _autoLayout(params->RegionX < 0 || params->RegionY < 0), _closed(false), _suppressSizeUpdate(false),
	_regionX(params->RegionX < 0 ? 0 : params->RegionX), _regionY(params->RegionY < 0 ? 0 : params->RegionY),
	_regionW(params->Width > 0 ? params->Width : 400), _regionH(params->Height > 0 ? params->Height : 300),
	_minWidth(params->MinWidth), _minHeight(params->MinHeight),
	_resizedCallback(params->ResizedHandler), _movedCallback(params->MovedHandler), _closingCallback(params->ClosingHandler),
	_closedCallback(params->ClosedHandler), _focusCallback(params->FocusHandler)
{
	_followOwnerVisibility = params->FollowOwnerVisibility;
	if (params->Title != NULL) _title = owner->ToUTF16String(params->Title);
	if (params->Url != NULL) _url = owner->ToUTF16String(params->Url);

	DWORD style = _chromeless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
	if (!_chromeless && !_resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	if (_chromeless && _resizable) style |= WS_THICKFRAME;
	style |= WS_CLIPCHILDREN;
	DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP;
	if (_topmost) exStyle |= WS_EX_TOPMOST;
	if (_sharedFocus) exStyle |= WS_EX_NOACTIVATE;
	if (!_owned) exStyle |= WS_EX_APPWINDOW;

	HWND ownerHwnd = _owned ? owner->getHwnd() : nullptr;
	int left = params->Left;
	int top = params->Top;
	if (params->CenterOnParent || params->UseOsDefaultLocation)
	{
		RECT ownerRect{};
		GetWindowRect(owner->getHwnd(), &ownerRect);
		left = ownerRect.left + 60;
		top = ownerRect.top + 60;
	}
	double dpiScale = DpiScaleForPoint(left, top);
	double zoom = owner->GetZoomFactor();
	RECT rc{ 0, 0, (LONG)std::lround(_regionW * zoom * dpiScale), (LONG)std::lround(_regionH * zoom * dpiScale) };
	AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, (UINT)std::lround(dpiScale * 96));
	int windowWidth = rc.right - rc.left;
	int windowHeight = rc.bottom - rc.top;
	if (params->CenterOnParent)
	{
		RECT ownerRect{};
		GetWindowRect(owner->getHwnd(), &ownerRect);
		left = ownerRect.left + ((ownerRect.right - ownerRect.left) - windowWidth) / 2;
		top = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - windowHeight) / 2;
	}

	_hWnd = CreateWindowEx(exStyle, SURFACE_CLASS_NAME, _title.c_str(), style, left, top, windowWidth, windowHeight, ownerHwnd, nullptr, GetModuleHandle(nullptr), this);
	if (_hWnd == nullptr) return;

	DWORD cornerPreference = 2;
	DwmSetWindowAttribute(_hWnd, 33, &cornerPreference, sizeof(cornerPreference));

	HICON iconBig = (HICON)SendMessage(owner->getHwnd(), WM_GETICON, ICON_BIG, 0);
	HICON iconSmall = (HICON)SendMessage(owner->getHwnd(), WM_GETICON, ICON_SMALL, 0);
	if (iconBig) SendMessage(_hWnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
	if (iconSmall) SendMessage(_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

	CreateVisuals();
}

PhotinoSurface::~PhotinoSurface()
{
	try
	{
		if (_root) _root.Children().RemoveAll();
		if (_target) _target.Root(nullptr);
	}
	catch (...) {}
}

void PhotinoSurface::CreateVisuals()
{
	auto compositor = PhotinoCompositionHost::GetThreadCompositor();
	_target = PhotinoCompositionHost::CreateTarget(_hWnd, true);
	_root = compositor.CreateContainerVisual();
	_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
	_root.Clip(compositor.CreateInsetClip());
	_target.Root(_root);
	_scaler = compositor.CreateContainerVisual();
	_root.Children().InsertAtTop(_scaler);
	_redirect = compositor.CreateRedirectVisual(_owner->GetCompositionHost()->GetWebRoot());
	_scaler.Children().InsertAtTop(_redirect);
}

void PhotinoSurface::AttachHwnd(HWND hwnd)
{
	if (_hWnd == nullptr) _hWnd = hwnd;
}

bool PhotinoSurface::IsVisible() const
{
	return _hWnd != nullptr && IsWindowVisible(_hWnd) && !IsIconic(_hWnd);
}

double PhotinoSurface::GetDpiScale() const
{
	if (_hWnd == nullptr) return 1.0;
	UINT dpi = GetDpiForWindow(_hWnd);
	return dpi > 0 ? dpi / 96.0 : 1.0;
}

void PhotinoSurface::SetAutoRegionOrigin(int x, int y)
{
	_regionX = x;
	_regionY = y;
}

void PhotinoSurface::SetRegion(int x, int y, int width, int height)
{
	if (x >= 0 && y >= 0)
	{
		_autoLayout = false;
		_regionX = x;
		_regionY = y;
	}
	bool sizeChanged = false;
	if (width > 0 && width != _regionW) { _regionW = width; sizeChanged = true; }
	if (height > 0 && height != _regionH) { _regionH = height; sizeChanged = true; }
	if (sizeChanged) ResizeWindowToRegion();
	_owner->UpdateWebViewLayout();
}

void PhotinoSurface::SetAutoLayout(bool autoLayout)
{
	_autoLayout = autoLayout;
	_owner->UpdateWebViewLayout();
}

void PhotinoSurface::ApplyLayout(double rasterizationScale, double zoom, float webWidth, float webHeight)
{
	if (!_redirect) return;
	double dpiScale = GetDpiScale();
	float visualScale = (float)(dpiScale / rasterizationScale);
	_scaler.Scale({ visualScale, visualScale, 1.0f });
	_redirect.Offset({ -(float)(_regionX * rasterizationScale * zoom), -(float)(_regionY * rasterizationScale * zoom), 0.0f });
	_redirect.Size({ webWidth, webHeight });
}

POINT PhotinoSurface::ToWebViewPoint(POINT clientPoint) const
{
	double rasterizationScale = _owner->GetRasterizationScale();
	double zoom = _owner->GetZoomFactor();
	double dpiScale = GetDpiScale();
	POINT pt;
	pt.x = (LONG)std::lround(clientPoint.x * rasterizationScale / dpiScale + _regionX * rasterizationScale * zoom);
	pt.y = (LONG)std::lround(clientPoint.y * rasterizationScale / dpiScale + _regionY * rasterizationScale * zoom);
	return pt;
}

static std::wstring JsonEscape(const std::wstring &value)
{
	std::wstring out;
	for (wchar_t c : value)
	{
		if (c == L'"' || c == L'\\') { out += L'\\'; out += c; }
		else if (c == L'\n' || c == L'\r' || c == L'<') continue;
		else out += c;
	}
	return out;
}

void PhotinoSurface::AppendLayoutJson(std::wstring &json) const
{
	wchar_t buffer[256];
	swprintf(buffer, 256, L"{\"id\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"visible\":%s,\"dpiScale\":%.4f,\"url\":\"",
		_id, _regionX, _regionY, _regionW, _regionH, IsVisible() ? L"true" : L"false", GetDpiScale());
	json += buffer;
	json += JsonEscape(_url);
	json += L"\"}";
}

POINT PhotinoSurface::GetPopupDelta() const
{
	POINT delta{ 0, 0 };
	if (_hWnd == nullptr) return delta;
	POINT surfaceOrigin{ 0, 0 };
	ClientToScreen(_hWnd, &surfaceOrigin);
	POINT mainOrigin{ 0, 0 };
	ClientToScreen(_owner->getHwnd(), &mainOrigin);
	double rasterizationScale = _owner->GetRasterizationScale();
	double zoom = _owner->GetZoomFactor();
	delta.x = surfaceOrigin.x - (mainOrigin.x + (LONG)std::lround(_regionX * rasterizationScale * zoom));
	delta.y = surfaceOrigin.y - (mainOrigin.y + (LONG)std::lround(_regionY * rasterizationScale * zoom));
	return delta;
}

void PhotinoSurface::ResizeWindowToRegion()
{
	if (_hWnd == nullptr) return;
	double dpiScale = GetDpiScale();
	double zoom = _owner->GetZoomFactor();
	RECT rc{ 0, 0, (LONG)std::lround(_regionW * zoom * dpiScale), (LONG)std::lround(_regionH * zoom * dpiScale) };
	DWORD style = (DWORD)GetWindowLongPtr(_hWnd, GWL_STYLE);
	DWORD exStyle = (DWORD)GetWindowLongPtr(_hWnd, GWL_EXSTYLE);
	AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, GetDpiForWindow(_hWnd));
	_suppressSizeUpdate = true;
	SetWindowPos(_hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	_suppressSizeUpdate = false;
}

void PhotinoSurface::UpdateRegionFromClient()
{
	RECT client{};
	GetClientRect(_hWnd, &client);
	double dpiScale = GetDpiScale();
	double zoom = _owner->GetZoomFactor();
	int w = (std::max)(1, (int)std::lround(client.right / (zoom * dpiScale)));
	int h = (std::max)(1, (int)std::lround(client.bottom / (zoom * dpiScale)));
	bool changed = w != _regionW || h != _regionH;
	_regionW = w;
	_regionH = h;
	_owner->UpdateWebViewLayout();
	if (changed && _resizedCallback) _resizedCallback(_regionW, _regionH);
}

void PhotinoSurface::SetVisible(bool visible)
{
	if (_hWnd == nullptr) return;
	if (visible)
	{
		ShowWindow(_hWnd, _sharedFocus ? SW_SHOWNOACTIVATE : SW_SHOW);
		UpdateWindow(_hWnd);
	}
	else
		ShowWindow(_hWnd, SW_HIDE);
	_owner->UpdateWebViewLayout();
	_owner->ReevaluateAutoSuspend();
}

void PhotinoSurface::SetPosition(int x, int y)
{
	if (_hWnd) SetWindowPos(_hWnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void PhotinoSurface::GetPosition(int *x, int *y)
{
	RECT rect{};
	if (_hWnd) GetWindowRect(_hWnd, &rect);
	if (x) *x = rect.left;
	if (y) *y = rect.top;
}

void PhotinoSurface::SetSize(int cssWidth, int cssHeight)
{
	SetRegion(-1, -1, cssWidth, cssHeight);
}

void PhotinoSurface::GetSize(int *cssWidth, int *cssHeight)
{
	if (cssWidth) *cssWidth = _regionW;
	if (cssHeight) *cssHeight = _regionH;
}

void PhotinoSurface::SetMinSize(int cssWidth, int cssHeight)
{
	_minWidth = cssWidth;
	_minHeight = cssHeight;
}

void PhotinoSurface::SetTitle(AutoString utf8Title)
{
	if (utf8Title == NULL) return;
	_title = _owner->ToUTF16String(utf8Title);
	if (_hWnd) SetWindowText(_hWnd, _title.c_str());
}

void PhotinoSurface::SetResizable(bool resizable)
{
	_resizable = resizable;
	if (_hWnd == nullptr) return;
	LONG_PTR style = GetWindowLongPtr(_hWnd, GWL_STYLE);
	if (resizable) style |= WS_THICKFRAME | (_chromeless ? 0 : WS_MAXIMIZEBOX);
	else style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	SetWindowLongPtr(_hWnd, GWL_STYLE, style);
	SetWindowPos(_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void PhotinoSurface::SetTopmost(bool topmost)
{
	_topmost = topmost;
	if (_hWnd) SetWindowPos(_hWnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void PhotinoSurface::Center()
{
	if (_hWnd == nullptr) return;
	RECT ownerRect{}, rect{};
	GetWindowRect(_owner->getHwnd(), &ownerRect);
	GetWindowRect(_hWnd, &rect);
	int w = rect.right - rect.left;
	int h = rect.bottom - rect.top;
	SetPosition(ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2, ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2);
}

void PhotinoSurface::Activate()
{
	if (_hWnd == nullptr) return;
	if (_sharedFocus)
	{
		if (GetForegroundWindow() != _owner->getHwnd()) SetForegroundWindow(_owner->getHwnd());
		SetWindowPos(_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	else
		SetForegroundWindow(_hWnd);
}

void PhotinoSurface::Close()
{
	if (_hWnd) PostMessage(_hWnd, WM_CLOSE, 0, 0);
}

static bool IsKeyboardMessage(UINT msg)
{
	switch (msg)
	{
	case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR: case WM_UNICHAR:
	case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR: case WM_SYSDEADCHAR:
		return true;
	}
	return false;
}

LRESULT PhotinoSurface::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto host = _owner->GetCompositionHost();
	switch (msg)
	{
	case WM_CREATE:
		EnableDarkMode(_hWnd, IsDarkModeEnabled());
		if (IsDarkModeEnabled()) RefreshNonClientArea(_hWnd);
		return 0;
	case WM_SETTINGCHANGE:
		if (IsColorSchemeChange(lParam)) SendMessageW(_hWnd, WM_THEMECHANGED, 0, 0);
		return 0;
	case WM_THEMECHANGED:
		EnableDarkMode(_hWnd, IsDarkModeEnabled());
		RefreshNonClientArea(_hWnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		BeginPaint(_hWnd, &ps);
		EndPaint(_hWnd, &ps);
		return 0;
	}
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT && host && host->ApplyCursor()) return TRUE;
		break;
	case WM_MOUSEACTIVATE:
		if (_sharedFocus)
		{
			HWND ownerHwnd = _owner->getHwnd();
			if (GetForegroundWindow() != ownerHwnd) SetForegroundWindow(ownerHwnd);
			if (!_owned) SetWindowPos(_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			return MA_NOACTIVATE;
		}
		break;
	case WM_MOUSELEAVE:
		if (host) host->SendMouseLeave();
		return 0;
	case WM_ACTIVATE:
		if (_focusCallback) _focusCallback(LOWORD(wParam) != WA_INACTIVE);
		if (LOWORD(wParam) != WA_INACTIVE && !_sharedFocus)
		{
			auto cc = _owner->GetCompositionController();
			if (cc) cc->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, ToWebViewPoint(POINT{ 0, 0 }));
		}
		break;
	case WM_NCCALCSIZE:
		if (_chromeless && wParam == TRUE) return 0;
		break;
	case WM_NCHITTEST:
		if (_chromeless)
		{
			LRESULT hit = DefWindowProc(_hWnd, msg, wParam, lParam);
			if (hit != HTCLIENT || !_resizable) return hit;
			RECT rc{};
			GetWindowRect(_hWnd, &rc);
			int border = (int)std::lround(8 * GetDpiScale());
			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			bool l = x < rc.left + border, r = x >= rc.right - border, t = y < rc.top + border, b = y >= rc.bottom - border;
			if (t && l) return HTTOPLEFT;
			if (t && r) return HTTOPRIGHT;
			if (b && l) return HTBOTTOMLEFT;
			if (b && r) return HTBOTTOMRIGHT;
			if (t) return HTTOP;
			if (b) return HTBOTTOM;
			if (l) return HTLEFT;
			if (r) return HTRIGHT;
			return HTCLIENT;
		}
		break;
	case WM_GETMINMAXINFO:
	{
		MINMAXINFO *mmi = (MINMAXINFO *)lParam;
		double dpiScale = GetDpiScale();
		double zoom = _owner->GetZoomFactor();
		if (_minWidth > 0) mmi->ptMinTrackSize.x = (LONG)std::lround(_minWidth * zoom * dpiScale);
		if (_minHeight > 0) mmi->ptMinTrackSize.y = (LONG)std::lround(_minHeight * zoom * dpiScale);
		return 0;
	}
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED) return 0;
		if (!_suppressSizeUpdate) UpdateRegionFromClient();
		else _owner->UpdateWebViewLayout();
		return 0;
	case WM_MOVE:
	{
		RECT rect{};
		GetWindowRect(_hWnd, &rect);
		if (_movedCallback) _movedCallback(rect.left, rect.top);
		return 0;
	}
	case WM_DPICHANGED:
	{
		RECT *suggested = (RECT *)lParam;
		SetWindowPos(_hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
		_owner->UpdateWebViewLayout();
		return 0;
	}
	case WM_CLOSE:
		if (_closingCallback && _closingCallback()) return 0;
		DestroyWindow(_hWnd);
		return 0;
	case WM_DESTROY:
		_closed = true;
		_owner->RemoveSurface(this);
		if (_closedCallback) _closedCallback();
		return 0;
	default:
		break;
	}

	if (IsForwardedMouseMessage(msg))
	{
		if (msg == WM_MOUSEMOVE)
		{
			TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, _hWnd, 0 };
			TrackMouseEvent(&tme);
		}
		if (IsButtonDownMessage(msg) && _sharedFocus)
		{
			HWND input = _owner->GetWebViewInputHwnd();
			if (input == nullptr || GetFocus() != input) _owner->FocusWebView2();
			if (!_owned) SetWindowPos(_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		_owner->NotifyInputFromSurface(this);
		if (host) host->ForwardMouseMessage(_hWnd, msg, wParam, lParam, this);
		return 0;
	}

	if (!_sharedFocus && IsKeyboardMessage(msg))
	{
		_owner->NotifyInputFromSurface(this);
		HWND input = _owner->GetWebViewInputHwnd();
		if (input) SendMessage(input, msg, wParam, lParam);
		if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR)
			return DefWindowProc(_hWnd, msg, wParam, lParam);
		return 0;
	}

	return DefWindowProc(_hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK SurfaceWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_NCCREATE)
	{
		CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
		PhotinoSurface *created = (PhotinoSurface *)cs->lpCreateParams;
		if (created) created->AttachHwnd(hwnd);
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	PhotinoSurface *surface = (PhotinoSurface *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (surface == nullptr) return DefWindowProc(hwnd, uMsg, wParam, lParam);
	if (uMsg == WM_NCDESTROY)
	{
		SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
		delete surface;
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
	return surface->HandleMessage(uMsg, wParam, lParam);
}
#endif

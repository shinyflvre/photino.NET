#pragma once
#ifdef _WIN32
#include "Photino.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.System.h>
#include <ole2.h>
#include <map>
#include <string>

class PhotinoDropTarget;

class PhotinoCompositionHost
{
public:
	PhotinoCompositionHost(Photino *owner, HWND hwnd);
	~PhotinoCompositionHost();

	bool Initialize();
	IUnknown *GetRootVisualTarget();
	void SetWebViewSize(float width, float height);
	void SetBackgroundColor(COREWEBVIEW2_COLOR color);
	void SetContentAttached(bool attached);
	bool IsContentAttached() const { return _attached; }
	winrt::Windows::UI::Composition::Compositor GetCompositor() { return _compositor; }
	winrt::Windows::UI::Composition::ContainerVisual GetWebRoot() { return _webRoot; }
	void RegisterDropTarget(HWND hwnd, PhotinoSurface *surface);
	void RevokeDropTarget(HWND hwnd);
	void OnCursorChanged();
	bool ApplyCursor();
	void ForwardMouseMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, PhotinoSurface *surface);
	void SendMouseLeave();
	void EnsurePopupHook(UINT32 browserProcessId);

	static bool EnsureThreadCompositor();
	static winrt::Windows::UI::Composition::Compositor GetThreadCompositor();
	static winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget CreateTarget(HWND hwnd, bool topmost);

private:
	Photino *_owner;
	HWND _hWnd;
	bool _attached;
	HWINEVENTHOOK _popupHook;
	winrt::Windows::UI::Composition::Compositor _compositor{ nullptr };
	winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget _target{ nullptr };
	winrt::Windows::UI::Composition::ContainerVisual _root{ nullptr };
	winrt::Windows::UI::Composition::ContainerVisual _webRoot{ nullptr };
	winrt::Windows::UI::Composition::SpriteVisual _background{ nullptr };
	std::map<HWND, PhotinoDropTarget *> _dropTargets;
};

class PhotinoSurface
{
public:
	PhotinoSurface(Photino *owner, PhotinoSurfaceParams *params);
	~PhotinoSurface();

	static void RegisterWindowClass(HINSTANCE hInstance);

	void AttachHwnd(HWND hwnd);
	HWND GetHwnd() const { return _hWnd; }
	int GetId() const { return _id; }
	Photino *GetOwner() const { return _owner; }
	bool IsAutoLayout() const { return _autoLayout; }
	bool IsOwned() const { return _owned; }
	bool IsSharedFocus() const { return _sharedFocus; }
	bool FollowsOwnerVisibility() const { return _followOwnerVisibility; }
	bool IsVisible() const;
	bool IsClosed() const { return _closed; }
	const std::wstring &GetUrl() const { return _url; }

	int RegionX() const { return _regionX; }
	int RegionY() const { return _regionY; }
	int RegionWidth() const { return _regionW; }
	int RegionHeight() const { return _regionH; }
	void SetAutoRegionOrigin(int x, int y);
	int SlotX() const { return _slotX; }
	int SlotY() const { return _slotY; }
	int SlotWidth() const { return _slotW; }
	int SlotHeight() const { return _slotH; }
	void SetSlot(int x, int y, int width, int height) { _slotX = x; _slotY = y; _slotW = width; _slotH = height; }
	void SetRegion(int x, int y, int width, int height);
	void SyncRegionToWindow();
	void SetBackgroundColor(COREWEBVIEW2_COLOR color);
	void SetAutoLayout(bool autoLayout);
	void ApplyLayout(double rasterizationScale, double zoom, float webWidth, float webHeight);

	void SetVisible(bool visible);
	void SetPosition(int x, int y);
	void GetPosition(int *x, int *y);
	void SetSize(int cssWidth, int cssHeight);
	void GetSize(int *cssWidth, int *cssHeight);
	void SetMinSize(int cssWidth, int cssHeight);
	void SetTitle(AutoString utf8Title);
	void SetResizable(bool resizable);
	void SetTopmost(bool topmost);
	void Center();
	void Activate();
	void Close();
	double GetDpiScale() const;
	POINT ToWebViewPoint(POINT clientPoint) const;
	POINT GetPopupDelta() const;
	void AppendLayoutJson(std::wstring &json) const;

	LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
	void CreateVisuals();
	void ResizeWindowToRegion();
	void UpdateRegionFromClient();

	Photino *_owner;
	HWND _hWnd;
	int _id;
	bool _owned;
	bool _sharedFocus;
	bool _chromeless;
	bool _resizable;
	bool _topmost;
	bool _autoLayout;
	bool _followOwnerVisibility;
	bool _closed;
	bool _suppressSizeUpdate;
	int _regionX;
	int _regionY;
	int _regionW;
	int _regionH;
	int _slotX = -1;
	int _slotY = 0;
	int _slotW = 0;
	int _slotH = 0;
	int _minWidth;
	int _minHeight;
	std::wstring _title;
	std::wstring _url;

	SurfaceResizedCallback _resizedCallback;
	SurfaceMovedCallback _movedCallback;
	SurfaceClosingCallback _closingCallback;
	SurfaceClosedCallback _closedCallback;
	SurfaceFocusCallback _focusCallback;

	winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget _target{ nullptr };
	winrt::Windows::UI::Composition::ContainerVisual _root{ nullptr };
	winrt::Windows::UI::Composition::ContainerVisual _scaler{ nullptr };
	winrt::Windows::UI::Composition::RedirectVisual _redirect{ nullptr };
	winrt::Windows::UI::Composition::SpriteVisual _background{ nullptr };
};
#endif

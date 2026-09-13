#ifdef _WIN32
#include "Photino.Windows.Composition.h"
#include <wrl.h>
#include <algorithm>

using namespace Microsoft::WRL;

void Photino::CallDevToolsProtocolMethod(const wchar_t *method, const wchar_t *parametersJson)
{
	if (!_webviewWindow) return;
	_webviewWindow->CallDevToolsProtocolMethod(method, parametersJson,
		Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
}

void Photino::SuspendWebViewCore()
{
	wil::com_ptr<ICoreWebView2_3> webview3;
	if (!_webviewWindow || FAILED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview3)))) return;
	unsigned int generation = ++_suspendGeneration;
	webview3->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
		[this, generation](HRESULT, BOOL isSuccessful) -> HRESULT {
			if (generation != _suspendGeneration) return S_OK;
			_webviewSuspended = isSuccessful == TRUE;
			return S_OK;
		}).Get());
}

void Photino::SuspendResources(unsigned int mask)
{
	mask &= PHOTINO_RES_ALL;
	if (mask == 0) return;
	if (!_webviewController || !_webviewWindow)
	{
		_pendingSuspendMask |= mask;
		return;
	}

	unsigned int toApply = mask & ~_suspendedResources;
	if (toApply == 0) return;
	bool webviewRequested = (toApply & PHOTINO_RES_WEBVIEW) != 0;

	if (toApply & PHOTINO_RES_RENDERING)
		_webviewController->put_IsVisible(FALSE);

	if (toApply & PHOTINO_RES_AUDIO)
	{
		wil::com_ptr<ICoreWebView2_8> webview8;
		if (SUCCEEDED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview8))))
		{
			BOOL muted = FALSE;
			webview8->get_IsMuted(&muted);
			_audioWasMuted = muted == TRUE;
			webview8->put_IsMuted(TRUE);
		}
	}

	if (toApply & PHOTINO_RES_NETWORK)
	{
		CallDevToolsProtocolMethod(L"Network.enable", L"{\"maxTotalBufferSize\":0,\"maxResourceBufferSize\":0,\"maxPostDataSize\":0}");
		CallDevToolsProtocolMethod(L"Network.emulateNetworkConditions", L"{\"offline\":true,\"latency\":0,\"downloadThroughput\":-1,\"uploadThroughput\":-1}");
	}

	if ((toApply & PHOTINO_RES_JAVASCRIPT) && !webviewRequested)
		CallDevToolsProtocolMethod(L"Page.setWebLifecycleState", L"{\"state\":\"frozen\"}");

	if (toApply & PHOTINO_RES_GPU)
	{
		wil::com_ptr<ICoreWebView2_19> webview19;
		if (SUCCEEDED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview19))))
			webview19->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
		if (_composition) _composition->SetContentAttached(false);
	}

	if (webviewRequested)
	{
		_webviewController->put_IsVisible(FALSE);
		SuspendWebViewCore();
	}

	_suspendedResources |= toApply;
}

void Photino::ResumeResources(unsigned int mask)
{
	mask &= PHOTINO_RES_ALL;
	_pendingSuspendMask &= ~mask;
	if (!_webviewController || !_webviewWindow) return;

	unsigned int toApply = mask & _suspendedResources;
	if (toApply == 0) return;

	bool webviewSuspended = (_suspendedResources & PHOTINO_RES_WEBVIEW) != 0;
	if (webviewSuspended && (toApply & (PHOTINO_RES_JAVASCRIPT | PHOTINO_RES_RENDERING | PHOTINO_RES_GPU | PHOTINO_RES_WEBVIEW)))
	{
		++_suspendGeneration;
		wil::com_ptr<ICoreWebView2_3> webview3;
		if (SUCCEEDED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview3))))
			webview3->Resume();
		_webviewSuspended = false;
		toApply |= PHOTINO_RES_WEBVIEW;
	}

	if (toApply & PHOTINO_RES_GPU)
	{
		if (_composition) _composition->SetContentAttached(true);
		wil::com_ptr<ICoreWebView2_19> webview19;
		if (SUCCEEDED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview19))))
			webview19->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL);
	}

	if ((toApply & PHOTINO_RES_JAVASCRIPT) && !(toApply & PHOTINO_RES_WEBVIEW))
		CallDevToolsProtocolMethod(L"Page.setWebLifecycleState", L"{\"state\":\"active\"}");

	if (toApply & PHOTINO_RES_NETWORK)
	{
		CallDevToolsProtocolMethod(L"Network.emulateNetworkConditions", L"{\"offline\":false,\"latency\":0,\"downloadThroughput\":-1,\"uploadThroughput\":-1}");
		CallDevToolsProtocolMethod(L"Network.disable", L"{}");
	}

	if (toApply & PHOTINO_RES_AUDIO)
	{
		wil::com_ptr<ICoreWebView2_8> webview8;
		if (SUCCEEDED(_webviewWindow->QueryInterface(IID_PPV_ARGS(&webview8))))
			webview8->put_IsMuted(_audioWasMuted ? TRUE : FALSE);
	}

	_suspendedResources &= ~toApply;

	bool renderingStillSuspended = (_suspendedResources & (PHOTINO_RES_RENDERING | PHOTINO_RES_WEBVIEW)) != 0;
	if (toApply & (PHOTINO_RES_RENDERING | PHOTINO_RES_WEBVIEW))
	{
		if (!renderingStillSuspended)
		{
			_webviewController->put_IsVisible(TRUE);
			UpdateWebViewLayout();
		}
	}
	else if ((toApply & PHOTINO_RES_JAVASCRIPT) && !renderingStillSuspended)
	{
		_webviewController->put_IsVisible(FALSE);
		_webviewController->put_IsVisible(TRUE);
		UpdateWebViewLayout();
	}
}

void Photino::SetAutoSuspendOnMinimize(unsigned int mask)
{
	_autoSuspendMask = mask & PHOTINO_RES_ALL;
	if (_autoSuspendMask == 0 && _autoSuspendedResources != 0)
	{
		ResumeResources(_autoSuspendedResources);
		_autoSuspendedResources = 0;
	}
}

void Photino::OnHostWindowHidden()
{
	if (_autoSuspendMask == 0 || _autoSuspendedResources != 0) return;
	for (auto s : _surfaces)
		if (!s->IsClosed() && s->IsVisible()) return;
	_autoSuspendedResources = _autoSuspendMask & ~_suspendedResources;
	if (_autoSuspendedResources == 0) return;
	SuspendResources(_autoSuspendedResources);
}

void Photino::OnHostWindowShown()
{
	if (_autoSuspendedResources == 0) return;
	unsigned int toResume = _autoSuspendedResources;
	_autoSuspendedResources = 0;
	ResumeResources(toResume);
}

void Photino::ReevaluateAutoSuspend()
{
	bool anythingVisible = !_hostHidden;
	if (!anythingVisible)
		for (auto s : _surfaces)
			if (!s->IsClosed() && s->IsVisible()) { anythingVisible = true; break; }
	if (anythingVisible)
		OnHostWindowShown();
	else
		OnHostWindowHidden();
}

void Photino::OnHostVisibilityChanged(bool visible)
{
	_hostHidden = !visible;
	if (!visible)
	{
		_surfacesHiddenWithHost.clear();
		for (auto s : _surfaces)
		{
			if (s->IsClosed() || s->IsOwned() || !s->FollowsOwnerVisibility() || !s->IsVisible()) continue;
			_surfacesHiddenWithHost.push_back(s);
			s->SetVisible(false);
		}
	}
	else
	{
		std::vector<PhotinoSurface *> toShow = _surfacesHiddenWithHost;
		_surfacesHiddenWithHost.clear();
		for (auto s : toShow)
			if (std::find(_surfaces.begin(), _surfaces.end(), s) != _surfaces.end() && !s->IsClosed())
				s->SetVisible(true);
	}
	ReevaluateAutoSuspend();
}
#endif

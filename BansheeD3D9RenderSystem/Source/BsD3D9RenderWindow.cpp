#include "BsD3D9RenderWindow.h"
#include "BsInput.h"
#include "BsCoreThread.h"
#include "BsViewport.h"
#include "BsException.h"
#include "BsD3D9RenderSystem.h"
#include "BsRenderSystem.h"
#include "BsBitwise.h"
#include "Win32/BsPlatformWndProc.h"
#include "BsD3D9VideoModeInfo.h"
#include "BsD3D9DeviceManager.h"

namespace BansheeEngine
{
	void D3D9RenderWindowProperties::copyToBuffer(UINT8* buffer) const
	{
		*(D3D9RenderWindowProperties*)buffer = *this;
	}

	void D3D9RenderWindowProperties::copyFromBuffer(UINT8* buffer)
	{
		*this = *(D3D9RenderWindowProperties*)buffer;
	}

	UINT32 D3D9RenderWindowProperties::getSize() const
	{
		return sizeof(D3D9RenderWindowProperties);
	}

	D3D9RenderWindowCore::D3D9RenderWindowCore(D3D9RenderWindow* parent, RenderWindowProperties* properties, const RENDER_WINDOW_DESC& desc, HINSTANCE instance)
		: RenderWindowCore(parent, properties), mInstance(instance), mIsDepthBuffered(true), mIsChild(false), mDesc(desc),
		mStyle(0), mWindowedStyle(0), mWindowedStyleEx(0), mDevice(nullptr), mIsExternal(false), mDisplayFrequency(0), mDeviceValid(false)
	{ }

	D3D9RenderWindowCore::~D3D9RenderWindowCore()
	{ }

	void D3D9RenderWindowCore::initialize()
	{
		RenderWindowCore::initialize();

		HINSTANCE hInst = mInstance;

		mMultisampleType = D3DMULTISAMPLE_NONE;
		mMultisampleQuality = 0;
		mDisplayFrequency = Math::roundToInt(mDesc.videoMode.getRefreshRate());

		HWND parentHWnd = 0;
		HWND externalHandle = 0;

		NameValuePairList::const_iterator opt;
		opt = mDesc.platformSpecific.find("parentWindowHandle");
		if (opt != mDesc.platformSpecific.end())
			parentHWnd = (HWND)parseUnsignedInt(opt->second);

		opt = mDesc.platformSpecific.find("externalWindowHandle");
		if (opt != mDesc.platformSpecific.end())
			externalHandle = (HWND)parseUnsignedInt(opt->second);

		mIsChild = parentHWnd != 0;

		mWindowedStyle = WS_VISIBLE | WS_CLIPCHILDREN;
		mWindowedStyleEx = 0;

		if (!mDesc.fullscreen || mIsChild)
		{
			if (parentHWnd)
			{
				if (mDesc.toolWindow)
					mWindowedStyleEx = WS_EX_TOOLWINDOW;
				else
					mWindowedStyle |= WS_CHILD;
			}

			if (!parentHWnd || mDesc.toolWindow)
			{
				if (mDesc.border == WindowBorder::None)
					mWindowedStyle |= WS_POPUP;
				else if (mDesc.border == WindowBorder::Fixed)
					mWindowedStyle |= WS_OVERLAPPED | WS_BORDER | WS_CAPTION |
					WS_SYSMENU | WS_MINIMIZEBOX;
				else
					mWindowedStyle |= WS_OVERLAPPEDWINDOW;
			}
		}

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		if (!externalHandle)
		{
			MONITORINFO monitorInfo;
			RECT rc;

			HMONITOR hMonitor = NULL;
			const D3D9VideoModeInfo& videoModeInfo = static_cast<const D3D9VideoModeInfo&>(RenderSystem::instance().getVideoModeInfo());
			UINT32 numOutputs = videoModeInfo.getNumOutputs();
			if (numOutputs > 0)
			{
				UINT32 actualMonitorIdx = std::min(mDesc.videoMode.getOutputIdx(), numOutputs - 1);
				const D3D9VideoOutputInfo& outputInfo = static_cast<const D3D9VideoOutputInfo&>(videoModeInfo.getOutputInfo(actualMonitorIdx));
				hMonitor = outputInfo.getMonitorHandle();
			}

			// If we didn't specified the adapter index, or if it didn't find it
			if (hMonitor == NULL)
			{
				POINT windowAnchorPoint;

				// Fill in anchor point.
				windowAnchorPoint.x = mDesc.left;
				windowAnchorPoint.y = mDesc.top;

				// Get the nearest monitor to this window.
				hMonitor = MonitorFromPoint(windowAnchorPoint, MONITOR_DEFAULTTOPRIMARY);
			}

			// Get the target monitor info		
			memset(&monitorInfo, 0, sizeof(MONITORINFO));
			monitorInfo.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(hMonitor, &monitorInfo);

			unsigned int winWidth, winHeight;
			winWidth = mDesc.videoMode.getWidth();
			winHeight = mDesc.videoMode.getHeight();

			UINT32 left = mDesc.left;
			UINT32 top = mDesc.top;

			// No specified top left -> Center the window in the middle of the monitor
			if (left == -1 || top == -1)
			{
				int screenw = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
				int screenh = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

				// clamp window dimensions to screen size
				int outerw = (int(winWidth) < screenw) ? int(winWidth) : screenw;
				int outerh = (int(winHeight) < screenh) ? int(winHeight) : screenh;

				if (left == -1)
					left = monitorInfo.rcWork.left + (screenw - outerw) / 2;
				else if (hMonitor != NULL)
					left += monitorInfo.rcWork.left;

				if (top == -1)
					top = monitorInfo.rcWork.top + (screenh - outerh) / 2;
				else if (hMonitor != NULL)
					top += monitorInfo.rcWork.top;
			}
			else if (hMonitor != NULL)
			{
				left += monitorInfo.rcWork.left;
				top += monitorInfo.rcWork.top;
			}

			props->mWidth = mDesc.videoMode.getWidth();
			props->mHeight = mDesc.videoMode.getHeight();
			props->mTop = top;
			props->mLeft = left;

			DWORD dwStyle = 0;
			DWORD dwStyleEx = 0;
			if (mDesc.fullscreen && !mIsChild)
			{
				dwStyle = WS_VISIBLE | WS_CLIPCHILDREN | WS_POPUP;
				props->mTop = monitorInfo.rcMonitor.top;
				props->mLeft = monitorInfo.rcMonitor.left;
			}
			else
			{
				dwStyle = mWindowedStyle;
				dwStyleEx = mWindowedStyleEx;

				getAdjustedWindowSize(mDesc.videoMode.getWidth(), mDesc.videoMode.getHeight(), dwStyle, &winWidth, &winHeight);

				if (!mDesc.outerDimensions)
				{
					// Calculate window dimensions required
					// to get the requested client area
					SetRect(&rc, 0, 0, props->mWidth, props->mHeight);
					AdjustWindowRect(&rc, dwStyle, false);
					props->mWidth = rc.right - rc.left;
					props->mHeight = rc.bottom - rc.top;

					// Clamp window rect to the nearest display monitor.
					if (props->mLeft < monitorInfo.rcWork.left)
						props->mLeft = monitorInfo.rcWork.left;

					if (props->mTop < monitorInfo.rcWork.top)
						props->mTop = monitorInfo.rcWork.top;

					if (static_cast<int>(winWidth) > monitorInfo.rcWork.right - props->mLeft)
						winWidth = monitorInfo.rcWork.right - props->mLeft;

					if (static_cast<int>(winHeight) > monitorInfo.rcWork.bottom - props->mTop)
						winHeight = monitorInfo.rcWork.bottom - props->mTop;
				}
			}


			// Register the window class
			WNDCLASS wc = { 0, PlatformWndProc::_win32WndProc, 0, 0, hInst,
				LoadIcon(0, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW),
				(HBRUSH)GetStockObject(BLACK_BRUSH), 0, "D3D9Wnd" };
			RegisterClass(&wc);

			// Create our main window
			// Pass pointer to self
			mIsExternal = false;
			props->mHWnd = CreateWindowEx(dwStyleEx, "D3D9Wnd", mDesc.title.c_str(), dwStyle,
				props->mLeft, props->mTop, winWidth, winHeight, parentHWnd, 0, hInst, this);
			mStyle = dwStyle;
		}
		else
		{
			props->mHWnd = externalHandle;
			mIsExternal = true;
		}

		RECT rc;
		GetWindowRect(props->mHWnd, &rc);
		props->mTop = rc.top;
		props->mLeft = rc.left;

		GetClientRect(props->mHWnd, &rc);
		props->mWidth = rc.right;
		props->mHeight = rc.bottom;

		mIsDepthBuffered = mDesc.depthBuffer;
		props->mIsFullScreen = mDesc.fullscreen && !mIsChild;
		props->mColorDepth = 32;
		props->mActive = true;

		D3D9RenderSystem* rs = static_cast<D3D9RenderSystem*>(RenderSystem::instancePtr());
		rs->registerWindow(*this);

		// Sync HWnd to CoreObject immediately
		D3D9RenderWindow* parent = static_cast<D3D9RenderWindow*>(mParent);
		D3D9RenderWindowProperties* parentProps = static_cast<D3D9RenderWindowProperties*>(parent->mProperties);
		parentProps->mHWnd = props->mHWnd;
	}

	void D3D9RenderWindowCore::destroy()
	{
		if (mDevice != nullptr)
		{
			mDevice->detachRenderWindow(this);
			mDevice = nullptr;
		}

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		if (props->mHWnd && !mIsExternal)
		{
			DestroyWindow(props->mHWnd);
		}

		props->mHWnd = 0;
		props->mActive = false;

		markCoreDirty();

		RenderWindowCore::destroy();
	}

	void D3D9RenderWindowCore::setFullscreen(UINT32 width, UINT32 height, float refreshRate, UINT32 monitorIdx)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mIsChild)
			return;

		const D3D9VideoModeInfo& videoModeInfo = static_cast<const D3D9VideoModeInfo&>(RenderSystem::instance().getVideoModeInfo());
		UINT32 numOutputs = videoModeInfo.getNumOutputs();
		if (numOutputs == 0)
			return;

		UINT32 actualMonitorIdx = std::min(monitorIdx, numOutputs - 1);
		const D3D9VideoOutputInfo& outputInfo = static_cast<const D3D9VideoOutputInfo&>(videoModeInfo.getOutputInfo(actualMonitorIdx));

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		bool oldFullscreen = props->mIsFullScreen;

		props->mWidth = width;
		props->mHeight = height;
		mDisplayFrequency = Math::roundToInt(refreshRate);

		props->mIsFullScreen = true;

		HMONITOR hMonitor = outputInfo.getMonitorHandle();
		MONITORINFO monitorInfo;

		memset(&monitorInfo, 0, sizeof(MONITORINFO));
		monitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMonitor, &monitorInfo);

		props->mTop = monitorInfo.rcMonitor.top;
		props->mLeft = monitorInfo.rcMonitor.left;

		// Invalidate device, which resets it
		mDevice->invalidate(this);
		mDevice->acquire();

		markCoreDirty();
	}

	void D3D9RenderWindowCore::setFullscreen(const VideoMode& mode)
	{
		THROW_IF_NOT_CORE_THREAD;

		setFullscreen(mode.getWidth(), mode.getHeight(), mode.getRefreshRate(), mode.getOutputIdx());
	}

	void D3D9RenderWindowCore::setWindowed(UINT32 width, UINT32 height)
	{
		THROW_IF_NOT_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		if (!props->mIsFullScreen)
			return;

		props->mIsFullScreen = false;
		mStyle = mWindowedStyle;
		props->mWidth = width;
		props->mHeight = height;

		unsigned int winWidth, winHeight;
		getAdjustedWindowSize(props->mWidth, props->mHeight, mStyle, &winWidth, &winHeight);

		// Deal with centering when switching down to smaller resolution
		HMONITOR hMonitor = MonitorFromWindow(props->mHWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo;
		memset(&monitorInfo, 0, sizeof(MONITORINFO));
		monitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMonitor, &monitorInfo);

		LONG screenw = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
		LONG screenh = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

		int left = screenw > int(winWidth) ? ((screenw - int(winWidth)) / 2) : 0;
		int top = screenh > int(winHeight) ? ((screenh - int(winHeight)) / 2) : 0;

		SetWindowLong(props->mHWnd, GWL_STYLE, mStyle);
		SetWindowLong(props->mHWnd, GWL_EXSTYLE, mWindowedStyleEx);
		SetWindowPos(props->mHWnd, HWND_NOTOPMOST, left, top, winWidth, winHeight,
			SWP_DRAWFRAME | SWP_FRAMECHANGED | SWP_NOACTIVATE);

		mDevice->invalidate(this);
		mDevice->acquire();

		markCoreDirty();
	}

	void D3D9RenderWindowCore::setHidden(bool hidden)
	{
		THROW_IF_NOT_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		props->mHidden = hidden;
		if (!mIsExternal)
		{
			if (hidden)
				ShowWindow(props->mHWnd, SW_HIDE);
			else
				ShowWindow(props->mHWnd, SW_SHOWNORMAL);
		}

		markCoreDirty();
	}

	void D3D9RenderWindowCore::move(INT32 top, INT32 left)
	{
		THROW_IF_NOT_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		if (props->mHWnd && !props->mIsFullScreen)
		{
			props->mLeft = left;
			props->mTop = top;

			SetWindowPos(props->mHWnd, 0, top, left, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			markCoreDirty();
		}
	}

	void D3D9RenderWindowCore::resize(UINT32 width, UINT32 height)
	{
		THROW_IF_NOT_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		if (props->mHWnd && !props->mIsFullScreen)
		{
			props->mWidth = width;
			props->mHeight = height;

			unsigned int winWidth, winHeight;
			getAdjustedWindowSize(width, height, mStyle, &winWidth, &winHeight);

			SetWindowPos(props->mHWnd, 0, 0, 0, winWidth, winHeight,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

			markCoreDirty();
		}
	}

	void D3D9RenderWindowCore::getCustomAttribute(const String& name, void* pData) const
	{
		// Valid attributes and their equivalent native functions:
		// D3DDEVICE			: getD3DDevice
		// WINDOW				: getWindowHandle

		if( name == "D3DDEVICE" )
		{
			IDirect3DDevice9* *pDev = (IDirect3DDevice9**)pData;
			*pDev = _getD3D9Device();
			return;
		}		
		else if( name == "WINDOW" )
		{
			HWND *pHwnd = (HWND*)pData;
			*pHwnd = _getWindowHandle();
			return;
		}
		else if( name == "isTexture" )
		{
			bool *b = reinterpret_cast< bool * >( pData );
			*b = false;

			return;
		}
		else if( name == "D3DZBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getDepthBuffer(this);
			return;
		}
		else if( name == "DDBACKBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getBackBuffer(this);
			return;
		}
		else if( name == "DDFRONTBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getBackBuffer(this);
			return;
		}
	}

	void D3D9RenderWindowCore::swapBuffers()
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mDeviceValid)
			mDevice->present(this);		
	}

	void D3D9RenderWindowCore::copyToMemory(PixelData &dst, FrameBuffer buffer)
	{
		THROW_IF_NOT_CORE_THREAD;

		mDevice->copyContentsToMemory(this, dst, buffer);
	}

	void D3D9RenderWindowCore::_windowMovedOrResized()
	{
		THROW_IF_NOT_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		if (!props->mHWnd || IsIconic(props->mHWnd))
			return;
	
		updateWindowRect();

		RenderWindowCore::_windowMovedOrResized();
	}

	/************************************************************************/
	/* 						D3D9 IMPLEMENTATION SPECIFIC                    */
	/************************************************************************/

	void D3D9RenderWindowCore::getAdjustedWindowSize(UINT32 clientWidth, UINT32 clientHeight,
		DWORD style, UINT32* winWidth, UINT32* winHeight)
	{
		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		RECT rc;
		SetRect(&rc, 0, 0, clientWidth, clientHeight);
		AdjustWindowRect(&rc, style, false);
		*winWidth = rc.right - rc.left;
		*winHeight = rc.bottom - rc.top;

		HMONITOR hMonitor = MonitorFromWindow(props->mHWnd, MONITOR_DEFAULTTONEAREST);

		MONITORINFO monitorInfo;

		memset(&monitorInfo, 0, sizeof(MONITORINFO));
		monitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMonitor, &monitorInfo);

		LONG maxW = monitorInfo.rcWork.right  - monitorInfo.rcWork.left;
		LONG maxH = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

		if (*winWidth > (unsigned int)maxW)
			*winWidth = maxW;
		if (*winHeight > (unsigned int)maxH)
			*winHeight = maxH;
	}
	
	void D3D9RenderWindowCore::_buildPresentParameters(D3DPRESENT_PARAMETERS* presentParams) const
	{	
		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		IDirect3D9* pD3D = D3D9RenderSystem::getDirect3D9();
		D3DDEVTYPE devType = D3DDEVTYPE_HAL;

		if (mDevice != NULL)		
			devType = mDevice->getDeviceType();		
	
		ZeroMemory( presentParams, sizeof(D3DPRESENT_PARAMETERS) );
		presentParams->Windowed = !props->mIsFullScreen;
		presentParams->SwapEffect = D3DSWAPEFFECT_DISCARD;
		presentParams->BackBufferCount = 1;
		presentParams->EnableAutoDepthStencil = mIsDepthBuffered;
		presentParams->hDeviceWindow = props->mHWnd;
		presentParams->BackBufferWidth = props->mWidth;
		presentParams->BackBufferHeight = props->mHeight;
		presentParams->FullScreen_RefreshRateInHz = props->mIsFullScreen ? mDisplayFrequency : 0;
		
		if (presentParams->BackBufferWidth == 0)		
			presentParams->BackBufferWidth = 1;					

		if (presentParams->BackBufferHeight == 0)	
			presentParams->BackBufferHeight = 1;					

		if (getProperties().getVSync())
		{
			if (getProperties().isFullScreen())
			{
				switch(mVSyncInterval)
				{
				case 1:
				default:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
					break;
				case 2:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_TWO;
					break;
				case 3:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_THREE;
					break;
				case 4:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_FOUR;
					break;
				};

				D3DCAPS9 caps;
				pD3D->GetDeviceCaps(mDevice->getAdapterNumber(), devType, &caps);
				if (!(caps.PresentationIntervals & presentParams->PresentationInterval))
				{
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
				}

			}
			else
			{
				presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			}
		}
		else
		{
			presentParams->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		}

		presentParams->BackBufferFormat = D3DFMT_X8R8G8B8;

		if (FAILED(pD3D->CheckDeviceFormat(mDevice->getAdapterNumber(),
			devType, presentParams->BackBufferFormat, D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_SURFACE, D3DFMT_D24S8)))
		{
			if (FAILED(pD3D->CheckDeviceFormat(mDevice->getAdapterNumber(),
				devType, presentParams->BackBufferFormat, D3DUSAGE_DEPTHSTENCIL,
				D3DRTYPE_SURFACE, D3DFMT_D32)))
			{
				presentParams->AutoDepthStencilFormat = D3DFMT_D16;
			}
			else
			{
				presentParams->AutoDepthStencilFormat = D3DFMT_D32;
			}
		}
		else
		{
			if (SUCCEEDED(pD3D->CheckDepthStencilMatch(mDevice->getAdapterNumber(), devType,
				presentParams->BackBufferFormat, presentParams->BackBufferFormat, D3DFMT_D24S8)))
			{
				presentParams->AutoDepthStencilFormat = D3DFMT_D24S8;
			}
			else
			{
				presentParams->AutoDepthStencilFormat = D3DFMT_D24X8;
			}
		}

		D3D9RenderSystem* rs = static_cast<D3D9RenderSystem*>(BansheeEngine::RenderSystem::instancePtr());

		D3DMULTISAMPLE_TYPE multisampleType;
		DWORD multisampleQuality;

		rs->determineMultisampleSettings(mDevice->getD3D9Device(), getProperties().getMultisampleCount(), 
			presentParams->BackBufferFormat, getProperties().isFullScreen(), &multisampleType, &multisampleQuality);

		presentParams->MultiSampleType = multisampleType;
		presentParams->MultiSampleQuality = (multisampleQuality == 0) ? 0 : multisampleQuality;
	}

	HWND D3D9RenderWindowCore::_getWindowHandle() const
	{
		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		return props->getHWnd();
	}

	IDirect3DDevice9* D3D9RenderWindowCore::_getD3D9Device() const
	{
		return mDevice->getD3D9Device();
	}

	IDirect3DSurface9* D3D9RenderWindowCore::_getRenderSurface() const
	{
		return mDevice->getBackBuffer(this);
	}

	D3D9Device* D3D9RenderWindowCore::_getDevice() const
	{
		return mDevice;
	}

	void D3D9RenderWindowCore::_setDevice(D3D9Device* device)
	{
		mDevice = device;
		mDeviceValid = false;
	}

	bool D3D9RenderWindowCore::_isDepthBuffered() const
	{
		return mIsDepthBuffered;
	}

	void D3D9RenderWindowCore::updateWindowRect()
	{
		RECT rc;
		BOOL result;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);

		// Update top left parameters
		result = GetWindowRect(_getWindowHandle(), &rc);
		if (result == FALSE)
		{
			props->mTop = 0;
			props->mLeft = 0;
			props->mWidth = 0;
			props->mHeight = 0;

			markCoreDirty();
			return;
		}
		
		props->mTop = rc.top;
		props->mLeft = rc.left;

		// Width and height represent drawable area only
		result = GetClientRect(_getWindowHandle(), &rc);
		if (result == FALSE)
		{
			props->mTop = 0;
			props->mLeft = 0;
			props->mWidth = 0;
			props->mHeight = 0;

			markCoreDirty();
			return;
		}

		props->mWidth = rc.right - rc.left;
		props->mHeight = rc.bottom - rc.top;

		markCoreDirty();
	}

	bool D3D9RenderWindowCore::_validateDevice()
	{
		mDeviceValid = mDevice->validate(this);
		return mDeviceValid;
	}

	D3D9RenderWindow::D3D9RenderWindow(HINSTANCE instance)
		:mInstance(instance)
	{

	}

	void D3D9RenderWindow::getCustomAttribute(const String& name, void* pData) const
	{
		THROW_IF_CORE_THREAD;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		if (name == "WINDOW")
		{
			HWND *pWnd = (HWND*)pData;
			*pWnd = props->getHWnd();
			return;
		}

		RenderWindow::getCustomAttribute(name, pData);
	}

	Vector2I D3D9RenderWindow::screenToWindowPos(const Vector2I& screenPos) const
	{
		POINT pos;
		pos.x = screenPos.x;
		pos.y = screenPos.y;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		ScreenToClient(props->getHWnd(), &pos);
		return Vector2I(pos.x, pos.y);
	}

	Vector2I D3D9RenderWindow::windowToScreenPos(const Vector2I& windowPos) const
	{
		POINT pos;
		pos.x = windowPos.x;
		pos.y = windowPos.y;

		D3D9RenderWindowProperties* props = static_cast<D3D9RenderWindowProperties*>(mProperties);
		ClientToScreen(props->getHWnd(), &pos);
		return Vector2I(pos.x, pos.y);
	}

	D3D9RenderWindowCore* D3D9RenderWindow::getCore() const
	{
		return static_cast<D3D9RenderWindowCore*>(mCoreSpecific);
	}

	RenderTargetProperties* D3D9RenderWindow::createProperties() const
	{
		return bs_new<D3D9RenderWindowProperties>();
	}

	CoreObjectCore* D3D9RenderWindow::createCore() const
	{
		D3D9RenderWindowProperties* coreProperties = bs_new<D3D9RenderWindowProperties>();
		D3D9RenderWindowProperties* myProperties = static_cast<D3D9RenderWindowProperties*>(mProperties);

		*coreProperties = *myProperties;
		return bs_new<D3D9RenderWindowCore>(const_cast<D3D9RenderWindow*>(this), coreProperties, mDesc, mInstance);
	}
}

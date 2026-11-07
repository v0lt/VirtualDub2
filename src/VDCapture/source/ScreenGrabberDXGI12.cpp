// VirtualDub - Video processing and capture application
// A/V interface library
//
// Copyright (C) 2013 Avery Lee
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>
#define INITGUID

#include <math.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/system/thunk.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/resample_kernels.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vd2/VDCapture/ScreenGrabberDXGI12.h>

#include <d3d11_downscale_shader.inl>
#include <d3d11_colorconv_shader.inl>
#include <d3d11_pointer_shader.inl>
#include <d3d11_display_shader.inl>

//#define WIN7_TEST 1

namespace {
	struct Vertex {
		float x, y;
		float u, v;
	};

	typedef HRESULT (WINAPI *tpD3D11CreateDevice)(IDXGIAdapter *pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL *pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		ID3D11Device **ppDevice,
		D3D_FEATURE_LEVEL *pFeatureLevel,
		ID3D11DeviceContext **ppImmediateContext);
}

VDScreenGrabberDXGI12::VDScreenGrabberDXGI12()
	: mpCB(NULL)
	, mOutputW(0)
	, mOutputH(0)
	, mSrcW(0)
	, mSrcH(0)
	, mDstW(0)
	, mDstH(0)
	, mCaptureFormat(kVDScreenGrabberFormat_XRGB32)
	, mCaptureX(0)
	, mCaptureY(0)
	, mPointerX(0)
	, mPointerY(0)
	, mbPointerVisible(false)
	, mbCapturePointer(true)
	, mhmodDXGI(NULL)
	, mhmodD3D11(NULL)
	, mpFactory(NULL)
	, mpAdapter(NULL)
	, mpOutDup(NULL)
	, mpDevice(NULL)
	, mpDevCtx(NULL)
	, mpDownscaleSamplerState(NULL)
	, mpDownscaleVS(NULL)
	, mpGeometryIL(NULL)
	, mpGeometryVB(NULL)
	, mpBlendStateDefault(NULL)
	, mpRasterizerStateDefault(NULL)
	, mpDepthStencilStateDefault(NULL)
	, mpImageTex(NULL)
	, mpImageTexSRV(NULL)
	, mpImageTexRTV(NULL)
	, mpPointerImageTex(NULL)
	, mpPointerImageSRV(NULL)
	, mPointerShapeInfo()
	, mpPointerConstBuf(NULL)
	, mpPointerBlendState(NULL)
	, mpPointerMaskBlendState(NULL)
	, mpPointerVS(NULL)
	, mpPointerPSBlend(NULL)
	, mpPointerPSMaskA0(NULL)
	, mpPointerPSMaskA1(NULL)
	, mpDownscaleHorizPS(NULL)
	, mpDownscaleVertPS(NULL)
	, mpDownscaleIntRTV(NULL)
	, mpDownscaleIntSRV(NULL)
	, mpDownscaleIntTex(NULL)
	, mpDownscaleResultRTV(NULL)
	, mpDownscaleResultSRV(NULL)
	, mpDownscaleResultTex(NULL)
	, mpDownscaleHorizCoeffTex(NULL)
	, mpDownscaleHorizCoeffSRV(NULL)
	, mpDownscaleVertCoeffTex(NULL)
	, mpDownscaleVertCoeffSRV(NULL)
	, mpDownscaleConstBufHoriz(NULL)
	, mpDownscaleConstBufVert(NULL)
	, mpColorConvSamplerState(NULL)
	, mpColorConvVSLuma(NULL)
	, mpColorConvVSChroma(NULL)
	, mpColorConvPSLuma(NULL)
	, mpColorConvPSChroma(NULL)
	, mpColorConvConstBufY(NULL)
	, mpColorConvConstBufCb(NULL)
	, mpColorConvConstBufCr(NULL)
	, mpColorConvResultTex(NULL)
	, mpColorConvResultSRV(NULL)
	, mpColorConvResultRTV(NULL)
	, mpColorConvIB(NULL)
	, mpColorConvVB(NULL)
	, mpReadbackTex(NULL)
	, mDisplayWndClass(NULL)
	, mDisplayWndProc(NULL)
	, mhwndDisplay(NULL)
	, mhwndDisplayParent(NULL)
	, mDisplayArea(0, 0, 0, 0)
	, mbDisplayVisible(false)
	, mpDisplayIL(NULL)
	, mpDisplayVB(NULL)
	, mpDisplaySrcTex(NULL)
	, mpDisplaySrcTexSRV(NULL)
	, mpDisplaySamplerState(NULL)
	, mpDisplayRasterizerState(NULL)
	, mpDisplayPS(NULL)
	, mpDisplayVS(NULL)
	, mpDisplayRT(NULL)
	, mpDisplayRTV(NULL)
	, mpDisplaySwapChain(NULL)
	, mbDisplayFramePending(false)
{
}

VDScreenGrabberDXGI12::~VDScreenGrabberDXGI12() {
	Shutdown();
}

bool VDScreenGrabberDXGI12::Init(IVDScreenGrabberCallback *cb) {
	mpCB = cb;
	mbDisplayFramePending = false;

	if (!InitDXGI())
		return false;

	return true;
}

void VDScreenGrabberDXGI12::Shutdown() {
	ShutdownDisplay();
	ShutdownCapture();
	ShutdownDXGI();
}

bool VDScreenGrabberDXGI12::InitCapture(uint32 srcw, uint32 srch, uint32 dstw, uint32 dsth, VDScreenGrabberFormat format) {
	mSrcW = srcw;
	mSrcH = srch;
	mDstW = dstw;
	mDstH = dsth;
	mCaptureFormat = format;

	if (!InitDevice() || !InitRescale(mSrcW, mSrcH, mDstW, mDstH) || !InitPointer()) {
		ShutdownCapture();
		return false;
	}

	return true;
}

void VDScreenGrabberDXGI12::ShutdownCapture() {
	if (mpDevCtx) {
		mpDevCtx->ClearState();
		mpDevCtx->Flush();
	}

	ShutdownPointer();
	ShutdownRescale();
	ShutdownDevice();
}

void VDScreenGrabberDXGI12::SetCaptureOffset(int x, int y) {
	mCaptureX = x;
	mCaptureY = y;
}

void VDScreenGrabberDXGI12::SetCapturePointer(bool enable) {
	mbCapturePointer = enable;
}

bool VDScreenGrabberDXGI12::InitDisplay(HWND hwndParent, bool preview) {
	if (!InitDisplayInternal(hwndParent)) {
		ShutdownDisplayInternal();
		return false;
	}

	return true;
}

void VDScreenGrabberDXGI12::ShutdownDisplay() {
	ShutdownDisplayInternal();
}

void VDScreenGrabberDXGI12::SetDisplayVisible(bool visible) {
	mbDisplayVisible = visible;

	if (mhwndDisplay)
		ShowWindow(mhwndDisplay, visible ? SW_SHOW : SW_HIDE);
}

void VDScreenGrabberDXGI12::SetDisplayArea(const vdrect32& area) {
	mDisplayArea = area;

	mDisplayArea.bottom = std::max<int>(mDisplayArea.bottom, mDisplayArea.top);
	mDisplayArea.right = std::max<int>(mDisplayArea.right, mDisplayArea.left);

	if (mhwndDisplay)
		SetWindowPos(mhwndDisplay, NULL, mDisplayArea.left, mDisplayArea.top, mDisplayArea.width(), mDisplayArea.height(), SWP_NOZORDER | SWP_NOACTIVATE);
}

uint64 VDScreenGrabberDXGI12::GetCurrentTimestamp() {
	LARGE_INTEGER t = {};
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

sint64 VDScreenGrabberDXGI12::ConvertTimestampDelta(uint64 t, uint64 base) {
	LARGE_INTEGER f;
	if (!QueryPerformanceFrequency(&f))
		return 0;

	return VDRoundToInt64((double)(sint64)(t - base) * 1000000.0 / (double)f.QuadPart);
}

bool VDScreenGrabberDXGI12::AcquireFrame(bool dispatch) {
	DXGI_OUTDUPL_FRAME_INFO frameInfo = {};

#if !WIN7_TEST
	vdrefptr<IDXGIResource> pResource;
	HRESULT hr = mpOutDup->AcquireNextFrame(0, &frameInfo, ~pResource);

	QueryPerformanceCounter(&frameInfo.LastPresentTime);

	if (FAILED(hr)) {
		mpCB->ReceiveFrame(frameInfo.LastPresentTime.QuadPart, NULL, 0, 0, 0);
		return true;
	}
#else
	frameInfo.PointerPosition.Position.x = 100;
	frameInfo.PointerPosition.Position.y = 100;
	frameInfo.PointerPosition.Visible = TRUE;
	frameInfo.PointerShapeBufferSize = 0;

	if (true) {
		frameInfo.PointerShapeBufferSize = 1;
		QueryPerformanceCounter(&frameInfo.LastMouseUpdateTime);
	}

	QueryPerformanceCounter(&frameInfo.LastPresentTime);
#endif

	try {
		if (frameInfo.LastMouseUpdateTime.QuadPart) {
			mPointerX = frameInfo.PointerPosition.Position.x;
			mPointerY = frameInfo.PointerPosition.Position.y;
			mbPointerVisible = frameInfo.PointerPosition.Visible != 0;
		}

		// We MUST update the pointer texture immediately, as the new pointer data is only available
		// on this frame.
		if (frameInfo.PointerShapeBufferSize)
			UpdatePointer();

		int x = mCaptureX;
		int y = mCaptureY;

		if (x + mSrcW > mOutputW)
			x = mOutputW - mSrcW;

		if (y + mSrcH > mOutputH)
			y = mOutputH - mSrcH;

		if (x < 0)
			x = 0;

		if (y < 0)
			y = 0;

#if !WIN7_TEST
		vdrefptr<ID3D11Texture2D> pTexImage;
		hr = pResource->QueryInterface(IID_ID3D11Texture2D, (void **)~pTexImage);
		if (FAILED(hr)) {
			pResource.clear();
			mpOutDup->ReleaseFrame();
			return false;
		}

		D3D11_BOX copybox;
		copybox.left = x;
		copybox.top = y;
		copybox.right = std::min<sint32>(x + mSrcW, mOutputW);
		copybox.bottom = std::min<sint32>(y + mSrcH, mOutputH);
		copybox.front = 0;
		copybox.back = 1;

		mpDevCtx->CopySubresourceRegion(mpImageTex, 0, 0, 0, 0, pTexImage, 0, &copybox);
#endif

		UpdateFrame(frameInfo, x, y, dispatch);
	} catch(const MyError&) {
		pResource.clear();
		mpOutDup->ReleaseFrame();
		throw;
	}

#if !WIN7_TEST
	pResource.clear();
	mpOutDup->ReleaseFrame();
#endif

	return true;
}

void VDScreenGrabberDXGI12::UpdateFrame(const DXGI_OUTDUPL_FRAME_INFO& frameInfo, int x, int y, bool dispatch) {
	HRESULT hr;

	// render cursor
	if (mbPointerVisible && mbCapturePointer)
		RenderPointer(mPointerX - x, mPointerY - y);

	const UINT vbstride = sizeof(Vertex);
	const UINT vboffset = 0;
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.MinDepth = 0;
	vp.MaxDepth = 1;

	if (mpDownscaleHorizPS) {
		// downscale horizontal pass
		mpDevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		mpDevCtx->IASetInputLayout(mpGeometryIL);
		mpDevCtx->IASetVertexBuffers(0, 1, &mpGeometryVB, &vbstride, &vboffset);
		mpDevCtx->IASetIndexBuffer(NULL, DXGI_FORMAT_UNKNOWN, 0);

		mpDevCtx->VSSetConstantBuffers(0, 1, &mpDownscaleConstBufHoriz);
		mpDevCtx->VSSetShader(mpDownscaleVS, NULL, 0);

		mpDevCtx->PSSetConstantBuffers(0, 1, &mpDownscaleConstBufHoriz);

		ID3D11SamplerState *samplers[2] = { mpDownscaleSamplerState, mpDownscaleSamplerState };
		mpDevCtx->PSSetSamplers(0, 2, samplers);

		ID3D11ShaderResourceView *horizShaderInputs[2] = { mpImageTexSRV, mpDownscaleHorizCoeffSRV };
		mpDevCtx->PSSetShaderResources(0, 2, horizShaderInputs);
		mpDevCtx->PSSetShader(mpDownscaleHorizPS, NULL, 0);

		vp.Width = (float)mDstW;
		vp.Height = (float)mSrcH;
		mpDevCtx->RSSetViewports(1, &vp);
		mpDevCtx->RSSetState(mpRasterizerStateDefault);

		mpDevCtx->OMSetRenderTargets(1, &mpDownscaleIntRTV, NULL);
		const FLOAT blendFactor[4] = {0};
		mpDevCtx->OMSetBlendState(mpBlendStateDefault, blendFactor, 0xffffffff);
		mpDevCtx->OMSetDepthStencilState(mpDepthStencilStateDefault, 0);

		mpDevCtx->Draw(4, 0);

		ID3D11RenderTargetView *rtvnull = 0;
		mpDevCtx->OMSetRenderTargets(1, &rtvnull, NULL);

		// downscale vertical pass
		mpDevCtx->VSSetConstantBuffers(0, 1, &mpDownscaleConstBufVert);
		mpDevCtx->PSSetConstantBuffers(0, 1, &mpDownscaleConstBufVert);

		ID3D11ShaderResourceView *vertShaderInputs[2] = { mpDownscaleIntSRV, mpDownscaleVertCoeffSRV };
		mpDevCtx->PSSetShaderResources(0, 2, vertShaderInputs);
		mpDevCtx->PSSetShader(mpDownscaleVertPS, NULL, 0);

		vp.Width = (float)mDstW;
		vp.Height = (float)mDstH;
		mpDevCtx->RSSetViewports(1, &vp);

		mpDevCtx->OMSetRenderTargets(1, &mpDownscaleResultRTV, NULL);

		mpDevCtx->Draw(4, 0);
		mpDevCtx->OMSetRenderTargets(1, &rtvnull, NULL);
	}

	// do color conversion
	if (mCaptureFormat != kVDScreenGrabberFormat_XRGB32) {
		mpDevCtx->ClearState();
		mpDevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mpDevCtx->IASetInputLayout(mpGeometryIL);
		mpDevCtx->IASetVertexBuffers(0, 1, &mpColorConvVB, &vbstride, &vboffset);
		mpDevCtx->IASetIndexBuffer(mpColorConvIB, DXGI_FORMAT_R16_UINT, 0);

		mpDevCtx->RSSetState(mpRasterizerStateDefault);

		mpDevCtx->OMSetRenderTargets(1, &mpColorConvResultRTV, NULL);
		const FLOAT blendFactor[4] = {0};
		mpDevCtx->OMSetBlendState(mpBlendStateDefault, blendFactor, 0xffffffff);
		mpDevCtx->OMSetDepthStencilState(mpDepthStencilStateDefault, 0);

		mpDevCtx->PSSetShaderResources(0, 1, &mpDownscaleResultSRV);

		ID3D11SamplerState *samplers2[2] = { mpColorConvSamplerState, NULL};
		mpDevCtx->PSSetSamplers(0, 2, samplers2);

		mpDevCtx->VSSetShader(mpColorConvVSLuma, NULL, 0);
		mpDevCtx->PSSetShader(mpColorConvPSLuma, NULL, 0);

		mpDevCtx->VSSetConstantBuffers(0, 1, &mpColorConvConstBufY);
		mpDevCtx->PSSetConstantBuffers(0, 1, &mpColorConvConstBufY);

		if (mCaptureFormat == kVDScreenGrabberFormat_YV12) {
			vp.Width = (float)(mDstW >> 2);
			vp.Height = (float)(mDstH + (mDstH >> 1));
			mpDevCtx->RSSetViewports(1, &vp);

			mpDevCtx->DrawIndexed(6, 0, 0);

			mpDevCtx->VSSetShader(mpColorConvVSChroma, NULL, 0);
			mpDevCtx->PSSetShader(mpColorConvPSChroma, NULL, 0);

			mpDevCtx->VSSetConstantBuffers(0, 1, &mpColorConvConstBufCr);
			mpDevCtx->PSSetConstantBuffers(0, 1, &mpColorConvConstBufCr);
			mpDevCtx->DrawIndexed(12, 6, 0);

			mpDevCtx->VSSetConstantBuffers(0, 1, &mpColorConvConstBufCb);
			mpDevCtx->PSSetConstantBuffers(0, 1, &mpColorConvConstBufCb);
			mpDevCtx->DrawIndexed(12, 18, 0);
		} else if (mCaptureFormat == kVDScreenGrabberFormat_YUY2) {
			vp.Width = (float)(mDstW >> 1);
			vp.Height = (float)mDstH;
			mpDevCtx->RSSetViewports(1, &vp);

			mpDevCtx->DrawIndexed(6, 0, 0);
		}

		mpDevCtx->ClearState();
	}

	// readback to staging
	mpDevCtx->CopyResource(mpReadbackTex, mpColorConvResultTex);

	// map and extract frame
	if (dispatch) {
		D3D11_MAPPED_SUBRESOURCE mapped;
		hr = mpDevCtx->Map(mpReadbackTex, 0, D3D11_MAP_READ, 0, &mapped);
		if (FAILED(hr))
			return;

		uint32 rowlen = mDstW;
		uint32 rowcnt = mDstH;
		const void *data = mapped.pData;
		ptrdiff_t pitch = mapped.RowPitch;

		switch(mCaptureFormat) {
			case kVDScreenGrabberFormat_XRGB32:
			default:
				rowlen <<= 2;
				data = (const char *)data + pitch * (rowcnt - 1);
				pitch = -pitch;
				break;

			case kVDScreenGrabberFormat_YUY2:
				rowlen <<= 1;
				break;

			case kVDScreenGrabberFormat_YV12:
				rowcnt += rowcnt >> 1;
				break;
		}

		mpCB->ReceiveFrame(frameInfo.LastPresentTime.QuadPart, data, pitch, rowlen, rowcnt);

		mpDevCtx->Unmap(mpReadbackTex, 0);
	}

	if (mhwndDisplay && !mbDisplayFramePending) {
		mbDisplayFramePending = true;
		PostMessageW(mhwndDisplay, WM_USER + 100, 0, 0);
	}
}

bool VDScreenGrabberDXGI12::InitDXGI() {
	HRESULT hr;

	mhmodDXGI = VDLoadSystemLibraryW32("dxgi");
	if (!mhmodDXGI)
		return false;

	mhmodD3D11 = VDLoadSystemLibraryW32("d3d11");
	if (!mhmodD3D11)
		return false;

	typedef HRESULT (WINAPI *tpCreateDXGIFactory1)(REFIID riid, void **ppFactory);
	tpCreateDXGIFactory1 pCreateDXGIFactory1 = (tpCreateDXGIFactory1)GetProcAddress(mhmodDXGI, "CreateDXGIFactory1");

	if (!pCreateDXGIFactory1)
		return false;

	hr = pCreateDXGIFactory1(IID_IDXGIFactory1, (void **)&mpFactory);
	if (FAILED(hr))
		return false;

	hr = mpFactory->EnumAdapters1(0, &mpAdapter);
	if (FAILED(hr))
		return false;

	return true;
}

void VDScreenGrabberDXGI12::ShutdownDXGI() {
	vdsaferelease <<=
		mpAdapter,
		mpFactory;

	if (mhmodD3D11) {
		FreeLibrary(mhmodD3D11);
		mhmodD3D11 = NULL;
	}

	if (mhmodDXGI) {
		FreeLibrary(mhmodDXGI);
		mhmodDXGI = NULL;
	}
}

bool VDScreenGrabberDXGI12::InitDevice() {
	VDASSERT(!mpDevice);

	vdrefptr<IDXGIOutput> pOutput;
	HRESULT hr = mpAdapter->EnumOutputs(0, ~pOutput);
	if (FAILED(hr))
		return false;

#if !WIN7_TEST
	vdrefptr<IDXGIOutput1> pOutput1;
	hr = pOutput->QueryInterface(IID_IDXGIOutput1, (void **)~pOutput1);
	if (FAILED(hr))
		return false;
#endif

	tpD3D11CreateDevice pD3D11CreateDevice = (tpD3D11CreateDevice)GetProcAddress(mhmodD3D11, "D3D11CreateDevice");

	if (!pD3D11CreateDevice)
		return false;

	DWORD dwCreateFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifdef _DEBUG
	dwCreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	hr = pD3D11CreateDevice(mpAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, dwCreateFlags, NULL, 0, D3D11_SDK_VERSION, &mpDevice, NULL, &mpDevCtx);

	if (FAILED(hr)) {
#ifdef _DEBUGX
		dwCreateFlags &= ~D3D11_CREATE_DEVICE_DEBUG;

		hr = pD3D11CreateDevice(mpAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, dwCreateFlags, NULL, 0, D3D11_SDK_VERSION, &mpDevice, NULL, &mpDevCtx);
		if (FAILED(hr))
			return false;
#else
		return false;
#endif
	}

#if !WIN7_TEST
	hr = pOutput1->DuplicateOutput(mpDevice, &mpOutDup);
	if (FAILED(hr))
		return false;

	DXGI_OUTDUPL_DESC outDesc = {};
	mpOutDup->GetDesc(&outDesc);

	mOutputW = outDesc.ModeDesc.Width;
	mOutputH = outDesc.ModeDesc.Height;
#else
	mOutputW = 1920;
	mOutputH = 1080;
#endif

	D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = FALSE;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = mpDevice->CreateBlendState(&blendDesc, &mpBlendStateDefault);
	if (FAILED(hr))
		return false;

	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = TRUE;
    rastDesc.DepthBias = 0;
    rastDesc.DepthBiasClamp = 0;
    rastDesc.SlopeScaledDepthBias = 0;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.ScissorEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
	hr = mpDevice->CreateRasterizerState(&rastDesc, &mpRasterizerStateDefault);
	if (FAILED(hr))
		return false;

	D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;
	hr = mpDevice->CreateDepthStencilState(&depthDesc, &mpDepthStencilStateDefault);
	if (FAILED(hr))
		return false;

	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = mpDevice->CreateSamplerState(&sampDesc, &mpDownscaleSamplerState);
	if (FAILED(hr))
		return false;

	static const D3D11_INPUT_ELEMENT_DESC kInputElements[2]={
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = mpDevice->CreateInputLayout(kInputElements, 2, g_VDCapDXGI12_VS_Downsample, sizeof(g_VDCapDXGI12_VS_Downsample), &mpGeometryIL);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreateVertexShader(g_VDCapDXGI12_VS_Downsample, sizeof(g_VDCapDXGI12_VS_Downsample), NULL, &mpDownscaleVS);
	if (FAILED(hr))
		return false;

	return true;
}

void VDScreenGrabberDXGI12::ShutdownDevice() {
	vdsaferelease <<=
		mpDownscaleVS,
		mpGeometryIL,
		mpDownscaleSamplerState,
		mpDepthStencilStateDefault,
		mpRasterizerStateDefault,
		mpBlendStateDefault,
		mpOutDup,
		mpDevCtx,
		mpDevice;
}

bool VDScreenGrabberDXGI12::InitRescale(uint32 srcw, uint32 srch, uint32 dstw, uint32 dsth) {
	VDASSERT(srcw != 0);
	VDASSERT(srch != 0);
	VDASSERT(dstw != 0);
	VDASSERT(dsth != 0);

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = dstw;
	texDesc.Height = srch;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	HRESULT hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpDownscaleIntTex);
	if (FAILED(hr))
		return false;

	texDesc.Width = mSrcW;
	texDesc.Height = mSrcH;

#if WIN7_TEST
	vdfastvector<uint32> testbuf(mSrcW * mSrcH);
	uint32 *dst = testbuf.data();
	for(int y=0; y<mSrcH; ++y) {
		uint32 cy = y * 255 / (mSrcH - 1);
		double dy = y - (mSrcH >> 1);
		double dy2 = dy*dy;

		for(int x=0; x<mSrcW; ++x) {
			uint32 cx = (x * 255 / (mSrcW - 1)) << 16;
			double dx = x - (mSrcW >> 1);
			double dx2 = dx*dx;
			uint32 c = cx + cy + 0x0100 * (int)(128.5 + 127.0 * sin((dx2 + dy2) * 0.004f));

			*dst++ = c;
		}
	}

	D3D11_SUBRESOURCE_DATA initTestData;
	initTestData.pSysMem = testbuf.data();
	initTestData.SysMemPitch = mSrcW*4;
	initTestData.SysMemSlicePitch = 0;

	hr = mpDevice->CreateTexture2D(&texDesc, &initTestData, &mpImageTex);
#else
	hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpImageTex);
#endif
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreateShaderResourceView(mpImageTex, NULL, &mpImageTexSRV);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreateRenderTargetView(mpImageTex, NULL, &mpImageTexRTV);
	if (FAILED(hr))
		return false;

	// check if we actually need rescaling
	if (srcw == dstw && srch == dsth) {
		// nope -- just alias the source image SRV
		mpDownscaleResultTex = mpImageTex;
		mpDownscaleResultTex->AddRef();

		mpDownscaleResultSRV = mpImageTexSRV;
		mpDownscaleResultSRV->AddRef();
	} else {
		texDesc.Width = dstw;
		texDesc.Height = dsth;

		hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpDownscaleResultTex);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateShaderResourceView(mpDownscaleIntTex, NULL, &mpDownscaleIntSRV);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateShaderResourceView(mpDownscaleResultTex, NULL, &mpDownscaleResultSRV);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateRenderTargetView(mpDownscaleIntTex, NULL, &mpDownscaleIntRTV);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateRenderTargetView(mpDownscaleResultTex, NULL, &mpDownscaleResultRTV);
		if (FAILED(hr))
			return false;

		// select filters
		//
		// A cubic spline filter is nominally 4 taps wide at unity cutoff. A 1/4 ratio gives the
		// max 16-tap filter.
		const float h_ratio = std::clamp((float)dstw / (float)srcw, 0.25f, 1.0f);
		const float v_ratio = std::clamp((float)dsth / (float)srch, 0.25f, 1.0f);

		// create coefficient textures
		D3D11_SUBRESOURCE_DATA initData = {};
		vdfastvector<uint32> buf;

		buf.resize(dstw * 4);

		const VDResamplerCubicFilter horizFilter(h_ratio, -0.6f);
		CreateFilter(buf.data(), dstw, srcw, horizFilter);

		texDesc.Width = dstw;
		texDesc.Height = 4;
		texDesc.Usage = D3D11_USAGE_IMMUTABLE;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		texDesc.CPUAccessFlags = 0;

		initData.pSysMem = buf.data();
		initData.SysMemPitch = dstw * sizeof(uint32);
		hr = mpDevice->CreateTexture2D(&texDesc, &initData, &mpDownscaleHorizCoeffTex);
		if (FAILED(hr))
			return false;

		buf.resize(dsth * 4);

		const VDResamplerCubicFilter vertFilter(v_ratio, -0.6f);
		CreateFilter(buf.data(), dsth, srch, vertFilter);

		texDesc.Width = dsth;

		initData.pSysMem = buf.data();
		initData.SysMemPitch = dsth * sizeof(uint32);
		hr = mpDevice->CreateTexture2D(&texDesc, &initData, &mpDownscaleVertCoeffTex);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateShaderResourceView(mpDownscaleHorizCoeffTex, NULL, &mpDownscaleHorizCoeffSRV);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateShaderResourceView(mpDownscaleVertCoeffTex, NULL, &mpDownscaleVertCoeffSRV);
		if (FAILED(hr))
			return false;

		D3D11_BUFFER_DESC bufDesc = {};
		bufDesc.ByteWidth = sizeof(float) * 8;
		bufDesc.Usage = D3D11_USAGE_IMMUTABLE;
		bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		initData.SysMemPitch = 0;
		initData.SysMemSlicePitch = 0;

		const float hconst[8] = {
			1,0,0,0,
			1.0f / (float)srcw, 0, 0, 0
		};

		initData.pSysMem = hconst;

		hr = mpDevice->CreateBuffer(&bufDesc, &initData, &mpDownscaleConstBufHoriz);
		if (FAILED(hr))
			return false;

		const float vconst[8] = {
			0,1,0,0,
			0, 1.0f / (float)srch, 0, 0
		};
		initData.pSysMem = vconst;

		hr = mpDevice->CreateBuffer(&bufDesc, &initData, &mpDownscaleConstBufVert);
		if (FAILED(hr))
			return false;

		for(int i=0; i<2; ++i) {
			const int taps = i ? vertFilter.GetFilterWidth() : horizFilter.GetFilterWidth();
			const void *bytecode = NULL;
			size_t bclen = 0;

				 if (taps >= 16) { bytecode = g_VDCapDXGI12_PS_Downsample16; bclen = sizeof g_VDCapDXGI12_PS_Downsample16; }
			else if (taps >= 14) { bytecode = g_VDCapDXGI12_PS_Downsample14; bclen = sizeof g_VDCapDXGI12_PS_Downsample14; }
			else if (taps >= 12) { bytecode = g_VDCapDXGI12_PS_Downsample12; bclen = sizeof g_VDCapDXGI12_PS_Downsample12; }
			else if (taps >= 10) { bytecode = g_VDCapDXGI12_PS_Downsample10; bclen = sizeof g_VDCapDXGI12_PS_Downsample10; }
			else if (taps >=  8) { bytecode = g_VDCapDXGI12_PS_Downsample8;  bclen = sizeof g_VDCapDXGI12_PS_Downsample8; }
			else if (taps >=  6) { bytecode = g_VDCapDXGI12_PS_Downsample6;  bclen = sizeof g_VDCapDXGI12_PS_Downsample6; }
			else				 { bytecode = g_VDCapDXGI12_PS_Downsample4;  bclen = sizeof g_VDCapDXGI12_PS_Downsample4; }

			ID3D11PixelShader *ps = NULL;
			hr = mpDevice->CreatePixelShader(bytecode, bclen, NULL, &ps);
			if (FAILED(hr))
				return false;

			if (i)
				mpDownscaleVertPS = ps;
			else
				mpDownscaleHorizPS = ps;
		}
	}

	static const Vertex kVertices[4]={
		{-1,+1,0,0},
		{-1,-1,0,1},
		{+1,+1,1,0},
		{+1,-1,1,1}
	};

	D3D11_SUBRESOURCE_DATA vbInitData = {};
	vbInitData.pSysMem = kVertices;

	D3D11_BUFFER_DESC vbBufDesc = {};
	vbBufDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vbBufDesc.ByteWidth = sizeof(kVertices);
	vbBufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	hr = mpDevice->CreateBuffer(&vbBufDesc, &vbInitData, &mpGeometryVB);
	if (FAILED(hr))
		return false;

	// color conversion
	if (mCaptureFormat == kVDScreenGrabberFormat_XRGB32) {
		mpColorConvResultTex = mpDownscaleResultTex;
		mpColorConvResultTex->AddRef();

		texDesc.Width = mDstW;
		texDesc.Height = mDstH;
	} else {
		D3D11_SAMPLER_DESC sampDesc2 = {};
		sampDesc2.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		sampDesc2.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc2.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc2.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc2.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		sampDesc2.MaxLOD = D3D11_FLOAT32_MAX;
		hr = mpDevice->CreateSamplerState(&sampDesc2, &mpColorConvSamplerState);
		if (FAILED(hr))
			return false;

		if (mCaptureFormat == kVDScreenGrabberFormat_YUY2) {
			hr = mpDevice->CreateVertexShader(g_VDCapDXGI12_VS_ColorConvYUY2, sizeof g_VDCapDXGI12_VS_ColorConvYUY2, NULL, &mpColorConvVSLuma);
			if (FAILED(hr))
				return false;

			hr = mpDevice->CreatePixelShader(g_VDCapDXGI12_PS_ColorConvYUY2, sizeof g_VDCapDXGI12_PS_ColorConvYUY2, NULL, &mpColorConvPSLuma);
			if (FAILED(hr))
				return false;

			D3D11_BUFFER_DESC constBufDesc = {};
			constBufDesc.ByteWidth = sizeof(float) * 8;
			constBufDesc.Usage = D3D11_USAGE_IMMUTABLE;
			constBufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

			const float yconst[8] = {
				0.2567882f, 0.5041294f, 0.0979059f, 16.0f/255.0f,
				1.0f / (float)dstw, 0, 0, 0
			};

			D3D11_SUBRESOURCE_DATA constBufInitData = {};
			constBufInitData.pSysMem = yconst;

			hr = mpDevice->CreateBuffer(&constBufDesc, &constBufInitData, &mpColorConvConstBufY);
			if (FAILED(hr))
				return false;

			texDesc.Width = dstw >> 1;
			texDesc.Height = dsth;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			texDesc.CPUAccessFlags = 0;

			hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpColorConvResultTex);
			if (FAILED(hr))
				return false;
		} else if (mCaptureFormat == kVDScreenGrabberFormat_YV12) {
			hr = mpDevice->CreateVertexShader(g_VDCapDXGI12_VS_ColorConv1, sizeof g_VDCapDXGI12_VS_ColorConv1, NULL, &mpColorConvVSLuma);
			if (FAILED(hr))
				return false;

			hr = mpDevice->CreatePixelShader(g_VDCapDXGI12_PS_ColorConv1, sizeof g_VDCapDXGI12_PS_ColorConv1, NULL, &mpColorConvPSLuma);
			if (FAILED(hr))
				return false;

			hr = mpDevice->CreateVertexShader(g_VDCapDXGI12_VS_ColorConv2, sizeof g_VDCapDXGI12_VS_ColorConv2, NULL, &mpColorConvVSChroma);
			if (FAILED(hr))
				return false;

			hr = mpDevice->CreatePixelShader(g_VDCapDXGI12_PS_ColorConv2, sizeof g_VDCapDXGI12_PS_ColorConv2, NULL, &mpColorConvPSChroma);
			if (FAILED(hr))
				return false;

			D3D11_BUFFER_DESC constBufDesc = {};
			constBufDesc.ByteWidth = sizeof(float) * 8;
			constBufDesc.Usage = D3D11_USAGE_IMMUTABLE;
			constBufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

			const float yconst[8] = {
				0.2567882f, 0.5041294f, 0.0979059f, 16.0f/255.0f,
				0.5f / (float)dstw, 0, 0, 0
			};

			D3D11_SUBRESOURCE_DATA constBufInitData = {};
			constBufInitData.pSysMem = yconst;

			hr = mpDevice->CreateBuffer(&constBufDesc, &constBufInitData, &mpColorConvConstBufY);
			if (FAILED(hr))
				return false;

			const float cbconst[8] = {
				0.5f*-0.1482229f, 0.5f*-0.2909928f, 0.5f*0.4392157f, 128.0f/255.0f,
				1.0f / (float)dstw, 0, 0.5f / (float)dstw, 0
			};

			constBufInitData.pSysMem = cbconst;

			hr = mpDevice->CreateBuffer(&constBufDesc, &constBufInitData, &mpColorConvConstBufCb);
			if (FAILED(hr))
				return false;

			const float crconst[8] = {
				0.5f*0.4392157f, 0.5f*-0.3677883f, 0.5f*-0.0714274f, 128.0f/255.0f,
				1.0f / (float)dstw, 0, 0.5f / (float)dstw, 0
			};

			constBufInitData.pSysMem = crconst;

			hr = mpDevice->CreateBuffer(&constBufDesc, &constBufInitData, &mpColorConvConstBufCr);
			if (FAILED(hr))
				return false;

			texDesc.Width = dstw / 4;
			texDesc.Height = dsth + dsth / 2;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			texDesc.CPUAccessFlags = 0;

			hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpColorConvResultTex);
			if (FAILED(hr))
				return false;
		}

		const Vertex kCCVerticesYV12[20]={
			// luma
			{ -1.0f, +1.0f, 0.0f, 0.0f },
			{ -1.0f, -1.0f/3.0f, 0.0f, 1.0f },
			{ +1.0f, +1.0f, 1.0f, 0.0f },
			{ +1.0f, -1.0f/3.0f, 1.0f, 1.0f },

			// chroma blue - even lines
			{ -1.0f, -1.0f/3.0f, 0.0f - 0.5f / dstw, 0.0f - 1.0f / dsth },
			{ -1.0f, -2.0f/3.0f, 0.0f - 0.5f / dstw, 1.0f - 1.0f / dsth },
			{  0.0f, -1.0f/3.0f, 1.0f - 0.5f / dstw, 0.0f - 1.0f / dsth },
			{  0.0f, -2.0f/3.0f, 1.0f - 0.5f / dstw, 1.0f - 1.0f / dsth },

			// chroma blue - odd lines
			{  0.0f, -1.0f/3.0f, 0.0f - 0.5f / dstw, 0.0f + 1.0f / dsth },
			{  0.0f, -2.0f/3.0f, 0.0f - 0.5f / dstw, 1.0f + 1.0f / dsth },
			{  1.0f, -1.0f/3.0f, 1.0f - 0.5f / dstw, 0.0f + 1.0f / dsth },
			{  1.0f, -2.0f/3.0f, 1.0f - 0.5f / dstw, 1.0f + 1.0f / dsth },

			// chroma red - even lines
			{ -1.0f, -2.0f/3.0f, 0.0f - 0.5f / dstw, 0.0f - 1.0f / dsth },
			{ -1.0f, -3.0f/3.0f, 0.0f - 0.5f / dstw, 1.0f - 1.0f / dsth },
			{  0.0f, -2.0f/3.0f, 1.0f - 0.5f / dstw, 0.0f - 1.0f / dsth },
			{  0.0f, -3.0f/3.0f, 1.0f - 0.5f / dstw, 1.0f - 1.0f / dsth },

			// chroma red - odd lines
			{  0.0f, -2.0f/3.0f, 0.0f - 0.5f / dstw, 0.0f + 1.0f / dsth },
			{  0.0f, -3.0f/3.0f, 0.0f - 0.5f / dstw, 1.0f + 1.0f / dsth },
			{  1.0f, -2.0f/3.0f, 1.0f - 0.5f / dstw, 0.0f + 1.0f / dsth },
			{  1.0f, -3.0f/3.0f, 1.0f - 0.5f / dstw, 1.0f + 1.0f / dsth },
		};

		const Vertex kCCVerticesYUY2[4]={
			// luma
			{ -1.0f, +1.0f, 0.0f, 0.0f },
			{ -1.0f, -1.0f, 0.0f, 1.0f },
			{ +1.0f, +1.0f, 1.0f, 0.0f },
			{ +1.0f, -1.0f, 1.0f, 1.0f },
		};

		vbBufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		switch(mCaptureFormat) {
			case kVDScreenGrabberFormat_XRGB32:
				break;

			case kVDScreenGrabberFormat_YUY2:
				vbInitData.pSysMem = kCCVerticesYUY2;
				vbBufDesc.ByteWidth = sizeof(kCCVerticesYUY2);
				hr = mpDevice->CreateBuffer(&vbBufDesc, &vbInitData, &mpColorConvVB);
				break;

			case kVDScreenGrabberFormat_YV12:
				vbInitData.pSysMem = kCCVerticesYV12;
				vbBufDesc.ByteWidth = sizeof(kCCVerticesYV12);
				hr = mpDevice->CreateBuffer(&vbBufDesc, &vbInitData, &mpColorConvVB);
				break;
		}

		if (FAILED(hr))
			return false;

		const uint16 kCCIndices[30]={
			0,1,2,2,1,3,
			4,5,6,6,5,7,
			8,9,10,10,9,11,
			12,13,14,14,13,15,
			16,17,18,18,17,19
		};

		vbInitData.pSysMem = kCCIndices;

		vbBufDesc.ByteWidth = sizeof(kCCIndices);
		vbBufDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		hr = mpDevice->CreateBuffer(&vbBufDesc, &vbInitData, &mpColorConvIB);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateShaderResourceView(mpColorConvResultTex, NULL, &mpColorConvResultSRV);
		if (FAILED(hr))
			return false;

		hr = mpDevice->CreateRenderTargetView(mpColorConvResultTex, NULL, &mpColorConvResultRTV);
		if (FAILED(hr))
			return false;
	}

	// readback
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	hr = mpDevice->CreateTexture2D(&texDesc, NULL, &mpReadbackTex);
	if (FAILED(hr))
		return false;

	return true;
}

void VDScreenGrabberDXGI12::ShutdownRescale() {
	vdsaferelease <<=
		mpReadbackTex,

		mpColorConvVSLuma,
		mpColorConvPSLuma,
		mpColorConvVSChroma,
		mpColorConvPSChroma,
		mpColorConvSamplerState,
		mpColorConvConstBufY,
		mpColorConvConstBufCb,
		mpColorConvConstBufCr,
		mpColorConvVB,
		mpColorConvIB,
		mpColorConvResultTex,
		mpColorConvResultSRV,
		mpColorConvResultRTV,

		mpDownscaleVertPS,
		mpDownscaleHorizPS,
		mpGeometryVB,
		mpDownscaleConstBufVert,
		mpDownscaleConstBufHoriz,
		mpDownscaleVertCoeffSRV,
		mpDownscaleHorizCoeffSRV,
		mpDownscaleVertCoeffTex,
		mpDownscaleHorizCoeffTex,
		mpDownscaleResultRTV,
		mpDownscaleIntRTV,
		mpDownscaleResultSRV,
		mpDownscaleIntSRV,
		mpDownscaleResultTex,
		mpDownscaleIntTex,

		mpImageTexRTV,
		mpImageTexSRV,
		mpImageTex;
}

void VDScreenGrabberDXGI12::CreateFilter(uint32 *dst, uint32 dstw, uint32 srcw, const IVDResamplerFilter& filter) {
	double coeff[16];
	uint8 icoeff[16];

	uint32 *dst0 = dst;
	uint32 *dst1 = dst0 + dstw;
	uint32 *dst2 = dst1 + dstw;
	uint32 *dst3 = dst2 + dstw;

	const int taps = filter.GetFilterWidth();
	double koffset = 8.0;

		 if (taps >= 16) koffset = 8.0;
	else if (taps >= 14) koffset = 7.0;
	else if (taps >= 12) koffset = 6.0;
	else if (taps >= 10) koffset = 5.0;
	else if (taps >=  8) koffset = 4.0;
	else if (taps >=  6) koffset = 3.0;
	else				 koffset = 2.0;

	const int positap = (taps - 1) | 3;
	const int centap = taps >> 1;

	double dudx = (double)srcw / (double)dstw;
	double u = dudx * 0.5 + 0.5 - koffset;
	for(uint32 i=0; i<dstw; ++i) {
		double sum = 0;

		double f = u - floor(u);
		u += dudx;

		for(int j=0; j<16; ++j) {
			const double c = filter.EvaluateFilter((double)j + 1.0f - koffset - f);

			coeff[j] = c;
			sum += c;
		}

		const double scale = 128.0 / sum;

		int ierror = 64*16 + 128;
		for(int j=0; j<16; ++j) {
			const uint8 ic = (uint8)(coeff[j] * scale + 64.5);

			icoeff[j] = ic;

			ierror -= ic;
		}

		icoeff[centap] += ierror;

		// Replace the last tap in the last pixel with the offset.
		icoeff[positap] = (uint8)(128.5f - f * 64.0f);

		dst0[i] = ((uint32)icoeff[ 0] << 16) + ((uint32)icoeff[ 1] <<  8) + ((uint32)icoeff[ 2] <<  0) + ((uint32)icoeff[ 3] << 24);
		dst1[i] = ((uint32)icoeff[ 4] << 16) + ((uint32)icoeff[ 5] <<  8) + ((uint32)icoeff[ 6] <<  0) + ((uint32)icoeff[ 7] << 24);
		dst2[i] = ((uint32)icoeff[ 8] << 16) + ((uint32)icoeff[ 9] <<  8) + ((uint32)icoeff[10] <<  0) + ((uint32)icoeff[11] << 24);
		dst3[i] = ((uint32)icoeff[12] << 16) + ((uint32)icoeff[13] <<  8) + ((uint32)icoeff[14] <<  0) + ((uint32)icoeff[15] << 24);
	}
}

bool VDScreenGrabberDXGI12::InitPointer() {
	mPointerX = 0;
	mPointerY = 0;
	mbPointerVisible = false;

	D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = sizeof(float) * 4;
	bufDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = mpDevice->CreateBuffer(&bufDesc, NULL, &mpPointerConstBuf);
	if (FAILED(hr))
		return false;

	D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = mpDevice->CreateBlendState(&blendDesc, &mpPointerBlendState);
	if (FAILED(hr))
		return false;

	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_INV_DEST_COLOR;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	hr = mpDevice->CreateBlendState(&blendDesc, &mpPointerMaskBlendState);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreateVertexShader(&g_VDCapDXGI12_VS_Pointer, sizeof g_VDCapDXGI12_VS_Pointer, NULL, &mpPointerVS);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreatePixelShader(&g_VDCapDXGI12_PS_Pointer_Blend, sizeof g_VDCapDXGI12_PS_Pointer_Blend, NULL, &mpPointerPSBlend);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreatePixelShader(&g_VDCapDXGI12_PS_Pointer_MaskA0, sizeof g_VDCapDXGI12_PS_Pointer_MaskA0, NULL, &mpPointerPSMaskA0);
	if (FAILED(hr))
		return false;

	hr = mpDevice->CreatePixelShader(&g_VDCapDXGI12_PS_Pointer_MaskA1, sizeof g_VDCapDXGI12_PS_Pointer_MaskA1, NULL, &mpPointerPSMaskA1);
	if (FAILED(hr))
		return false;

	return true;
}

void VDScreenGrabberDXGI12::ShutdownPointer() {
	vdsaferelease <<=
		mpPointerPSMaskA1,
		mpPointerPSMaskA0,
		mpPointerPSBlend,
		mpPointerVS,
		mpPointerMaskBlendState,
		mpPointerBlendState,
		mpPointerConstBuf,
		mpPointerImageSRV,
		mpPointerImageTex;
}

void VDScreenGrabberDXGI12::UpdatePointer() {
	DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo = {};
	HRESULT hr;

#if WIN7_TEST
	static const uint8 kPointerShape[256]={
		// and mask
		0x7F,0xFF,0xFF,0xFF,
		0x3F,0xFF,0xFF,0xFF,
		0x1F,0xFF,0xFF,0xFF,
		0x0F,0xFF,0xFF,0xFF,
		0x07,0xFF,0xFF,0xFF,
		0x03,0xFF,0xFF,0xFF,
		0x01,0xFF,0xFF,0xFF,
		0x00,0xFF,0xFF,0xFF,
		0x00,0x7F,0xFF,0xFF,
		0x00,0x3F,0xFF,0xFF,
		0x00,0x1F,0xFF,0xFF,
		0x00,0x0F,0xFF,0xFF,
		0x00,0x07,0xFF,0xFF,
		0x00,0x03,0xFF,0xFF,
		0x00,0x01,0xFF,0xFF,
		0x00,0x00,0xFF,0xFF,
		0x00,0x00,0x7F,0xFF,
		0x00,0x00,0x3F,0xFF,
		0x00,0x00,0x1F,0xFF,
		0x00,0x00,0x0F,0xFF,
		0x00,0x00,0x07,0xFF,
		0x00,0x00,0x03,0xFF,
		0x00,0x00,0x01,0xFF,
		0x00,0x00,0x00,0xFF,

		0xFF,0xFF,0x0F,0xFF,
		0xFF,0xFF,0x87,0xFF,
		0xFF,0xFF,0xC3,0xFF,
		0xFF,0xFF,0xE1,0xFF,
		0xFF,0xFF,0xF0,0xFF,
		0xFF,0xFF,0xF8,0x7F,
		0xFF,0xFF,0xFC,0x3F,
		0xFF,0xFF,0xFE,0x7F,

		// xor mask
		0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,
		0x40,0x00,0x00,0x00,
		0x60,0x00,0x00,0x00,
		0x70,0x00,0x00,0x00,
		0x78,0x00,0x00,0x00,
		0x7C,0x00,0x00,0x00,
		0x7E,0x00,0x00,0x00,
		0x7F,0x00,0x00,0x00,
		0x7F,0x80,0x00,0x00,
		0x7F,0xC0,0x00,0x00,
		0x7F,0xE0,0x00,0x00,
		0x7F,0xF0,0x00,0x00,
		0x7F,0xF8,0x00,0x00,
		0x7F,0xFC,0x00,0x00,
		0x7F,0xFE,0x00,0x00,
		0x7F,0xFF,0x00,0x00,
		0x7F,0xFF,0x80,0x00,
		0x7F,0xFF,0xC0,0x00,
		0x7F,0xFF,0xE0,0x00,
		0x7F,0xFF,0xF0,0x00,
		0x7F,0xFF,0xF8,0x00,
		0x7F,0xFF,0xFC,0x00,
		0x00,0x00,0x60,0x00,

		0x00,0x00,0x60,0x00,
		0x00,0x00,0x30,0x00,
		0x00,0x00,0x18,0x00,
		0x00,0x00,0x0C,0x00,
		0x00,0x00,0x06,0x00,
		0x00,0x00,0x03,0x00,
		0x00,0x00,0x01,0x80,
		0x00,0x00,0x00,0x00,
	};

	mPointerShapeBuffer.resize(256);
	memcpy(mPointerShapeBuffer.data(), kPointerShape, 256);

	shapeInfo.Type = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
	shapeInfo.Width = 32;
	shapeInfo.Height = 64;
	shapeInfo.Pitch = 4*32;
	shapeInfo.HotSpot.x = 1;
	shapeInfo.HotSpot.y = 1;
#else
	if (mPointerShapeBuffer.empty())
		mPointerShapeBuffer.resize(4096);

	UINT sizeReq = 0;
	hr = mpOutDup->GetFramePointerShape((UINT)mPointerShapeBuffer.size(), mPointerShapeBuffer.data(), &sizeReq, &shapeInfo);

	if (hr == DXGI_ERROR_MORE_DATA) {
		mPointerShapeBuffer.resize(sizeReq);

		hr = mpOutDup->GetFramePointerShape((UINT)mPointerShapeBuffer.size(), mPointerShapeBuffer.data(), &sizeReq, &shapeInfo);
	}

	if (FAILED(hr))
		return;
#endif

	vdsaferelease <<= mpPointerImageSRV,
		mpPointerImageTex;

	mPointerShapeInfo = shapeInfo;

	const uint32 w = shapeInfo.Width;
	const uint32 h = (mPointerShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) ? shapeInfo.Height >> 1 : shapeInfo.Height;
	mPointerImageBuffer.resize(w * h);

	uint32 *imgDst = mPointerImageBuffer.data();

	switch(mPointerShapeInfo.Type) {
		case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
			{
				const uint8 *imgSrc = (const uint8 *)mPointerShapeBuffer.data();
				ptrdiff_t imgSrcPitch = (int)(((w + 31) >> 5) << 2);
				ptrdiff_t imgPlaneOffset = h * imgSrcPitch;

				for(uint32 y=0; y<h; ++y) {
					for(uint32 x=0; x<w; ++x) {
						const uint8 mask = 0x80 >> (x & 7);
						uint8 andByte = imgSrc[x >> 3];
						uint8 xorByte = imgSrc[(x >> 3) + imgPlaneOffset];

						uint32 c = (xorByte & mask) ? 0x00FFFFFF : 0x00000000;

						if (andByte & mask)
							c += 0xFF000000;

						c = VDSwizzleU32(c);

						*imgDst++ = (c >> 8) + (c << 24);
					}

					imgSrc += imgSrcPitch;
				}
			}
			break;

		case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
		case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
			{
				const uint32 *imgSrc = (const uint32 *)mPointerShapeBuffer.data();

				for(uint32 y=0; y<h; ++y) {
					for(uint32 x=0; x<w; ++x) {
						uint32 c = imgSrc[x];

						c = VDSwizzleU32(c);

						*imgDst++ = (c >> 8) + (c << 24);
					}

					imgSrc += w;
				}
			}
			break;
	}

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_IMMUTABLE;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = mPointerImageBuffer.data();
	initData.SysMemPitch = sizeof(uint32)*shapeInfo.Width;
	hr = mpDevice->CreateTexture2D(&texDesc, &initData, &mpPointerImageTex);

	if (FAILED(hr))
		return;

	mpDevice->CreateShaderResourceView(mpPointerImageTex, NULL, &mpPointerImageSRV);
}

void VDScreenGrabberDXGI12::RenderPointer(int x, int y) {
	D3D11_MAPPED_SUBRESOURCE lockInfo;

	HRESULT hr = mpDevCtx->Map(mpPointerConstBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &lockInfo);
	if (FAILED(hr))
		return;

	float iw = 2.0f / (float)mSrcW;
	float ih = 2.0f / (float)mSrcH;

	float *dst = (float *)lockInfo.pData;
	dst[0] = (float)mPointerShapeInfo.Width * iw;
	dst[1] = (float)mPointerShapeInfo.Height * -ih;
	dst[2] = x * iw - 1.0f;
	dst[3] = 1.0f - y * ih;

	if (mPointerShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME)
		dst[1] *= 0.5f;

	mpDevCtx->Unmap(mpPointerConstBuf, 0);

	mpDevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mpDevCtx->IASetInputLayout(mpGeometryIL);
	const UINT vbstrides = sizeof(Vertex);
	const UINT vboffsets = 0;
	mpDevCtx->IASetVertexBuffers(0, 1, &mpGeometryVB, &vbstrides, &vboffsets);
	mpDevCtx->IASetIndexBuffer(0, DXGI_FORMAT_UNKNOWN, 0);

	mpDevCtx->VSSetConstantBuffers(0, 1, &mpPointerConstBuf);
	mpDevCtx->VSSetShader(mpPointerVS, NULL, 0);

	const float blendFactor[4]={0,0,0,0};

	mpDevCtx->PSSetShaderResources(0, 1, &mpPointerImageSRV);
	mpDevCtx->PSSetSamplers(0, 1, &mpDownscaleSamplerState);

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = (float)mSrcW;
	vp.Height = (float)mSrcH;
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	mpDevCtx->RSSetViewports(1, &vp);
	mpDevCtx->RSSetState(mpRasterizerStateDefault);

	mpDevCtx->OMSetRenderTargets(1, &mpImageTexRTV, NULL);
	mpDevCtx->OMSetDepthStencilState(mpDepthStencilStateDefault, 0);

	if (mPointerShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
		mpDevCtx->OMSetBlendState(mpPointerBlendState, blendFactor, 0xFFFFFFFF);
		mpDevCtx->PSSetShader(mpPointerPSBlend, NULL, 0);
		mpDevCtx->Draw(4, 0);
	} else {
		mpDevCtx->OMSetBlendState(mpPointerMaskBlendState, blendFactor, 0xFFFFFFFF);
		mpDevCtx->PSSetShader(mpPointerPSMaskA1, NULL, 0);
		mpDevCtx->Draw(4, 0);

		mpDevCtx->OMSetBlendState(mpBlendStateDefault, blendFactor, 0xFFFFFFFF);
		mpDevCtx->PSSetShader(mpPointerPSMaskA0, NULL, 0);
		mpDevCtx->Draw(4, 0);
	}

	ID3D11RenderTargetView *rtvnull = 0;
	mpDevCtx->OMSetRenderTargets(1, &rtvnull, NULL);
}

bool VDScreenGrabberDXGI12::InitDisplayInternal(VDZHWND hwndParent) {
	VDASSERT(mpDevice);
	VDASSERT(!mDisplayWndProc);

	mhwndDisplayParent = hwndParent;

	mDisplayWndProc = (WNDPROC)VDCreateFunctionThunkFromMethod(this, &VDScreenGrabberDXGI12::DisplayWndProc, true);
	if (!mDisplayWndProc)
		return false;

	VDStringW className;
	className.sprintf(L"VDScreenGrabberDXGI12_%p", this);

	WNDCLASSW wc = {};
	wc.style         = 0;
	wc.lpfnWndProc   = mDisplayWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = VDGetLocalModuleHandleW32();
	wc.hIcon         = NULL;
	wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = className.c_str();

	mDisplayWndClass = RegisterClassW(&wc);

	HWND hwndDisplay = CreateWindowExW(0, (LPCWSTR)mDisplayWndClass, L"", mbDisplayVisible ? WS_CHILD | WS_VISIBLE : WS_CHILD, mDisplayArea.left, mDisplayArea.top, mDisplayArea.width(), mDisplayArea.height(), mhwndDisplayParent, NULL, wc.hInstance, NULL);
	if (!hwndDisplay)
		return false;

	mbDisplayFramePending = false;
	mhwndDisplay = hwndDisplay;

	// create input layout
	static const D3D11_INPUT_ELEMENT_DESC kInputElements[2]={
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	HRESULT hr = mpDevice->CreateInputLayout(kInputElements, 2, g_VDCapDXGI12_VS_Display, sizeof(g_VDCapDXGI12_VS_Display), &mpDisplayIL);
	if (FAILED(hr))
		return false;

	// create vertex buffer
	static const Vertex kVertices[4]={
		{-1,+1,0,0},
		{-1,-1,0,1},
		{+1,+1,1,0},
		{+1,-1,1,1}
	};

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = kVertices;

	D3D11_BUFFER_DESC bufDesc = {};
	bufDesc.ByteWidth = sizeof(kVertices);
	bufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	hr = mpDevice->CreateBuffer(&bufDesc, &initData, &mpDisplayVB);
	if (FAILED(hr))
		return false;

	// create vertex shader
	hr = mpDevice->CreateVertexShader(g_VDCapDXGI12_VS_Display, sizeof g_VDCapDXGI12_VS_Display, NULL, &mpDisplayVS);
	if (FAILED(hr))
		return false;

	// create pixel shader
	hr = mpDevice->CreatePixelShader(g_VDCapDXGI12_PS_Display, sizeof g_VDCapDXGI12_PS_Display, NULL, &mpDisplayPS);
	if (FAILED(hr))
		return false;

	// create swap chain
	DXGI_SWAP_CHAIN_DESC swapDesc = {};
	swapDesc.BufferDesc.Width = 1;
	swapDesc.BufferDesc.Height = 1;

	if (!mDisplayArea.empty()) {
		swapDesc.BufferDesc.Width = mDisplayArea.width();
		swapDesc.BufferDesc.Height = mDisplayArea.height();
	}

	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
	swapDesc.BufferCount = 2;
	swapDesc.OutputWindow = mhwndDisplay;
	swapDesc.Windowed = TRUE;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	hr = mpFactory->CreateSwapChain(mpDevice, &swapDesc, &mpDisplaySwapChain);
	if (FAILED(hr))
		return false;

	// create sampler state
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = mpDevice->CreateSamplerState(&sampDesc, &mpDisplaySamplerState);
	if (FAILED(hr))
		return false;

	D3D11_RASTERIZER_DESC rastDesc = {};
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.FrontCounterClockwise = TRUE;
    rastDesc.DepthBias = 0;
    rastDesc.DepthBiasClamp = 0;
    rastDesc.SlopeScaledDepthBias = 0;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.ScissorEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
	hr = mpDevice->CreateRasterizerState(&rastDesc, &mpDisplayRasterizerState);
	if (FAILED(hr))
		return false;

	// open shared texture
	mpDisplaySrcTex = mpDownscaleResultTex;
	mpDisplaySrcTex->AddRef();

	hr = mpDevice->CreateShaderResourceView(mpDisplaySrcTex, NULL, &mpDisplaySrcTexSRV);
	if (FAILED(hr))
		return false;

	return InitDisplayBuffers();
}

void VDScreenGrabberDXGI12::ShutdownDisplayInternal() {
	ShutdownDisplayBuffers();

	vdsaferelease <<=
		mpDisplayRTV,
		mpDisplayRT,
		mpDisplayVS,
		mpDisplayPS,
		mpDisplayRasterizerState,
		mpDisplaySamplerState,
		mpDisplaySrcTexSRV,
		mpDisplaySrcTex,
		mpDisplayVB,
		mpDisplayIL,
		mpDisplaySwapChain;

	HWND hwndDisplay;
	hwndDisplay = mhwndDisplay;
	mhwndDisplay = NULL;

	if (hwndDisplay)
		DestroyWindow(hwndDisplay);

	if (mDisplayWndClass) {
		UnregisterClassW((LPCWSTR)mDisplayWndClass, VDGetLocalModuleHandleW32());
		mDisplayWndClass = NULL;
	}

	if (mDisplayWndProc) {
		VDDestroyFunctionThunk((VDFunctionThunk *)mDisplayWndProc);
		mDisplayWndProc = NULL;
	}
}

bool VDScreenGrabberDXGI12::InitDisplayBuffers() {
	HRESULT hr;

	// get back buffer
	if (!mpDisplayRT) {
		hr = mpDisplaySwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void **)&mpDisplayRT);
		if (FAILED(hr))
			return false;
	}

	// create back buffer RTV
	if (!mpDisplayRTV) {
		hr = mpDevice->CreateRenderTargetView(mpDisplayRT, NULL, &mpDisplayRTV);
		if (FAILED(hr))
			return false;
	}

	return true;
}

void VDScreenGrabberDXGI12::ShutdownDisplayBuffers() {
	if (mpDevCtx) {
		mpDevCtx->ClearState();
		mpDevCtx->Flush();
	}

	vdsaferelease <<=
		mpDisplayRTV,
		mpDisplayRT;
}

void VDScreenGrabberDXGI12::ResizeDisplaySwapChain(int w, int h) {
	if (!mpDisplaySwapChain)
		return;

	ShutdownDisplayBuffers();

	HRESULT hr = mpDisplaySwapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	if (FAILED(hr))
		return;

	InitDisplayBuffers();
}

void VDScreenGrabberDXGI12::Display() {
	if (!mpDisplayRTV)
		return;

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = (float)mDisplayArea.width();
	vp.Height = (float)mDisplayArea.height();
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	mpDevCtx->RSSetViewports(1, &vp);
	mpDevCtx->RSSetState(mpDisplayRasterizerState);

	const float kClearColor[]={0.5f, 0.5f, 0.5f, 0};
	mpDevCtx->ClearRenderTargetView(mpDisplayRTV, kClearColor);

	mpDevCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	mpDevCtx->IASetInputLayout(mpDisplayIL);
	const UINT vbstride = sizeof(Vertex);
	const UINT vboffset = 0;
	mpDevCtx->IASetVertexBuffers(0, 1, &mpDisplayVB, &vbstride, &vboffset);

	mpDevCtx->VSSetShader(mpDisplayVS, NULL, 0);
	mpDevCtx->PSSetSamplers(0, 1, &mpDisplaySamplerState);
	mpDevCtx->PSSetShaderResources(0, 1, &mpDisplaySrcTexSRV);
	mpDevCtx->PSSetShader(mpDisplayPS, NULL, 0);

	mpDevCtx->OMSetRenderTargets(1, &mpDisplayRTV, NULL);

	mpDevCtx->Draw(4, 0);
	mpDevCtx->ClearState();

	HRESULT hr = mpDisplaySwapChain->Present(0, 0);
}

LRESULT VDScreenGrabberDXGI12::DisplayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_PAINT:
			{
				PAINTSTRUCT ps;

				HDC hdc = BeginPaint(hwnd, &ps);
				if (hdc) {
					if (mpDisplaySwapChain)
						Display();

					EndPaint(hwnd, &ps);
				}
			}
			return 0;

		case WM_SIZE:
			{
				RECT r;

				if (GetClientRect(hwnd, &r) && r.right > 0 && r.bottom > 0) {
					ResizeDisplaySwapChain(r.right, r.bottom);
					Display();
				}
			}
			return 0;

		case WM_USER + 100:
			mbDisplayFramePending = false;
			InvalidateRect(hwnd, NULL, FALSE);
			return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

IVDScreenGrabber *VDCreateScreenGrabberDXGI12() {
	return new VDScreenGrabberDXGI12;
}

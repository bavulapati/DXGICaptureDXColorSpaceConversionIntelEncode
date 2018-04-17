// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
#include "stdafx.h"
#include "DuplicationManager.h"
#include <time.h>
using namespace DirectX;

clock_t start2 = 0, duration2 = 0;
clock_t start3 = 0, duration3 = 0;

// Below are lists of errors expect from Dxgi API calls when a transition event like mode change, PnpStop, PnpStart
// desktop switch, TDR or session disconnect/reconnect. In all these cases we want the application to clean up the threads that process
// the desktop updates and attempt to recreate them.
// If we get an error that is not on the appropriate list then we exit the application

// These are the errors we expect from general Dxgi API due to a transition
HRESULT SystemTransitionsExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	DXGI_ERROR_ACCESS_LOST,
	static_cast<HRESULT>(WAIT_ABANDONED),
	S_OK                                    // Terminate list with zero valued HRESULT
};


// These are the errors we expect from IDXGIOutput1::DuplicateOutput due to a transition
HRESULT CreateDuplicationExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	static_cast<HRESULT>(E_ACCESSDENIED),
	DXGI_ERROR_UNSUPPORTED,
	DXGI_ERROR_SESSION_DISCONNECTED,
	S_OK                                    // Terminate list with zero valued HRESULT
};

// These are the errors we expect from IDXGIOutputDuplication methods due to a transition
HRESULT FrameInfoExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	DXGI_ERROR_ACCESS_LOST,
	S_OK                                    // Terminate list with zero valued HRESULT
};

// These are the errors we expect from IDXGIAdapter::EnumOutputs methods due to outputs becoming stale during a transition
HRESULT EnumOutputsExpectedErrors[] = {
	DXGI_ERROR_NOT_FOUND,
	S_OK                                    // Terminate list with zero valued HRESULT
};


//
// Constructor sets up references / variables
//
DUPLICATIONMANAGER::DUPLICATIONMANAGER() : m_DeskDupl(nullptr),
                                           m_AcquiredDesktopImage(nullptr),
										   m_CPUAccessibleLuminanceSurf(nullptr),
										   m_CPUAccessibleChrominanceSurf(nullptr),
                                           m_OutputNumber(0),
										   m_ImagePitch(0),
										   m_ScaleSrcSurf(nullptr),
										   m_TempSharedSurf(nullptr),
										   m_LuminanceSurf(nullptr),
										   m_ChrominanceSurf(nullptr),
										   m_LuminanceRTV(nullptr),
										   m_ChrominanceRTV(nullptr),
										   m_VertexShader(nullptr),
										   m_PixelShaderLuminance(nullptr),
										   m_PixelShaderChrominance(nullptr),
										   m_PixelShaderCbCr(nullptr),
										   m_VertexBuffer(nullptr),
										   m_SamplerLinear(nullptr),
										   m_NeedsResize(true),
										   m_Device(nullptr),
										   m_DeviceContext(nullptr),
										  /* GuardSurface(nullptr),*/
										   CaptureFrameAvailable(false)
{
    RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

//
// Destructor simply calls CleanRefs to destroy everything
//
DUPLICATIONMANAGER::~DUPLICATIONMANAGER()
{
	ReleaseSurfaces();

	if (m_VertexShader)
	{
		m_VertexShader->Release();
		m_VertexShader = nullptr;
	}
	if (m_PixelShaderLuminance)
	{
		m_PixelShaderLuminance->Release();
		m_PixelShaderLuminance = nullptr;
	}
	if (m_PixelShaderChrominance)
	{
		m_PixelShaderChrominance->Release();
		m_PixelShaderChrominance = nullptr;
	}
	if (m_PixelShaderCbCr)
	{
		m_PixelShaderCbCr->Release();
		m_PixelShaderCbCr = nullptr;
	}
	if (m_VertexBuffer)
	{
		m_VertexBuffer->Release();
		m_VertexBuffer = nullptr;
	}
	if (m_SamplerLinear)
	{
		m_SamplerLinear->Release();
		m_SamplerLinear = nullptr;
	}
    if (m_DeskDupl)
    {
        m_DeskDupl->Release();
        m_DeskDupl = nullptr;
    }
    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }
	if (m_LuminanceRTV)
	{
		m_LuminanceRTV->Release();
		m_LuminanceRTV = nullptr;
	}
	if (m_ChrominanceRTV)
	{
		m_ChrominanceRTV->Release();
		m_ChrominanceRTV = nullptr;
	}
    if (m_Device)
    {
		m_Device->Release();
		m_Device = nullptr;
    }
	if (m_DeviceContext)
	{
		m_DeviceContext->Release();
		m_DeviceContext = nullptr;
	}
}


//
// Indicates that screen resolution has been changed.
//
void DUPLICATIONMANAGER::SurfaceResize()
{
	m_NeedsResize = true;
}

//
// Initialize duplication interfaces
//
DUPL_RETURN DUPLICATIONMANAGER::InitDupl(_In_ FILE *log_file, UINT Output)
{
	//GuardSurface = CreateMutex(NULL, false, NULL);
	m_log_file = log_file;
	DUPL_RETURN Ret = InitializeDx(); 
	if (Ret != DUPL_RETURN_SUCCESS)
	{
		fprintf_s(log_file, "DX_RESOURCES couldn't be initialized.");
		return Ret;
	}
    m_OutputNumber = Output;

    // Get DXGI device
    IDXGIDevice* DxgiDevice = nullptr;
    HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DXGI Device", hr);
    }

    // Get DXGI adapter
    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get parent DXGI Adapter", hr, SystemTransitionsExpectedErrors);
    }

    // Get output
    IDXGIOutput* DxgiOutput = nullptr;
    hr = DxgiAdapter->EnumOutputs(Output, &DxgiOutput);
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get specified output in DUPLICATIONMANAGER", hr, EnumOutputsExpectedErrors);
    }

    DxgiOutput->GetDesc(&m_OutputDesc);

    // QI for Output 1
    IDXGIOutput1* DxgiOutput1 = nullptr;
    hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
    DxgiOutput->Release();
    DxgiOutput = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DxgiOutput1 in DUPLICATIONMANAGER", hr);
    }

    // Create desktop duplication
    hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DeskDupl);
    DxgiOutput1->Release();
    DxgiOutput1 = nullptr;
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
        {
            MessageBoxW(nullptr, L"There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.", L"Error", MB_OK);
            return DUPL_RETURN_ERROR_UNEXPECTED;
        }
        return ProcessFailure(m_Device, L"Failed to get duplicate output in DUPLICATIONMANAGER", hr, CreateDuplicationExpectedErrors);
    }

	D3D11_SAMPLER_DESC desc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
	// Create the sample state
	hr = m_Device->CreateSamplerState(&desc, &m_SamplerLinear
	);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create sampler state in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	
	return InitShaders();
}



//
// Get next frame and write it into Data
//
_Success_(*Timeout == false && return == DUPL_RETURN_SUCCESS)
DUPL_RETURN DUPLICATIONMANAGER::GetFrame()
{
    IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	start3 = clock();
    // Get new frame
    HRESULT hr = m_DeskDupl->AcquireNextFrame(500, &FrameInfo, &DesktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        return DUPL_RETURN_SUCCESS;
    }

    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to acquire next frame in DUPLICATIONMANAGER", hr, FrameInfoExpectedErrors);
    }

    // If still holding old frame, destroy it
    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    // QI for IDXGIResource
    hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_AcquiredDesktopImage));
    DesktopResource->Release();
    DesktopResource = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DUPLICATIONMANAGER", hr);
    }

	//WaitForSingleObject(GuardSurface, INFINITE);
	m_DeviceContext->CopyResource(m_TempSharedSurf, m_AcquiredDesktopImage);
	//CaptureFrameAvailable = true;
	//ReleaseMutex(GuardSurface);
	//m_ScaleSrcSurf = m_AcquiredDesktopImage;

	DoneWithFrame();

    return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN DUPLICATIONMANAGER::TransformFrame(mfxFrameSurface1 * pSurface)
{
	
	m_DeviceContext->CopyResource(m_ScaleSrcSurf, m_TempSharedSurf);
	
	DUPL_RETURN Ret = DrawNV12Frame();
	
	if (Ret != DUPL_RETURN_SUCCESS)
		return DUPL_RETURN_ERROR_UNEXPECTED;


	CopyNV12Image(pSurface);

	//duration2 += clock() - start2;

	return DUPL_RETURN_SUCCESS;
}

DUPL_RETURN DUPLICATIONMANAGER::TransformFrame(_Inout_ AVFrame* pOutFrame)
{
	m_DeviceContext->CopyResource(m_ScaleSrcSurf, m_TempSharedSurf);
	DUPL_RETURN Ret = DrawYCbCrFrame();
	
	if (Ret != DUPL_RETURN_SUCCESS)
		return DUPL_RETURN_ERROR_UNEXPECTED;

	CopyYCbCrImage(pOutFrame);

	//duration2 += clock() - start2;

	return DUPL_RETURN_SUCCESS;

}

void DUPLICATIONMANAGER::ReleaseSurfaces()
{
	if (m_ScaleSrcSurf)
	{
		m_ScaleSrcSurf->Release();
		m_ScaleSrcSurf = nullptr;
	}
	if (m_TempSharedSurf)
	{
		m_TempSharedSurf->Release();
		m_TempSharedSurf = nullptr;
	}
	if (m_LuminanceSurf)
	{
		m_LuminanceSurf->Release();
		m_LuminanceSurf = nullptr;
	}
	if (m_ChrominanceSurf)
	{
		m_ChrominanceSurf->Release();
		m_ChrominanceSurf = nullptr;
	}
	if (m_CbSurf)
	{
		m_CbSurf->Release();
		m_CbSurf = nullptr;
	}
	if (m_CrSurf)
	{
		m_CrSurf->Release();
		m_CrSurf = nullptr;
	}
	if (m_CPUAccessibleLuminanceSurf)
	{
		m_CPUAccessibleLuminanceSurf->Release();
		m_CPUAccessibleLuminanceSurf = nullptr;
	}
	if (m_CPUAccessibleChrominanceSurf)
	{
		m_CPUAccessibleChrominanceSurf->Release();
		m_CPUAccessibleChrominanceSurf = nullptr;
	}
}

//
// Resize surfaces
//
DUPL_RETURN DUPLICATIONMANAGER::ResizeNV12Surfaces()
{
	ReleaseSurfaces();

	DXGI_OUTDUPL_DESC lOutputDuplDesc;
	m_DeskDupl->GetDesc(&lOutputDuplDesc);


	// Create shared texture for all duplication threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = lOutputDuplDesc.ModeDesc.Width;
	DeskTexD.Height = lOutputDuplDesc.ModeDesc.Height;
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = lOutputDuplDesc.ModeDesc.Format;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_ScaleSrcSurf);
	if (FAILED(hr) || !m_ScaleSrcSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create shared texture", hr);
	}
	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_TempSharedSurf);
	if (FAILED(hr) || !m_TempSharedSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create temp texture", hr);
	}

	DeskTexD.Format = DXGI_FORMAT_R8_UNORM; 
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_LuminanceSurf);
	if (FAILED(hr) || !m_LuminanceSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create luminance texture", hr);
	}

	DeskTexD.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	DeskTexD.Usage = D3D11_USAGE_STAGING;
	DeskTexD.BindFlags = 0;

	hr = m_Device->CreateTexture2D(&DeskTexD, NULL, &m_CPUAccessibleLuminanceSurf);

	if (FAILED(hr) || m_CPUAccessibleLuminanceSurf == nullptr)
	{
		ProcessFailure(nullptr, L"Creating cpu accessable luminance texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

	SetViewPort(&m_VPLuminance, DeskTexD.Width, DeskTexD.Height);
	DUPL_RETURN Ret = MakeRTV(&m_LuminanceRTV, m_LuminanceSurf);
	if(Ret != DUPL_RETURN_SUCCESS)
		return Ret;

	DeskTexD.Width = lOutputDuplDesc.ModeDesc.Width / 2;
	DeskTexD.Height = lOutputDuplDesc.ModeDesc.Height / 2;
	DeskTexD.Format = DXGI_FORMAT_R8G8_UNORM; 
	
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_ChrominanceSurf);
	if (FAILED(hr) || !m_ChrominanceSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create chrominance texture", hr);
	}

	DeskTexD.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	DeskTexD.Usage = D3D11_USAGE_STAGING;
	DeskTexD.BindFlags = 0;

	hr = m_Device->CreateTexture2D(&DeskTexD, NULL, &m_CPUAccessibleChrominanceSurf);

	if (FAILED(hr) || m_CPUAccessibleChrominanceSurf == nullptr)
	{
		ProcessFailure(nullptr, L"Creating cpu accessable chrominance texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

	SetViewPort(&m_VPChrominance, DeskTexD.Width, DeskTexD.Height);
	return MakeRTV(&m_ChrominanceRTV, m_ChrominanceSurf);
}

//
// Resize surfaces
//
DUPL_RETURN DUPLICATIONMANAGER::ResizeYCbCrSurfaces()
{
	ReleaseSurfaces();

	DXGI_OUTDUPL_DESC lOutputDuplDesc;
	m_DeskDupl->GetDesc(&lOutputDuplDesc);


	// Create shared texture for all duplication threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = lOutputDuplDesc.ModeDesc.Width;
	DeskTexD.Height = lOutputDuplDesc.ModeDesc.Height;
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = lOutputDuplDesc.ModeDesc.Format;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_ScaleSrcSurf);
	if (FAILED(hr) || !m_ScaleSrcSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create shared texture", hr);
	}
	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_TempSharedSurf);
	if (FAILED(hr) || !m_TempSharedSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create temp texture", hr);
	}

	DeskTexD.Format = DXGI_FORMAT_R8_UNORM; 
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_LuminanceSurf);
	if (FAILED(hr) || !m_LuminanceSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create luminance texture", hr);
	}

	DeskTexD.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	DeskTexD.Usage = D3D11_USAGE_STAGING;
	DeskTexD.BindFlags = 0;

	hr = m_Device->CreateTexture2D(&DeskTexD, NULL, &m_CPUAccessibleLuminanceSurf);

	if (FAILED(hr) || m_CPUAccessibleLuminanceSurf == nullptr)
	{
		ProcessFailure(nullptr, L"Creating cpu accessable luminance texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

	SetViewPort(&m_VPLuminance, DeskTexD.Width, DeskTexD.Height);
	DUPL_RETURN Ret = MakeRTV(&m_LuminanceRTV, m_LuminanceSurf);
	if(Ret != DUPL_RETURN_SUCCESS)
		return Ret;
	
	DeskTexD.Width = lOutputDuplDesc.ModeDesc.Width / 2;
	DeskTexD.Height = lOutputDuplDesc.ModeDesc.Height / 2;
	
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET;

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_CbSurf);
	if (FAILED(hr) || !m_CbSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create cb texture", hr);
	}

	hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_CrSurf);
	if (FAILED(hr) || !m_CrSurf)
	{
		return ProcessFailure(m_Device, L"Failed to create cr texture", hr);
	}

	DeskTexD.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	DeskTexD.Usage = D3D11_USAGE_STAGING;
	DeskTexD.BindFlags = 0;

	hr = m_Device->CreateTexture2D(&DeskTexD, NULL, &m_CPUAccessibleChrominanceSurf);

	if (FAILED(hr) || m_CPUAccessibleChrominanceSurf == nullptr)
	{
		ProcessFailure(nullptr, L"Creating cpu accessable chrominance texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

	SetViewPort(&m_VPChrominance, DeskTexD.Width, DeskTexD.Height);
	Ret = MakeRTV(&m_CbRTV, m_CbSurf);
	if(Ret != DUPL_RETURN_SUCCESS)
		return Ret;
	return MakeRTV(&m_CrRTV, m_CrSurf);
}

DUPL_RETURN DUPLICATIONMANAGER::MakeRTV(ID3D11RenderTargetView** pRTV, ID3D11Texture2D* pSurf)
{
	if(*pRTV)
	{
		(*pRTV)->Release();
		*pRTV = nullptr;
	}
	// Create a render target view
	HRESULT hr = m_Device->CreateRenderTargetView(pSurf, nullptr, pRTV);

	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create render target view in OUTPUTMANAGER", hr);
	}

	return DUPL_RETURN_SUCCESS;
}

void DUPLICATIONMANAGER::SetViewPort(D3D11_VIEWPORT* VP, UINT Width, UINT Height)
{
	VP->Width = static_cast<FLOAT>(Width);
	VP->Height = static_cast<FLOAT>(Height);
	VP->MinDepth = 0.0f;
	VP->MaxDepth = 1.0f;
	VP->TopLeftX = 0;
	VP->TopLeftY = 0;
}

DUPL_RETURN DUPLICATIONMANAGER::InitShaders()
{
	HRESULT hr;

	UINT Size = ARRAYSIZE(g_VS);
	hr = m_Device->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create vertex shader in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}

	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);

	// Vertices for drawing whole texture
	VERTEX Vertices[NUMVERTICES] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;

	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	// Create vertex buffer
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &m_VertexBuffer);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create vertex buffer when drawing a frame", hr, SystemTransitionsExpectedErrors);
	}
	m_DeviceContext->IASetVertexBuffers(0, 1, &m_VertexBuffer, &Stride, &Offset);

	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	UINT NumElements = ARRAYSIZE(Layout);
	hr = m_Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create input layout in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}
	m_DeviceContext->IASetInputLayout(m_InputLayout);

	Size = ARRAYSIZE(g_PS_Y);
	hr = m_Device->CreatePixelShader(g_PS_Y, Size, nullptr, &m_PixelShaderLuminance);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create pixel shader in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}

	Size = ARRAYSIZE(g_PS_CbCr);
	hr = m_Device->CreatePixelShader(g_PS_CbCr, Size, nullptr, &m_PixelShaderCbCr);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create pixel CbCr in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}

	Size = ARRAYSIZE(g_PS_UV);
	hr = m_Device->CreatePixelShader(g_PS_UV, Size, nullptr, &m_PixelShaderChrominance);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create pixel UV in OUTPUTMANAGER", hr, SystemTransitionsExpectedErrors);
	}

	return DUPL_RETURN_SUCCESS;
}

//
// Draw frame for NV12 texture
//
DUPL_RETURN DUPLICATIONMANAGER::DrawNV12Frame()
{
	HRESULT hr;

	// If window was resized, resize swapchain
	if (m_NeedsResize)
	{
		DUPL_RETURN Ret = ResizeNV12Surfaces();
		if (Ret != DUPL_RETURN_SUCCESS)
		{
			return Ret;
		}
		m_NeedsResize = false;
	}

	D3D11_TEXTURE2D_DESC FrameDesc;
	m_ScaleSrcSurf->GetDesc(&FrameDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = FrameDesc.Format;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = FrameDesc.MipLevels - 1;
	ShaderDesc.Texture2D.MipLevels = FrameDesc.MipLevels;

	// Create new shader resource view
	ID3D11ShaderResourceView* ShaderResource = nullptr;
	hr = m_Device->CreateShaderResourceView(m_ScaleSrcSurf, &ShaderDesc, &ShaderResource);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame", hr);
	}
	m_DeviceContext->PSSetShaderResources(0, 1, &ShaderResource);

	// Set resources
	m_DeviceContext->OMSetRenderTargets(1, &m_LuminanceRTV, nullptr);
	m_DeviceContext->PSSetShader(m_PixelShaderLuminance, nullptr, 0);
	m_DeviceContext->RSSetViewports(1, &m_VPLuminance);

	// Draw textured quad onto render target
	m_DeviceContext->Draw(NUMVERTICES, 0);

	m_DeviceContext->OMSetRenderTargets(1, &m_ChrominanceRTV, nullptr);
	m_DeviceContext->PSSetShader(m_PixelShaderChrominance, nullptr, 0);
	m_DeviceContext->RSSetViewports(1, &m_VPChrominance);

	// Draw textured quad onto render target
	m_DeviceContext->Draw(NUMVERTICES, 0);

	// Release shader resource
	ShaderResource->Release();
	ShaderResource = nullptr;

	return DUPL_RETURN_SUCCESS;
}

//
// Draw frame for NV12 texture
//
DUPL_RETURN DUPLICATIONMANAGER::DrawYCbCrFrame()
{
	HRESULT hr;

	// If window was resized, resize swapchain
	if (m_NeedsResize)
	{
		DUPL_RETURN Ret = ResizeYCbCrSurfaces();
		if (Ret != DUPL_RETURN_SUCCESS)
		{
			return Ret;
		}
		m_NeedsResize = false;
	}

	D3D11_TEXTURE2D_DESC FrameDesc;
	m_ScaleSrcSurf->GetDesc(&FrameDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = FrameDesc.Format;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = FrameDesc.MipLevels - 1;
	ShaderDesc.Texture2D.MipLevels = FrameDesc.MipLevels;

	// Create new shader resource view
	ID3D11ShaderResourceView* ShaderResource = nullptr;
	hr = m_Device->CreateShaderResourceView(m_ScaleSrcSurf, &ShaderDesc, &ShaderResource);
	if (FAILED(hr))
	{
		return ProcessFailure(m_Device, L"Failed to create shader resource when drawing a frame", hr);
	}
	m_DeviceContext->PSSetShaderResources(0, 1, &ShaderResource);
	
	// Set resources
	m_DeviceContext->OMSetRenderTargets(1, &m_LuminanceRTV, nullptr);
	m_DeviceContext->PSSetShader(m_PixelShaderLuminance, nullptr, 0);
	m_DeviceContext->RSSetViewports(1, &m_VPLuminance);

	// Draw textured quad onto Y render target
	m_DeviceContext->Draw(NUMVERTICES, 0);

	ID3D11RenderTargetView* pRenderTargetViews[2] = {m_CbRTV, m_CrRTV};

	m_DeviceContext->OMSetRenderTargets(2, pRenderTargetViews, nullptr);
	m_DeviceContext->PSSetShader(m_PixelShaderCbCr, nullptr, 0);
	m_DeviceContext->RSSetViewports(1, &m_VPChrominance);

	// Draw textured quad onto Cb, Cr render targets
	m_DeviceContext->Draw(NUMVERTICES, 0);

	// Release shader resource
	ShaderResource->Release();
	ShaderResource = nullptr;

	return DUPL_RETURN_SUCCESS;
}

char buf[100];
int i = 0;
//bool yes = true;
clock_t start1 = 0, stop1 = 0, duration1 = 0;

void  DUPLICATIONMANAGER::CopyNV12Image(mfxFrameSurface1* pSurface)
{
	m_DeviceContext->CopyResource(m_CPUAccessibleLuminanceSurf, m_LuminanceSurf);
	D3D11_MAPPED_SUBRESOURCE resource;
	UINT subresource = D3D11CalcSubresource(0, 0, 0);

	HRESULT hr = m_DeviceContext->Map(m_CPUAccessibleLuminanceSurf, subresource, D3D11_MAP_READ, 0, &resource);

	BYTE* sptr = reinterpret_cast<BYTE*>(resource.pData);
	BYTE* dptr = pSurface->Data.Y;

	//Store Image Pitch
	m_ImagePitch = resource.RowPitch < pSurface->Data.Pitch ? resource.RowPitch : pSurface->Data.Pitch;


	int height = GetImageHeight();
	int width = GetImageWidth();

	duration1 = 0;
	start1 = clock();

	for (int i = 0; i < height; i++)
	{
		if (memcpy_s(dptr, m_ImagePitch, sptr, m_ImagePitch))
		{
			fprintf_s(m_log_file, "There is a problem transferring the luminance data to NV12 texture.\n");
		}
		sptr += resource.RowPitch;
		dptr += pSurface->Data.Pitch;
	}
	m_DeviceContext->Unmap(m_CPUAccessibleLuminanceSurf, subresource);

	m_DeviceContext->CopyResource(m_CPUAccessibleChrominanceSurf, m_ChrominanceSurf);
	hr = m_DeviceContext->Map(m_CPUAccessibleChrominanceSurf, subresource, D3D11_MAP_READ, 0, &resource);
	
	sptr = reinterpret_cast<BYTE*>(resource.pData);
	dptr = pSurface->Data.UV;
	
	m_ImagePitch = resource.RowPitch < pSurface->Data.Pitch ? resource.RowPitch : pSurface->Data.Pitch;
	height /= 2;
	width /= 2;

	for (int i = 0; i < height; i++)
	{
		if (memcpy_s(dptr, m_ImagePitch, sptr, m_ImagePitch))
		{
			fprintf_s(m_log_file, "There is a problem transferring the chrominance data to NV12 texture.\n");
		}
		sptr += resource.RowPitch;
		dptr += pSurface->Data.Pitch;
	}

	m_DeviceContext->Unmap(m_CPUAccessibleChrominanceSurf, subresource);

	stop1 = clock();
	duration1 += stop1 - start1;
	//fprintf_s(m_log_file, "time for copying - %ld\n", duration1);
	//yes = false;
}

void DUPLICATIONMANAGER::CopyYCbCrImage(_Inout_ AVFrame* pOutFrame)
{
	duration1 = 0;
	start1 = clock();

	m_DeviceContext->CopyResource(m_CPUAccessibleLuminanceSurf, m_LuminanceSurf);
	D3D11_MAPPED_SUBRESOURCE resource;
	UINT subresource = D3D11CalcSubresource(0, 0, 0);

	HRESULT hr = m_DeviceContext->Map(m_CPUAccessibleLuminanceSurf, subresource, D3D11_MAP_READ, 0, &resource);

	BYTE* sptr = reinterpret_cast<BYTE*>(resource.pData);
	BYTE* dptr = pOutFrame->data[0];
	pOutFrame->linesize[0] = resource.RowPitch;

	if (memcpy_s(dptr, resource.DepthPitch, sptr, resource.DepthPitch))
	{
		fprintf_s(m_log_file, "There is a problem transferring the luminance data to YUV texture.\n");
	}

	m_DeviceContext->Unmap(m_CPUAccessibleLuminanceSurf, subresource);

	m_DeviceContext->CopyResource(m_CPUAccessibleChrominanceSurf, m_CbSurf);
	hr = m_DeviceContext->Map(m_CPUAccessibleChrominanceSurf, subresource, D3D11_MAP_READ, 0, &resource);

	sptr = reinterpret_cast<BYTE*>(resource.pData);
	dptr = pOutFrame->data[1];
	pOutFrame->linesize[1] = resource.RowPitch;

	if (memcpy_s(dptr, resource.DepthPitch, sptr, resource.DepthPitch))
	{
		fprintf_s(m_log_file, "There is a problem transferring the Cb data to YUV texture.\n");
	}
	
	m_DeviceContext->Unmap(m_CPUAccessibleChrominanceSurf, subresource);

	m_DeviceContext->CopyResource(m_CPUAccessibleChrominanceSurf, m_CrSurf);

	hr = m_DeviceContext->Map(m_CPUAccessibleChrominanceSurf, subresource, D3D11_MAP_READ, 0, &resource);

	sptr = reinterpret_cast<BYTE*>(resource.pData);
	dptr = pOutFrame->data[2];
	pOutFrame->linesize[2] = resource.RowPitch;

	if (memcpy_s(dptr, resource.DepthPitch, sptr, resource.DepthPitch))
	{
		fprintf_s(m_log_file, "There is a problem transferring the Cr data to YUV texture.\n");
	}

	m_DeviceContext->Unmap(m_CPUAccessibleChrominanceSurf, subresource);

	stop1 = clock();
	duration1 += stop1 - start1;
}

int DUPLICATIONMANAGER::GetImageHeight()
{
	return m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;
}


int DUPLICATIONMANAGER::GetImageWidth()
{
	return m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
}


int DUPLICATIONMANAGER::GetImagePitch()
{
	return m_ImagePitch;
}

void DUPLICATIONMANAGER::PrintTimings()
{
	int numFrames = 1500;
	double elapsed2 = duration2 / 1000.0;
	double elapsed3 = duration3 / 1000.0;
	double fps2 = numFrames / elapsed2;
	double fps3 = numFrames / elapsed3;
	fprintf_s(m_log_file, "The avg time for capturing screen in bgra color space %3.2f s & %3.2f fps\n", elapsed3, fps3);
	fprintf_s(m_log_file, "The avg time for rgb to yuv color space conversion %3.2f s & %3.2f fps\n", elapsed2, fps2);
}

//
// Release frame
//
DUPL_RETURN DUPLICATIONMANAGER::DoneWithFrame()
{
    HRESULT hr = m_DeskDupl->ReleaseFrame();
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to release frame in DUPLICATIONMANAGER", hr, FrameInfoExpectedErrors);
    }

    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    return DUPL_RETURN_SUCCESS;
}

//
// Gets output desc into DescPtr
//
void DUPLICATIONMANAGER::GetOutputDesc(_Out_ DXGI_OUTPUT_DESC* DescPtr)
{
    *DescPtr = m_OutputDesc;
}

_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN DUPLICATIONMANAGER::ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors)
{
	HRESULT TranslatedHr;

	// On an error check if the DX device is lost
	if (Device)
	{
		HRESULT DeviceRemovedReason = Device->GetDeviceRemovedReason();

		switch (DeviceRemovedReason)
		{
		case DXGI_ERROR_DEVICE_REMOVED:
		case DXGI_ERROR_DEVICE_RESET:
		case static_cast<HRESULT>(E_OUTOFMEMORY) :
		{
			// Our device has been stopped due to an external event on the GPU so map them all to
			// device removed and continue processing the condition
			TranslatedHr = DXGI_ERROR_DEVICE_REMOVED;
			break;
		}

		case S_OK:
		{
			// Device is not removed so use original error
			TranslatedHr = hr;
			break;
		}

		default:
		{
			// Device is removed but not a error we want to remap
			TranslatedHr = DeviceRemovedReason;
		}
		}
	}
	else
	{
		TranslatedHr = hr;
	}

	// Check if this error was expected or not
	if (ExpectedErrors)
	{
		HRESULT* CurrentResult = ExpectedErrors;

		while (*CurrentResult != S_OK)
		{
			if (*(CurrentResult++) == TranslatedHr)
			{
				return DUPL_RETURN_ERROR_EXPECTED;
			}
		}
	}

	// Error was not expected so display the message box
	DisplayMsg(Str, TranslatedHr);

	return DUPL_RETURN_ERROR_UNEXPECTED;
}

//
// Displays a message
//
void DUPLICATIONMANAGER::DisplayMsg(_In_ LPCWSTR Str, HRESULT hr)
{
	if (SUCCEEDED(hr))
	{
		fprintf_s(m_log_file, "%ls\n", Str);
		return;
	}

	const UINT StringLen = (UINT)(wcslen(Str) + sizeof(" with HRESULT 0x########."));
	wchar_t* OutStr = new wchar_t[StringLen];
	if (!OutStr)
	{
		return;
	}

	INT LenWritten = swprintf_s(OutStr, StringLen, L"%s with 0x%X.", Str, hr);
	if (LenWritten != -1)
	{
		fprintf_s(m_log_file, "%ls\n", OutStr);
	}

	delete[] OutStr;
}

//
// Get DX_RESOURCES
//
DUPL_RETURN DUPLICATIONMANAGER::InitializeDx()
{

	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, &m_Device, &FeatureLevel, &m_DeviceContext);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr))
	{

		return ProcessFailure(nullptr, L"Failed to create device in InitializeDx", hr);
	}

	fprintf_s(m_log_file, "The feature level - %04x\n", FeatureLevel);

	return DUPL_RETURN_SUCCESS;
}
#pragma once
#include <pch.h>

#include "Win32_DirectXAppUtil.h"
#include <OVR_CAPI_D3D.h>

/* Status enumeration */
#define R_E_INIT_FAILED 0x00010001 // Init failed
#define R_E_NOT_STARTED 0x00010002 // Disconnected (initial)

class GuardianSystem
{
public:
    GuardianSystem(HRESULT& result, std::function<void(std::wstring, int32_t)>& log) :
        m_result(result), Log(log)
    {
    }

    void start_ovr()
    {
        __try
        {
            [&, this]
            {
                ovrResult result = ovr_Initialize(nullptr);
                if (!OVR_SUCCESS(result))
                    Log(L"ovr_Initialize failed", 2);

                ovrGraphicsLuid luid;
                result = ovr_Create(&mSession, &luid);
                if (!OVR_SUCCESS(result))
                    Log(L"ovr_Create failed", 2);

                if (!DIRECTX.InitWindow(nullptr, L"GuardianSystemDemo"))
                    Log(L"DIRECTX.InitWindow failed", 2);

                // Use HMD desc to initialize device
                ovrHmdDesc hmdDesc = ovr_GetHmdDesc(mSession);
                if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2,
                                        hmdDesc.Resolution.h / 2,
                                        reinterpret_cast<LUID*>(&luid)))
                    Log(L"DIRECTX.InitDevice failed", 2);

                // Use FloorLevel tracking origin
                ovr_SetTrackingOriginType(mSession, ovrTrackingOrigin_FloorLevel);

                InitRenderTargets(hmdDesc);
                vrObjects = (ovr_GetConnectedControllerTypes(mSession) >> 8) & 0xf;

                // Main Loop
                Render();
            }();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            [&, this]
            {
                Log(L"OVR initialization failure!", 2);
                m_result = R_E_INIT_FAILED;
            }();
        }
    }

    void InitRenderTargets(const ovrHmdDesc& hmdDesc)
    {
        // For each eye
        for (int i = 0; i < ovrEye_Count; ++i)
        {
            // Viewport
            const ovrSizei idealSize = ovr_GetFovTextureSize(
                mSession, static_cast<ovrEyeType>(i),
                hmdDesc.DefaultEyeFov[i], 1.0f);

            mEyeRenderViewport[i] = {
                0, 0, idealSize.w, idealSize.h
            };

            // Create Swap Chain
            ovrTextureSwapChainDesc desc = {
                ovrTexture_2D, OVR_FORMAT_R8G8B8A8_UNORM_SRGB, 1,
                idealSize.w, idealSize.h, 1, 1,
                ovrFalse, ovrTextureMisc_DX_Typeless, ovrTextureBind_DX_RenderTarget
            };

            // Configure Eye render layers
            mEyeRenderLayer.Header.Type = ovrLayerType_EyeFov;
            mEyeRenderLayer.Viewport[i] = mEyeRenderViewport[i];
            mEyeRenderLayer.Fov[i] = hmdDesc.DefaultEyeFov[i];
            mHmdToEyePose[i] = ovr_GetRenderDesc(
                mSession, static_cast<ovrEyeType>(i),
                hmdDesc.DefaultEyeFov[i]).HmdToEyePose;

            // DirectX 11 - Generate RenderTargetView from textures in swap chain
            // ----------------------------------------------------------------------
            ovrResult result = ovr_CreateTextureSwapChainDX(
                mSession, DIRECTX.Device, &desc, &mTextureChain[i]);

            if (!OVR_SUCCESS(result))
                Log(L"ovr_CreateTextureSwapChainDX failed", 2);

            // Render Target, normally triple-buffered
            int textureCount = 0;
            ovr_GetTextureSwapChainLength(mSession, mTextureChain[i], &textureCount);
            for (int j = 0; j < textureCount; ++j)
            {
                ID3D11Texture2D* renderTexture = nullptr;
                ovr_GetTextureSwapChainBufferDX(mSession, mTextureChain[i], j, IID_PPV_ARGS(&renderTexture));

                D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {
                    DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D
                };

                ID3D11RenderTargetView* renderTargetView = nullptr;
                DIRECTX.Device->CreateRenderTargetView(renderTexture,
                                                       &renderTargetViewDesc, &renderTargetView);
                mEyeRenderTargets[i].push_back(renderTargetView);
                renderTexture->Release();
            }

            // DirectX 11 - Generate Depth
            // ----------------------------------------------------------------------
            D3D11_TEXTURE2D_DESC depthTextureDesc = {
                static_cast<UINT>(idealSize.w), static_cast<UINT>(idealSize.h), 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0},
                D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0
            };

            ID3D11Texture2D* depthTexture = nullptr;
            DIRECTX.Device->CreateTexture2D(&depthTextureDesc, nullptr, &depthTexture);
            DIRECTX.Device->CreateDepthStencilView(depthTexture, nullptr, &mEyeDepthTarget[i]);
            depthTexture->Release();
        }
    }

    void Render()
    {
        // Get current eye pose for rendering
        double eyePoseTime = 0;
        ovrPosef eyePose[ovrEye_Count] = {};
        ovr_GetEyePoses(mSession, mFrameIndex, ovrTrue, mHmdToEyePose, eyePose, &eyePoseTime);

        // Render each eye
        for (int i = 0; i < ovrEye_Count; ++i)
        {
            int renderTargetIndex = 0;
            ovr_GetTextureSwapChainCurrentIndex(mSession, mTextureChain[i], &renderTargetIndex);
            ID3D11RenderTargetView* renderTargetView = mEyeRenderTargets[i][renderTargetIndex];
            ID3D11DepthStencilView* depthTargetView = mEyeDepthTarget[i];

            // Clear and set render/depth target and viewport
            DIRECTX.SetAndClearRenderTarget(renderTargetView, depthTargetView, 0.0f, 0.0f, 0.0f, 1.0f);
            // THE SCREEN RENDER COLOUR
            DIRECTX.SetViewport(static_cast<float>(mEyeRenderViewport[i].Pos.x),
                                static_cast<float>(mEyeRenderViewport[i].Pos.y),
                                static_cast<float>(mEyeRenderViewport[i].Size.w),
                                static_cast<float>(mEyeRenderViewport[i].Size.h));

            // Render and commit to swap chain
            ovr_CommitTextureSwapChain(mSession, mTextureChain[i]);

            // Update eye layer
            mEyeRenderLayer.ColorTexture[i] = mTextureChain[i];
            mEyeRenderLayer.RenderPose[i] = eyePose[i];
            mEyeRenderLayer.SensorSampleTime = eyePoseTime;
        }

        // Submit frames
        ovrLayerHeader* layers = &mEyeRenderLayer.Header;
        ovrResult result = ovr_SubmitFrame(mSession, mFrameIndex++, nullptr, &layers, 1);

        if (!OVR_SUCCESS(result))
            Log(L"ovr_SubmitFrame failed", 2);
    }

    DirectX11 DIRECTX;
    uint32_t vrObjects;
    ovrSession mSession = nullptr;

private:
    HRESULT& m_result;

    uint32_t mFrameIndex = 0; // Global frame counter
    ovrPosef mHmdToEyePose[ovrEye_Count] = {}; // Offset from the center of the HMD to each eye
    ovrRecti mEyeRenderViewport[ovrEye_Count] = {}; // Eye render target viewport

    ovrLayerEyeFov mEyeRenderLayer = {}; // OVR  - Eye render layers description
    ovrTextureSwapChain mTextureChain[ovrEye_Count] = {}; // OVR  - Eye render target swap chain
    ID3D11DepthStencilView* mEyeDepthTarget[ovrEye_Count] = {}; // DX11 - Eye depth view
    std::vector<ID3D11RenderTargetView*> mEyeRenderTargets[ovrEye_Count]; // DX11 - Eye render view

    bool mShouldQuit = false;

    // From parent for logging
    std::function<void(std::wstring, int32_t)>& Log;
};

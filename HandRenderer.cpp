// This is all stolen from https://github.com/microsoft/OpenXR-MixedReality/blob/main/samples/BasicXrApp/CubeGraphics.cpp
//
// Original copyright and license:
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "HandRenderer.h"

namespace CubeShader {
    struct Vertex {
        XrVector3f Position;
        XrVector3f Color;
    };

    // https://www.schemecolor.com/real-skin-tones-color-palette.php
    constexpr XrVector3f Bright1{ 255 / 255.f, 219 / 255.f, 172 / 255.f };
    constexpr XrVector3f Bright2{ 255 / 255.f, 219 / 255.f, 172 / 255.f };
    constexpr XrVector3f Medium1{ 224 / 255.f, 172 / 255.f, 105 / 255.f };
    constexpr XrVector3f Medium2{ 224 / 255.f, 172 / 255.f, 105 / 255.f };
    constexpr XrVector3f Dark1{ 141 / 255.f, 85 / 255.f, 36 / 255.f };
    constexpr XrVector3f Dark2{ 141 / 255.f, 85 / 255.f, 36 / 255.f };
    constexpr XrVector3f Darker1{ 77 / 255.f, 42 / 255.f, 34 / 255.f };
    constexpr XrVector3f Darker2{ 77 / 255.f, 42 / 255.f, 34 / 255.f };

    // Vertices for a 1x1x1 meter cube. (Left/Right, Top/Bottom, Front/Back)
    constexpr XrVector3f LBB{ -0.5f, -0.5f, -0.5f };
    constexpr XrVector3f LBF{ -0.5f, -0.5f, 0.5f };
    constexpr XrVector3f LTB{ -0.5f, 0.5f, -0.5f };
    constexpr XrVector3f LTF{ -0.5f, 0.5f, 0.5f };
    constexpr XrVector3f RBB{ 0.5f, -0.5f, -0.5f };
    constexpr XrVector3f RBF{ 0.5f, -0.5f, 0.5f };
    constexpr XrVector3f RTB{ 0.5f, 0.5f, -0.5f };
    constexpr XrVector3f RTF{ 0.5f, 0.5f, 0.5f };

#define CUBE_SIDE(V1, V2, V3, V4, V5, V6, COLOR) {V1, COLOR}, {V2, COLOR}, {V3, COLOR}, {V4, COLOR}, {V5, COLOR}, {V6, COLOR},

    constexpr Vertex c_cubeVerticesBright[] = {
        CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, Bright1)  // -X
        CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Bright1)  // +X
        CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, Bright2)  // -Y
        CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Bright2)  // +Y
        CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, Bright1)  // -Z
        CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Bright2)  // +Z
    };

    constexpr Vertex c_cubeVerticesMedium[] = {
        CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, Medium1)  // -X
        CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Medium1)  // +X
        CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, Medium2)  // -Y
        CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Medium2)  // +Y
        CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, Medium1)  // -Z
        CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Medium2)  // +Z
    };

    constexpr Vertex c_cubeVerticesDark[] = {
        CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, Dark1)  // -X
        CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Dark1)  // +X
        CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, Dark2)  // -Y
        CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Dark2)  // +Y
        CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, Dark1)  // -Z
        CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Dark2)  // +Z
    };

    constexpr Vertex c_cubeVerticesDarker[] = {
        CUBE_SIDE(LTB, LBF, LBB, LTB, LTF, LBF, Darker1)  // -X
        CUBE_SIDE(RTB, RBB, RBF, RTB, RBF, RTF, Darker1)  // +X
        CUBE_SIDE(LBB, LBF, RBF, LBB, RBF, RBB, Darker2)  // -Y
        CUBE_SIDE(LTB, RTB, RTF, LTB, RTF, LTF, Darker2)  // +Y
        CUBE_SIDE(LBB, RBB, RTB, LBB, RTB, LTB, Darker1)  // -Z
        CUBE_SIDE(LBF, LTF, RTF, LBF, RTF, RBF, Darker2)  // +Z
    };

    // Winding order is clockwise. Each side uses a different color.
    constexpr unsigned short c_cubeIndices[] = {
        0,  1,  2,  3,  4,  5,  // -X
        6,  7,  8,  9,  10, 11, // +X
        12, 13, 14, 15, 16, 17, // -Y
        18, 19, 20, 21, 22, 23, // +Y
        24, 25, 26, 27, 28, 29, // -Z
        30, 31, 32, 33, 34, 35, // +Z
    };

    struct ModelConstantBuffer {
        DirectX::XMFLOAT4X4 Model;
    };

    struct ViewProjectionConstantBuffer {
        DirectX::XMFLOAT4X4 ViewProjection[2];
    };

    constexpr uint32_t MaxViewInstance = 2;

    // Separate entrypoints for the vertex and pixel shader functions.
    constexpr char ShaderHlsl[] = R"_(
            struct VSOutput {
                float4 Pos : SV_POSITION;
                float3 Color : COLOR0;
                uint viewId : SV_RenderTargetArrayIndex;
            };
            struct VSInput {
                float3 Pos : POSITION;
                float3 Color : COLOR0;
                uint instId : SV_InstanceID;
            };
            cbuffer ModelConstantBuffer : register(b0) {
                float4x4 Model;
            };
            cbuffer ViewProjectionConstantBuffer : register(b1) {
                float4x4 ViewProjection[2];
            };

            VSOutput MainVS(VSInput input) {
                VSOutput output;
                output.Pos = mul(mul(float4(input.Pos, 1), Model), ViewProjection[input.instId]);
                output.Color = input.Color;
                output.viewId = input.instId;
                return output;
            }

            float4 MainPS(VSOutput input) : SV_TARGET {
                return float4(input.Color, 1);
            }
            )_";

    ComPtr<ID3DBlob> CompileShader(const char* hlsl, const char* entrypoint, const char* shaderTarget) {
        ComPtr<ID3DBlob> compiled;
        ComPtr<ID3DBlob> errMsgs;
        DWORD flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;

#ifdef _DEBUG
        flags |= D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        HRESULT hr =
            D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, entrypoint, shaderTarget, flags, 0, compiled.GetAddressOf(), errMsgs.GetAddressOf());
        if (FAILED(hr)) {
            std::string errMsg((const char*)errMsgs->GetBufferPointer(), errMsgs->GetBufferSize());
            CHECK_HRESULT(hr, "D3DCompile failed");
        }

        return compiled;
    }

} // namespace CubeShader

void HandRenderer::SetDevice(ComPtr<ID3D11Device> device)
{
	m_device = device;
    if (!device)
    {
        m_deviceContext = nullptr;
        m_vertexShader = nullptr;
        m_pixelShader = nullptr;
        m_modelCBuffer = nullptr;
        m_viewProjectionCBuffer = nullptr;
        m_inputLayout = nullptr;
        m_cubeVertexBufferBright = nullptr;
        m_cubeVertexBufferMedium = nullptr;
        m_cubeVertexBufferDark = nullptr;
        m_cubeVertexBufferDarker = nullptr;
        m_cubeVertexBuffer = nullptr;
        m_cubeIndexBuffer = nullptr;
        m_reversedZDepthNoStencilTest = nullptr;
        return;
    }

	m_device->GetImmediateContext(m_deviceContext.ReleaseAndGetAddressOf());

	// Create resources necessary for rendering.
    const ComPtr<ID3DBlob> vertexShaderBytes = CubeShader::CompileShader(CubeShader::ShaderHlsl, "MainVS", "vs_5_0");
    CHECK_HRCMD(m_device->CreateVertexShader(
        vertexShaderBytes->GetBufferPointer(), vertexShaderBytes->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()));

    const ComPtr<ID3DBlob> pixelShaderBytes = CubeShader::CompileShader(CubeShader::ShaderHlsl, "MainPS", "ps_5_0");
    CHECK_HRCMD(m_device->CreatePixelShader(
        pixelShaderBytes->GetBufferPointer(), pixelShaderBytes->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()));

    const D3D11_INPUT_ELEMENT_DESC vertexDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    CHECK_HRCMD(m_device->CreateInputLayout(vertexDesc,
        (UINT)std::size(vertexDesc),
        vertexShaderBytes->GetBufferPointer(),
        vertexShaderBytes->GetBufferSize(),
        m_inputLayout.ReleaseAndGetAddressOf()));

    const CD3D11_BUFFER_DESC modelConstantBufferDesc(sizeof(CubeShader::ModelConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
    CHECK_HRCMD(m_device->CreateBuffer(&modelConstantBufferDesc, nullptr, m_modelCBuffer.ReleaseAndGetAddressOf()));

    const CD3D11_BUFFER_DESC viewProjectionConstantBufferDesc(sizeof(CubeShader::ViewProjectionConstantBuffer),
        D3D11_BIND_CONSTANT_BUFFER);
    CHECK_HRCMD(m_device->CreateBuffer(&viewProjectionConstantBufferDesc, nullptr, m_viewProjectionCBuffer.ReleaseAndGetAddressOf()));

    {
        const D3D11_SUBRESOURCE_DATA vertexBufferData{ CubeShader::c_cubeVerticesBright };
        const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(CubeShader::c_cubeVerticesBright), D3D11_BIND_VERTEX_BUFFER);
        CHECK_HRCMD(m_device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, m_cubeVertexBufferBright.ReleaseAndGetAddressOf()));
    }

    {
        const D3D11_SUBRESOURCE_DATA vertexBufferData{ CubeShader::c_cubeVerticesMedium };
        const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(CubeShader::c_cubeVerticesMedium), D3D11_BIND_VERTEX_BUFFER);
        CHECK_HRCMD(m_device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, m_cubeVertexBufferMedium.ReleaseAndGetAddressOf()));
    }

    {
        const D3D11_SUBRESOURCE_DATA vertexBufferData{ CubeShader::c_cubeVerticesDark };
        const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(CubeShader::c_cubeVerticesDark), D3D11_BIND_VERTEX_BUFFER);
        CHECK_HRCMD(m_device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, m_cubeVertexBufferDark.ReleaseAndGetAddressOf()));
    }

    {
        const D3D11_SUBRESOURCE_DATA vertexBufferData{ CubeShader::c_cubeVerticesDarker };
        const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(CubeShader::c_cubeVerticesDarker), D3D11_BIND_VERTEX_BUFFER);
        CHECK_HRCMD(m_device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, m_cubeVertexBufferDarker.ReleaseAndGetAddressOf()));
    }

    const D3D11_SUBRESOURCE_DATA indexBufferData{ CubeShader::c_cubeIndices };
    const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(CubeShader::c_cubeIndices), D3D11_BIND_INDEX_BUFFER);
    CHECK_HRCMD(m_device->CreateBuffer(&indexBufferDesc, &indexBufferData, m_cubeIndexBuffer.ReleaseAndGetAddressOf()));

    m_cubeVertexBuffer = m_cubeVertexBufferMedium;

    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options;
    m_device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof(options));
    CHECK_MSG(options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer,
        "This sample requires VPRT support. Adjust sample shaders on GPU without VRPT.");

    CD3D11_DEPTH_STENCIL_DESC depthStencilDesc(CD3D11_DEFAULT{});
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    CHECK_HRCMD(m_device->CreateDepthStencilState(&depthStencilDesc, m_reversedZDepthNoStencilTest.ReleaseAndGetAddressOf()));
}

void HandRenderer::RenderHands(
    ID3D11RenderTargetView* const rtv[2],
    ID3D11DepthStencilView* const dsv[2],
    XrRect2Di imageRect,
    bool isVPRT,
    bool clearDepthBuffer,
    float depthNear,
	float depthFar)
{
    // Use a deferred context so we can use the context saving feature.
    // TODO: Optimization: this is not efficient.
    ComPtr<ID3D11DeviceContext> deferredContext;
    CHECK_HRCMD(m_device->CreateDeferredContext(0, deferredContext.GetAddressOf()));

    deferredContext->ClearState();

    CD3D11_VIEWPORT viewport(
        (float)imageRect.offset.x, (float)imageRect.offset.y, (float)imageRect.extent.width, (float)imageRect.extent.height);
	deferredContext->RSSetViewports(1, &viewport);
    deferredContext->OMSetDepthStencilState(depthNear > depthFar ? m_reversedZDepthNoStencilTest.Get() : nullptr, 0);

    // Setup shaders.
    ID3D11Buffer* const constantBuffers[] = { m_modelCBuffer.Get(), m_viewProjectionCBuffer.Get() };
    deferredContext->VSSetConstantBuffers(0, (UINT)std::size(constantBuffers), constantBuffers);
    deferredContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    deferredContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // Set cube primitive data.
    const UINT strides[] = { sizeof(CubeShader::Vertex) };
    const UINT offsets[] = { 0 };
    ID3D11Buffer* vertexBuffers[] = { m_cubeVertexBuffer.Get() };
    deferredContext->IASetVertexBuffers(0, (UINT)std::size(vertexBuffers), vertexBuffers, strides, offsets);
    deferredContext->IASetIndexBuffer(m_cubeIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    deferredContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deferredContext->IASetInputLayout(m_inputLayout.Get());

    // Render each view.
    for (uint32_t k = 0; k < (isVPRT ? 1u : 2u); k++)
    {
        CubeShader::ViewProjectionConstantBuffer viewProjectionCBufferData{};
        if (isVPRT)
        {
            deferredContext->OMSetRenderTargets(1, rtv, dsv[0]);

            for (uint32_t k = 0; k < 2; k++) {
                const DirectX::XMMATRIX spaceToView = xr::math::LoadInvertedXrPose(m_eyePose[k]);
                xr::math::NearFar nearFar{ depthNear, depthFar };
                const DirectX::XMMATRIX projectionMatrix = xr::math::ComposeProjectionMatrix(m_eyeFov[k], nearFar);

                // Set view projection matrix for each view, transpose for shader usage.
                DirectX::XMStoreFloat4x4(&viewProjectionCBufferData.ViewProjection[k],
                    DirectX::XMMatrixTranspose(spaceToView * projectionMatrix));
            }
            deferredContext->UpdateSubresource(m_viewProjectionCBuffer.Get(), 0, nullptr, &viewProjectionCBufferData, 0, 0);
        }
        else
        {
            deferredContext->OMSetRenderTargets(1, &rtv[k], dsv[k]);

            const DirectX::XMMATRIX spaceToView = xr::math::LoadInvertedXrPose(m_eyePose[k]);
            xr::math::NearFar nearFar{ depthNear, depthFar };
            const DirectX::XMMATRIX projectionMatrix = xr::math::ComposeProjectionMatrix(m_eyeFov[k], nearFar);

            // Set view projection matrix for the first, transpose for shader usage.
            DirectX::XMStoreFloat4x4(&viewProjectionCBufferData.ViewProjection[0],
                DirectX::XMMatrixTranspose(spaceToView * projectionMatrix));
            deferredContext->UpdateSubresource(m_viewProjectionCBuffer.Get(), 0, nullptr, &viewProjectionCBufferData, 0, 0);

        }

        if (clearDepthBuffer)
        {
            const float depthClearValue = depthNear > depthFar ? 0.f : 1.f;
            deferredContext->ClearDepthStencilView(dsv[k], D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depthClearValue, 0);
        }

        // Render each joint for each hand.
        for (uint32_t side = 0; side < 2; side++)
        {
            if (m_handResult[side] != XR_SUCCESS)
            {
                continue;
            }

            for (uint32_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
            {
                if (!xr::math::Pose::IsPoseValid(m_jointLocations[side][i].locationFlags))
                {
                    continue;
                }

                // Compute and update the model transform for each cube, transpose for shader usage.
                CubeShader::ModelConstantBuffer model;
                const DirectX::XMMATRIX scaleMatrix = DirectX::XMMatrixScaling(
                    m_jointLocations[side][i].radius, min(0.0025f, m_jointLocations[side][i].radius), max(0.015f, m_jointLocations[side][i].radius));
                DirectX::XMStoreFloat4x4(&model.Model, DirectX::XMMatrixTranspose(scaleMatrix * xr::math::LoadXrPose(m_jointLocations[side][i].pose)));
                deferredContext->UpdateSubresource(m_modelCBuffer.Get(), 0, nullptr, &model, 0, 0);

                // Draw the cube.
                deferredContext->DrawIndexedInstanced((UINT)std::size(CubeShader::c_cubeIndices), isVPRT ? 2 : 1, 0, 0, k);
            }
        }
    }

    // Execute the commands now.
    ComPtr<ID3D11CommandList> commandList;
    CHECK_HRCMD(deferredContext->FinishCommandList(FALSE, commandList.GetAddressOf()));

    m_deviceContext->ExecuteCommandList(commandList.Get(), TRUE);
}

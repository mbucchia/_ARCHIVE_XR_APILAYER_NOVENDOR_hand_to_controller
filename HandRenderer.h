#pragma once

#include "pch.h"

using Microsoft::WRL::ComPtr;

class HandRenderer
{
public:
	HandRenderer()
	{
	}

	void SetDevice(ComPtr<ID3D11Device> device);

	void SetProperties(
		const int skinTone,
		const float opacity)
	{
		switch (skinTone)
		{
		case 0:
			m_cubeVertexBuffer = m_cubeVertexBufferBright;
			break;
		case 1:
			m_cubeVertexBuffer = m_cubeVertexBufferMedium;
			break;
		case 2:
			m_cubeVertexBuffer = m_cubeVertexBufferDark;
			break;
		default:
			m_cubeVertexBuffer = m_cubeVertexBufferDarker;
			break;
		}
	}

	void SetEyePoses(
		const XrPosef eyePose[2],
		const XrFovf eyeFov[2])
	{
		for (int side = 0; side < 2; side++)
		{
			m_eyePose[side] = eyePose[side];
			m_eyeFov[side] = eyeFov[side];
		}
	}

	void SetJointsLocations(
		const XrResult handResult[2],
		const XrHandJointLocationEXT jointLocations[2][XR_HAND_JOINT_COUNT_EXT])
	{
		for (int side = 0; side < 2; side++)
		{
			m_handResult[side] = handResult[side];
			for (uint32_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
			{
				m_jointLocations[side][i] = jointLocations[side][i];
			}
		}
	}

	void RenderHands(
		ID3D11RenderTargetView* const rtv[2],
		ID3D11DepthStencilView* const dsv[2],
		XrRect2Di imageRect,
		bool isVPRT,
		bool clearDepthBuffer,
		float depthNear,
		float depthFar);

private:
	ComPtr<ID3D11Device> m_device;
	ComPtr<ID3D11DeviceContext> m_deviceContext;

	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11Buffer> m_modelCBuffer;
	ComPtr<ID3D11Buffer> m_viewProjectionCBuffer;
	ComPtr<ID3D11InputLayout> m_inputLayout;
	ComPtr<ID3D11Buffer> m_cubeVertexBufferBright;
	ComPtr<ID3D11Buffer> m_cubeVertexBufferMedium;
	ComPtr<ID3D11Buffer> m_cubeVertexBufferDark;
	ComPtr<ID3D11Buffer> m_cubeVertexBufferDarker;
	ComPtr<ID3D11Buffer> m_cubeVertexBuffer;
	ComPtr<ID3D11Buffer> m_cubeIndexBuffer;
	ComPtr<ID3D11DepthStencilState> m_reversedZDepthNoStencilTest;

	XrPosef m_eyePose[2];
	XrFovf m_eyeFov[2];
	XrResult m_handResult[2];
	XrHandJointLocationEXT m_jointLocations[2][XR_HAND_JOINT_COUNT_EXT];
};

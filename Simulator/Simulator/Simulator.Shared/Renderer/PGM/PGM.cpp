#include "pch.h"
#include "PGM.h"

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace Platform;

namespace Renderer
{
	PGM::PGM(const std::shared_ptr<DeviceResources>& devResources) :
		deviceResources(devResources)
	{
	}

	bool PGM::Initialize()
	{
		// THIS IS JUST FOR TESTING
		primaryRadius = 1.0f;
		sphereWorldPos = Base::Math::VectorInit({ 0.0f, 0.0f, 0.0f });
		numGridPointsX = 16;
		numGridPointsY = 16;

		MakeGridPoints();
		// END JUST FOR TESTING
		
		pgmShader = std::make_unique<Shader>(deviceResources, "PGMVertexShader.cso", "", "PGMGeometryShader.cso");
		pgmShader->SetInputDescriptor(gridPointIED, 1);
		pgmShader->Initialize();
		
		rasterizationShader = std::make_unique<Shader>(deviceResources, "PGMVertexShader.cso", "PGMPixelShader.cso");
		rasterizationShader->SetInputDescriptor(pgmRasterizationIED, 2);
		rasterizationShader->Initialize();


		// create the SO buffer to get data between PGM and rasterization stages
		auto context = deviceResources->GetD3DDeviceContext();

		Platform::Array<byte>^ GSFile;
		GSFile = LoadShaderFile(pgmShader->gsFileName);

		deviceResources->GetD3DDevice()->CreateGeometryShaderWithStreamOutput(
			GSFile->Data, 
			GSFile->Length,
			gridPointSOIED,
			2,
			NULL,
			0,
			0,
			NULL,
			pgmShader->geometryshader.GetAddressOf()
			);
		
		// create the vertex buffer for stream out between PGM projection and rasterization stages
		D3D11_BUFFER_DESC vertexBD =
		{
			numGridPointsX * numGridPointsY * 4 * 3,
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT,
			0,
			0,
			0
		};
		//vertexBD.Usage = D3D11_USAGE_DEFAULT;
		//vertexBD.ByteWidth = numGridPointsX * numGridPointsY;
		//vertexBD.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT;
		//vertexBD.CPUAccessFlags = 0;
		//vertexBD.MiscFlags = 0;
		
		ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreateBuffer(&vertexBD, nullptr, &streamOutVertexBuffer)
			);

		return true;
	}

	void PGM::SetCamera(const Renderer::Camera& c)
	{
		ViewCamera.Set(c);
		SamplingCamera.Set(ViewCamera);
	}

	void PGM::CreateWindowSizeDependentResources()
	{
		Size outputSize = deviceResources->GetOutputSize();
		float aspectRatio = outputSize.Width / outputSize.Height;
		float fovAngleY = 70.0f * XM_PI / 180.0f;

		// This is a simple example of change that can be made when the app is in
		// portrait or snapped view.
		if (aspectRatio < 1.0f)
		{
			fovAngleY *= 2.0f;
		}

		// Note that the OrientationTransform3D matrix is post-multiplied here
		// in order to correctly orient the scene to match the display orientation.
		// This post-multiplication step is required for any draw calls that are
		// made to the swap chain render target. For draw calls to other targets,
		// this transform should not be applied.

		XMFLOAT4X4 orientation = deviceResources->GetOrientationTransform3D();

		ViewCamera.OrientMatrix = XMLoadFloat4x4(&orientation);

		SamplingCamera.OrientMatrix = XMLoadFloat4x4(&orientation);

		MakeGridPoints();
	}

	void PGM::Update()
	{

		// update view camera
		ViewCamera.UpdateMatrices();
		// update sampling camera
		SamplingCamera.UpdateMatrices();
		// compute ray-sphere intersection point & sphere intersection normal
		//   gamma1 & gamma 2
		//		* gamma1 = asin((d / r) sin w) - w : first intersection angle from nadir
		//		* gamma2 = -asin((d / r) sin w) - w + pi : second intersection angle from nadir

		// set our new render target object as the active render target
		auto context = deviceResources->GetD3DDeviceContext();

#if 0
		rasterizationShader->Apply();
		// set the vertex buffer
		UINT stride = sizeof(PGM::GridVertex);
		UINT offset = 0;
		context->IASetVertexBuffers(0, 1, gridvertexbuffer.GetAddressOf(), &stride, &offset);

		// set the primitive topology
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

		// draw 3 vertices, starting from vertex 0
		context->Draw(numIndices, 0);

		rasterizationShader->Disable();
#else

		// set up render target buffers for Deferred Rendering:
		//  * Pass 1:
		//		input:
		//		* vertex/index buffers
		//		output:
		//		* positions
		//		* normals
		//	* Pass 2:
		//		input:
		//		* position buffer
		//		* normal buffer
		//		* texture atlas
		//		* height atlas
		//		output:
		//		* rendered scene

		D3D11_DEPTH_STENCIL_DESC dsd;
		dsd.DepthEnable = FALSE;
		dsd.StencilEnable = FALSE;
		ComPtr<ID3D11DepthStencilState> dss;
		deviceResources->GetD3DDevice()->CreateDepthStencilState(&dsd, dss.GetAddressOf());
		context->OMSetDepthStencilState(dss.Get(), 0);

		pgmShader->Apply();
		
		// update grid points (per the new sampling viewport)
		//	* calc new extents to edges of sphere for frusta
		//	* calc new distance to planet
		// build grid points into vertex buffer and index buffer
		// send grid points:
		// set the vertex buffer

		// PGM Stage:
		//	* send PGM info (camera info, shape info) in constant buffer
		//	* set grid points vertex buffer / index buffer created for sampling camera to IA stage
		//	* set the stream out vertex buffer to stream out stage

		UINT stride = sizeof(PGM::GridVertex);
		UINT offset = 0;
		// set the vertex buffer (grid points)
		context->IASetVertexBuffers(0, 1, gridvertexbuffer.GetAddressOf(), &stride, &offset);

		// set the stream-out buffer for output from Geometry Shader
		context->SOSetTargets(1,streamOutVertexBuffer.GetAddressOf(),&offset);

		// set the primitive topology
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);


		XMMATRIX matFinal = ViewCamera.ViewMatrix * ViewCamera.ProjMatrix;

		// update the constant buffers with relevant info for PGM (camera & surface info)
		PGM_Pass0_CBuffer pgmCbuffer;
		pgmCbuffer.CameraPosition = SamplingCamera.Position;
		pgmCbuffer.ViewVector = SamplingCamera.View;
		pgmCbuffer.matWVP = XMMatrixIdentity();
		context->UpdateSubresource(pgmShader->constantbuffer.Get(), 0, 0, &pgmCbuffer, 0, 0);

		// invoke the shader code for raycasting PGM
		context->Draw(numIndices, 0);

		pgmShader->Disable();

		D3D11_DEPTH_STENCIL_DESC dsd2;
		ComPtr<ID3D11DepthStencilState> dss2;
		deviceResources->GetD3DDevice()->CreateDepthStencilState(&dsd2,dss2.GetAddressOf());
		context->OMSetDepthStencilState(dss2.Get(),0);

		rasterizationShader->Apply();

		// Rasterization Stage:
		//	* set stream-out stage target to null
		//	* send main camera info & transform from sampling to main camera
		//	* set stream out vertex buffer (from PGM stage) to IA stage

		// clear stream out stage target from previous stage
		ComPtr<ID3D11Buffer> bufferArray = { 0 };
		context->SOSetTargets(1, bufferArray.GetAddressOf(), &offset);

		stride = sizeof(PGM::SOVertex);
		offset = 0;
		// set the vertex buffer to the stream out buffer from the previous stage
		context->IASetVertexBuffers(0, 1, streamOutVertexBuffer.GetAddressOf(), &stride, &offset);

		// set the primitive topology
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);

		// update the constant buffers with relevant info for PGM (camera & surface info)
		PGM_Pass1_CBuffer rasterizationCbuffer;
		rasterizationCbuffer.CameraPosition = ViewCamera.Position;
		rasterizationCbuffer.ViewVector = ViewCamera.View;

		rasterizationCbuffer.matWVP = matFinal;

		// Lighting related
		rasterizationCbuffer.matRotation = XMMatrixIdentity();
		rasterizationCbuffer.DiffuseVector = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
		rasterizationCbuffer.DiffuseColor = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);
		rasterizationCbuffer.AmbientColor = XMVectorSet(0.2f, 0.2f, 0.2f, 1.0f);

		context->UpdateSubresource(rasterizationShader->constantbuffer.Get(), 0, 0, &rasterizationCbuffer, 0, 0);

		// Draw the vertices created from the stream-out stage
		context->DrawAuto();

		rasterizationShader->Disable();

#endif
	}

	void PGM::MakeGridPoints()
	{
		numIndices = numGridPointsX * numGridPointsY;
		std::vector<PGM::GridVertex> OurVertices;
		std::vector<UINT> OurIndices;
		int index = 0;
		// need to set up verts & inds based on grid
		for (int i = 0; i < numGridPointsX; i++)
		{
			for (int j = 0; j < numGridPointsY; j++)
			{
				float x, y, z;
				x = (float)(i) / (float)(numGridPointsX - 1)*2.0f - 1.0f;
				y = (float)(j) / (float)(numGridPointsY - 1)*2.0f - 1.0f;
				z = 0.0f;
				PGM::GridVertex v = { x, y, z };
				OurVertices.push_back(v);
				OurIndices.push_back(index++);
			}
		}
		
		// create the vertex buffer
		D3D11_BUFFER_DESC vertexBD = { 0 };
		vertexBD.ByteWidth = sizeof(PGM::GridVertex) * numIndices;
		vertexBD.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		D3D11_SUBRESOURCE_DATA vertexSRD = { OurVertices.data(), 0, 0 };

		ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreateBuffer(&vertexBD, &vertexSRD, &gridvertexbuffer)
			);

		// create the index buffer
		// buffer description
		D3D11_BUFFER_DESC indexBD = { 0 };
		indexBD.ByteWidth = sizeof(UINT) * (int)OurIndices.size();    // indices are stored in short values
		indexBD.BindFlags = D3D11_BIND_INDEX_BUFFER;

		// subresource data
		D3D11_SUBRESOURCE_DATA indexSRD = { OurIndices.data(), 0, 0 };

		ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreateBuffer(&indexBD, &indexSRD, &gridindexbuffer)
			);
	}

	bool PGM::UnInitialize()
	{
		return true;
	}

	void PGM::OnSuspending()
	{

	}

	void PGM::OnResuming()
	{

	}
}
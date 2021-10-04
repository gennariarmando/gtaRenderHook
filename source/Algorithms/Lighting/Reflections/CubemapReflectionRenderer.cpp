// This is an open source non-commercial project. Dear PVS-Studio, please check
// it. PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "plugin.h"

#include "gta_sa_ptrs.h"
#include "CubemapReflectionRenderer.h"
#include "D3D1XDepthStencilTexture.h"
#include "D3D1XRenderBuffersManager.h"
#include "D3D1XStateManager.h"
#include "D3DRenderer.h"
#include "RwD3D1XEngine.h"
#include "RwMethods.h"
#include <game_sa\CScene.h>

#define ENVMAPSIZE 512
#define MIPLEVELS 7

CCubemapReflectionRenderer::CCubemapReflectionRenderer( int size )
    : m_nCubemapSize( size )
{
    ID3D11Device *device = GET_D3D_DEVICE;

    m_pReflCamera = RwCameraCreate();
    RwCameraSetProjection( m_pReflCamera, RwCameraProjection::rwPERSPECTIVE );

    RwCameraSetNearClipPlane( m_pReflCamera, 0.1f );
    RwCameraSetFarClipPlane( m_pReflCamera, 100 );

    RwCameraSetRaster( m_pReflCamera,
                       RwRasterCreate( m_nCubemapSize, m_nCubemapSize, 32,
                                       rwRASTERTYPECAMERATEXTURE ) );
    RwCameraSetZRaster( m_pReflCamera,
                        RwRasterCreate( m_nCubemapSize, m_nCubemapSize, 32,
                                        rwRASTERTYPEZBUFFER ) );

    CameraSize( m_pReflCamera, nullptr, tanf( 3.14f / 4 ), 1.0f );
    m_pReflCameraFrame = RwFrameCreate();
    RwObjectHasFrameSetFrame( m_pReflCamera, m_pReflCameraFrame );
    RpWorldAddCamera( Scene.m_pRpWorld, m_pReflCamera );
    UINT mipCount = max( (UINT)log2( m_nCubemapSize ) - 1, 1 );
    // Create cubic depth stencil texture.
    D3D11_TEXTURE2D_DESC dstex{};
    dstex.Width              = m_nCubemapSize;
    dstex.Height             = m_nCubemapSize;
    dstex.MipLevels          = 1;
    dstex.ArraySize          = 6;
    dstex.SampleDesc.Count   = 1;
    dstex.SampleDesc.Quality = 0;
    dstex.Usage              = D3D11_USAGE_DEFAULT;
    dstex.CPUAccessFlags     = 0;

    // Create the cube map for env map render target
    dstex.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    dstex.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    dstex.MiscFlags =
        D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_TEXTURECUBE;
    dstex.MipLevels = mipCount;
    if ( FAILED( device->CreateTexture2D( &dstex, NULL, &g_pEnvMap ) ) )
    {
        g_pDebug->printMsg( "Failed to create depth env map.", 0 );
        return;
    }

    // Create the 6-face render target view
    D3D11_RENDER_TARGET_VIEW_DESC DescRT{};
    DescRT.Format                         = dstex.Format;
    DescRT.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    DescRT.Texture2DArray.FirstArraySlice = 0;
    DescRT.Texture2DArray.ArraySize       = 6;
    DescRT.Texture2DArray.MipSlice        = 0;
    if ( FAILED( device->CreateRenderTargetView( g_pEnvMap, &DescRT,
                                                 &g_pEnvMapRTV ) ) )
    {
        g_pDebug->printMsg( "Failed to create depth env map.", 0 );
        return;
    }
    // Create the one-face render target views
    DescRT.Texture2DArray.ArraySize = 1;
    for ( int i = 0; i < 6; ++i )
    {
        DescRT.Texture2DArray.FirstArraySlice = i;
        if ( FAILED( device->CreateRenderTargetView( g_pEnvMap, &DescRT,
                                                     &g_apEnvMapOneRTV[i] ) ) )
            g_pDebug->printMsg( "Failed to create depth env map.", 0 );
    }

    // Create the shader resource view for the cubic env map
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
    ZeroMemory( &SRVDesc, sizeof( SRVDesc ) );
    SRVDesc.Format                      = dstex.Format;
    SRVDesc.ViewDimension               = D3D11_SRV_DIMENSION_TEXTURECUBE;
    SRVDesc.TextureCube.MipLevels       = mipCount;
    SRVDesc.TextureCube.MostDetailedMip = 0;
    if ( FAILED( device->CreateShaderResourceView( g_pEnvMap, &SRVDesc,
                                                   &g_pEnvMapSRV ) ) )
        g_pDebug->printMsg( "Failed to create depth env map.", 0 );
}

CCubemapReflectionRenderer::~CCubemapReflectionRenderer()
{
    RwRasterDestroy( m_pReflCamera->zBuffer );
    RwRasterDestroy( m_pReflCamera->frameBuffer );
    RwCameraDestroy( m_pReflCamera );
    if ( m_pReflCB )
        m_pReflCB->Release();
    if ( g_pEnvMap )
        g_pEnvMap->Release();
    if ( g_pEnvMapRTV )
        g_pEnvMapRTV->Release();
    for ( int i = 0; i < 6; ++i )
    {
        if ( g_apEnvMapOneRTV[i] )
            g_apEnvMapOneRTV[i]->Release();
    }
    if ( g_pEnvMapSRV )
        g_pEnvMapSRV->Release();
}

#define FindPlayerCoors( outPoint, playerIndex )                               \
    ( (RwV3d * (__cdecl *)(RwV3d *, int))0x56E010 )( outPoint, playerIndex )
void CCubemapReflectionRenderer::RenderToCubemap( void ( *renderCB )() )
{
    // Move reflection camera to game camera position.
    auto  s_cam_frame = RwCameraGetFrame( m_pReflCamera );
    RwV3d campos;
    // campos =
    // *RwMatrixGetPos(RwFrameGetMatrix(RwCameraGetFrame(Scene.m_pRwCamera)));
    FindPlayerCoors( &campos, 0 );
    // Render each face.
    RwV3d At, Up, Right;
    At    = {1.0f, 0.0f, 0.0f};
    Up    = {0.0f, 1.0f, 0.0f};
    Right = {0.0f, 0.0f, 1.0f};

    RenderOneFace( renderCB, 0, 0, At, 270, Up, campos );   // forward
    RenderOneFace( renderCB, 1, 0, Right, 90, Up, campos ); // backward
    RenderOneFace( renderCB, 2, 270, At, 0, Up, campos );   // right
    RenderOneFace( renderCB, 3, 90, At, 0, Up, campos );    // left
    RenderOneFace( renderCB, 4, 0, At, 0, Up, campos );     // down
    RenderOneFace( renderCB, 5, 0, At, 180, Up, campos );   // up
    ID3D11DeviceContext *context = GET_D3D_CONTEXT;
    context->GenerateMips( g_pEnvMapSRV );
}

void CCubemapReflectionRenderer::RenderOneFace(
    void ( *renderCB )(),
    int id, float angleA, RwV3d axisA, float angleB, RwV3d axisB, RwV3d camPos /*const RW::V3d&At, const RW::V3d&Up, const RW::V3d&Right, RW::V3d&  Pos*/ )
{
    ID3D11DeviceContext *context = GET_D3D_CONTEXT;

    RwFrameSetIdentity( m_pReflCameraFrame );
    RwFrameRotate( m_pReflCameraFrame, &axisA, angleA, rwCOMBINEREPLACE );
    RwFrameRotate( m_pReflCameraFrame, &axisB, angleB, rwCOMBINEPOSTCONCAT );
    RwFrameTranslate( m_pReflCameraFrame, &camPos, rwCOMBINEPOSTCONCAT );
    RwFrameUpdateObjects( m_pReflCameraFrame );
    RwCameraSetNearClipPlane( m_pReflCamera, 0.1f );
    RwCameraSetFarClipPlane( m_pReflCamera, 100.0f );
    RwCameraClear( m_pReflCamera, gColourTop, rwCAMERACLEARZ );

    RwCameraBeginUpdate( m_pReflCamera );

    float ClearColor[4] = {0.0f, 0.0f, 0.0f, 0.4f};
    context->ClearRenderTargetView( g_apEnvMapOneRTV[id], ClearColor );
    auto depthBuffer = dynamic_cast<CD3D1XDepthStencilTexture *>(
        GetD3D1XRaster( m_pReflCamera->zBuffer )->resourse );
    ID3D11RenderTargetView *aRTViews[1] = {g_apEnvMapOneRTV[id]};
    context->OMSetRenderTargets( 1, aRTViews,
                                 depthBuffer->GetDepthStencilView() );
    D3D11_VIEWPORT vp{};
    vp.Width    = (FLOAT)m_nCubemapSize;
    vp.Height   = (FLOAT)m_nCubemapSize;
    vp.MaxDepth = 1.0;
    g_pStateMgr->SetViewport( vp );
    g_pStateMgr->FlushStates();
    renderCB();
    RwCameraEndUpdate( m_pReflCamera );
}

void CCubemapReflectionRenderer::SetCubemap()
{
    ID3D11DeviceContext *context = GET_D3D_CONTEXT;
    context->PSSetShaderResources( 5, 1, &g_pEnvMapSRV );
}

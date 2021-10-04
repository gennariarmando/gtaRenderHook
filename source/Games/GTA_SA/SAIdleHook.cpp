// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "plugin.h"
#include "CPointLights.h" 
#include "CFont.h"
#include "CWorld.h"
#include "CScene.h"
#include "CClock.h"
#include "CStreaming.h"
#include "CClouds.h"
#include "CWeaponEffects.h"
#include "CSpecialFX.h"
#include "CCoronas.h"
#include "Fx_c.h"
#include "CGame.h"
#include "CDraw.h"
#include "CVisibilityPlugins.h"
#include "CBirds.h"
#include "CRopes.h"
#include "CRenderer.h"
#include "CTimer.h"

#include "MemoryFuncs.h"
#include "SAIdleHook.h"
#include "DeferredRenderer.h"
#include "ShadowRenderer.h"
#include "Renderer.h"
#include "D3D1XStateManager.h"
#include "D3D1XRenderBuffersManager.h"
#include "VoxelOctreeRenderer.h"
#include "LightManager.h"
#include "HDRTonemapping.h"
#include "CustomCarFXPipeline.h"
#include "CustomBuildingPipeline.h"
#include "CustomBuildingDNPipeline.h"
#include "CWaterLevel.h"
#include "RwMethods.h"
#include <AntTweakBar.h>
#include "DebugRendering.h"
#include "D3DRenderer.h"
#include "DebugBBox.h"
#include "CVisibilityPluginsRH.h"
#include "AmbientOcclusion.h"
#include "CloudRendering.h"
#include "VolumetricLighting.h"
#include "FullscreenQuad.h"
#include "CubemapReflectionRenderer.h"
#include "TemporalAA.h"

/* GTA Definitions TODO: Move somewhere else*/
#define g_breakMan ((void *)0xBB4240) // Break manager
#define CPostEffects__m_bDisableAllPostEffect (*(bool *)0xC402CF)	

int drawCallCount = 0;
float CSAIdleHook::m_fShadowDNBalance = 1.0;
void CSAIdleHook::Patch()
{
    RedirectCall(0x53ECBD, Idle);
}

void CSAIdleHook::Idle( void *Data )
{
    SettingsHolder::Instance()->InitGUI();
    if ( !gDebugSettings.GetToggleField( "UseIdleHook" ) )
    {

        RsEventHandler(rsIDLE, Data);
        SettingsHolder::Instance()->DrawGUI();
        return;
    }
    SettingsHolder::Instance()->ReloadShadersIfRequired();
    // Update timers
    TimeUpdate();
    // Init 2D stuff per frame.
    InitPerFrame2D();
    // Update game processes
    GameUpdate();
    // Update lighting
    LightUpdate();

    // reload textures if required
    g_pDeferredRenderer->QueueTextureReload();
    CVolumetricLighting::QueueTextureReload();
    CCloudRendering::QueueTextureReload();
    CAmbientOcclusion::QueueTextureReload();
    CFullscreenQuad::QueueTextureReload();
    CTemporalAA::QueueTextureReload();
    // TODO: move to RwD3D1XEngine
    CRwD3D1XEngine* dxEngine = (CRwD3D1XEngine*)g_pRwCustomEngine;
    if ( dxEngine->m_bScreenSizeChanged || g_pDeferredRenderer->m_pShadowRenderer->m_bRequiresReloading ||
         g_pDeferredRenderer->m_bRequiresReloading ||
         CVolumetricLighting::m_bRequiresReloading ||
         CCloudRendering::m_bRequiresReloading )
    {
        dxEngine->ReloadTextures();
        dxEngine->m_bScreenSizeChanged = false;
        g_pDeferredRenderer->m_pShadowRenderer->m_bRequiresReloading = false;
        g_pDeferredRenderer->m_bRequiresReloading = false;
        CVolumetricLighting::m_bRequiresReloading                    = false;
        CAmbientOcclusion::m_bRequiresReloading                      = false;
        CCloudRendering::m_bRequiresReloading                        = false;
    }

    if ( !Data )
        return;
    PrepareRwCamera();
    if ( !RsCameraBeginUpdate( Scene.m_pRwCamera ) )
        return;
    if ( !FrontEndMenuManager.m_bMenuActive && TheCamera.GetScreenFadeStatus() != 2 )
    {
        RenderInGame();
        SettingsHolder::Instance()->DrawGUI();
        DefinedState2d();
        if ( SettingsHolder::Instance()->IsGUIEnabled() )
        {
            POINT mousePos;
            GetCursorPos( &mousePos );
            //g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, TRUE);
            FrontEndMenuManager.m_apTextures[23].SetRenderState();
            FrontEndMenuManager.m_apTextures[23].DrawTxRect(
                { (float)mousePos.x, (float)mousePos.y,
                (float)mousePos.x + 10.0f, (float)mousePos.y + 16.0f },
                { 255,255,255,255 } );
        }
    }
    RenderHUD();
}

void CSAIdleHook::UpdateShadowDNBalance()
{
    // TODO: make times adjustable perhaps?
    float currentMinutes = ( CClock::ms_nGameClockMinutes + 60.0f * CClock::ms_nGameClockHours ) + CClock::ms_nGameClockSeconds / 60.0f;
    // if time is less than 7 am then it's night
    if ( currentMinutes < 360.0 )
    {
        m_fShadowDNBalance = 1.0;
        return;
    }
    if ( currentMinutes < 420.0 )
    {
        m_fShadowDNBalance = ( 420.0f - currentMinutes ) / 60.0f;
        return;
    }
    // if time is between 7 am and 7 pm than it's day
    if ( currentMinutes < 1080.0 )
    {
        m_fShadowDNBalance = 0.0;
        return;
    }
    // else it's night
    if ( currentMinutes >= 1140.0 )
        m_fShadowDNBalance = 1.0;
    else
        m_fShadowDNBalance = 1.0f - ( 1140.0f - currentMinutes ) / 60.0f;
}

// Render one game frame
void CSAIdleHook::RenderInGame()
{
    CPerfTimer shadowTimer( "Shadow Time" );
    CPerfTimer deferredTimer( "Deferred Time" );
    CPerfTimer scanTimer( "Renderlist construction time" );
    CPerfTimer gameTimer( "Game Time" );

    PrepareRenderStuff();

    DefinedState();
    TheCamera.m_mViewMatrix.Update();
    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATESTENCILENABLE, FALSE );

    RwCameraSetFarClipPlane( Scene.m_pRwCamera, CTimeCycle::m_CurrentColours.m_fFarClip );

    Scene.m_pRwCamera->fogPlane = CTimeCycle::m_CurrentColours.m_fFogStart;

    // Game variables initialization
    const auto sunDirs = reinterpret_cast<RwV3d*>( 0xB7CA50 );		// Sun direction table pointer(sun directions is always the same)
    const auto curr_sun_dir = *reinterpret_cast<int*>( 0xB79FD0 );	// Current sun direction id
    const auto curr_sun_dirvec = &sunDirs[curr_sun_dir];			// Current sun direction vector

    g_pStateMgr->SetSunDir( curr_sun_dirvec, ( CGame::currArea == 0 ? m_fShadowDNBalance : 1.0f ) );
    g_pStateMgr->SetFogStart( CTimeCycle::m_CurrentColours.m_fFogStart );
    g_pStateMgr->SetFogRange( CTimeCycle::m_CurrentColours.m_fFarClip - CTimeCycle::m_CurrentColours.m_fFogStart );
    g_shaderRenderStateBuffer.vSkyLightCol = { CTimeCycle::m_CurrentColours.m_nSkyTopRed / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_nSkyTopGreen / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_nSkyTopBlue / 255.0f,1.0f };
    g_shaderRenderStateBuffer.vHorizonCol = { CTimeCycle::m_CurrentColours.m_nSkyBottomRed / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_nSkyBottomGreen / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_nSkyBottomBlue / 255.0f,1.0f };
    g_shaderRenderStateBuffer.vSunColor = { CTimeCycle::m_CurrentColours.m_nSunCoreRed / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_nSunCoreGreen / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_nSunCoreBlue / 255.0f, 4.5f/*Timecycle->m_fCurrentSpriteBrightness */ };
    g_shaderRenderStateBuffer.vWaterColor = { CTimeCycle::m_CurrentColours.m_fWaterRed / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_fWaterGreen / 255.0f ,
                                    CTimeCycle::m_CurrentColours.m_fWaterBlue / 255.0f,
                                    CTimeCycle::m_CurrentColours.m_fWaterAlpha / 255.0f };
    g_shaderRenderStateBuffer.vGradingColor0 = { CTimeCycle::m_CurrentColours.m_fPostFx1Red / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_fPostFx1Green / 255.0f ,
                                        CTimeCycle::m_CurrentColours.m_fPostFx1Blue / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_fPostFx1Alpha / 255.0f };
    g_shaderRenderStateBuffer.vGradingColor1 = { CTimeCycle::m_CurrentColours.m_fPostFx2Red / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_fPostFx2Green / 255.0f ,
                                        CTimeCycle::m_CurrentColours.m_fPostFx2Blue / 255.0f,
                                        CTimeCycle::m_CurrentColours.m_fPostFx2Alpha / 255.0f };
    g_shaderRenderStateBuffer.fFarClip = Scene.m_pRwCamera->farPlane;

    // First forward pass(clouds, sky etc.)
    RenderForwardBeforeDeferred();

    g_pDeferredRenderer->m_pShadowRenderer->m_bShadowsRendered = false;
    if ( !CGame::currArea && ( m_fShadowDNBalance < 1.0 ) )
        PrepareRealTimeShadows( sunDirs[curr_sun_dir] );

    DebugRendering::ResetList();

    //
    drawCallCount = 0;

    // Render custom preprocess effects - shadows and voxel GI(disabled atm)

    m_uiDeferredStage = 5;
    //
    scanTimer.Start();
    CRendererRH::ConstructRenderList();
    scanTimer.Stop();

    CRenderer::PreRender();
    CRendererRH::PreRender();
    CWorld::ProcessPedsAfterPreRender();

    RwCameraEndUpdate( Scene.m_pRwCamera );
    g_pDeferredRenderer->RenderToCubemap( RenderForward );

    shadowTimer.Start();

    if ( !CGame::currArea&&m_fShadowDNBalance < 1.0 ) // Render shadows only if we are not inside interiors
        RenderRealTimeShadows( sunDirs[curr_sun_dir] );

    shadowTimer.Stop();

    RwCameraBeginUpdate( Scene.m_pRwCamera );
    CTemporalAA::JitterProjMatrix();
    deferredTimer.Start();
    g_pCustomCarFXPipe->ResetAlphaList();
    g_pCustomBuildingPipe->ResetAlphaList();
    g_pCustomBuildingDNPipe->ResetAlphaList();
    // Render scene to geometry buffers.
    g_pDeferredRenderer->RenderToGBuffer( RenderDeferred );

    // Reset renderstates and disable Z-Test
    DefinedState();
    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATEZTESTENABLE, 0 );

    // Render deferred shading
    CD3DRenderer* renderer = static_cast<CRwD3D1XEngine*>( g_pRwCustomEngine )->getRenderer();
    renderer->BeginDebugEvent( L"Deferred composition pass" );
    g_pDeferredRenderer->RenderOutput();
    renderer->EndDebugEvent();
    deferredTimer.Stop();

    // Enable Z-Test and render alpha entities
    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATEZTESTENABLE, 1 );
    m_uiDeferredStage = 0;

    renderer->BeginDebugEvent( L"Forward after deferred" );
    RenderForwardAfterDeferred();
    renderer->EndDebugEvent();

    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATEZTESTENABLE, 0 );
    renderer->BeginDebugEvent( L"Tonemapping pass" );
    g_pDeferredRenderer->RenderTonemappedOutput(); //TODO fix
    renderer->EndDebugEvent();
    //g_pRwCustomEngine->SetRenderTargets( &Scene.m_pRwCamera->frameBuffer,
    //                                     Scene.m_pRwCamera->zBuffer, 1 );
    DebugRendering::Render();
    if ( gDebugSettings.GetToggleField( "DebugRenderTarget" ) &&
         gDebugSettings.DebugRenderTargetList[gDebugSettings.DebugRenderTargetNumber] != nullptr )
        DebugRendering::RenderRaster( gDebugSettings.DebugRenderTargetList[gDebugSettings.DebugRenderTargetNumber] );

    DefinedState();
    int Render2dStuffAddress = *(DWORD *)0x53EB13 + 0x53EB12 + 5;
    ( ( int( __cdecl * )( ) )Render2dStuffAddress )( );
    //Render2dStuff();
    // Render preformance counters if required.
    if ( gDebugSettings.GetToggleField( "ShowPreformanceCounters" ) )
    {
        CFont::SetFontStyle( eFontStyle::FONT_SUBTITLES );
        CRGBA color{ 255,255,255,255 };//pi/360
        //CFont::SetAlignment(eFontAlignment::ALIGN_RIGHT);
        //CFont::SetScale(0.5, 0.5);
        CFont::SetColor( color );
        float FontXPos = Scene.m_pRwCamera->frameBuffer->width - 20.0f;
        float FontYPos = Scene.m_pRwCamera->frameBuffer->height - 20.0f;
        CFont::PrintString( FontXPos, FontYPos - 750.0f, (char*)( "NumBlocksOutsideWorldToBeRendered : " + to_string( CWaterLevel::m_NumBlocksOutsideWorldToBeRendered ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 600.0f, (char*)( "Visible refl entities count: " + to_string( CRendererRH::ms_aVisibleReflectionObjects.size() ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 550.0f, (char*)( "CameraHeading: " + to_string( CRendererRH::ms_fCameraHeading ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 500.0f, (char*)( "Visible Entity count: " + to_string( CRendererRH::ms_nNoOfVisibleEntities ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 450.0f, (char*)( "Draw call count: " + to_string( drawCallCount ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 400.0f, (char*)( "Visible Lod Entity count: " + to_string( CRendererRH::ms_nNoOfVisibleLods ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 350.0f, (char*)( "SC 0 Entity count: " + to_string( CRendererRH::ms_aVisibleShadowCasters[0].size() ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 300.0f, (char*)( "SC 1 Entity count: " + to_string( CRendererRH::ms_aVisibleShadowCasters[1].size() ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 250.0f, (char*)( "SC 2 Entity count: " + to_string( CRendererRH::ms_aVisibleShadowCasters[2].size() ) ).c_str() );
        CFont::PrintString( FontXPos, FontYPos - 200.0f, (char*)( "SC 3 Entity count: " + to_string( CRendererRH::ms_aVisibleShadowCasters[3].size() ) ).c_str() );


        //CFont::PrintString(FontXPos, FontYPos - 150.0f, (char*)("SC 3 Entity count: " + to_string(CRendererRH::ms_aVisibleShadowCasters[3].size())).c_str());

        CFont::PrintString( FontXPos, FontYPos - 150.0f, (char*)scanTimer.GetTimerResult().c_str() );
        CFont::PrintString( FontXPos, FontYPos - 100.0f, (char*)shadowTimer.GetTimerResult().c_str() );
        CFont::PrintString( FontXPos, FontYPos - 50.0f, (char*)deferredTimer.GetTimerResult().c_str() );
    }

}

void CSAIdleHook::RenderHUD()
{
    DefinedState();
    if ( FrontEndMenuManager.m_bMenuActive )
        DrawMenuManagerFrontEnd( &FrontEndMenuManager ); // CMenuManager::DrawFrontEnd
    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATETEXTURERASTER, NULL );

    DoFade();
    Render2dStuffAfterFade();

    DoRWStuffEndOfFrame();
}

void CSAIdleHook::RenderForwardBeforeDeferred()
{
}

void CSAIdleHook::RenderForwardAfterDeferred()
{
    CD3DRenderer* renderer = static_cast<CRwD3D1XEngine*>( g_pRwCustomEngine )->getRenderer();

    renderer->BeginDebugEvent( L"Water rendering pass" );
    g_pDeferredRenderer->SetNormalDepthRaster();
    g_pDeferredRenderer->SetPreviousNonTonemappedFinalRaster();
    g_pDeferredRenderer->m_pShadowRenderer->SetShadowBuffer();
    CWaterLevel::RenderWater();
    DefinedState();
    renderer->EndDebugEvent();

    renderer->BeginDebugEvent( L"Alpha-blended objects rendering pass" );
    g_pDeferredRenderer->m_pShadowRenderer->SetShadowBuffer();
    g_pDeferredRenderer->m_pReflRenderer->SetCubemap();
    g_pCustomCarFXPipe->RenderAlphaList();
    g_pCustomBuildingPipe->RenderAlphaList();
    g_pCustomBuildingDNPipe->RenderAlphaList();
    renderer->EndDebugEvent();

    //CPostEffects__m_bDisableAllPostEffect = true;
    // Render effects and 2d stuff
    DefinedState();
    renderer->BeginDebugEvent( L"Effects rendering pass" );
    RenderEffects();
    renderer->EndDebugEvent();
    DefinedState();
    //RenderGrass();
    //DefinedState();
}

void CSAIdleHook::RenderDeferred()
{
    CVisibilityPluginsRH::ClearWeaponPedsList();

    //g_pRwCustomEngine->RenderStateSet(rwRENDERSTATECULLMODE, rwCULLMODECULLBACK);
    CRendererRH::RenderRoads();
    CRendererRH::RenderEverythingBarRoads();
    CVisibilityPlugins::RenderFadingUnderwaterEntities();
    BreakManager_c__Render( g_breakMan, 0 );
    CVisibilityPlugins::RenderFadingEntities();
    BreakManager_c__Render( g_breakMan, 1 );
    // CRenderer::RenderFadingInUnderwaterEntities
    CVisibilityPluginsRH::RenderWeaponPedsNoMuzzleFlash();
    //CRenderer::RenderTOBJs();
    //CWaterLevel::RenderSeaBed();
}

void CSAIdleHook::RenderForward()
{
    g_pRwCustomEngine->RenderStateSet( rwRENDERSTATECULLMODE, rwCULLMODECULLNONE );
    CRendererRH::RenderCubemapEntities();
}

void CSAIdleHook::RenderEffects()
{
    //CClouds::Render();
    CBirds::Render();
    //CSkidmarks::Render();
    CRopes::Render();
    //CGlass::Render();
    //CMovingThings::Render();
    CVisibilityPlugins::RenderReallyDrawLastObjects();
    //CCoronas::RenderReflections();
    //CCoronas::RenderSunReflection();
    CD3DRenderer* renderer = static_cast<CRwD3D1XEngine*>( g_pRwCustomEngine )->getRenderer();
    renderer->BeginDebugEvent( L"Coronas rendering" );
    CCoronas::Render();
    renderer->EndDebugEvent();
    //g_pStateMgr->SetAlphaTestEnable(true);
    g_fx.Render( TheCamera.m_pRwCamera, 0 );
    //CWaterCannons::Render();
    //CWaterLevel::RenderWaterFog();
    CClouds::VolumetricCloudsRender();
    CClouds::MovingFogRender();
    /*if (CHeli::NumberOfSearchLights || CTheScripts::NumberOfScriptSearchLights)
    {
        CHeli::Pre_SearchLightCone();
        CHeli::RenderAllHeliSearchLights();
        CTheScripts::RenderAllSearchLights();
        CHeli::Post_SearchLightCone();
    }*/
    CWeaponEffects::Render();
    /*if (CReplay::Mode != 1 && !CPad::GetPad(0)->field_10E)
    {
        v1 = FindPlayerPed(-1);
        CPlayerPed::DrawTriangleForMouseRecruitPed(v1);
    }*/
    CSpecialFX::Render();
    //CVehicleRecording::Render();
    //CPointLights::RenderFogEffect();
    CRenderer::RenderFirstPersonVehicle();
    //g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEZTESTENABLE, 1);
    //g_pRwCustomEngine->RenderStateSet(rwRENDERSTATEZWRITEENABLE, 1);
    //CVisibilityPlugins__RenderWeaponPedsForPC();
    //CPostEffects::Render();
}

void CSAIdleHook::PrepareRealTimeShadows( const RwV3d &sundir )
{
    g_pDebug->printMsg( "Shadow PrePass: begin", 1 );

    auto shadowRenderer = g_pDeferredRenderer->m_pShadowRenderer;

    shadowRenderer->CalculateShadowDistances( Scene.m_pRwCamera->nearPlane, Scene.m_pRwCamera->farPlane );
    // Render cascades
    for ( int i = 0; i < gShadowSettings.ShadowCascadeCount; i++ )
        shadowRenderer->DirectionalLightTransform( Scene.m_pRwCamera, sundir, i );

    g_pDebug->printMsg( "Shadow PrePass: end", 1 );
}
void CSAIdleHook::RenderRealTimeShadows( const RwV3d &sundir )
{
    g_pDebug->printMsg( "Shadow Pass: begin", 1 );

    auto shadowRenderer = g_pDeferredRenderer->m_pShadowRenderer;
    m_uiDeferredStage = 2;
    RwCameraClear( shadowRenderer->m_pShadowCamera, gColourTop, rwCAMERACLEARZ );

    // Render cascades
    for ( int i = 0; i < gShadowSettings.ShadowCascadeCount; i++ )
    {
        shadowRenderer->RenderShadowToBuffer( i, [] ( int k )
        {
            CRendererRH::RenderShadowCascade( k );
        } );
    }
    shadowRenderer->m_bShadowsRendered = true;
    g_pDebug->printMsg( "Shadow Pass: end", 1 );
}

void RenderEntity2dfx( CEntity* e ) { }

void* CreateEntity2dfx( void* e )
{
    return reinterpret_cast<void*>( 0xC3EF84 );
}

void CopyViewMatrix( RwMatrix* viewTransformRef, RwFrame* camframe )
{
    RwMatrixInvert( viewTransformRef, RwFrameGetLTM( camframe ) );
    viewTransformRef->right.x = -viewTransformRef->right.x;
    viewTransformRef->up.x = -viewTransformRef->up.x;
    viewTransformRef->at.x = -viewTransformRef->at.x;
    viewTransformRef->pos.x = -viewTransformRef->pos.x;
    viewTransformRef->flags = 0;
    viewTransformRef->pad1 = 0;
    viewTransformRef->pad2 = 0;
    viewTransformRef->pad3 = 0x3F800000;
}
//typedef std::chrono::high_resolution_clock hr_clock;

void CSAIdleHook::RenderVoxelGI()
{
    auto s_cam_frame = RwCameraGetFrame( CVoxelOctreeRenderer::m_pVoxelCamera );
    auto campos = RwMatrixGetPos( RwFrameGetMatrix( RwCameraGetFrame( Scene.m_pRwCamera ) ) );
    /*if (FindPlayerPos(&cpos, 0) == nullptr)
        Campos = RwMatrixGetPos(RwFrameGetMatrix(RwCameraGetFrame(Scene.curCamera)));
    else
        Campos = &cpos;*/
    RwFrameTranslate( s_cam_frame, campos, rwCOMBINEREPLACE );
    //g_pRwCustomEngine->RenderStateSet(rwRENDERSTATECULLMODE, rwCULLMODECULLNONE);
    CVoxelOctreeRenderer::CleanVoxelOctree();

    for ( size_t i = 1; i < 4; i++ )
    {
        m_uiDeferredStage = 3;
        CVoxelOctreeRenderer::SetVoxelLODSize( i );
        CVoxelOctreeRenderer::RenderToVoxelOctree( RenderDeferred, i );

        g_pDeferredRenderer->m_pShadowRenderer->SetShadowBuffer();
        CLightManager::SortByDistance( *campos );

        m_uiDeferredStage = 4;
        CVoxelOctreeRenderer::InjectRadiance( g_pDeferredRenderer->m_pShadowRenderer->m_pShadowCamera->zBuffer, CRendererRH::RenderTOBJs, i - 1 );

    }
    CVoxelOctreeRenderer::FilterVoxelOctree();
}

void CSAIdleHook::PrepareRenderStuff()
{
    RwV2d mousePos;
    mousePos.x = RsGlobal.maximumWidth * 0.5f;
    mousePos.y = RsGlobal.maximumHeight * 0.5f;
    POINT pMousePos;
    // TODO: add custom key to show menu
    auto guiEnabled = GetKeyState( VK_F12 ) & 1;
    if ( guiEnabled )
        SettingsHolder::Instance()->EnableGUI();
    else
        SettingsHolder::Instance()->DisableGUI();

    GetCursorPos( &pMousePos );
    if ( !guiEnabled )
        RsMouseSetPos( &mousePos );
    else
        TwMouseMotion( pMousePos.x, pMousePos.y );
    //ShowCursor(guiEnabled);
    CTimer::m_UserPause = guiEnabled;
}

void CSAIdleHook::Render2dStuffAfterFade()
{
    CHud__DrawAfterFade();
    CMessages__Display( 0 ); // CMessages::Display
    CFont__RenderFontBuffer();     // CFont::DrawFonts
    if ( CCredits__bCreditsGoing )
    {
        if ( !FrontEndMenuManager.m_bMenuActive )
            CCredits__Render();       // CCredits::Render
    }
}

void CSAIdleHook::DoRWStuffEndOfFrame()
{
    DebugDisplayTextBuffer();
    FlushObrsPrintfs();
    RwCameraEndUpdate( Scene.m_pRwCamera );
    RsCameraShowRaster( Scene.m_pRwCamera );
}

void CSAIdleHook::PrepareRwCamera()
{
    CDraw::CalculateAspectRatio(); // CDraw::CalculateAspectRatio
    CameraSize( Scene.m_pRwCamera, 0, tanf( CDraw::ms_fFOV * ( 3.1415927f / 360.0f ) ), CDraw::ms_fAspectRatio );
    CVisibilityPlugins::SetRenderWareCamera( Scene.m_pRwCamera ); // CVisibilityPlugins::SetRenderWareCamera
    RwCameraClear( Scene.m_pRwCamera, gColourTop, rwCAMERACLEARZ );
}

void CSAIdleHook::LightUpdate()
{
    CPointLights::NumLights = 0;
    CLightManager::Reset();
    UpdateShadowDNBalance();
    SetLightsWithTimeOfDayColour( Scene.m_pRpWorld );
}

void CSAIdleHook::GameUpdate()
{
    CGame::Process();
    CAudioEngine__Service( AudioEngine );
}

void CSAIdleHook::InitPerFrame2D()
{
    CSprite2d::InitPerFrame();
    CFont::InitPerFrame();
}

void CSAIdleHook::TimeUpdate()
{
    //while (CTimer__GetTimeMillisecondsFromStart() - CGame__TimeMillisecondsFromStart < 1)
    //	;
    CGame__TimeMillisecondsFromStart = CTimer__GetTimeMillisecondsFromStart();
    CTimer__Update();
}


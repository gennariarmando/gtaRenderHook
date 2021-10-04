// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "plugin.h"

#include "CDebug.h"
#include "RwD3D1XEngine.h"
#include "SAIdleHook.h"
#include "CustomBuildingPipeline.h"
#include "CustomBuildingDNPipeline.h"
#include "CustomCarFXPipeline.h"
#include "CustomSeabedPipeline.h"
#include "CustomWaterPipeline.h"
#include "DeferredRenderer.h"
#include "VoxelOctreeRenderer.h"
#include "LightManager.h"
#include "FullscreenQuad.h"
#include "D3D1XTextureMemoryManager.h"
#include "DebugBBox.h"
#include "HDRTonemapping.h"
#include "ShadowRenderer.h"
#include "gta_sa_ptrs.h"
#include "SettingsHolder.h"
#include "D3DRenderer.h"
#include "PBSMaterial.h"
#include "D3D1XStateManager.h"
#include "CVisibilityPluginsRH.h"
#include <game_sa\CModelInfo.h>
#include <game_sa\CVehicle.h>
#include <game_sa\CRadar.h>
#include <game_sa\CGame.h>
#include "DebugRendering.h"
#include "VolumetricLighting.h"
#include "AmbientOcclusion.h"
#include <game_sa\CStreaming.h>
#include <game_sa\CEntity.h>
#include "CRwGameHooks.h"
#include "GTASAHooks.h"
#include "StreamingRH.h"
#include "SampHaxx.h"
#include "TemporalAA.h"

CDebug*				g_pDebug;
CIRwRenderEngine*	g_pRwCustomEngine;

void Init() 
{
    // Load RenderHook settings
    SettingsHolder::Instance()->ReloadFile();
    // Init basic stuff
    g_pDebug = new CDebug("debug.log");
    g_pRwCustomEngine = new CRwD3D1XEngine(g_pDebug);

    // Path rw/game stuff
    CRwGameHooks::Patch(CRwGameHooks::ms_rwPointerTableSA);
    CGTASAHooks::Patch();
    // Replace all pipelines(move to CRwGameHooks or CGTASAHooks)
    CCustomBuildingPipeline::Patch();
    CCustomBuildingDNPipeline::Patch();
    CCustomCarFXPipeline::Patch();
    SampHaxx::Patch();
}

void Shutdown() {
    delete g_pDebug;
    delete g_pRwCustomEngine;
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER( lpReserved );
    UNREFERENCED_PARAMETER( hModule );
    switch ( ul_reason_for_call )
    {
    case DLL_PROCESS_ATTACH:
        Init();
        break;
    case DLL_PROCESS_DETACH:
        Shutdown();
        break;
    default:
        break;
    }
    return TRUE;
}

// TODO: move this out of here
RxD3D9InstanceData* GetModelsData( RxInstanceData * data )
{
    return reinterpret_cast<RxD3D9InstanceData*>( data + 1 );
}

RxD3D9InstanceData *GetModelsData2( RxD3D9ResEntryHeader *data )
{
    return reinterpret_cast<RxD3D9InstanceData *>( data + 1 );
}

RpMesh * GetMeshesData( RpD3DMeshHeader * data )
{
    return reinterpret_cast<RpMesh*>( data + 1 );
}

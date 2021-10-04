// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "plugin.h"
#include "D3D1XVertexDeclarationManager.h"
#include "D3D1XVertexDeclaration.h"

std::list<CD3D1XVertexDeclaration*> CD3D1XVertexDeclarationManager::vdeclList = {};

CD3D1XVertexDeclaration * CD3D1XVertexDeclarationManager::AddNew( CD3D1XShader* pVS, UINT flags )
{
    for ( auto &vdecl : vdeclList )
    {
        if ( vdecl->getShader() == pVS && vdecl->getInputInfo() == flags )
            return vdecl;
    }
    vdeclList.push_back( new CD3D1XVertexDeclaration( pVS, flags ) );
    return vdeclList.back();
}

void CD3D1XVertexDeclarationManager::Shutdown()
{
    for ( auto &vdecl : vdeclList )
    {
        delete vdecl;
    }
}

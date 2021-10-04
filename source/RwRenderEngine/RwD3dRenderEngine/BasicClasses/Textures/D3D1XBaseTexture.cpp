// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "plugin.h"
#include "D3D1XBaseTexture.h"
#include "CDebug.h"

CD3D1XBaseTexture::CD3D1XBaseTexture( RwRaster * parent, eTextureDimension dimension, const std::string& resourceTypeName ) :
    m_pParent( parent ), m_dimension( dimension ), m_resourceTypeName( resourceTypeName )
{
}

CD3D1XBaseTexture::~CD3D1XBaseTexture()
{
    if ( m_pTextureResource )
    {
        m_pTextureResource->Release();
        m_pTextureResource = nullptr;
    }
}

void CD3D1XBaseTexture::SetDebugName( const std::string & name )
{
    if ( m_pTextureResource )
        g_pDebug->SetD3DName( m_pTextureResource, name + "(" + m_resourceTypeName + ", TextureBuffer)" );
}

void CD3D1XBaseTexture::Reallocate()
{
}

void * CD3D1XBaseTexture::LockToRead()
{
    return nullptr;
}

void CD3D1XBaseTexture::UnlockFromRead()
{
}

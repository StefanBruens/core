/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <opengl/program.hxx>

#include <vcl/opengl/OpenGLHelper.hxx>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

OpenGLProgram::OpenGLProgram() :
    mnId( 0 ),
    mnEnabledAttribs( 0 ),
    mnPositionAttrib( SAL_MAX_UINT32 ),
    mnTexCoordAttrib( SAL_MAX_UINT32 ),
    mnAlphaCoordAttrib( SAL_MAX_UINT32 ),
    mbBlending( false )
{
}

OpenGLProgram::~OpenGLProgram()
{
    maUniformLocations.clear();
    if( mnId != 0 )
        glDeleteProgram( mnId );
}

bool OpenGLProgram::Load( const OUString& rVertexShader, const OUString& rFragmentShader )
{
    mnId = OpenGLHelper::LoadShaders( rVertexShader, rFragmentShader );
    return ( mnId != 0 );
}

bool OpenGLProgram::Use()
{
    if( !mnId )
        return false;

    glUseProgram( mnId );
    return true;
}

bool OpenGLProgram::Clean()
{
    // unbind all textures
    if( !maTextures.empty() )
    {
        int nIndex( maTextures.size() - 1 );
        TextureList::reverse_iterator it( maTextures.rbegin() );
        while( it != maTextures.rend() )
        {
            glActiveTexture( GL_TEXTURE0 + nIndex-- );
            it->Unbind();
            ++it;
        }
        maTextures.clear();
    }

    // disable any enabled vertex attrib array
    if( mnEnabledAttribs )
    {
        for( int i = 0; i < 32; i++ )
        {
            if( mnEnabledAttribs & ( 1 << i ) )
                glDisableVertexAttribArray( i );
        }
        mnEnabledAttribs = 0;
    }

    // disable blending if enabled
    if( mbBlending )
    {
        mbBlending = false;
        glDisable( GL_BLEND );
    }

    CHECK_GL_ERROR();
    return true;
}

void OpenGLProgram::SetVertexAttrib( GLuint& rAttrib, const OString& rName, const GLvoid* pData )
{
    if( rAttrib == SAL_MAX_UINT32 )
        rAttrib = glGetAttribLocation( mnId, (char*) rName.getStr() );
    if( (mnEnabledAttribs & ( 1 << rAttrib )) == 0 )
    {
        glEnableVertexAttribArray( rAttrib );
        mnEnabledAttribs |= ( 1 << rAttrib );
    }
    glVertexAttribPointer( rAttrib, 2, GL_FLOAT, GL_FALSE, 0, pData );
}

void OpenGLProgram::SetVertices( const GLvoid* pData )
{
    SetVertexAttrib( mnPositionAttrib, "position", pData );
}

void OpenGLProgram::SetTextureCoord( const GLvoid* pData )
{
    SetVertexAttrib( mnTexCoordAttrib, "tex_coord_in", pData );
}

void OpenGLProgram::SetAlphaCoord( const GLvoid* pData )
{
    SetVertexAttrib( mnAlphaCoordAttrib, "alpha_coord_in", pData );
}

GLuint OpenGLProgram::GetUniformLocation( const OString& rName )
{
    boost::unordered_map<OString, GLuint>::iterator it;

    it = maUniformLocations.find( rName );
    if( it == maUniformLocations.end() )
    {
        GLuint nLocation = glGetUniformLocation( mnId, (char*) rName.getStr() );
        maUniformLocations[rName] = nLocation;
        return nLocation;
    }

    return it->second;
}

void OpenGLProgram::SetUniform1f( const OString& rName, GLfloat v1 )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform1f( nUniform, v1 );
}

void OpenGLProgram::SetUniform2f( const OString& rName, GLfloat v1, GLfloat v2 )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform2f( nUniform, v1, v2 );
}

void OpenGLProgram::SetUniform1fv( const OString& rName, GLsizei nCount, GLfloat* aValues )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform1fv( nUniform, nCount, aValues );
}

void OpenGLProgram::SetUniform2fv( const OString& rName, GLsizei nCount, GLfloat* aValues )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform2fv( nUniform, nCount, aValues );
}

void OpenGLProgram::SetColor( const OString& rName, SalColor nColor, sal_uInt8 nTransparency )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform4f( nUniform,
                 ((float) SALCOLOR_RED( nColor )) / 255,
                 ((float) SALCOLOR_GREEN( nColor )) / 255,
                 ((float) SALCOLOR_BLUE( nColor )) / 255,
                 (100 - nTransparency) * (1.0 / 100) );

    if( nTransparency > 0 )
        SetBlendMode( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

void OpenGLProgram::SetColorf( const OString& rName, SalColor nColor, double fTransparency )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform4f( nUniform,
                 ((float) SALCOLOR_RED( nColor )) / 255,
                 ((float) SALCOLOR_GREEN( nColor )) / 255,
                 ((float) SALCOLOR_BLUE( nColor )) / 255,
                 (1.0f - fTransparency) );

    if( fTransparency > 0.0 )
        SetBlendMode( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

void OpenGLProgram::SetColor( const OString& rName, const Color& rColor )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform4f( nUniform,
                 ((float) rColor.GetRed()) / 255,
                 ((float) rColor.GetGreen()) / 255,
                 ((float) rColor.GetBlue()) / 255,
                 1.0f - ((float) rColor.GetTransparency()) / 255 );

    if( rColor.GetTransparency() > 0 )
        SetBlendMode( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
}

void OpenGLProgram::SetColorWithIntensity( const OString& rName, const Color& rColor, long nFactor )
{
    GLuint nUniform = GetUniformLocation( rName );
    glUniform4f( nUniform,
                 ((float) rColor.GetRed()) * nFactor / 25500.0,
                 ((float) rColor.GetGreen()) * nFactor / 25500.0,
                 ((float) rColor.GetBlue()) * nFactor / 25500.0,
                 1.0f );
}

void OpenGLProgram::SetTexture( const OString& rName, OpenGLTexture& rTexture )
{
    GLuint nUniform = GetUniformLocation( rName );
    int nIndex = maTextures.size();

    glUniform1i( nUniform, nIndex );
    glActiveTexture( GL_TEXTURE0 + nIndex );
    rTexture.Bind();
    maTextures.push_back( rTexture );
}

void OpenGLProgram::SetTransform(
    const OString& rName,
    const OpenGLTexture& rTexture,
    const basegfx::B2DPoint& rNull,
    const basegfx::B2DPoint& rX,
    const basegfx::B2DPoint& rY )
{
    auto nTexWidth = rTexture.GetWidth();
    auto nTexHeight = rTexture.GetHeight();
    if (nTexWidth == 0 || nTexHeight == 0)
        return;

    GLuint nUniform = GetUniformLocation( rName );
    const basegfx::B2DVector aXRel = rX - rNull;
    const basegfx::B2DVector aYRel = rY - rNull;
    const float aValues[] = {
        (float) aXRel.getX()/nTexWidth,  (float) aXRel.getY()/nTexWidth,  0, 0,
        (float) aYRel.getX()/nTexHeight, (float) aYRel.getY()/nTexHeight, 0, 0,
        0,                               0,                               1, 0,
        (float) rNull.getX(),            (float) rNull.getY(),            0, 1 };
    glm::mat4 mMatrix = glm::make_mat4( aValues );
    glUniformMatrix4fv( nUniform, 1, GL_FALSE, glm::value_ptr( mMatrix ) );
}

void OpenGLProgram::SetBlendMode( GLenum nSFactor, GLenum nDFactor )
{
    glEnable( GL_BLEND );
    glBlendFunc( nSFactor, nDFactor );
    mbBlending = true;
}

bool OpenGLProgram::DrawTexture( OpenGLTexture& rTexture )
{
    GLfloat aPosition[8] = { -1, -1, -1, 1, 1, 1, 1, -1 };
    GLfloat aTexCoord[8];

    if( !rTexture )
        return false;

    rTexture.GetWholeCoord( aTexCoord );
    SetVertices( aPosition );
    SetTextureCoord( aTexCoord );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
    CHECK_GL_ERROR();

    return true;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

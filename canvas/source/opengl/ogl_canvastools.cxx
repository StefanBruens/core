/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ogl_canvastools.hxx"

#include <canvas/debug.hxx>
#include <tools/diagnose_ex.h>
#include <basegfx/tools/canvastools.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/tools/tools.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolygontriangulator.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>

#include <com/sun/star/rendering/ARGBColor.hpp>

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>


using namespace ::com::sun::star;

namespace oglcanvas
{
    /// triangulates polygon before
    //move to canvashelper, or take renderHelper as parameter?
    void renderComplexPolyPolygon( const ::basegfx::B2DPolyPolygon& rPolyPoly )
    {
        ::basegfx::B2DPolyPolygon aPolyPoly(rPolyPoly);
        if( aPolyPoly.areControlPointsUsed() )
            aPolyPoly = rPolyPoly.getDefaultAdaptiveSubdivision();
        const ::basegfx::B2DRange& rBounds(aPolyPoly.getB2DRange());
        const double nWidth=rBounds.getWidth();
        const double nHeight=rBounds.getHeight();
        const ::basegfx::B2DPolygon& rTriangulatedPolygon(
            ::basegfx::triangulator::triangulate(aPolyPoly));

        for( sal_uInt32 i=0; i<rTriangulatedPolygon.count(); i++ )
        {
            const ::basegfx::B2DPoint& rPt( rTriangulatedPolygon.getB2DPoint(i) );
            const double s(rPt.getX()/nWidth);
            const double t(rPt.getY()/nHeight);
            glTexCoord2f(s,t); glVertex2d(rPt.getX(), rPt.getY());
        }
    }

    /** only use this for line polygons.

        better not leave triangulation to OpenGL. also, ignores texturing
    */
    void renderPolyPolygon( const ::basegfx::B2DPolyPolygon& rPolyPoly, RenderHelper *renderHelper, glm::vec4 color)
    {
        ::basegfx::B2DPolyPolygon aPolyPoly(rPolyPoly);
        if( aPolyPoly.areControlPointsUsed() )
            aPolyPoly = rPolyPoly.getDefaultAdaptiveSubdivision();
        for(sal_uInt32 i=0; i<aPolyPoly.count(); i++ )
        {


            const ::basegfx::B2DPolygon& rPolygon( aPolyPoly.getB2DPolygon(i) );

            const sal_uInt32 nPts=rPolygon.count();
            const sal_uInt32 nExtPts=nPts + int(rPolygon.isClosed());
            GLfloat vertices[nExtPts*2];
            for( sal_uInt32 j=0; j<nExtPts; j++ )
            {
                const ::basegfx::B2DPoint& rPt( rPolygon.getB2DPoint( j % nPts ) );
                vertices[j*2] = rPt.getX();
                vertices[j*2+1] = rPt.getY();
            }
            renderHelper->renderVertexConstColor(vertices, color, GL_LINE_STRIP);
            /*
            glBegin(GL_LINE_STRIP);

            const ::basegfx::B2DPolygon& rPolygon( aPolyPoly.getB2DPolygon(i) );

            const sal_uInt32 nPts=rPolygon.count();
            const sal_uInt32 nExtPts=nPts + int(rPolygon.isClosed());
            for( sal_uInt32 j=0; j<nExtPts; j++ )
            {
                const ::basegfx::B2DPoint& rPt( rPolygon.getB2DPoint( j % nPts ) );
                glVertex2d(rPt.getX(), rPt.getY());
            }

            glEnd();
            */

        }

    }

    glm::mat4 setupState( const ::basegfx::B2DHomMatrix&   rTransform,
                     GLenum                           eSrcBlend,
                     GLenum                           eDstBlend)
    {
        float aGLTransform[] =
            {
                (float) rTransform.get(0,0), (float) rTransform.get(1,0), 0, 0,
                (float) rTransform.get(0,1), (float) rTransform.get(1,1), 0, 0,
                0,                   0,                   1, 0,
                (float) rTransform.get(0,2), (float) rTransform.get(1,2), 0, 1
            };

        glEnable(GL_BLEND);
        glBlendFunc(eSrcBlend, eDstBlend);
        return glm::make_mat4(aGLTransform);
    }

    void renderOSD( const std::vector<double>& rNumbers, double scale, RenderHelper *renderHelper)
    {
        double y=4.0;
        basegfx::B2DHomMatrix aTmp;
        basegfx::B2DHomMatrix aScaleShear;
        aScaleShear.shearX(-0.1);
        aScaleShear.scale(scale,scale);

        for( size_t i=0; i<rNumbers.size(); ++i )
        {
            aTmp.identity();
            aTmp.translate(0,y);
            y += 1.2*scale; //send to renderHelper

            basegfx::B2DPolyPolygon aPoly=
                basegfx::tools::number2PolyPolygon(rNumbers[i],10,3);

            aTmp=aTmp*aScaleShear;
            aPoly.transform(aTmp);
            glColor4f(0,1,0,1);
            glm::vec4 color  = glm::vec4(1, 0, 0, 1);
            renderPolyPolygon(aPoly, renderHelper, color);
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

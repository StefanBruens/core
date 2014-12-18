/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ogl_canvashelper.hxx"

#include <rtl/crc.h>
#include <canvas/debug.hxx>
#include <tools/diagnose_ex.h>
#include <basegfx/tools/canvastools.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolygontriangulator.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>

 #include <basegfx/polygon/b2dpolygontools.hxx>

#include <com/sun/star/rendering/TexturingMode.hpp>
#include <com/sun/star/rendering/CompositeOperation.hpp>
#include <com/sun/star/rendering/RepaintResult.hpp>
#include <com/sun/star/rendering/PathCapType.hpp>
#include <com/sun/star/rendering/PathJoinType.hpp>

#include <vcl/virdev.hxx>
#include <vcl/metric.hxx>
#include <vcl/font.hxx>

#include <basegfx/polygon/b2dlinegeometry.hxx>

#include "ogl_canvasfont.hxx"
#include "ogl_canvastools.hxx"
#include "ogl_canvasbitmap.hxx"
#include "ogl_spritecanvas.hxx"
#include "ogl_texturecache.hxx"
#include "ogl_tools.hxx"

#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <vcl/opengl/GLMHelper.hxx>

#include <boost/scoped_array.hpp>


using namespace ::com::sun::star;

namespace oglcanvas
{
    /* Concepts:
       =========

       This OpenGL canvas implementation tries to keep all render
       output as high-level as possible, i.e. geometry data and
       externally-provided bitmaps. Therefore, calls at the
       XCanvas-interfaces are not immediately transformed into colored
       pixel inside some GL buffer, but are retained simply with their
       call parameters. Only after XSpriteCanvas::updateScreen() has
       been called, this all gets transferred to the OpenGL subsystem
       and converted to a visible scene. The big advantage is, this
       makes sprite modifications practically zero-overhead, and saves
       a lot on texture memory (compared to the directx canvas, which
       immediately dumps every render call into a texture).

       The drawback, of course, is that complex images churn a lot of
       GPU cycles on every re-rendering.

       For the while, I'll be using immediate mode, i.e. transfer data
       over and over again to the OpenGL subsystem. Alternatively,
       there are display lists, which at least keep the data on the
       server, or even better, vertex buffers, which copy geometry
       data over en bloc.

       Next todo: put polygon geometry into vertex buffer (LRU cache
       necessary?) - or, rather, buffer objects! prune entries older
       than one updateScreen() call)

       Text: http://www.opengl.org/resources/features/fontsurvey/
     */

    struct CanvasHelper::Action
    {
        ::basegfx::B2DHomMatrix         maTransform;
        GLenum                          meSrcBlendMode;
        GLenum                          meDstBlendMode;
        rendering::ARGBColor            maARGBColor;
        ::basegfx::B2DPolyPolygonVector maPolyPolys;

        ::boost::function6< bool,
                            const CanvasHelper&,
                            const ::basegfx::B2DHomMatrix&,
                            GLenum,
                            GLenum,
                            const rendering::ARGBColor&,
                            const ::basegfx::B2DPolyPolygonVector& > maFunction;
    };

    namespace
    {


       basegfx::B2DLineJoin b2DJoineFromJoin( sal_Int8 nJoinType )
       {
           switch( nJoinType )
           {
               case rendering::PathJoinType::NONE:
                   return basegfx::B2DLINEJOIN_NONE;

               case rendering::PathJoinType::MITER:
                   return basegfx::B2DLINEJOIN_MITER;

               case rendering::PathJoinType::ROUND:
                   return basegfx::B2DLINEJOIN_ROUND;

               case rendering::PathJoinType::BEVEL:
                   return basegfx::B2DLINEJOIN_BEVEL;

               default:
                   ENSURE_OR_THROW( false,
                                     "b2DJoineFromJoin(): Unexpected join type" );
           }

           return basegfx::B2DLINEJOIN_NONE;
       }

       drawing::LineCap unoCapeFromCap( sal_Int8 nCapType)
       {
           switch ( nCapType)
           {
               case rendering::PathCapType::BUTT:
                   return drawing::LineCap_BUTT;

               case rendering::PathCapType::ROUND:
                   return drawing::LineCap_ROUND;

               case rendering::PathCapType::SQUARE:
                   return drawing::LineCap_SQUARE;

               default:
                   ENSURE_OR_THROW( false,
                                     "unoCapeFromCap(): Unexpected cap type" );
           }
           return drawing::LineCap_BUTT;
       }


        bool lcl_drawPoint( const CanvasHelper&              rHelper,
                            const ::basegfx::B2DHomMatrix&   rTransform,
                            GLenum                           eSrcBlend,
                            GLenum                           eDstBlend,
                            const rendering::ARGBColor&      rColor,
                            const geometry::RealPoint2D&     rPoint)
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            glm::vec4 color  = glm::vec4( (float) rColor.Red,
                                (float) rColor.Green,
                                (float) rColor.Blue,
                                (float) rColor.Alpha);
            std::vector<glm::vec2> vertices;
            vertices.reserve(1);
            vertices.push_back(glm::vec2((float) rPoint.X, (float) rPoint.Y));
            pRenderHelper->renderVertexConstColor(vertices, color, GL_POINTS);
            return true;
        }

        bool lcl_drawLine( const CanvasHelper&              rHelper,
                           const ::basegfx::B2DHomMatrix&   rTransform,
                           GLenum                           eSrcBlend,
                           GLenum                           eDstBlend,
                           const rendering::ARGBColor&      rColor,
                           const geometry::RealPoint2D&     rStartPoint,
                           const geometry::RealPoint2D&     rEndPoint)
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            glm::vec4 color  = glm::vec4( (float) rColor.Red,
                                (float) rColor.Green,
                                (float) rColor.Blue,
                                (float) rColor.Alpha);

            std::vector<glm::vec2> vertices;
            vertices.reserve(2);
            vertices.push_back(glm::vec2((float) rStartPoint.X, (float) rStartPoint.Y));
            vertices.push_back(glm::vec2((float) rEndPoint.X, (float) rEndPoint.Y));
            pRenderHelper->renderVertexConstColor(vertices, color, GL_LINES);
            return true;
        }

        bool lcl_drawPolyPolygon( const CanvasHelper&                    rHelper,
                                  const ::basegfx::B2DHomMatrix&         rTransform,
                                  GLenum                                 eSrcBlend,
                                  GLenum                                 eDstBlend,
                                  const rendering::ARGBColor&            rColor,
                                  const ::basegfx::B2DPolyPolygonVector& rPolyPolygons)
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            glm::vec4 color  = glm::vec4( (float) rColor.Red,
                                (float) rColor.Green,
                                (float) rColor.Blue,
                                (float) rColor.Alpha);

            ::basegfx::B2DPolyPolygonVector::const_iterator aCurr=rPolyPolygons.begin();
            const ::basegfx::B2DPolyPolygonVector::const_iterator aEnd=rPolyPolygons.end();
            while( aCurr != aEnd )
            {
                renderPolyPolygon(*aCurr++, pRenderHelper, color);
            }

            return true;
        }

        bool lcl_fillPolyPolygon( const CanvasHelper&                    rHelper,
                                  const ::basegfx::B2DHomMatrix&         rTransform,
                                  GLenum                                 eSrcBlend,
                                  GLenum                                 eDstBlend,
                                  const rendering::ARGBColor&            rColor,
                                  const ::basegfx::B2DPolyPolygonVector& rPolyPolygons )
        {
            //no texture bind ?
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            glm::vec4 color  = glm::vec4( (float) rColor.Red,
                                          (float) rColor.Green,
                                          (float) rColor.Blue,
                                          (float) rColor.Alpha);

            ::basegfx::B2DPolyPolygonVector::const_iterator aCurr=rPolyPolygons.begin();
            const ::basegfx::B2DPolyPolygonVector::const_iterator aEnd=rPolyPolygons.end();
            while( aCurr != aEnd )
            {
                renderComplexPolyPolygon(*aCurr++, pRenderHelper, color);
            }

            return true;
        }

        bool lcl_fillGradientPolyPolygon( const CanvasHelper&                            rHelper,
                                          const ::basegfx::B2DHomMatrix&                 rTransform,
                                          GLenum                                         eSrcBlend,
                                          GLenum                                         eDstBlend,
                                          const ::canvas::ParametricPolyPolygon::Values& rValues,
                                          const rendering::Texture&                      rTexture,
                                          const ::basegfx::B2DPolyPolygonVector&         rPolyPolygons )
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            glm::vec4 color  = glm::vec4( (float) rendering::ARGBColor().Red,
                                (float) rendering::ARGBColor().Green,
                                (float) rendering::ARGBColor().Blue,
                                (float) rendering::ARGBColor().Alpha);

            // convert to weird canvas textur coordinate system (not
            // [0,1]^2, but path coordinate system)
            ::basegfx::B2DHomMatrix aTextureTransform;
            ::basegfx::unotools::homMatrixFromAffineMatrix( aTextureTransform,
                                                            rTexture.AffineTransform );
            ::basegfx::B2DRange aBounds;
            ::basegfx::B2DPolyPolygonVector::const_iterator aCurr=rPolyPolygons.begin();
            const ::basegfx::B2DPolyPolygonVector::const_iterator aEnd=rPolyPolygons.end();
            while( aCurr != aEnd )
                aBounds.expand(::basegfx::tools::getRange(*aCurr++));
            aTextureTransform.translate(-aBounds.getMinX(), -aBounds.getMinY());
            aTextureTransform.scale(1/aBounds.getWidth(), 1/aBounds.getHeight());
            aTextureTransform.invert();
            const float aTextureTransformation[] =
            {
                    float(aTextureTransform.get(0,0)), float(aTextureTransform.get(1,0)),
                    float(aTextureTransform.get(0,1)), float(aTextureTransform.get(1,1)),
                    float(aTextureTransform.get(0,2)), float(aTextureTransform.get(1,2))
            };
            const glm::mat3x2 aTexTransform = glm::make_mat3x2(aTextureTransformation);

            const sal_Int32 nNumCols=rValues.maColors.getLength();
            uno::Sequence< rendering::ARGBColor > aColors(nNumCols);
            rendering::ARGBColor* const pColors=aColors.getArray();
            rendering::ARGBColor* pCurrCol=pColors;
            for( sal_Int32 i=0; i<nNumCols; ++i )
                *pCurrCol++ = rHelper.getDevice()->getDeviceColorSpace()->convertToARGB(rValues.maColors[i])[0];

            OSL_ASSERT(nNumCols == rValues.maStops.getLength());

            aCurr=rPolyPolygons.begin();
            while( aCurr != aEnd )
            {
                ::basegfx::B2DPolyPolygon aPolyPoly(*aCurr++);
                if( aPolyPoly.areControlPointsUsed() )
                    aPolyPoly =   aPolyPoly.getDefaultAdaptiveSubdivision();
                const ::basegfx::B2DRange& rBounds(aPolyPoly.getB2DRange());
                const double nWidth=rBounds.getWidth();
                const double nHeight=rBounds.getHeight();
                const ::basegfx::B2DPolygon& rTriangulatedPolygon(
                    ::basegfx::triangulator::triangulate(aPolyPoly));
                if(rTriangulatedPolygon.count()>0)
                {
                    std::vector<glm::vec2> vertices;
                    vertices.reserve(rTriangulatedPolygon.count());
                    for( sal_uInt32 i=0; i<rTriangulatedPolygon.count(); i++ )
                    {
                        const ::basegfx::B2DPoint& rPt( rTriangulatedPolygon.getB2DPoint(i) );
                        vertices.push_back(glm::vec2(rPt.getX(),rPt.getY()));
                    }
                    switch(rValues.meType)
                    {
                        case ::canvas::ParametricPolyPolygon::GRADIENT_LINEAR:
                            pRenderHelper->renderLinearGradient( vertices, nWidth, nHeight, GL_TRIANGLES,
                                                    pColors, rValues.maStops, aTexTransform);
                            break;
                        case ::canvas::ParametricPolyPolygon::GRADIENT_ELLIPTICAL:
                            pRenderHelper->renderRadialGradient( vertices, nWidth, nHeight, GL_TRIANGLES,
                                                    pColors, rValues.maStops, aTexTransform);
                            break;
                        case ::canvas::ParametricPolyPolygon::GRADIENT_RECTANGULAR:
                            pRenderHelper->renderRectangularGradient( vertices, nWidth, nHeight, GL_TRIANGLES,
                                                    pColors, rValues.maStops, aTexTransform);
                            break;
                        default:
                            ENSURE_OR_THROW( false,
                                      "CanvasHelper lcl_fillGradientPolyPolygon(): Unexpected case" );
                    }
                }
            }

            glUseProgram(0);

            return true;
        }

        bool lcl_drawOwnBitmap( const CanvasHelper&              rHelper,
                                const ::basegfx::B2DHomMatrix&   rTransform,
                                GLenum                           eSrcBlend,
                                GLenum                           eDstBlend,
                                const rendering::ARGBColor&      rColor,
                                const CanvasBitmap&              rBitmap )
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));
            return rBitmap.renderRecordedActions();
        }

        bool lcl_drawGenericBitmap( const CanvasHelper&              rHelper,
                                    const ::basegfx::B2DHomMatrix&   rTransform,
                                    GLenum                           eSrcBlend,
                                    GLenum                           eDstBlend,
                                    const rendering::ARGBColor&      rColor,
                                    const geometry::IntegerSize2D&   rPixelSize,
                                    const uno::Sequence<sal_Int8>&   rPixelData,
                                    sal_uInt32                       nPixelCrc32)

        {

            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));

            const unsigned int nTexId=rHelper.getDeviceHelper()->getTextureCache().getTexture(
                rPixelSize, rPixelData.getConstArray(), nPixelCrc32);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, nTexId);
            glEnable(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,
                        GL_ONE_MINUS_SRC_ALPHA);

            // blend against fixed vertex color; texture alpha is multiplied in
            glm::vec4 color  = glm::vec4(1, 1, 1, 1);

            std::vector<glm::vec2> vertices;
            vertices.reserve(4);
            vertices.push_back(glm::vec2((float) rPixelSize.Width, 0));
            vertices.push_back(glm::vec2((float) rPixelSize.Width, (float) rPixelSize.Height));
            vertices.push_back(glm::vec2(0, 0));
            vertices.push_back(glm::vec2(0, (float) rPixelSize.Height));

            std::vector<glm::vec2> uvCoordinates;
            uvCoordinates.reserve(4);
            uvCoordinates.push_back(glm::vec2(1, 1));
            uvCoordinates.push_back(glm::vec2(1, 0));
            uvCoordinates.push_back(glm::vec2(0, 1));
            uvCoordinates.push_back(glm::vec2(0, 0));
            pRenderHelper->renderVertexUVTex(vertices, uvCoordinates, color, GL_TRIANGLE_STRIP );

            glBindTexture(GL_TEXTURE_2D, 0);

            return true;
        }

        bool lcl_fillTexturedPolyPolygon( const CanvasHelper&                    rHelper,
                                          const ::basegfx::B2DHomMatrix&         rTransform,
                                          GLenum                                 eSrcBlend,
                                          GLenum                                 eDstBlend,
                                          const rendering::Texture&              rTexture,
                                          const geometry::IntegerSize2D&         rPixelSize,
                                          const uno::Sequence<sal_Int8>&         rPixelData,
                                          sal_uInt32                             nPixelCrc32,
                                          const ::basegfx::B2DPolyPolygonVector& rPolyPolygons )
        {
            RenderHelper* pRenderHelper = rHelper.getDeviceHelper()->getRenderHelper();
            pRenderHelper->SetModelAndMVP(setupState(rTransform, eSrcBlend, eDstBlend));

            //blend against fixed color
            glm::vec4 color  = glm::vec4(1,1,1,1);
            const unsigned int nTexId=rHelper.getDeviceHelper()->getTextureCache().getTexture(
                rPixelSize, rPixelData.getConstArray(), nPixelCrc32);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, nTexId);
            glEnable(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,
                            GL_TEXTURE_MAG_FILTER,
                            GL_NEAREST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,
                        GL_ONE_MINUS_SRC_ALPHA);

            // convert to weird canvas textur coordinate system (not
            // [0,1]^2, but path coordinate system)
            ::basegfx::B2DHomMatrix aTextureTransform;
            ::basegfx::unotools::homMatrixFromAffineMatrix( aTextureTransform,
                                                            rTexture.AffineTransform );
            ::basegfx::B2DRange aBounds;
            ::basegfx::B2DPolyPolygonVector::const_iterator aCurr=rPolyPolygons.begin();
            const ::basegfx::B2DPolyPolygonVector::const_iterator aEnd=rPolyPolygons.end();
            while( aCurr != aEnd )
                aBounds.expand(::basegfx::tools::getRange(*aCurr++));
            aTextureTransform.translate(-aBounds.getMinX(), -aBounds.getMinY());
            aTextureTransform.scale(1/aBounds.getWidth(), 1/aBounds.getHeight());
            aTextureTransform.invert();

            float aTexTransform[] =
                {
                    (float) aTextureTransform.get(0,0), (float) aTextureTransform.get(1,0), 0, 0,
                    (float) aTextureTransform.get(0,1), -(float) aTextureTransform.get(1,1), 0, 0,
                    0,                          0,                          1, 0,
                    (float) aTextureTransform.get(0,2), (float) aTextureTransform.get(1,2), 0, 1
                };

            aCurr=rPolyPolygons.begin();
            while( aCurr != aEnd )
            {
                renderTransformComplexPolygon(*aCurr++, pRenderHelper, color, glm::make_mat4(aTexTransform));
            }


            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);

            return true;
        }
    }

    CanvasHelper::CanvasHelper() :
        mpDevice( NULL ),
        mpDeviceHelper( NULL ),
        mpRecordedActions()
    {
    }

    CanvasHelper::~CanvasHelper()
    {}

    CanvasHelper& CanvasHelper::operator=( const CanvasHelper& rSrc )
    {
        mpDevice = rSrc.mpDevice;
        mpDeviceHelper = rSrc.mpDeviceHelper;
        mpRecordedActions = rSrc.mpRecordedActions;
        return *this;
    }

    void CanvasHelper::disposing()
    {
        RecordVectorT aThrowaway;
        mpRecordedActions.swap( aThrowaway );
        mpDevice = NULL;
        mpDeviceHelper = NULL;
    }

    void CanvasHelper::init( rendering::XGraphicDevice& rDevice,
                             SpriteDeviceHelper& rDeviceHelper )
    {
        mpDevice = &rDevice;
        mpDeviceHelper = &rDeviceHelper;
    }

    void CanvasHelper::clear()
    {
        mpRecordedActions->clear();
    }

    void CanvasHelper::drawPoint( const rendering::XCanvas*     /*pCanvas*/,
                                  const geometry::RealPoint2D&  aPoint,
                                  const rendering::ViewState&   viewState,
                                  const rendering::RenderState& renderState )
    {
        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            rAct.maFunction = ::boost::bind(&lcl_drawPoint,
                                            _1,_2,_3,_4,_5,
                                            aPoint);
        }
    }

    void CanvasHelper::drawLine( const rendering::XCanvas*      /*pCanvas*/,
                                 const geometry::RealPoint2D&   aStartPoint,
                                 const geometry::RealPoint2D&   aEndPoint,
                                 const rendering::ViewState&    viewState,
                                 const rendering::RenderState&  renderState )
    {
        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();
            setupGraphicsState( rAct, viewState, renderState );
            rAct.maFunction = ::boost::bind(&lcl_drawLine,
                                            _1,_2,_3,_4,_5,
                                            aStartPoint,aEndPoint);
        }
    }

    void CanvasHelper::drawBezier( const rendering::XCanvas*            /*pCanvas*/,
                                   const geometry::RealBezierSegment2D& aBezierSegment,
                                   const geometry::RealPoint2D&         aEndPoint,
                                   const rendering::ViewState&          viewState,
                                   const rendering::RenderState&        renderState )
    {
        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            //untested code, otherways commented out should work...
            basegfx::B2DPolygon aPoly;
            aPoly.append(basegfx::B2DPoint(aBezierSegment.Px, aBezierSegment.Py));
            aPoly.appendBezierSegment( basegfx::B2DPoint(aBezierSegment.C1x, aBezierSegment.C1y),
                                       basegfx::B2DPoint(aBezierSegment.C1x, aBezierSegment.C1y),
                                       basegfx::B2DPoint(aEndPoint.X,aEndPoint.Y));

            basegfx::B2DPolyPolygon aPolyPoly;
            aPolyPoly.append(aPoly);
            rAct.maPolyPolys.push_back(aPolyPoly);
            rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

            rAct.maFunction = lcl_drawPolyPolygon;
            // TODO(F2): subdivide&render whole curve
        /*    rAct.maFunction = ::boost::bind(&lcl_drawLine,
                                            _1,_2,_3,_4,_5,
                                            geometry::RealPoint2D(
                                                aBezierSegment.Px,
                                                aBezierSegment.Py),
                                            aEndPoint);*/

        }
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::drawPolyPolygon( const rendering::XCanvas*                          /*pCanvas*/,
                                                                                 const uno::Reference< rendering::XPolyPolygon2D >& xPolyPolygon,
                                                                                 const rendering::ViewState&                        viewState,
                                                                                 const rendering::RenderState&                      renderState )
    {
        ENSURE_OR_THROW( xPolyPolygon.is(),
                          "CanvasHelper::drawPolyPolygon: polygon is NULL");

        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            rAct.maPolyPolys.push_back(
                ::basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(xPolyPolygon));
            rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

            rAct.maFunction = &lcl_drawPolyPolygon;
        }

        // TODO(P1): Provide caching here.
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::strokePolyPolygon( const rendering::XCanvas*                            /*pCanvas*/,
                                                                                   const uno::Reference< rendering::XPolyPolygon2D >&   xPolyPolygon,
                                                                                   const rendering::ViewState&                          viewState,
                                                                                   const rendering::RenderState&                        renderState,
                                                                                   const rendering::StrokeAttributes&                   strokeAttributes )
    {
        ENSURE_OR_THROW( xPolyPolygon.is(),
                          "CanvasHelper::strokePolyPolygon: polygon is NULL");

        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            ::basegfx::B2DSize aLinePixelSize(strokeAttributes.StrokeWidth,
                                              strokeAttributes.StrokeWidth);
            ::basegfx::B2DPolyPolygon aPolyPoly(
                     ::basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(xPolyPolygon) );


            if( strokeAttributes.DashArray.getLength() )
            {
                const ::std::vector<double>& aDashArray(
                    ::comphelper::sequenceToContainer< ::std::vector<double> >(strokeAttributes.DashArray) );

                ::basegfx::B2DPolyPolygon aDashedPolyPoly;

                for( sal_uInt32 i=0; i<aPolyPoly.count(); ++i )
                {
                    // AW: new interface; You may also get gaps in the same run now
                    basegfx::tools::applyLineDashing(aPolyPoly.getB2DPolygon(i),
                                                     aDashArray,
                                                     &aDashedPolyPoly);
                }

                aPolyPoly = aDashedPolyPoly;
            }

            if( aLinePixelSize.getLength() < 1.42 )
            {
                // line width < 1.0 in device pixel, thus, output as a
                // simple hairline poly-polygon
                rAct.maPolyPolys.push_back(aPolyPoly);
                rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

                rAct.maFunction = &lcl_drawPolyPolygon;
            }
            else
            {
                // render as a 'thick' line
                ::basegfx::B2DPolyPolygon aStrokedPolyPoly;
                for( sal_uInt32 i=0; i<aPolyPoly.count(); ++i )
                {
                    // TODO(F2): Use MiterLimit from StrokeAttributes,
                    // need to convert it here to angle.

                    // TODO(F2): Also use Cap settings from
                    // StrokeAttributes, the
                    // createAreaGeometryForLineStartEnd() method does not
                    // seem to fit very well here

                    // AW: New interface, will create bezier polygons now
                    aStrokedPolyPoly.append(basegfx::tools::createAreaGeometry(
                        aPolyPoly.getB2DPolygon(i),
                        strokeAttributes.StrokeWidth*0.5,
                        b2DJoineFromJoin(strokeAttributes.JoinType),
                        unoCapeFromCap(strokeAttributes.StartCapType)
                        ));
                }
                rAct.maPolyPolys.push_back(aStrokedPolyPoly);
                rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

                rAct.maFunction = &lcl_fillPolyPolygon;
            }
        }

        // TODO(P1): Provide caching here.
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::strokeTexturedPolyPolygon( const rendering::XCanvas*                            /*pCanvas*/,
                                                                                           const uno::Reference< rendering::XPolyPolygon2D >&   /*xPolyPolygon*/,
                                                                                           const rendering::ViewState&                          /*viewState*/,
                                                                                           const rendering::RenderState&                        /*renderState*/,
                                                                                           const uno::Sequence< rendering::Texture >&           /*textures*/,
                                                                                           const rendering::StrokeAttributes&                   /*strokeAttributes*/ )
    {
        // TODO
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::strokeTextureMappedPolyPolygon( const rendering::XCanvas*                           /*pCanvas*/,
                                                                                                const uno::Reference< rendering::XPolyPolygon2D >&  /*xPolyPolygon*/,
                                                                                                const rendering::ViewState&                         /*viewState*/,
                                                                                                const rendering::RenderState&                       /*renderState*/,
                                                                                                const uno::Sequence< rendering::Texture >&          /*textures*/,
                                                                                                const uno::Reference< geometry::XMapping2D >&       /*xMapping*/,
                                                                                                const rendering::StrokeAttributes&                  /*strokeAttributes*/ )
    {
        // TODO
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XPolyPolygon2D >   CanvasHelper::queryStrokeShapes( const rendering::XCanvas*                            /*pCanvas*/,
                                                                                   const uno::Reference< rendering::XPolyPolygon2D >&   /*xPolyPolygon*/,
                                                                                   const rendering::ViewState&                          /*viewState*/,
                                                                                   const rendering::RenderState&                        /*renderState*/,
                                                                                   const rendering::StrokeAttributes&                   /*strokeAttributes*/ )
    {
        // TODO
        return uno::Reference< rendering::XPolyPolygon2D >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::fillPolyPolygon( const rendering::XCanvas*                          /*pCanvas*/,
                                                                                 const uno::Reference< rendering::XPolyPolygon2D >& xPolyPolygon,
                                                                                 const rendering::ViewState&                        viewState,
                                                                                 const rendering::RenderState&                      renderState )
    {
        ENSURE_OR_THROW( xPolyPolygon.is(),
                          "CanvasHelper::fillPolyPolygon: polygon is NULL");

        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            rAct.maPolyPolys.push_back(
                ::basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(xPolyPolygon));
            rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

            rAct.maFunction = &lcl_fillPolyPolygon;
        }

        // TODO(P1): Provide caching here.
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::fillTexturedPolyPolygon( const rendering::XCanvas*                          /*pCanvas*/,
                                                                                         const uno::Reference< rendering::XPolyPolygon2D >& xPolyPolygon,
                                                                                         const rendering::ViewState&                        viewState,
                                                                                         const rendering::RenderState&                      renderState,
                                                                                         const uno::Sequence< rendering::Texture >&         textures )
    {
        ENSURE_OR_THROW( xPolyPolygon.is(),
                          "CanvasHelper::fillPolyPolygon: polygon is NULL");

        if( mpDevice )
        {
            mpRecordedActions->push_back( Action() );
            Action& rAct=mpRecordedActions->back();

            setupGraphicsState( rAct, viewState, renderState );
            rAct.maPolyPolys.push_back(
                ::basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(xPolyPolygon));
            rAct.maPolyPolys.back().makeUnique(); // own copy, for thread safety

            // TODO(F1): Multi-texturing
            if( textures[0].Gradient.is() )
            {
                // try to cast XParametricPolyPolygon2D reference to
                // our implementation class.
                ::canvas::ParametricPolyPolygon* pGradient =
                      dynamic_cast< ::canvas::ParametricPolyPolygon* >( textures[0].Gradient.get() );

                if( pGradient )
                {
                    // copy state from Gradient polypoly locally
                    // (given object might change!)
                    const ::canvas::ParametricPolyPolygon::Values& rValues(
                        pGradient->getValues() );

                    rAct.maFunction = ::boost::bind(&lcl_fillGradientPolyPolygon,
                                                    _1,_2,_3,_4,
                                                    rValues,
                                                    textures[0],
                                                    _6);
                }
                else
                {
                    // TODO(F1): The generic case is missing here
                    ENSURE_OR_THROW( false,
                                      "CanvasHelper::fillTexturedPolyPolygon(): unknown parametric polygon encountered" );
                }
            }
            else if( textures[0].Bitmap.is() )
            {
                // own bitmap?
                CanvasBitmap* pOwnBitmap=dynamic_cast<CanvasBitmap*>(textures[0].Bitmap.get());
                if( pOwnBitmap )
                {
                    // TODO(F2): own texture bitmap
                }
                else
                {
                    // TODO(P3): Highly inefficient - simply copies pixel data

                    uno::Reference< rendering::XIntegerReadOnlyBitmap > xIntegerBitmap(
                        textures[0].Bitmap,
                        uno::UNO_QUERY);
                    if( xIntegerBitmap.is() )
                    {
                        const geometry::IntegerSize2D aSize=xIntegerBitmap->getSize();
                        rendering::IntegerBitmapLayout aLayout;
                        uno::Sequence<sal_Int8> aPixelData=
                            xIntegerBitmap->getData(
                                aLayout,
                                geometry::IntegerRectangle2D(0,0,aSize.Width,aSize.Height));

                        // force-convert color to ARGB8888 int color space
                        uno::Sequence<sal_Int8> aARGBBytes(
                            aLayout.ColorSpace->convertToIntegerColorSpace(
                                aPixelData,
                                canvas::tools::getStdColorSpace()));

                        rAct.maFunction = ::boost::bind(&lcl_fillTexturedPolyPolygon,
                                                        _1,_2,_3,_4,
                                                        textures[0],
                                                        aSize,
                                                        aARGBBytes,
                                                        rtl_crc32(0,
                                                                  aARGBBytes.getConstArray(),
                                                                  aARGBBytes.getLength()),
                                                        _6);
                    }
                    // TODO(F1): handle non-integer case
                }
            }
        }

        // TODO(P1): Provide caching here.
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::fillTextureMappedPolyPolygon( const rendering::XCanvas*                             /*pCanvas*/,
                                                                                              const uno::Reference< rendering::XPolyPolygon2D >&    /*xPolyPolygon*/,
                                                                                              const rendering::ViewState&                           /*viewState*/,
                                                                                              const rendering::RenderState&                         /*renderState*/,
                                                                                              const uno::Sequence< rendering::Texture >&            /*textures*/,
                                                                                              const uno::Reference< geometry::XMapping2D >&         /*xMapping*/ )
    {
        // TODO
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCanvasFont > CanvasHelper::createFont( const rendering::XCanvas*                    /*pCanvas*/,
                                                                       const rendering::FontRequest&                fontRequest,
                                                                       const uno::Sequence< beans::PropertyValue >& extraFontProperties,
                                                                       const geometry::Matrix2D&                    fontMatrix )
    {
        if( mpDevice )
            return uno::Reference< rendering::XCanvasFont >(
                    new CanvasFont(fontRequest, extraFontProperties, fontMatrix ) );

        return uno::Reference< rendering::XCanvasFont >();
    }

    uno::Sequence< rendering::FontInfo > CanvasHelper::queryAvailableFonts( const rendering::XCanvas*                       /*pCanvas*/,
                                                                            const rendering::FontInfo&                      /*aFilter*/,
                                                                            const uno::Sequence< beans::PropertyValue >&    /*aFontProperties*/ )
    {
        // TODO
        return uno::Sequence< rendering::FontInfo >();
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::drawText( const rendering::XCanvas*                         /*pCanvas*/,
                                                                          const rendering::StringContext&                   /*text*/,
                                                                          const uno::Reference< rendering::XCanvasFont >&   /*xFont*/,
                                                                          const rendering::ViewState&                       /*viewState*/,
                                                                          const rendering::RenderState&                     /*renderState*/,
                                                                          sal_Int8                                          /*textDirection*/ )
    {
        // TODO - but not used from slideshow
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::drawTextLayout( const rendering::XCanvas*                       /*pCanvas*/,
                                                                                const uno::Reference< rendering::XTextLayout >& xLayoutetText,
                                                                                const rendering::ViewState&                     viewState,
                                                                                const rendering::RenderState&                   renderState )
    {
        ENSURE_OR_THROW( xLayoutetText.is(),
                          "CanvasHelper::drawTextLayout: text is NULL");

        if( mpDevice )
        {
            VirtualDevice aVDev;
            aVDev.EnableOutput(false);

            CanvasFont* pFont=dynamic_cast<CanvasFont*>(xLayoutetText->getFont().get());
            const rendering::StringContext& rTxt=xLayoutetText->getText();
            if( pFont && rTxt.Length )
            {
                // create the font
                const rendering::FontRequest& rFontRequest = pFont->getFontRequest();
                const geometry::Matrix2D&     rFontMatrix = pFont->getFontMatrix();
                vcl::Font aFont(
                    rFontRequest.FontDescription.FamilyName,
                    rFontRequest.FontDescription.StyleName,
                    Size( 0, ::basegfx::fround(rFontRequest.CellSize)));

                aFont.SetAlign( ALIGN_BASELINE );
                aFont.SetCharSet( (rFontRequest.FontDescription.IsSymbolFont==util::TriState_YES) ? RTL_TEXTENCODING_SYMBOL : RTL_TEXTENCODING_UNICODE );
                aFont.SetVertical( rFontRequest.FontDescription.IsVertical==util::TriState_YES );
                aFont.SetWeight( static_cast<FontWeight>(rFontRequest.FontDescription.FontDescription.Weight) );
                aFont.SetItalic( (rFontRequest.FontDescription.FontDescription.Letterform<=8) ? ITALIC_NONE : ITALIC_NORMAL );

                // adjust to stretched font
                if(!::rtl::math::approxEqual(rFontMatrix.m00, rFontMatrix.m11))
                {
                    const Size aSize = aVDev.GetFontMetric( aFont ).GetSize();
                    const double fDividend( rFontMatrix.m10 + rFontMatrix.m11 );
                    double fStretch = (rFontMatrix.m00 + rFontMatrix.m01);

                    if( !::basegfx::fTools::equalZero( fDividend) )
                        fStretch /= fDividend;

                    const sal_Int32 nNewWidth = ::basegfx::fround( aSize.Width() * fStretch );

                    aFont.SetWidth( nNewWidth );
                }

                // set font
                aVDev.SetFont(aFont);

                mpRecordedActions->push_back( Action() );
                Action& rAct=mpRecordedActions->back();

                setupGraphicsState( rAct, viewState, renderState );

                // handle custom spacing, if there
                uno::Sequence<double> aLogicalAdvancements=xLayoutetText->queryLogicalAdvancements();
                if( aLogicalAdvancements.getLength() )
                {
                    // create the DXArray
                    const sal_Int32 nLen( aLogicalAdvancements.getLength() );
                    ::boost::scoped_array<long> pDXArray( new long[nLen] );
                    for( sal_Int32 i=0; i<nLen; ++i )
                        pDXArray[i] = basegfx::fround( aLogicalAdvancements[i] );

                    // get the glyphs
                    aVDev.GetTextOutlines(rAct.maPolyPolys,
                                          rTxt.Text,
                                          0,
                                          rTxt.StartPosition,
                                          rTxt.Length,
                                          true,
                                          0,
                                          pDXArray.get() );
                }
                else
                {
                    // get the glyphs
                    aVDev.GetTextOutlines(rAct.maPolyPolys,
                                          rTxt.Text,
                                          0,
                                          rTxt.StartPosition,
                                          rTxt.Length );
                }

                // own copy, for thread safety
                std::for_each(rAct.maPolyPolys.begin(),
                              rAct.maPolyPolys.end(),
                              ::boost::mem_fn(&::basegfx::B2DPolyPolygon::makeUnique));

                rAct.maFunction = &lcl_fillPolyPolygon;
            }
        }

        // TODO
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::drawBitmap( const rendering::XCanvas*                   /*pCanvas*/,
                                                                            const uno::Reference< rendering::XBitmap >& xBitmap,
                                                                            const rendering::ViewState&                 viewState,
                                                                            const rendering::RenderState&               renderState )
    {
        ENSURE_OR_THROW( xBitmap.is(),
                          "CanvasHelper::drawBitmap: bitmap is NULL");

        if( mpDevice )
        {
            // own bitmap?
            CanvasBitmap* pOwnBitmap=dynamic_cast<CanvasBitmap*>(xBitmap.get());
            if( pOwnBitmap )
            {

                // insert as transformed copy of bitmap action vector -
                // during rendering, this gets rendered into a temporary
                // buffer, and then composited to the front
                mpRecordedActions->push_back( Action() );
                Action& rAct=mpRecordedActions->back();

                setupGraphicsState( rAct, viewState, renderState );
                rAct.maFunction = ::boost::bind(&lcl_drawOwnBitmap,
                                                _1,_2,_3,_4,_5,
                                                *pOwnBitmap);

            }
            else
            {
                // TODO(P3): Highly inefficient - simply copies pixel data

                uno::Reference< rendering::XIntegerReadOnlyBitmap > xIntegerBitmap(
                    xBitmap, uno::UNO_QUERY);
                if( xIntegerBitmap.is() )
                {
                    const geometry::IntegerSize2D aSize=xBitmap->getSize();
                    rendering::IntegerBitmapLayout aLayout;
                    uno::Sequence<sal_Int8> aPixelData=
                        xIntegerBitmap->getData(
                            aLayout,
                            geometry::IntegerRectangle2D(0,0,aSize.Width,aSize.Height));

                    // force-convert color to ARGB8888 int color space
                    uno::Sequence<sal_Int8> aARGBBytes(
                        aLayout.ColorSpace->convertToIntegerColorSpace(
                            aPixelData,
                            canvas::tools::getStdColorSpace()));

                    mpRecordedActions->push_back( Action() );
                    Action& rAct=mpRecordedActions->back();

                    setupGraphicsState( rAct, viewState, renderState );
                    rAct.maFunction = ::boost::bind(&lcl_drawGenericBitmap,
                                                    _1,_2,_3,_4,_5,
                                                    aSize, aARGBBytes,
                                                    rtl_crc32(0,
                                                              aARGBBytes.getConstArray(),
                                                              aARGBBytes.getLength()));
                }
                // TODO(F1): handle non-integer case
            }
        }

        // TODO(P1): Provide caching here.
        return uno::Reference< rendering::XCachedPrimitive >(NULL);
    }

    uno::Reference< rendering::XCachedPrimitive > CanvasHelper::drawBitmapModulated( const rendering::XCanvas*                      pCanvas,
                                                                                     const uno::Reference< rendering::XBitmap >&    xBitmap,
                                                                                     const rendering::ViewState&                    viewState,
                                                                                     const rendering::RenderState&                  renderState )
    {
        // TODO(F3): remove this wart altogether
        return drawBitmap(pCanvas, xBitmap, viewState, renderState);
    }


    void CanvasHelper::setupGraphicsState( Action&                       o_action,
                                           const rendering::ViewState&   viewState,
                                           const rendering::RenderState& renderState )
    {
        ENSURE_OR_THROW( mpDevice,
                          "CanvasHelper::setupGraphicsState: reference device invalid" );

        // TODO(F3): clipping
        // TODO(P2): think about caching transformations between canvas calls

        // setup overall transform only now. View clip above was
        // relative to view transform
        ::basegfx::B2DHomMatrix aTransform;
        ::canvas::tools::mergeViewAndRenderTransform(o_action.maTransform,
                                                     viewState,
                                                     renderState);
        // setup compositing - mapping courtesy David Reveman
        // (glitz_operator.c)
        switch( renderState.CompositeOperation )
        {
            case rendering::CompositeOperation::OVER:
                o_action.meSrcBlendMode=GL_ONE;
                o_action.meDstBlendMode=GL_ONE_MINUS_SRC_ALPHA;
                break;
            case rendering::CompositeOperation::CLEAR:
                o_action.meSrcBlendMode=GL_ZERO;
                o_action.meDstBlendMode=GL_ZERO;
                break;
            case rendering::CompositeOperation::SOURCE:
                o_action.meSrcBlendMode=GL_ONE;
                o_action.meDstBlendMode=GL_ZERO;
                break;
            case rendering::CompositeOperation::UNDER:
                // FALLTHROUGH intended - but correct?!
            case rendering::CompositeOperation::DESTINATION:
                o_action.meSrcBlendMode=GL_ZERO;
                o_action.meDstBlendMode=GL_ONE;
                break;
            case rendering::CompositeOperation::INSIDE:
                o_action.meSrcBlendMode=GL_DST_ALPHA;
                o_action.meDstBlendMode=GL_ZERO;
                break;
            case rendering::CompositeOperation::INSIDE_REVERSE:
                o_action.meSrcBlendMode=GL_ONE_MINUS_DST_ALPHA;
                o_action.meDstBlendMode=GL_ZERO;
                break;
            case rendering::CompositeOperation::OUTSIDE:
                o_action.meSrcBlendMode=GL_ONE_MINUS_DST_ALPHA;
                o_action.meDstBlendMode=GL_ONE;
                break;
            case rendering::CompositeOperation::OUTSIDE_REVERSE:
                o_action.meSrcBlendMode=GL_ZERO;
                o_action.meDstBlendMode=GL_ONE_MINUS_SRC_ALPHA;
                break;
            case rendering::CompositeOperation::ATOP:
                o_action.meSrcBlendMode=GL_DST_ALPHA;
                o_action.meDstBlendMode=GL_ONE_MINUS_SRC_ALPHA;
                break;
            case rendering::CompositeOperation::ATOP_REVERSE:
                o_action.meSrcBlendMode=GL_ONE_MINUS_DST_ALPHA;
                o_action.meDstBlendMode=GL_SRC_ALPHA;
                break;
            case rendering::CompositeOperation::XOR:
                o_action.meSrcBlendMode=GL_ONE_MINUS_DST_ALPHA;
                o_action.meDstBlendMode=GL_ONE_MINUS_SRC_ALPHA;
                break;
            case rendering::CompositeOperation::ADD:
                o_action.meSrcBlendMode=GL_ONE;
                o_action.meDstBlendMode=GL_ONE;
                break;
            case rendering::CompositeOperation::SATURATE:
                o_action.meSrcBlendMode=GL_SRC_ALPHA_SATURATE;
                o_action.meDstBlendMode=GL_SRC_ALPHA_SATURATE;
                break;

            default:
                ENSURE_OR_THROW( false, "CanvasHelper::setupGraphicsState: unexpected mode" );
                break;
        }

        if (renderState.DeviceColor.getLength())
            o_action.maARGBColor =
                mpDevice->getDeviceColorSpace()->convertToARGB(renderState.DeviceColor)[0];
    }

    void CanvasHelper::flush() const
    {
    }

    bool CanvasHelper::renderRecordedActions() const
    {
        std::vector<Action>::const_iterator aCurr(mpRecordedActions->begin());
        const std::vector<Action>::const_iterator aEnd(mpRecordedActions->end());
        while( aCurr != aEnd )
        {
            if( !aCurr->maFunction( *this,
                                    aCurr->maTransform,
                                    aCurr->meSrcBlendMode,
                                    aCurr->meDstBlendMode,
                                    aCurr->maARGBColor,
                                    aCurr->maPolyPolys ) )
                return false;

            ++aCurr;
        }

        return true;
    }

    size_t CanvasHelper::getRecordedActionCount() const
    {
        return mpRecordedActions->size();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

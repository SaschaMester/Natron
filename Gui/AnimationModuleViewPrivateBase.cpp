/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "AnimationModuleViewPrivateBase.h"

#include <QGLWidget>
#include <QApplication>

#include <QThread>
#include <QImage>

#include "Engine/KnobTypes.h"
#include "Engine/Settings.h"
#include "Engine/StringAnimationManager.h"
#include "Engine/TimeLine.h"

#include "Gui/ActionShortcuts.h"
#include "Gui/AnimationModule.h"
#include "Gui/AnimationModuleViewBase.h"
#include "Gui/AnimationModuleSelectionModel.h"
#include "Gui/CurveGui.h"
#include "Gui/KnobAnim.h"
#include "Gui/Menu.h"
#include "Gui/NodeAnim.h"
#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/TableItemAnim.h"




NATRON_NAMESPACE_ENTER;

AnimationModuleViewPrivateBase::AnimationModuleViewPrivateBase(Gui* gui, AnimationViewBase* publicInterface, const AnimationModuleBasePtr& model)
: _publicInterface(publicInterface)
, _gui(gui)
, _model(model)
, zoomCtxMutex()
, zoomCtx()
, zoomOrPannedSinceLastFit(false)
, textRenderer()
, selectionRect()
, selectedKeysBRect()
, kfTexturesIDs()
, _rightClickMenu( new Menu(publicInterface) )
, savedTexture(0)
, drawnOnce(false)
{
    for (int i = 0; i < KF_TEXTURES_COUNT; ++i) {
        kfTexturesIDs[i] = 0;
    }
}

AnimationModuleViewPrivateBase::~AnimationModuleViewPrivateBase()
{
    if (kfTexturesIDs[0]) {
        GL_GPU::glDeleteTextures(KF_TEXTURES_COUNT, kfTexturesIDs);
    }

}

void
AnimationModuleViewPrivateBase::drawTimelineMarkers()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );
    assert( QGLContext::currentContext() == _publicInterface->context() );
    glCheckError(GL_GPU);

    TimeLinePtr timeline = _model.lock()->getTimeline();
    if (!timeline) {
        return;
    }

    double cursorR, cursorG, cursorB;
    double boundsR, boundsG, boundsB;
    SettingsPtr settings = appPTR->getCurrentSettings();
    settings->getTimelinePlayheadColor(&cursorR, &cursorG, &cursorB);
    settings->getTimelineBoundsColor(&boundsR, &boundsG, &boundsB);

    QPointF topLeft = zoomCtx.toZoomCoordinates(0, 0);
    QPointF btmRight = zoomCtx.toZoomCoordinates(_publicInterface->width() - 1, _publicInterface->height() - 1);

    {
        GLProtectAttrib<GL_GPU> a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_POLYGON_BIT | GL_COLOR_BUFFER_BIT);

        GL_GPU::glEnable(GL_BLEND);
        GL_GPU::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_GPU::glEnable(GL_LINE_SMOOTH);
        GL_GPU::glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
        GL_GPU::glColor4f(boundsR, boundsG, boundsB, 1.);

        double leftBound, rightBound;
        _gui->getApp()->getFrameRange(&leftBound, &rightBound);
        GL_GPU::glBegin(GL_LINES);
        GL_GPU::glVertex2f( leftBound, btmRight.y() );
        GL_GPU::glVertex2f( leftBound, topLeft.y() );
        GL_GPU::glVertex2f( rightBound, btmRight.y() );
        GL_GPU::glVertex2f( rightBound, topLeft.y() );
        GL_GPU::glColor4f(cursorR, cursorG, cursorB, 1.);
        GL_GPU::glVertex2f( timeline->currentFrame(), btmRight.y() );
        GL_GPU::glVertex2f( timeline->currentFrame(), topLeft.y() );
        GL_GPU::glEnd();
        glCheckErrorIgnoreOSXBug(GL_GPU);

        GL_GPU::glEnable(GL_POLYGON_SMOOTH);
        GL_GPU::glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);

        QPointF topLeft = zoomCtx.toZoomCoordinates(0, 0);
        QPointF btmRight = zoomCtx.toZoomCoordinates(_publicInterface->width() - 1, _publicInterface->height() - 1);
        QPointF btmCursorBtm( timeline->currentFrame(), btmRight.y() );
        QPointF btmcursorBtmWidgetCoord = zoomCtx.toWidgetCoordinates( btmCursorBtm.x(), btmCursorBtm.y() );
        QPointF btmCursorTop = zoomCtx.toZoomCoordinates(btmcursorBtmWidgetCoord.x(), btmcursorBtmWidgetCoord.y() - TO_DPIX(CURSOR_HEIGHT));
        QPointF btmCursorLeft = zoomCtx.toZoomCoordinates( btmcursorBtmWidgetCoord.x() - TO_DPIX(CURSOR_WIDTH) / 2., btmcursorBtmWidgetCoord.y() );
        QPointF btmCursorRight = zoomCtx.toZoomCoordinates( btmcursorBtmWidgetCoord.x() + TO_DPIX(CURSOR_WIDTH) / 2., btmcursorBtmWidgetCoord.y() );
        QPointF topCursortop( timeline->currentFrame(), topLeft.y() );
        QPointF topcursorTopWidgetCoord = zoomCtx.toWidgetCoordinates( topCursortop.x(), topCursortop.y() );
        QPointF topCursorBtm = zoomCtx.toZoomCoordinates(topcursorTopWidgetCoord.x(), topcursorTopWidgetCoord.y() + TO_DPIX(CURSOR_HEIGHT));
        QPointF topCursorLeft = zoomCtx.toZoomCoordinates( topcursorTopWidgetCoord.x() - TO_DPIX(CURSOR_WIDTH) / 2., topcursorTopWidgetCoord.y() );
        QPointF topCursorRight = zoomCtx.toZoomCoordinates( topcursorTopWidgetCoord.x() + TO_DPIX(CURSOR_WIDTH) / 2., topcursorTopWidgetCoord.y() );


        GL_GPU::glBegin(GL_POLYGON);
        GL_GPU::glVertex2f( btmCursorTop.x(), btmCursorTop.y() );
        GL_GPU::glVertex2f( btmCursorLeft.x(), btmCursorLeft.y() );
        GL_GPU::glVertex2f( btmCursorRight.x(), btmCursorRight.y() );
        GL_GPU::glEnd();
        glCheckErrorIgnoreOSXBug(GL_GPU);

        GL_GPU::glBegin(GL_POLYGON);
        GL_GPU::glVertex2f( topCursorBtm.x(), topCursorBtm.y() );
        GL_GPU::glVertex2f( topCursorLeft.x(), topCursorLeft.y() );
        GL_GPU::glVertex2f( topCursorRight.x(), topCursorRight.y() );
        GL_GPU::glEnd();
    } // GLProtectAttrib a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_POLYGON_BIT);
    glCheckErrorIgnoreOSXBug(GL_GPU);
} // AnimationModuleViewPrivateBase::drawTimelineMarkers

void
AnimationModuleViewPrivateBase::drawSelectionRectangle()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );
    assert( QGLContext::currentContext() == _publicInterface->context() );

    {
        GLProtectAttrib<GL_GPU> a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);

        GL_GPU::glEnable(GL_BLEND);
        GL_GPU::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_GPU::glEnable(GL_LINE_SMOOTH);
        GL_GPU::glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);

        GL_GPU::glColor4f(0.3, 0.3, 0.3, 0.2);

        GL_GPU::glBegin(GL_POLYGON);
        GL_GPU::glVertex2f( selectionRect.x1, selectionRect.y1 );
        GL_GPU::glVertex2f( selectionRect.x1, selectionRect.y2 );
        GL_GPU::glVertex2f( selectionRect.x2, selectionRect.y2 );
        GL_GPU::glVertex2f( selectionRect.x2, selectionRect.y1 );
        GL_GPU::glEnd();


        GL_GPU::glLineWidth(1.5);

        GL_GPU::glColor4f(0.5, 0.5, 0.5, 1.);
        GL_GPU::glBegin(GL_LINE_LOOP);
        GL_GPU::glVertex2f( selectionRect.x1, selectionRect.y1 );
        GL_GPU::glVertex2f( selectionRect.x1, selectionRect.y2 );
        GL_GPU::glVertex2f( selectionRect.x2, selectionRect.y2 );
        GL_GPU::glVertex2f( selectionRect.x2, selectionRect.y1 );
        GL_GPU::glEnd();

        glCheckError(GL_GPU);
    } // GLProtectAttrib a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
}


void
AnimationModuleViewPrivateBase::drawSelectedKeyFramesBbox()
{
    {
        GLProtectAttrib<GL_GPU> a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);

        GL_GPU::glEnable(GL_BLEND);
        GL_GPU::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_GPU::glEnable(GL_LINE_SMOOTH);
        GL_GPU::glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);

        QPointF topLeftWidget = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x1, selectedKeysBRect.y2 );
        QPointF btmRightWidget = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x2, selectedKeysBRect.y1 );
        double xMid = ( selectedKeysBRect.x1 + selectedKeysBRect.x2 ) / 2.;
        double yMid = ( selectedKeysBRect.y1 + selectedKeysBRect.y2 ) / 2.;

        GL_GPU::glLineWidth(1.5);

        GL_GPU::glColor4f(0.5, 0.5, 0.5, 1.);
        GL_GPU::glBegin(GL_LINE_LOOP);
        GL_GPU::glVertex2f( selectedKeysBRect.x1, selectedKeysBRect.y1 );
        GL_GPU::glVertex2f( selectedKeysBRect.x1, selectedKeysBRect.y2 );
        GL_GPU::glVertex2f( selectedKeysBRect.x2, selectedKeysBRect.y2 );
        GL_GPU::glVertex2f( selectedKeysBRect.x2, selectedKeysBRect.y1 );
        GL_GPU::glEnd();

        QPointF middleWidgetCoord = zoomCtx.toWidgetCoordinates( xMid, yMid );
        QPointF middleLeft = zoomCtx.toZoomCoordinates( middleWidgetCoord.x() - TO_DPIX(XHAIR_SIZE), middleWidgetCoord.y() );
        QPointF middleRight = zoomCtx.toZoomCoordinates( middleWidgetCoord.x() + TO_DPIX(XHAIR_SIZE), middleWidgetCoord.y() );
        QPointF middleTop = zoomCtx.toZoomCoordinates(middleWidgetCoord.x(), middleWidgetCoord.y() - TO_DPIX(XHAIR_SIZE));
        QPointF middleBottom = zoomCtx.toZoomCoordinates(middleWidgetCoord.x(), middleWidgetCoord.y() + TO_DPIX(XHAIR_SIZE));



        GL_GPU::glBegin(GL_LINES);
        GL_GPU::glVertex2f( std::max( middleLeft.x(), selectedKeysBRect.x1 ), middleLeft.y() );
        GL_GPU::glVertex2f( std::min( middleRight.x(), selectedKeysBRect.x2 ), middleRight.y() );
        GL_GPU::glVertex2f( middleBottom.x(), std::max( middleBottom.y(), selectedKeysBRect.y1 ) );
        GL_GPU::glVertex2f( middleTop.x(), std::min( middleTop.y(), selectedKeysBRect.y2 ) );

        //top tick
        {
            double yBottom = zoomCtx.toZoomCoordinates(0, topLeftWidget.y() + TO_DPIX(BOUNDING_BOX_HANDLE_SIZE)).y();
            double yTop = zoomCtx.toZoomCoordinates(0, topLeftWidget.y() - TO_DPIX(BOUNDING_BOX_HANDLE_SIZE)).y();
            GL_GPU::glVertex2f(xMid, yBottom);
            GL_GPU::glVertex2f(xMid, yTop);
        }
        //left tick
        {
            double xLeft = zoomCtx.toZoomCoordinates(topLeftWidget.x() - TO_DPIX(BOUNDING_BOX_HANDLE_SIZE), 0).x();
            double xRight = zoomCtx.toZoomCoordinates(topLeftWidget.x() + TO_DPIX(BOUNDING_BOX_HANDLE_SIZE), 0).x();
            GL_GPU::glVertex2f(xLeft, yMid);
            GL_GPU::glVertex2f(xRight, yMid);
        }
        //bottom tick
        {
            double yBottom = zoomCtx.toZoomCoordinates(0, btmRightWidget.y() + TO_DPIX(BOUNDING_BOX_HANDLE_SIZE)).y();
            double yTop = zoomCtx.toZoomCoordinates(0, btmRightWidget.y() - TO_DPIX(BOUNDING_BOX_HANDLE_SIZE)).y();
            GL_GPU::glVertex2f(xMid, yBottom);
            GL_GPU::glVertex2f(xMid, yTop);
        }
        //right tick
        {
            double xLeft = zoomCtx.toZoomCoordinates(btmRightWidget.x() - TO_DPIX(BOUNDING_BOX_HANDLE_SIZE), 0).x();
            double xRight = zoomCtx.toZoomCoordinates(btmRightWidget.x() + TO_DPIX(BOUNDING_BOX_HANDLE_SIZE), 0).x();
            GL_GPU::glVertex2f(xLeft, yMid);
            GL_GPU::glVertex2f(xRight, yMid);
        }
        GL_GPU::glEnd();

        GL_GPU::glPointSize(TO_DPIX(BOUNDING_BOX_HANDLE_SIZE));
        GL_GPU::glBegin(GL_POINTS);
        GL_GPU::glVertex2f( selectedKeysBRect.x1, selectedKeysBRect.y1 );
        GL_GPU::glVertex2f( selectedKeysBRect.x1, selectedKeysBRect.y2 );
        GL_GPU::glVertex2f( selectedKeysBRect.x2, selectedKeysBRect.y2 );
        GL_GPU::glVertex2f( selectedKeysBRect.x2, selectedKeysBRect.y1 );
        GL_GPU::glEnd();

        glCheckError(GL_GPU);
    } // GLProtectAttrib a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
} // AnimationModuleViewPrivateBase::drawSelectedKeyFramesBbox


void
AnimationModuleViewPrivateBase::drawSelectionRect() const
{

    // Perform drawing
    {
        GLProtectAttrib<GL_GPU> a(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_LINE_BIT);

        GL_GPU::glEnable(GL_BLEND);
        GL_GPU::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_GPU::glEnable(GL_LINE_SMOOTH);
        GL_GPU::glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);

        GL_GPU::glColor4f(0.3, 0.3, 0.3, 0.2);

        // Draw rect
        GL_GPU::glBegin(GL_POLYGON);
        GL_GPU::glVertex2f(selectionRect.x1, selectionRect.y1);
        GL_GPU::glVertex2f(selectionRect.x1, selectionRect.y2);
        GL_GPU::glVertex2f(selectionRect.x2, selectionRect.y2);
        GL_GPU::glVertex2f(selectionRect.x2, selectionRect.y1);
        GL_GPU::glEnd();

        GL_GPU::glLineWidth(1.5);

        // Draw outline
        GL_GPU::glColor4f(0.5, 0.5, 0.5, 1.);
        GL_GPU::glBegin(GL_LINE_LOOP);
        GL_GPU::glVertex2f(selectionRect.x1, selectionRect.y1);
        GL_GPU::glVertex2f(selectionRect.x1, selectionRect.y2);
        GL_GPU::glVertex2f(selectionRect.x2, selectionRect.y2);
        GL_GPU::glVertex2f(selectionRect.x2, selectionRect.y1);
        GL_GPU::glEnd();
        
        glCheckError(GL_GPU);
    }
}

void
AnimationModuleViewPrivateBase::drawTexturedKeyframe(AnimationModuleViewPrivateBase::KeyframeTexture textureType,
                                                     bool drawTime,
                                                     double time,
                                                     const QColor& textColor,
                                                     const RectD &rect) const
{
    GLProtectAttrib<GL_GPU> a(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TRANSFORM_BIT);
    GLProtectMatrix<GL_GPU> pr(GL_MODELVIEW);

    GL_GPU::glEnable(GL_TEXTURE_2D);
    GL_GPU::glBindTexture(GL_TEXTURE_2D, kfTexturesIDs[textureType]);
    
    GL_GPU::glBegin(GL_POLYGON);
    GL_GPU::glTexCoord2f(0.0f, 1.0f);
    GL_GPU::glVertex2f( rect.left(), rect.top() );
    GL_GPU::glTexCoord2f(0.0f, 0.0f);
    GL_GPU::glVertex2f( rect.left(), rect.bottom() );
    GL_GPU::glTexCoord2f(1.0f, 0.0f);
    GL_GPU::glVertex2f( rect.right(), rect.bottom() );
    GL_GPU::glTexCoord2f(1.0f, 1.0f);
    GL_GPU::glVertex2f( rect.right(), rect.top() );
    GL_GPU::glEnd();

    GL_GPU::glColor4f(1, 1, 1, 1);
    GL_GPU::glBindTexture(GL_TEXTURE_2D, 0);

    GL_GPU::glDisable(GL_TEXTURE_2D);

    if (drawTime) {
        QString text = QString::number(time);
        QPointF p = zoomCtx.toWidgetCoordinates( rect.right(), rect.bottom() );
        p.rx() += 3;
        p = zoomCtx.toZoomCoordinates( p.x(), p.y() );
        renderText(p.x(), p.y(), text, textColor, _publicInterface->font());
    }
}

void
AnimationModuleViewPrivateBase::renderText(double x,
                                      double y,
                                      const QString & text,
                                      const QColor & color,
                                      const QFont & font,
                                      int flags) const
{
    if ( text.isEmpty() ) {
        return;
    }

    double w = (double)_publicInterface->width();
    double h = (double)_publicInterface->height();
    double bottom = zoomCtx.bottom();
    double left = zoomCtx.left();
    double top =  zoomCtx.top();
    double right = zoomCtx.right();
    if ( (w <= 0) || (h <= 0) || (right <= left) || (top <= bottom) ) {
        return;
    }
    double scalex = (right - left) / w;
    double scaley = (top - bottom) / h;
    textRenderer.renderText(x, y, scalex, scaley, text, color, font, flags);
    glCheckError(GL_GPU);
}


bool
AnimationModuleViewPrivateBase::isNearbyTimelineTopPoly(const QPoint & pt) const
{
    TimeLinePtr timeline = _model.lock()->getTimeline();
    if (!timeline) {
        return false;
    }

    QPointF pt_opengl = zoomCtx.toZoomCoordinates( pt.x(), pt.y() );

    QPointF topLeft = zoomCtx.toZoomCoordinates(0, 0);
    QPointF topCursortop( timeline->currentFrame(), topLeft.y() );
    QPointF topcursorTopWidgetCoord = zoomCtx.toWidgetCoordinates( topCursortop.x(), topCursortop.y() );
    QPointF topCursorBtm = zoomCtx.toZoomCoordinates(topcursorTopWidgetCoord.x(), topcursorTopWidgetCoord.y() + TO_DPIY(CURSOR_HEIGHT));
    QPointF topCursorLeft = zoomCtx.toZoomCoordinates( topcursorTopWidgetCoord.x() - TO_DPIX(CURSOR_WIDTH) / 2., topcursorTopWidgetCoord.y() );
    QPointF topCursorRight = zoomCtx.toZoomCoordinates( topcursorTopWidgetCoord.x() + TO_DPIX(CURSOR_WIDTH) / 2., topcursorTopWidgetCoord.y() );

    QPolygonF poly;
    poly.push_back(topCursorBtm);
    poly.push_back(topCursorLeft);
    poly.push_back(topCursorRight);

    return poly.containsPoint(pt_opengl, Qt::OddEvenFill);
}

bool
AnimationModuleViewPrivateBase::isNearbyTimelineBtmPoly(const QPoint & pt) const
{
    TimeLinePtr timeline = _model.lock()->getTimeline();
    if (!timeline) {
        return false;
    }
    QPointF pt_opengl = zoomCtx.toZoomCoordinates( pt.x(), pt.y() );

    QPointF btmRight = zoomCtx.toZoomCoordinates(_publicInterface->width() - 1, _publicInterface->height() - 1);
    QPointF btmCursorBtm( timeline->currentFrame(), btmRight.y() );
    QPointF btmcursorBtmWidgetCoord = zoomCtx.toWidgetCoordinates( btmCursorBtm.x(), btmCursorBtm.y() );
    QPointF btmCursorTop = zoomCtx.toZoomCoordinates(btmcursorBtmWidgetCoord.x(), btmcursorBtmWidgetCoord.y() - TO_DPIY(CURSOR_HEIGHT));
    QPointF btmCursorLeft = zoomCtx.toZoomCoordinates( btmcursorBtmWidgetCoord.x() - TO_DPIX(CURSOR_WIDTH) / 2., btmcursorBtmWidgetCoord.y() );
    QPointF btmCursorRight = zoomCtx.toZoomCoordinates( btmcursorBtmWidgetCoord.x() + TO_DPIX(CURSOR_WIDTH) / 2., btmcursorBtmWidgetCoord.y() );

    QPolygonF poly;
    poly.push_back(btmCursorTop);
    poly.push_back(btmCursorLeft);
    poly.push_back(btmCursorRight);

    return poly.containsPoint(pt_opengl, Qt::OddEvenFill);
}

bool
AnimationModuleViewPrivateBase::isNearbySelectedKeyFramesCrossWidget(const QPoint & pt) const
{
    double xMid = ( selectedKeysBRect.x1 + selectedKeysBRect.x2 ) / 2.;
    double yMid = ( selectedKeysBRect.y1 + selectedKeysBRect.y2 ) / 2.;

    QPointF middleWidgetCoord = zoomCtx.toWidgetCoordinates( xMid, yMid );
    QPointF middleLeft = zoomCtx.toZoomCoordinates( middleWidgetCoord.x() - TO_DPIX(XHAIR_SIZE), middleWidgetCoord.y() );
    QPointF middleRight = zoomCtx.toZoomCoordinates( middleWidgetCoord.x() + TO_DPIX(XHAIR_SIZE), middleWidgetCoord.y() );
    QPointF middleTop = zoomCtx.toZoomCoordinates(middleWidgetCoord.x(), middleWidgetCoord.y() - TO_DPIX(XHAIR_SIZE));
    QPointF middleBottom = zoomCtx.toZoomCoordinates(middleWidgetCoord.x(), middleWidgetCoord.y() + TO_DPIX(XHAIR_SIZE));


    if ( ( pt.x() >= (middleLeft.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (middleRight.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (middleLeft.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (middleLeft.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        //is nearby horizontal line
        return true;
    } else if ( ( pt.y() >= (middleBottom.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
               ( pt.y() <= (middleTop.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
               ( pt.x() <= (middleBottom.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
               ( pt.x() >= (middleBottom.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        //is nearby vertical line
        return true;
    } else {
        return false;
    }
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxTopLeft(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x1, selectedKeysBRect.y2 );

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxMidLeft(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates(selectedKeysBRect.x1,
                                                selectedKeysBRect.y2 - (selectedKeysBRect.height()) / 2.);

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxBtmLeft(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x1, selectedKeysBRect.y1);

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxMidBtm(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x1 + selectedKeysBRect.width() / 2., selectedKeysBRect.y1 );

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxBtmRight(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x2, selectedKeysBRect.y1);

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxMidRight(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates(selectedKeysBRect.x2, selectedKeysBRect.y1 + selectedKeysBRect.height() / 2.);

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxTopRight(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x2, selectedKeysBRect.y2 );

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }

    return false;
}

bool
AnimationModuleViewPrivateBase::isNearbyBboxMidTop(const QPoint& pt) const
{
    QPointF other = zoomCtx.toWidgetCoordinates( selectedKeysBRect.x1 + selectedKeysBRect.width() / 2., selectedKeysBRect.y2 );

    if ( ( pt.x() >= (other.x() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.x() <= (other.x() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() <= (other.y() + TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) &&
        ( pt.y() >= (other.y() - TO_DPIX(CLICK_DISTANCE_TOLERANCE)) ) ) {
        return true;
    }
    
    return false;
}

void
AnimationModuleViewPrivateBase::generateKeyframeTextures()
{
    QImage kfTexturesImages[KF_TEXTURES_COUNT];

    kfTexturesImages[0].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_constant.png") );
    kfTexturesImages[1].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_constant_selected.png") );
    kfTexturesImages[2].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_linear.png") );
    kfTexturesImages[3].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_linear_selected.png") );
    kfTexturesImages[4].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve.png") );
    kfTexturesImages[5].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_selected.png") );
    kfTexturesImages[6].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_break.png") );
    kfTexturesImages[7].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_break_selected.png") );
    kfTexturesImages[8].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_c.png") );
    kfTexturesImages[9].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_c_selected.png") );
    kfTexturesImages[10].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_h.png") );
    kfTexturesImages[11].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_h_selected.png") );
    kfTexturesImages[12].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_r.png") );
    kfTexturesImages[13].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_r_selected.png") );
    kfTexturesImages[14].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_z.png") );
    kfTexturesImages[15].load( QString::fromUtf8(NATRON_IMAGES_PATH "interp_curve_z_selected.png") );
    kfTexturesImages[16].load( QString::fromUtf8(NATRON_IMAGES_PATH "keyframe_node_root.png") );
    kfTexturesImages[17].load( QString::fromUtf8(NATRON_IMAGES_PATH "keyframe_node_root_selected.png") );

    GL_GPU::glGenTextures(KF_TEXTURES_COUNT, kfTexturesIDs);

    GL_GPU::glEnable(GL_TEXTURE_2D);

    for (int i = 0; i < KF_TEXTURES_COUNT; ++i) {
        if (std::max( kfTexturesImages[i].width(), kfTexturesImages[i].height() ) != KF_PIXMAP_SIZE) {
            kfTexturesImages[i] = kfTexturesImages[i].scaled(KF_PIXMAP_SIZE, KF_PIXMAP_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        kfTexturesImages[i] = QGLWidget::convertToGLFormat(kfTexturesImages[i]);
        GL_GPU::glBindTexture(GL_TEXTURE_2D, kfTexturesIDs[i]);

        GL_GPU::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        GL_GPU::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        GL_GPU::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        GL_GPU::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        GL_GPU::glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, KF_PIXMAP_SIZE, KF_PIXMAP_SIZE, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, kfTexturesImages[i].bits() );
    }

    GL_GPU::glBindTexture(GL_TEXTURE_2D, 0);
    GL_GPU::glDisable(GL_TEXTURE_2D);
}

AnimationModuleViewPrivateBase::KeyframeTexture
AnimationModuleViewPrivateBase::kfTextureFromKeyframeType(KeyframeTypeEnum kfType,
                                                bool selected) const
{
    AnimationModuleViewPrivateBase::KeyframeTexture ret = AnimationModuleViewPrivateBase::kfTextureNone;

    switch (kfType) {
        case eKeyframeTypeConstant:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpConstantSelected : AnimationModuleViewPrivateBase::kfTextureInterpConstant;
            break;
        case eKeyframeTypeLinear:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpLinearSelected : AnimationModuleViewPrivateBase::kfTextureInterpLinear;
            break;
        case eKeyframeTypeBroken:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpBreakSelected : AnimationModuleViewPrivateBase::kfTextureInterpBreak;
            break;
        case eKeyframeTypeFree:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpCurveSelected : AnimationModuleViewPrivateBase::kfTextureInterpCurve;
            break;
        case eKeyframeTypeSmooth:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpCurveZSelected : AnimationModuleViewPrivateBase::kfTextureInterpCurveZ;
            break;
        case eKeyframeTypeCatmullRom:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpCurveRSelected : AnimationModuleViewPrivateBase::kfTextureInterpCurveR;
            break;
        case eKeyframeTypeCubic:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpCurveCSelected : AnimationModuleViewPrivateBase::kfTextureInterpCurveC;
            break;
        case eKeyframeTypeHorizontal:
            ret = (selected) ? AnimationModuleViewPrivateBase::kfTextureInterpCurveHSelected : AnimationModuleViewPrivateBase::kfTextureInterpCurveH;
            break;
        default:
            ret = AnimationModuleViewPrivateBase::kfTextureNone;
            break;
    }
    
    return ret;
}


std::vector<CurveGuiPtr>
AnimationModuleViewPrivateBase::getSelectedCurves() const
{
    const AnimItemDimViewKeyFramesMap& keys = _model.lock()->getSelectionModel()->getCurrentKeyFramesSelection();

    std::vector<CurveGuiPtr> curves;
    for (AnimItemDimViewKeyFramesMap::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        CurveGuiPtr guiCurve = it->first.item->getCurveGui(it->first.dim, it->first.view);
        if (guiCurve) {
            curves.push_back(guiCurve);
        }
    }
    return curves;
}



void
AnimationModuleViewPrivateBase::createMenu()
{
    // always running in the main thread
    assert( qApp && qApp->thread() == QThread::currentThread() );
    
    _rightClickMenu->clear();


    Menu* editMenu = new Menu(_rightClickMenu);
    //editMenu->setFont( QFont(appFont,appFontSize) );
    editMenu->setTitle( tr("Edit") );
    _rightClickMenu->addAction( editMenu->menuAction() );

    Menu* interpMenu = new Menu(_rightClickMenu);
    //interpMenu->setFont( QFont(appFont,appFontSize) );
    interpMenu->setTitle( tr("Interpolation") );
    _rightClickMenu->addAction( interpMenu->menuAction() );

    Menu* viewMenu = new Menu(_rightClickMenu);
    //viewMenu->setFont( QFont(appFont,appFontSize) );
    viewMenu->setTitle( tr("View") );
    _rightClickMenu->addAction( viewMenu->menuAction() );


    Menu* optionsMenu = new Menu(_rightClickMenu);
    optionsMenu->setTitle( tr("Options") );
    _rightClickMenu->addAction( optionsMenu->menuAction() );


    QAction* deleteKeyFramesAction = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleRemoveKeys,
                                                            kShortcutDescActionAnimationModuleRemoveKeys, editMenu);
    deleteKeyFramesAction->setShortcut( QKeySequence(Qt::Key_Backspace) );
    QObject::connect( deleteKeyFramesAction, SIGNAL(triggered()), _publicInterface, SLOT(onRemoveSelectedKeyFramesActionTriggered()) );
    editMenu->addAction(deleteKeyFramesAction);

    QAction* copyKeyFramesAction = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleCopy,
                                                          kShortcutDescActionAnimationModuleCopy, editMenu);
    copyKeyFramesAction->setShortcut( QKeySequence(Qt::CTRL + Qt::Key_C) );

    QObject::connect( copyKeyFramesAction, SIGNAL(triggered()), _publicInterface, SLOT(onCopySelectedKeyFramesToClipBoardActionTriggered()) );
    editMenu->addAction(copyKeyFramesAction);

    QAction* pasteKeyFramesAction = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModulePasteKeyframes,
                                                           kShortcutDescActionAnimationModulePasteKeyframes, editMenu);
    pasteKeyFramesAction->setShortcut( QKeySequence(Qt::CTRL + Qt::Key_V) );
    QObject::connect( pasteKeyFramesAction, SIGNAL(triggered()), _publicInterface, SLOT(onPasteClipBoardKeyFramesActionTriggered()) );
    editMenu->addAction(pasteKeyFramesAction);

    QAction* selectAllAction = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleSelectAll,
                                                      kShortcutDescActionAnimationModuleSelectAll, editMenu);
    selectAllAction->setShortcut( QKeySequence(Qt::CTRL + Qt::Key_A) );
    QObject::connect( selectAllAction, SIGNAL(triggered()), _publicInterface, SLOT(onSelectAllKeyFramesActionTriggered()) );
    editMenu->addAction(selectAllAction);


    QAction* constantInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleConstant,
                                                     kShortcutDescActionAnimationModuleConstant, interpMenu);
    constantInterp->setShortcut( QKeySequence(Qt::Key_K) );
    constantInterp->setData((int)eKeyframeTypeConstant);
    QObject::connect( constantInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(constantInterp);

    QAction* linearInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleLinear,
                                                   kShortcutDescActionAnimationModuleLinear, interpMenu);
    linearInterp->setShortcut( QKeySequence(Qt::Key_L) );
    constantInterp->setData((int)eKeyframeTypeLinear);
    QObject::connect( linearInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(linearInterp);


    QAction* smoothInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleSmooth,
                                                   kShortcutDescActionAnimationModuleSmooth, interpMenu);
    smoothInterp->setShortcut( QKeySequence(Qt::Key_Z) );
    constantInterp->setData((int)eKeyframeTypeSmooth);
    QObject::connect( smoothInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(smoothInterp);


    QAction* catmullRomInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleCatmullrom,
                                                       kShortcutDescActionAnimationModuleCatmullrom, interpMenu);
    catmullRomInterp->setShortcut( QKeySequence(Qt::Key_R) );
    constantInterp->setData((int)eKeyframeTypeCatmullRom);
    QObject::connect( catmullRomInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(catmullRomInterp);


    QAction* cubicInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleCubic,
                                                  kShortcutDescActionAnimationModuleCubic, interpMenu);
    cubicInterp->setShortcut( QKeySequence(Qt::Key_C) );
    constantInterp->setData((int)eKeyframeTypeCubic);
    QObject::connect( cubicInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(cubicInterp);

    QAction* horizontalInterp = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleHorizontal,
                                                       kShortcutDescActionAnimationModuleHorizontal, interpMenu);
    horizontalInterp->setShortcut( QKeySequence(Qt::Key_H) );
    constantInterp->setData((int)eKeyframeTypeHorizontal);
    QObject::connect( horizontalInterp, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(horizontalInterp);


    QAction* breakDerivatives = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleBreak,
                                                       kShortcutDescActionAnimationModuleBreak, interpMenu);
    breakDerivatives->setShortcut( QKeySequence(Qt::Key_X) );
    constantInterp->setData((int)eKeyframeTypeBroken);
    QObject::connect( breakDerivatives, SIGNAL(triggered()), _publicInterface, SLOT(onSetInterpolationActionTriggered()) );
    interpMenu->addAction(breakDerivatives);

    QAction* frameAll = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleCenterAll,
                                               kShortcutDescActionAnimationModuleCenterAll, interpMenu);
    frameAll->setShortcut( QKeySequence(Qt::Key_A) );
    QObject::connect( frameAll, SIGNAL(triggered()), _publicInterface, SLOT(onCenterAllCurvesActionTriggered()) );
    viewMenu->addAction(frameAll);

    QAction* frameCurve = new ActionWithShortcut(kShortcutGroupAnimationModule, kShortcutIDActionAnimationModuleCenter,
                                                 kShortcutDescActionAnimationModuleCenter, interpMenu);
    frameCurve->setShortcut( QKeySequence(Qt::Key_F) );
    QObject::connect( frameCurve, SIGNAL(triggered()), _publicInterface, SLOT(onCenterOnSelectedCurvesActionTriggered()) );
    viewMenu->addAction(frameCurve);


    QAction* updateOnPenUp = new QAction(tr("Update on mouse release only"), _rightClickMenu);
    updateOnPenUp->setCheckable(true);
    updateOnPenUp->setChecked( appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly() );
    optionsMenu->addAction(updateOnPenUp);
    QObject::connect( updateOnPenUp, SIGNAL(triggered()), _publicInterface, SLOT(onUpdateOnPenUpActionTriggered()) );

    addMenuOptions();
} // createMenu

NATRON_NAMESPACE_EXIT;
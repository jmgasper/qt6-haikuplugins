/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Copyright (C) 2015-2017 Gerasim Troeglazov,
** Contact: 3dEyes@gmail.com
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qhaikubackingstore.h"
#include "qhaikuwindow.h"
#include "qhaikucursor.h"
#include "qhaikuintegration.h"
#include "qhaikuscreen.h"

#include <QtGui/private/qpixmap_raster_p.h>
#include <QtGui/private/qguiapplication_p.h>

#include <qpa/qplatformcursor.h>
#include <qpa/qplatformwindow.h>

#include <qdebug.h>
#include <QtCore/qmath.h>

QT_BEGIN_NAMESPACE

// B_DEVICE_PIXEL_BITMAP of <InterfaceDefs.h> on systems that have it: the
// bitmap holds frame buffer pixels and is drawn one to one.
static const uint32 kDevicePixelBitmap = 0x00001000;


QHaikuBackingStore::QHaikuBackingStore(QWindow *window)
    : QPlatformBackingStore(window)
    , m_bitmap(NULL)
    , m_requestedSize(window->size())
    , m_scale(1.0)
{
	ensureBitmap();
}


QHaikuBackingStore::~QHaikuBackingStore()
{
	m_image = QImage();
	delete m_bitmap;
}


QPaintDevice *QHaikuBackingStore::paintDevice()
{
    return &m_image;
}


void QHaikuBackingStore::drawChildWindows(QWindow *topwin)
{
	QHaikuWindow *topHaikuWin = QHaikuWindow::windowForWinId(topwin->winId());
	for (int i = 0; i < topHaikuWin->fakeChildList()->size(); ++i) {
		QHaikuWindow *win = topHaikuWin->fakeChildList()->at(i);
		if (!win->window()->isTopLevel() && win->window()->isVisible()) {
			QHaikuSurfaceView *view = QHaikuWindow::viewForWinId(topwin->winId());
			QPoint origin = win->mapToGlobal(QPoint()) - topwin->mapToGlobal(QPoint());
			view->DrawBitmapAsync(win->openGLBitmap(), BPoint(origin.x(), origin.y()));
		}
	}
}


void QHaikuBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    if (m_image.size().isEmpty())// && !window->isTopLevel())
        return;

    QSize imageSize = m_image.deviceIndependentSize().toSize();
    QRegion clipped = QRect(0, 0, window->width(), window->height());
    clipped &= QRect(0, 0, imageSize.width(), imageSize.height()).translated(-offset);
    QRect bounds = clipped.boundingRect().translated(offset);

    if (bounds.isNull())
        return;

    WId id = window->winId();

	QHaikuSurfaceView *view = QHaikuWindow::viewForWinId(id);
    QRect outline = region.boundingRect();

	if (view->LockLooperWithTimeout(10000) == B_OK) {
		view->SetDrawingMode(B_OP_COPY);
		QHaikuWindow *topHaikuWin = QHaikuWindow::windowForWinId(id)->topLevelWindow();
		BRect rect(outline.left(), outline.top(), outline.right(), outline.bottom());
		BRegion winregion(rect);
		BRegion region = topHaikuWin->getClippingRegion();
		view->ConstrainClippingRegion(&region);
		if (m_scale != 1.0) {
			// The pixels covering the region, from the one its top left
			// corner falls on: the window system puts that one on the
			// frame buffer pixel where the same coordinate falls.
			int left = qMax(0, qFloor(outline.left() * m_scale));
			int top = qMax(0, qFloor(outline.top() * m_scale));
			int right = qMin(m_image.width(), qCeil((outline.right() + 1) * m_scale)) - 1;
			int bottom = qMin(m_image.height(), qCeil((outline.bottom() + 1) * m_scale)) - 1;
			if (left <= right && top <= bottom) {
				view->DrawBitmapAsync(m_bitmap, BRect(left, top, right, bottom),
					BRect(left / m_scale, top / m_scale, (right + 1) / m_scale,
						(bottom + 1) / m_scale), kDevicePixelBitmap);
			}
		} else
			view->DrawBitmapAsync(m_bitmap, rect, rect);
		winregion.Exclude(&region);
		view->ConstrainClippingRegion(&winregion);
		drawChildWindows(window);
		view->Sync();
    	view->UnlockLooper();
    }

    m_windowAreaHash[id] = bounds;
}


void QHaikuBackingStore::resize(const QSize &size, const QRegion &)
{
	m_requestedSize = size;
	ensureBitmap();
}


void QHaikuBackingStore::beginPaint(const QRegion &)
{
	// the device pixel ratio may have changed since the last resize
	ensureBitmap();
}


// The image has the device pixel ratio's worth of pixels for each coordinate
// unit of the window, rounded up so that it covers every frame buffer pixel
// of the window wherever it is.
void QHaikuBackingStore::ensureBitmap()
{
	qreal scale = window()->handle() != NULL
		? window()->handle()->devicePixelRatio() : QHaikuScreen::deviceScale();
	QSize pixels(qCeil(m_requestedSize.width() * scale),
		qCeil(m_requestedSize.height() * scale));
	if (m_bitmap != NULL && m_image.size() == pixels && m_scale == scale)
		return;
	if (pixels.isEmpty())
		pixels = QSize(1, 1);

	QHaikuSurfaceView *view = window()->handle() != NULL
		? QHaikuWindow::viewForWinId(window()->winId()) : NULL;
	bool locked = view != NULL && view->LockLooperWithTimeout(10000) == B_OK;
	if (view != NULL && !locked)
		return;

	m_image = QImage();
	delete m_bitmap;
	m_bitmap = new BBitmap(BRect(0, 0, pixels.width() - 1, pixels.height() - 1),
		B_RGB32, true);
	m_image = QImage((uchar*)m_bitmap->Bits(), pixels.width(), pixels.height(),
		m_bitmap->BytesPerRow(), QImage::Format_RGB32);
	m_image.setDevicePixelRatio(scale);
	m_scale = scale;

	if (locked)
		view->UnlockLooper();
}


bool QHaikuBackingStore::scroll(const QRegion &area, int dx, int dy)
{
	if (m_image.isNull())
		return false;

	// At a fractional ratio a scroll is a fraction of a pixel: paint again.
	if (m_scale != qRound(m_scale))
		return false;

	int scale = qRound(m_scale);
	for (const QRect &rect : area) {
		qt_scrollRectInImage(m_image, QRect(rect.topLeft() * scale,
			rect.size() * scale), QPoint(dx, dy) * scale);
	}

	return true;
}

QT_END_NAMESPACE

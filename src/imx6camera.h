/****************************************************************************
**
** Copyright (C) 2015 Intopalo Oy
** Copyright (C) 2014 Pelagicore AG
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt-project.org/legal
**
** This file is based on Qt5 multimedia imx6 videonode plugin from
** http://code.qt.io/cgit/qt/qtmultimedia.git/tree/src/plugins/videonode/imx6
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#ifndef IMAX6CAMERA_H
#define IMAX6CAMERA_H

#include <QObject>
#include <QQuickItem>
#include <QMutex>
#include <QSGMaterial>
#include <QSize>
#include <QtQuick/qsgnode.h>
#include "imx6cameracontrol.h"

class QSGVivanteVideoMaterial : public QSGMaterial
{
public:
    QSGVivanteVideoMaterial();
    ~QSGVivanteVideoMaterial();

    virtual QSGMaterialType *type() const;
    virtual QSGMaterialShader *createShader() const;
    virtual int compare(const QSGMaterial *other) const;
    void updateBlending();
    void setCurrentFrame(const IMX6CameraFrame &frame);
    void bind();
    GLuint vivanteMapping(IMX6CameraFrame texIdVideoFramePair);
    void setOpacity(float o) { mOpacity = o; }

private:
    qreal mOpacity;
    int mWidth;
    int mHeight;
    IMX6CameraFrame::PixelFormat mFormat;
    QMap<const uchar*, GLuint> mBitsToTextureMap;
    IMX6CameraFrame mCurrentFrame, mNextFrame;
    GLuint mCurrentTexture;
    QMutex mFrameMutex;
};

class QSGVivanteVideoMaterialShader : public QSGMaterialShader
{
public:
    void updateState(const RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial);
    virtual char const *const *attributeNames() const;

protected:
    virtual const char *vertexShader() const;
    virtual const char *fragmentShader() const;
    virtual void initialize();

private:
    int mIdMatrix;
    int mIdTexture;
    int mIdOpacity;
};

class QSGVivanteVideoNode : public QSGGeometryNode
{
public:
    QSGVivanteVideoNode(IMX6CameraFrame::PixelFormat format);
    ~QSGVivanteVideoNode();

    virtual IMX6CameraFrame::PixelFormat pixelFormat() const { return mFormat; }
    void setCurrentFrame(const IMX6CameraFrame &frame);
    void setTexturedRectGeometry(const QRectF &boundingRect, const QRectF &textureRect, int orientation);
    static const QMap<IMX6CameraFrame::PixelFormat, GLenum>& getVideoFormat2GLFormatMap();

private:
    IMX6CameraFrame::PixelFormat mFormat;
    QSGVivanteVideoMaterial *mMaterial;
    QRectF m_rect;
    QRectF m_textureRect;
    int m_orientation;
    static QMap<IMX6CameraFrame::PixelFormat, GLenum> static_VideoFormat2GLFormatMap;
};

class QOpenGLContext;

class IMX6Camera : public QQuickItem
{
    Q_OBJECT
    Q_ENUMS(CameraParameter)
    Q_PROPERTY(qreal contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(qreal brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY mirrorChanged)
    Q_PROPERTY(bool isCameraConnected READ isCameraConnected NOTIFY cameraConnectionChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    IMX6Camera();
    ~IMX6Camera();
    QSGVivanteVideoNode *createNote(IMX6CameraFrame::PixelFormat m_format);
    void scheduleOpenGLContextUpdate();

    enum CameraParameter {
        WhiteBalancePreset,
        ColorTemperature,
        Contrast,
        Saturation,
        Brightness,
        Sharpening,
        Denoising,
        HorizontaMirror,
    };

    uint contrast() const;
    uint saturation() const;
    uint sharpening() const;
    uint brightness() const;
    bool mirror() const;
    bool isCameraConnected() const;
    QSize sourceSize() const;

public Q_SLOTS:
    void start();
    void stop();

    void setContrast(uint value);
    void setSaturation(uint value);
    void setSharpening(uint value);
    void setBrightness(uint value);
    void setMirror(bool value);
    void present(const IMX6CameraFrame &frame);
    void updateOpenGLContext();
    bool isParameterSupported(CameraParameter id) const;

Q_SIGNALS:
    void contrastChanged(uint);
    void saturationChanged(uint);
    void sharpeningChanged(uint);
    void brightnessChanged(uint);
    void mirrorChanged(bool);
    void cameraConnectionChanged(bool);
    void sourceSizeChanged(QSize);

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *);

private:
    QMutex m_frameMutex;
    bool m_frameChanged;
    QRectF m_renderedRect;         // Destination pixel coordinates, clipped
    QRectF m_sourceTextureRect;    // Source texture coordinates
    IMX6CameraFrame::PixelFormat m_format;
    QOpenGLContext *m_glContext;
    IMX6CameraControl *cameraControl;
    IMX6CameraFrame m_frame;
    bool m_isMirror;
    uint m_contrast;
    uint m_saturation;
    uint m_sharpening;
    uint m_brightness;
    int m_sessionId;
};

#endif // IMAX6CAMERA_H

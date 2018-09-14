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

#ifdef ARM_TARGET
#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#endif // ARM_TARGET
#include "imx6camera.h"

#include <QtCore/qmetatype.h>
#include <QtCore/qshareddata.h>
#include <QtCore/qvariant.h>
#include <QOpenGLContext>

IMX6Camera::IMX6Camera() : cameraControl(NULL)
  , m_isMirror(false)
  , m_contrast(0)
  , m_saturation(0)
  , m_sharpening(0)
  , m_brightness(0)
  , m_sessionId(0)
{
    cameraControl = IMX6CameraControl::cameraControl(&m_sessionId);
    start();
    setFlag(ItemHasContents, true);

    qRegisterMetaType<IMX6CameraFrame>("IMX6CameraFrame");
    connect(cameraControl, &IMX6CameraControl::frameReady, this, &IMX6Camera::present);
    connect(cameraControl, &IMX6CameraControl::cameraConnectionChanged, this, &IMX6Camera::cameraConnectionChanged);
    connect(cameraControl, &IMX6CameraControl::sourceSizeChanged, this, &IMX6Camera::sourceSizeChanged);
}

IMX6Camera::~IMX6Camera()
{
    cameraControl->stopCameraStream(m_sessionId);
    disconnect(cameraControl, &IMX6CameraControl::frameReady, this, &IMX6Camera::present);
    disconnect(cameraControl, &IMX6CameraControl::cameraConnectionChanged, this, &IMX6Camera::cameraConnectionChanged);
    disconnect(cameraControl, &IMX6CameraControl::sourceSizeChanged, this, &IMX6Camera::sourceSizeChanged);

    if (m_frame.buffer)
        m_frame.buffer->release();
}

QSGVivanteVideoNode *IMX6Camera::createNote(IMX6CameraFrame::PixelFormat format)
{
    return new QSGVivanteVideoNode(format);
}

void IMX6Camera::start()
{
    QMetaObject::invokeMethod(cameraControl, "startCamera", Qt::QueuedConnection, Q_ARG(uint, m_sessionId));
}

void IMX6Camera::stop()
{
    cameraControl->stopCameraStream(m_sessionId);
}

void IMX6Camera::setContrast(uint value)
{
    if (!cameraControl->setParameter(Contrast, value))
        return;
    if (value == m_contrast)
        return;
    m_contrast = value;
    emit contrastChanged(m_contrast);
}

void IMX6Camera::setSaturation(uint value)
{
    if (!cameraControl->setParameter(Saturation, value))
        return;
    if (value == m_saturation)
        return;
    m_saturation = value;
    emit saturationChanged(m_saturation);
}

void IMX6Camera::setSharpening(uint value)
{
    if (!cameraControl->setParameter(Sharpening, value))
        return;
    if (value == m_sharpening)
        return;
    m_sharpening = value;
    emit sharpeningChanged(m_sharpening);
}

void IMX6Camera::setBrightness(uint value)
{
    if (!cameraControl->setParameter(Brightness, value))
        return;
    if (value ==  m_brightness)
        return;
    m_brightness = value;
    emit brightnessChanged(m_brightness);
}

void IMX6Camera::setMirror(bool value)
{
    if (m_isMirror == value)
        return;
    m_isMirror = value;
    emit mirrorChanged(m_isMirror);
}

void IMX6Camera::present(const IMX6CameraFrame &frame)
{
    m_frameMutex.lock();
    if (m_frameChanged)
        m_frame.buffer->release();//Old frame is not updated to video node, replace it with new frame
    m_frame = frame;
    m_frameChanged = true;
    m_frameMutex.unlock();
    update();
}

void IMX6Camera::scheduleOpenGLContextUpdate()
{
    //This method is called from render thread
    QMetaObject::invokeMethod(this, "updateOpenGLContext");
}

uint IMX6Camera::contrast() const
{
    return cameraControl->parameter(Contrast);
}

uint IMX6Camera::saturation() const
{
    return cameraControl->parameter(Saturation);
}

uint IMX6Camera::sharpening() const
{
    return cameraControl->parameter(Sharpening);
}

uint IMX6Camera::brightness() const
{
    return cameraControl->parameter(Brightness);

}

bool IMX6Camera::mirror() const
{
    return m_isMirror;
}

bool IMX6Camera::isCameraConnected() const
{
    return cameraControl->isCameraConnected();
}

QSize IMX6Camera::sourceSize() const
{
    return cameraControl->sourceSize();
}

void IMX6Camera::updateOpenGLContext()
{
    //Set a dynamic property to access the OpenGL context in Qt Quick render thread.
    this->setProperty("GLContext", QVariant::fromValue<QObject*>(m_glContext));
}

bool IMX6Camera::isParameterSupported(IMX6Camera::CameraParameter id) const
{
    return cameraControl->isParameterSupported(id);
}

QSGNode *IMX6Camera::updatePaintNode(QSGNode *oldNode, QQuickItem::UpdatePaintNodeData *data)
{
    Q_UNUSED(data);

    if (cameraControl->state() != IMX6CameraControl::ActiveState)
        return 0;

    QSGVivanteVideoNode *videoNode = static_cast<QSGVivanteVideoNode *>(oldNode);

    QMutexLocker lock(&m_frameMutex);

    if (!m_glContext) {
        m_glContext = QOpenGLContext::currentContext();
        scheduleOpenGLContextUpdate();
    }

    if (!videoNode)
        videoNode = createNote(m_format);

    m_renderedRect = QRect(0, 0, width(), height());
    m_sourceTextureRect = QRect(0, 0, 1, 1);
    videoNode->setTexturedRectGeometry(m_renderedRect, m_sourceTextureRect, -1);

    if (m_frameChanged) {
        videoNode->setCurrentFrame(m_frame);
        m_frameChanged = false;
    }

    return videoNode;
}

QMap<IMX6CameraFrame::PixelFormat, GLenum> QSGVivanteVideoNode::static_VideoFormat2GLFormatMap = QMap<IMX6CameraFrame::PixelFormat, GLenum>();

QSGVivanteVideoNode::QSGVivanteVideoNode(IMX6CameraFrame::PixelFormat format) :
    mFormat(format), m_orientation(-1)
{
    setFlag(QSGNode::OwnsMaterial, true);
    mMaterial = new QSGVivanteVideoMaterial();
    setMaterial(mMaterial);
}

QSGVivanteVideoNode::~QSGVivanteVideoNode()
{
}

void QSGVivanteVideoNode::setCurrentFrame(const IMX6CameraFrame &frame)
{
    mMaterial->setCurrentFrame(frame);
    markDirty(DirtyMaterial);
}

static inline void qSetGeom(QSGGeometry::TexturedPoint2D *v, const QPointF &p)
{
    v->x = p.x();
    v->y = p.y();
}

static inline void qSetTex(QSGGeometry::TexturedPoint2D *v, const QPointF &p)
{
    v->tx = p.x();
    v->ty = p.y();
}

void QSGVivanteVideoNode::setTexturedRectGeometry(const QRectF &boundingRect, const QRectF &textureRect, int orientation)
{
    if (boundingRect == m_rect && textureRect == m_textureRect && orientation == m_orientation)
        return;

    m_rect = boundingRect;
    m_textureRect = textureRect;
    m_orientation = orientation;

    QSGGeometry *g = geometry();

    if (g == 0)
        g = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);

    QSGGeometry::TexturedPoint2D *v = g->vertexDataAsTexturedPoint2D();

    // Set geometry first
    qSetGeom(v + 0, m_rect.topLeft());
    qSetGeom(v + 1, m_rect.bottomLeft());
    qSetGeom(v + 2, m_rect.topRight());
    qSetGeom(v + 3, m_rect.bottomRight());

    // and then texture coordinates
    // tl, bl, tr, br
    qSetTex(v + 0, textureRect.topLeft());
    qSetTex(v + 1, textureRect.bottomLeft());
    qSetTex(v + 2, textureRect.topRight());
    qSetTex(v + 3, textureRect.bottomRight());

    if (!geometry())
        setGeometry(g);

    markDirty(DirtyGeometry);
}

const QMap<IMX6CameraFrame::PixelFormat, GLenum>& QSGVivanteVideoNode::getVideoFormat2GLFormatMap()
{
#ifdef ARM_TARGET
    if (static_VideoFormat2GLFormatMap.isEmpty()) {
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_YUV420P,  GL_VIV_I420);
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_YV12,     GL_VIV_YV12);
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_NV12,     GL_VIV_NV12);
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_NV21,     GL_VIV_NV21);
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_UYVY,     GL_VIV_UYVY);
        static_VideoFormat2GLFormatMap.insert(IMX6CameraFrame::Format_YUYV,     GL_VIV_YUY2);
    }
#endif
    return static_VideoFormat2GLFormatMap;
}

QSGVivanteVideoMaterial::QSGVivanteVideoMaterial() :
    mOpacity(1.0),
    mWidth(0),
    mHeight(0),
    mFormat(IMX6CameraFrame::Format_Invalid),
    mCurrentTexture(0)
{
#ifdef QT_VIVANTE_VIDEO_DEBUG
    qDebug() << Q_FUNC_INFO;
#endif

    setFlag(Blending, false);
}

QSGVivanteVideoMaterial::~QSGVivanteVideoMaterial()
{
#ifdef ARM_TARGET
    Q_FOREACH (GLuint id, mBitsToTextureMap.values()) {
#ifdef QT_VIVANTE_VIDEO_DEBUG
        qDebug() << "delete texture: " << id;
#endif
        glDeleteTextures(1, &id);
    }
#endif
    if (mCurrentFrame.buffer)
        mCurrentFrame.buffer->release();
    if (mNextFrame.buffer)
        mNextFrame.buffer->release();
}

QSGMaterialType *QSGVivanteVideoMaterial::type() const {
    static QSGMaterialType theType;
    return &theType;
}

QSGMaterialShader *QSGVivanteVideoMaterial::createShader() const {
    return new QSGVivanteVideoMaterialShader;
}

int QSGVivanteVideoMaterial::compare(const QSGMaterial *other) const {
    if (this->type() == other->type()) {
        const QSGVivanteVideoMaterial *m = static_cast<const QSGVivanteVideoMaterial *>(other);
        if (this->mBitsToTextureMap == m->mBitsToTextureMap)
            return 0;
        else
            return 1;
    }
    return 1;
}

void QSGVivanteVideoMaterial::updateBlending() {
    setFlag(Blending, qFuzzyCompare(mOpacity, qreal(1.0)) ? false : true);
}

void QSGVivanteVideoMaterial::setCurrentFrame(const IMX6CameraFrame &frame) {
    QMutexLocker lock(&mFrameMutex);
    if (mNextFrame.isValid()) {
        // Old frame is not binded to texture yet, release it before updating new frame
        if (!mCurrentFrame.isValid() || mNextFrame.buffer != mCurrentFrame.buffer)
            mNextFrame.buffer->release();
    }
    mNextFrame = frame;
}

void QSGVivanteVideoMaterial::bind()
{
    QOpenGLContext *glcontext = QOpenGLContext::currentContext();
    if (glcontext == 0) {
        qWarning() << Q_FUNC_INFO << "no QOpenGLContext::currentContext() => return";
        return;
    }
#ifdef ARM_TARGET
    QMutexLocker lock(&mFrameMutex);
    if (mNextFrame.isValid()) {
        if (mCurrentFrame.buffer)
            mCurrentFrame.buffer->release();
        mCurrentFrame = mNextFrame;
        mCurrentTexture = vivanteMapping(mNextFrame);
    } else {
        glBindTexture(GL_TEXTURE_2D, mCurrentTexture);
    }
#endif
}

GLuint QSGVivanteVideoMaterial::vivanteMapping(IMX6CameraFrame vF)
{
    QOpenGLContext *glcontext = QOpenGLContext::currentContext();
    if (glcontext == 0) {
        qWarning() << Q_FUNC_INFO << "no QOpenGLContext::currentContext() => return 0";
        return 0;
    }
#ifndef ARM_TARGET
    Q_UNUSED(vF)
#else
    static PFNGLTEXDIRECTVIVMAPPROC glTexDirectVIVMap_LOCAL = 0;
    static PFNGLTEXDIRECTINVALIDATEVIVPROC glTexDirectInvalidateVIV_LOCAL = 0;

    if (glTexDirectVIVMap_LOCAL == 0 || glTexDirectInvalidateVIV_LOCAL == 0) {
        glTexDirectVIVMap_LOCAL = reinterpret_cast<PFNGLTEXDIRECTVIVMAPPROC>(glcontext->getProcAddress("glTexDirectVIVMap"));
        glTexDirectInvalidateVIV_LOCAL = reinterpret_cast<PFNGLTEXDIRECTINVALIDATEVIVPROC>(glcontext->getProcAddress("glTexDirectInvalidateVIV"));
    }
    if (glTexDirectVIVMap_LOCAL == 0 || glTexDirectInvalidateVIV_LOCAL == 0) {
        qWarning() << Q_FUNC_INFO << "couldn't find \"glTexDirectVIVMap\" and/or \"glTexDirectInvalidateVIV\" => do nothing and return";
        return 0;
    }

    if (mWidth != vF.size.width() || mHeight != vF.size.height() || mFormat != vF.format) {
        mWidth = vF.size.width();
        mHeight = vF.size.height();
        mFormat = vF.format;
        Q_FOREACH (GLuint id, mBitsToTextureMap.values()) {
#ifdef QT_VIVANTE_VIDEO_DEBUG
            qDebug() << "delete texture: " << id;
#endif
            glDeleteTextures(1, &id);
        }
        mBitsToTextureMap.clear();
    }

    if (!mBitsToTextureMap.contains(vF.buffer->start())) {
        GLuint tmpTexId;
        glGenTextures(1, &tmpTexId);
        mBitsToTextureMap.insert(vF.buffer->start(), tmpTexId);

        const uchar *constBits = vF.buffer->start();
        void *bits = (void*)constBits;

        GLuint physical = ~0U;

        glBindTexture(GL_TEXTURE_2D, tmpTexId);
        glTexDirectVIVMap_LOCAL(GL_TEXTURE_2D,
                                vF.size.width(), vF.size.height(),
                                QSGVivanteVideoNode::getVideoFormat2GLFormatMap().value(vF.format),
                                &bits, &physical);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexDirectInvalidateVIV_LOCAL(GL_TEXTURE_2D);

        return tmpTexId;
    } else {
        glBindTexture(GL_TEXTURE_2D, mBitsToTextureMap.value(vF.buffer->start()));
        glTexDirectInvalidateVIV_LOCAL(GL_TEXTURE_2D);
        return mBitsToTextureMap.value(vF.buffer->start());
    }

    Q_ASSERT(false); // should never reach this line!;
#endif
    return 0;
}

void QSGVivanteVideoMaterialShader::updateState(const RenderState &state,
                                                QSGMaterial *newMaterial,
                                                QSGMaterial *oldMaterial)
{
    Q_UNUSED(oldMaterial);

    QSGVivanteVideoMaterial *mat = static_cast<QSGVivanteVideoMaterial *>(newMaterial);
    program()->setUniformValue(mIdTexture, 0);
    mat->bind();
    if (state.isOpacityDirty()) {
        mat->setOpacity(state.opacity());
        program()->setUniformValue(mIdOpacity, state.opacity());
    }
    if (state.isMatrixDirty())
        program()->setUniformValue(mIdMatrix, state.combinedMatrix());
}

const char * const *QSGVivanteVideoMaterialShader::attributeNames() const {
    static const char *names[] = {
        "qt_VertexPosition",
        "qt_VertexTexCoord",
        0
    };
    return names;
}

const char *QSGVivanteVideoMaterialShader::vertexShader() const {
    static const char *shader =
            "uniform highp mat4 qt_Matrix;                      \n"
            "attribute highp vec4 qt_VertexPosition;            \n"
            "attribute highp vec2 qt_VertexTexCoord;            \n"
            "varying highp vec2 qt_TexCoord;                    \n"
            "void main() {                                      \n"
            "    qt_TexCoord = qt_VertexTexCoord;               \n"
            "    gl_Position = qt_Matrix * qt_VertexPosition;   \n"
            "}";
    return shader;
}

const char *QSGVivanteVideoMaterialShader::fragmentShader() const {
    static const char *shader =
            "uniform sampler2D texture;"
            "uniform lowp float opacity;"
            ""
            "varying highp vec2 qt_TexCoord;"
            ""
            "void main()"
            "{"
            "  gl_FragColor = vec4(texture2D( texture, qt_TexCoord ).rgb, 1.0) * opacity;\n"
            "}";
    return shader;
}

void QSGVivanteVideoMaterialShader::initialize() {
    mIdMatrix = program()->uniformLocation("qt_Matrix");
    mIdTexture = program()->uniformLocation("texture");
    mIdOpacity = program()->uniformLocation("opacity");
}

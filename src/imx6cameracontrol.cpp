/****************************************************************************
**
** Copyright (C) 2015 Intopalo Oy
** Copyright (C) 2014 Pelagicore AG
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt-project.org/legal
**
** This file is based on Qt5 multimedia from
** http://code.qt.io/cgit/qt/qtmultimedia.git/tree/src
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

#include "imx6cameracontrol.h"
#include "imx6camera.h"
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>

#include <linux/videodev2.h>
#include <libv4l2.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <unistd.h>
#include "math.h"

#define V_MAP_MODE V4L2_MEMORY_MMAP
#define V_BUFFER_COUNT 4
#define V4L2_PREFERRED_FORMAT V4L2_PIX_FMT_YUV420

#define DEBUG_V4L2_CAMERA(...) ((void)0)
//#define DEBUG_V4L2_CAMERA qDebug

static inline IMX6CameraFrame::PixelFormat v4l2PixelFormat(quint32 format)
{
    switch (format) {
    case V4L2_PIX_FMT_YUYV:
        return IMX6CameraFrame::Format_YUYV;
    case V4L2_PIX_FMT_UYVY:
        return IMX6CameraFrame::Format_UYVY;
    case V4L2_PIX_FMT_YUV444:
        return IMX6CameraFrame::Format_YUV444;
    case V4L2_PIX_FMT_YUV420:
        return IMX6CameraFrame::Format_YUV420P;
    case V4L2_PIX_FMT_NV12:
        return IMX6CameraFrame::Format_NV12;
    case V4L2_PIX_FMT_NV21:
        return IMX6CameraFrame::Format_NV21;
    default:
        break;
    }
    return IMX6CameraFrame::Format_Invalid;
}

class IMX6CameraControlPrivate
{
public:
    IMX6CameraControlPrivate()
        : state(IMX6CameraControl::UnloadedState)
        , device("/dev/video0") // TODO: implement QMediaServiceSupportedDevicesInterface for device enumeration
        , handle(-1)
        , socketNotifier(NULL)
        , size(QSize(720, 576))
        , cameraDetectTimer(NULL)
        , reloadCount(0)
        , pollCount(0)
        , isCameraConnected(false)
        , action(IMX6CameraControl::NoAction)
    {
        memset(&buffers[0], 0, sizeof(Buffer));
        memset(&buffers[1], 0, sizeof(Buffer));
        memset(&buffers[2], 0, sizeof(Buffer));
        memset(&buffers[3], 0, sizeof(Buffer));
    }

    IMX6CameraControl::State state;

    QByteArray device;
    v4l2_buf_type bufferType;

    int handle;
    QSocketNotifier *socketNotifier;
    QSet<int> indexs;
    IMX6CameraFrame::PixelFormat pixelFormat;
    QSize size;
    Buffer buffers[V_BUFFER_COUNT];
    QHash<int, V4L2CameraFrameBuffer *> frameBuffers;
    QHash<int, v4l2_queryctrl> supportedControls;
    QTimer *cameraDetectTimer;
    int reloadCount;
    int pollCount;
    bool isCameraConnected;
    IMX6CameraControl::Action action;
    static int sessionId;
    static QSet<int> openSessionIdList;
};

int IMX6CameraControlPrivate::sessionId = 0;
QSet<int> IMX6CameraControlPrivate::openSessionIdList;
IMX6CameraControl* IMX6CameraControl::s_cameraControl = NULL;

IMX6CameraControl::IMX6CameraControl(QObject *parent)
    : QObject(parent)
    , d_ptr(new IMX6CameraControlPrivate)
{
    Q_D(IMX6CameraControl);
    for (int i = 0; i < V_BUFFER_COUNT; ++i)
        d->frameBuffers.insert(i, new V4L2CameraFrameBuffer(this));
}

IMX6CameraControl::~IMX6CameraControl()
{
    Q_D(IMX6CameraControl);
    unload();
    d->cameraDetectTimer->stop();

    QHashIterator<int, V4L2CameraFrameBuffer *> it(d->frameBuffers);
    while (it.hasNext()) {
        it.next();
        delete it.value();
    }
    d->frameBuffers.clear();
}

IMX6CameraControl *IMX6CameraControl::cameraControl(int *sessionId, QObject *parent)
{
    if (!s_cameraControl)
        s_cameraControl = new IMX6CameraControl(parent);
    *sessionId = IMX6CameraControlPrivate::sessionId;
    ++IMX6CameraControlPrivate::sessionId;
    return s_cameraControl;
}

bool IMX6CameraControl::load()
{
    Q_D(IMX6CameraControl);
    if (d->state != UnloadedState)
        return true;

    // Re-open the connection for proper initialization
    v4l2_close(d->handle);
    d->handle = v4l2_open(d->device.constData(), O_RDWR | O_NONBLOCK, 0);
    if (d->handle < 0) {
        qCritical("Could not open the video device.");
    }

    v4l2_capability capability;
    if (v4l2_ioctl(d->handle, VIDIOC_QUERYCAP, &capability) < 0) {
        qCritical("Failed to query the device capabilities.");
        v4l2_close(d->handle);
        d->handle = -1;
        return false;
    }

    if (!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        qCritical("The device does not support video capture.");
        v4l2_close(d->handle);
        d->handle = -1;
        return false;
    }

    int index = 1;
    if (v4l2_ioctl(d->handle, VIDIOC_S_INPUT, &index)) {
        qCritical("Could not set the video input index. %d %s", errno, strerror(errno));
        v4l2_close(d->handle);
        d->handle = -1;
        return false;
    }

    v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // Read size from driver side
    format.fmt.pix.width = 0;
    format.fmt.pix.height = 0;

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (v4l2_ioctl(d->handle, VIDIOC_S_FMT, &format) < 0) {
        qCritical("Could not set the video format. %d %s", errno, strerror(errno));
        v4l2_close(d->handle);
        d->handle = -1;
        return false;
    }

    const IMX6CameraFrame::PixelFormat pixelFormat = v4l2PixelFormat(format.fmt.pix.pixelformat);
    d->pixelFormat = pixelFormat;
    if (d->size != QSize(format.fmt.pix.width, format.fmt.pix.height)) {
        d->size = QSize(format.fmt.pix.width, format.fmt.pix.height);
        emit sourceSizeChanged(d->size);
    }

    v4l2_requestbuffers bufferRequest;
    memset(&bufferRequest, 0, sizeof(bufferRequest));
    bufferRequest.count = V_BUFFER_COUNT;
    bufferRequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufferRequest.memory = V_MAP_MODE;
    if (v4l2_ioctl(d->handle, VIDIOC_REQBUFS, &bufferRequest) < 0) {
        qCritical("Could not complete the buffer request.");
        v4l2_close(d->handle);
        d->handle = -1;
        return false;
    }

    d->socketNotifier = new QSocketNotifier(d->handle, QSocketNotifier::Read, this);
    connect(d->socketNotifier, &QSocketNotifier::activated, this, &IMX6CameraControl::dequeueFrame);

    for (int i = 0; i < V_BUFFER_COUNT; ++i) {
        v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V_MAP_MODE;
        buffer.index = i;
        if (v4l2_ioctl(d->handle, VIDIOC_QUERYBUF, &buffer) < 0) {
            qCritical("Could not query video buffer.");
            unload();
            return false;
        }

        void *data = v4l2_mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, d->handle, buffer.m.offset);
        if (data == MAP_FAILED) {
            qCritical("Failed to map video buffer.");
            unload();
            return false;
        }
        d->buffers[i].start = reinterpret_cast<uchar *>(data);
        d->buffers[i].bytesPerLine = format.fmt.pix.bytesperline;
        // Workaround for alignment assumption in front-end
        if (pixelFormat == IMX6CameraFrame::Format_YUV420P || pixelFormat == IMX6CameraFrame::Format_YV12)
            d->buffers[i].length = d->buffers[i].bytesPerLine * format.fmt.pix.height * 1.5;
        else
            d->buffers[i].length = buffer.length;

        d->frameBuffers[i]->set_values(d->buffers[i], i);
    }

    d->state =  LoadedState;
    queryControls();
    return true;
}

bool IMX6CameraControl::unload()
{
    Q_D(IMX6CameraControl);

    if (d->state == UnloadedState)
        return false;

    if (d->state == ActiveState)
        stopStream();

    QSet<int>::Iterator it = d->indexs.begin();
    for (; it != d->indexs.end(); ++it)
        queueFrame(*it);

    if (d->handle >= 0) {
        if (d->socketNotifier) {
            d->socketNotifier->setEnabled(false);
            d->socketNotifier->deleteLater();
        }

        for (int i = 0; i < V_BUFFER_COUNT; ++i)
            v4l2_munmap(d->buffers[i].start, d->buffers[i].length);
        v4l2_close(d->handle);
        d->handle = -1;
    }

    d->state = UnloadedState;
    return true;
}

#define CAMERA_LOAD_DETECTION_INTERVAL 200
#define CAMERA_REMOVE_DETECTION_INTERVAL 1000
bool IMX6CameraControl::startCamera(uint sessionId)
{
    Q_D(IMX6CameraControl);
    IMX6CameraControlPrivate::openSessionIdList.insert(sessionId);
    switch (d->state) {
    case LoadedState:
        startCameraStream();
        break;
    case UnloadedState:
        d->action = StartCamera;
        startCameraDetection(CAMERA_LOAD_DETECTION_INTERVAL);
        break;
    default:
        break;
    }
    return true;
}

bool IMX6CameraControl::stopCameraStream(int sessionId)
{
    Q_D(IMX6CameraControl);
    IMX6CameraControlPrivate::openSessionIdList.remove(sessionId);
    d->action = StopCamera;
    stopStream();

    if (!IMX6CameraControlPrivate::openSessionIdList.isEmpty()) {
        QMetaObject::invokeMethod(this
                                  , "startCamera"
                                  , Qt::QueuedConnection
                                  , Q_ARG(uint, IMX6CameraControlPrivate::openSessionIdList.values().first()));
    }
    return true;
}

bool IMX6CameraControl::startCameraStream()
{
    Q_D(IMX6CameraControl);
    d->action = StartCamera;
    return startStream();
}

bool IMX6CameraControl::startStream()
{
    Q_D(IMX6CameraControl);
    if (d->state != LoadedState)
        return false;

    Q_ASSERT(d->handle >= 0);
    d->reloadCount = 0;
    for (int i = 0; i < V_BUFFER_COUNT; ++i) {
        v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V_MAP_MODE;
        buffer.index = i;
        if (v4l2_ioctl(d->handle, VIDIOC_QBUF, &buffer) < 0) {
            qCritical("Could not queue buffer.");
            unload();
            return false;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_ioctl(d->handle, VIDIOC_STREAMON, &type) < 0) {
        qCritical( "Could not start the stream.");
        return false;
    }
    d->state = ActiveState;
    return true;
}

bool IMX6CameraControl::stopStream()
{
    Q_D(IMX6CameraControl);
    if (d->handle < 0)
        return false;

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_ioctl(d->handle, VIDIOC_STREAMOFF, &type) < 0) {
        qCritical("Could not stop the stream.");
        unload();
        return false;
    }
    d->state = LoadedState;

    QSet<int>::Iterator it = d->indexs.begin();
    for (; it != d->indexs.end(); ++it)
        queueFrame(*it);
    return true;
}

void IMX6CameraControl::startCameraDetection(int interval)
{
    Q_D(IMX6CameraControl);
    if (!d->cameraDetectTimer) {
        d->cameraDetectTimer = new QTimer(this);
        connect(d->cameraDetectTimer, &QTimer::timeout, this, &IMX6CameraControl::cameraDetectTimeout);
    }
    d->cameraDetectTimer->start(interval);
}

void IMX6CameraControl::cameraDetectTimeout()
{
    Q_D(IMX6CameraControl);
    DEBUG_V4L2_CAMERA("%s, %d, %d %d", Q_FUNC_INFO, d->state, d->action, d->handle);

    if (d->handle < 0) {
        d->handle = v4l2_open(d->device.constData(), O_RDWR | O_NONBLOCK, 0);
        if (d->handle < 0) {
            qCritical("Could not open the video device.");
        }
    }
    bool connected = pollVDLOSS();
    switch (d->state) {
    case ActiveState:
        if (d->action == StartCamera && !connected) {
            stopStream();
            DEBUG_V4L2_CAMERA("Camera removed, stop stream");
        }
        break;
    case LoadedState:
        if (d->action == StartCamera) {
            if (connected) {
                startStream();
                DEBUG_V4L2_CAMERA("camera connected and resume stream");
            } else {
                DEBUG_V4L2_CAMERA("waiting for camera connection");
            }
        }
        break;
    case UnloadedState:
        if (d->action == StartCamera) {
            if (connected && load()) {
                startStream();
                d->cameraDetectTimer->setInterval(CAMERA_REMOVE_DETECTION_INTERVAL);
                return;
            }
            DEBUG_V4L2_CAMERA("waiting for camera connection");
        }
        break;
    default:
        break;
    }

    if (d->isCameraConnected != connected) {
        d->isCameraConnected = connected;
        emit cameraConnectionChanged(connected);
    }
}

void IMX6CameraControl::queueFrame(int releasedIndex)
{
    Q_D(IMX6CameraControl);
    if (d->state != ActiveState)
        return;

    if (!d->indexs.contains(releasedIndex))
        return;

    d->indexs.remove(releasedIndex);
    v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V_MAP_MODE;
    buffer.index = releasedIndex;
    if (v4l2_ioctl(d->handle, VIDIOC_QBUF, &buffer) < 0) {
        qDebug("Could not queue new buffer. %d", releasedIndex);
        return;
    }
}

void IMX6CameraControl::dequeueFrame()
{
    Q_D(IMX6CameraControl);
    if (d->state != ActiveState)
        return;
    v4l2_buffer buffer;

    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V_MAP_MODE;
    if (ioctl(d->handle, VIDIOC_DQBUF, &buffer) < 0) { // use ioctl directly due to noisy v4l2
        qCritical("Could not dequeue buffer. %d, %s", errno, strerror(errno));
        return;
    }

    d->indexs.insert(buffer.index);
    IMX6CameraFrame frame(d->frameBuffers[buffer.index], d->size, d->pixelFormat);
    emit frameReady(frame);
}

void IMX6CameraControl::queryControls()
{
    Q_D(IMX6CameraControl);
    for (int index = V4L2_CID_BASE; index < V4L2_CID_LASTP1; ++index) {
        struct v4l2_queryctrl queryctrl;
        memset(&queryctrl, 0, sizeof(queryctrl));
        queryctrl.id = index;
        if (0 == ioctl(d->handle, VIDIOC_QUERYCTRL, &queryctrl)) {
            if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)
                continue;
            int readId;
            switch (queryctrl.id) {
            case V4L2_CID_BRIGHTNESS:
                readId = IMX6Camera::Brightness;
                break;
            case V4L2_CID_CONTRAST:
                readId = IMX6Camera::Contrast;
                break;
            case V4L2_CID_SATURATION:
                readId = IMX6Camera::Saturation;
                break;
            case V4L2_CID_HFLIP:
                readId = IMX6Camera::HorizontaMirror;
                break;
            default:
                DEBUG_V4L2_CAMERA("Skip Control %s\n", queryctrl.name);
                continue;
            }
            d->supportedControls.insert(readId, queryctrl);
        } else {
            if (errno == EINVAL)
                continue;
            qCritical("VIDIOC_QUERYCTRL error %d", errno);
        }
    }
}

IMX6CameraControl::State IMX6CameraControl::state() const
{
    Q_D(const IMX6CameraControl);
    return d->state;
}

bool IMX6CameraControl::setParameter(int id, uint adjustValue)
{
    if (adjustValue > 100) {
        DEBUG_V4L2_CAMERA("Value is higher than 100");
        return false;
    }

    Q_D(IMX6CameraControl);
    if (!d->supportedControls.contains(id)) {
        DEBUG_V4L2_CAMERA("Control ID %d: unsupported control", id);
        return false;
    }

    const struct v4l2_queryctrl &queryctrl = d->supportedControls[id];
    struct v4l2_control control;
    control.id = queryctrl.id;

    // ContrastAdjustment, SaturationAdjustment, BrightnessAdjustment, SharpeningAdjustment
    // and DenoisingAdjustment the value should be in [0-100] range
    control.value = (qint32)(adjustValue * (queryctrl.maximum - queryctrl.minimum)/100  + queryctrl.minimum);

    if (control.value > queryctrl.maximum)
        control.value = queryctrl.maximum;
    else if (control.value < queryctrl.minimum)
        control.value = queryctrl.minimum;

    DEBUG_V4L2_CAMERA("Adjust v4l2 camera %d %d %d\n", id, adjustValue, control.value);
    DEBUG_V4L2_CAMERA("max value %d mini %d\n", queryctrl.maximum, queryctrl.minimum);

    if (-1 == ioctl(d->handle, VIDIOC_S_CTRL, &control) && errno != ERANGE) {
        qCritical("Camera control adjust error %d", errno);
        return false;
    }
    return true;
}

bool IMX6CameraControl::isParameterSupported(int id) const
{
    Q_D(const IMX6CameraControl);
    return d->supportedControls.contains(id);
}

int IMX6CameraControl::parameter(int id) const
{
    Q_D(const IMX6CameraControl);
    if (!d->supportedControls.contains(id))
        return 0;
    float value;
    int returnValue = 0;
    const struct v4l2_queryctrl &queryctrl = d->supportedControls[id];
    struct v4l2_control control;
    memset(&control, 0, sizeof(control));
    control.id = queryctrl.id;

    if (0 == ioctl(d->handle, VIDIOC_G_CTRL, &control)) {
        value = control.value;
        // Contrast, Saturation, Brightness, Sharpening and Denoising the value should be in [0..100] range
        if (value > queryctrl.maximum)
            value = 1.0f;
        else if (value < queryctrl.minimum)
            value = 0.0f;
        else
            value = (value - queryctrl.minimum) / (queryctrl.maximum - queryctrl.minimum);

        returnValue= static_cast<int>(round(value * 100));

    } else {
        qCritical("Failed to get v4l2 control value %d", id);
    }
    return returnValue;
}
#ifndef V4L2_CID_VID_VIDEO_DETECT
#define V4L2_CID_VID_VIDEO_DETECT (V4L2_CID_BASE + 39)
#endif

bool IMX6CameraControl::pollVDLOSS()
{
    Q_D(IMX6CameraControl);
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_VID_VIDEO_DETECT;
    int ret = ioctl(d->handle, VIDIOC_G_CTRL, &ctrl);
    if (-1 == ret) {
        qCritical() << "ioctl VDLOSS failed";
        return false;
    }
    return ctrl.value == 1;
}

bool IMX6CameraControl::isCameraConnected() const
{
    Q_D(const IMX6CameraControl);
    return d->isCameraConnected;
}

QSize IMX6CameraControl::sourceSize() const
{
    Q_D(const IMX6CameraControl);
    return d->size;
}

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
#ifndef IMAX6CAMERACONTROL_H
#define IMAX6CAMERACONTROL_H

#include <QObject>
#include <QSize>
struct Buffer {
    uchar *start;
    size_t length;
    int bytesPerLine;
};

class IMX6CameraFrame;
class IMX6CameraControlPrivate;
class IMX6Camera;
class IMX6CameraControl : public QObject
{
    Q_OBJECT
public:
    enum State {
        UnloadedState,
        LoadedState,
        ActiveState
    };

    enum Action {
        NoAction,
        StartCamera,
        StopCamera
    };

    static IMX6CameraControl *cameraControl(int *sessionId, QObject *parent = 0);

    bool load();
    bool unload();

    State state() const;

    bool setParameter(int id, uint value);

    bool isParameterSupported(int id) const;
    int parameter(int id) const;
    bool pollVDLOSS();
    bool isCameraConnected() const;
    QSize sourceSize() const;

public slots:
    void queueFrame(int releasedIndex);
    void dequeueFrame();

    bool startCamera(uint sessionId);
    bool stopCameraStream(int sessionId);
    bool startCameraStream();
    void startCameraDetection(int interval);

signals:
    void frameReady(const IMX6CameraFrame &frame);
    void cameraConnectionChanged(bool);
    void sourceSizeChanged(QSize);

private slots:
    void cameraDetectTimeout();
    bool startStream();
    bool stopStream();

private:
    IMX6CameraControl(QObject *parent = 0);
    ~IMX6CameraControl();
    void queryControls();

private:
    static IMX6CameraControl* s_cameraControl;
    QScopedPointer<IMX6CameraControlPrivate> d_ptr;
    Q_DECLARE_PRIVATE(IMX6CameraControl)
};

class V4L2CameraFrameBuffer
{
public:
    enum MapMode
    {
        NotMapped = 0x00,
        ReadOnly  = 0x01,
        WriteOnly = 0x02,
        ReadWrite = ReadOnly | WriteOnly
    };

    V4L2CameraFrameBuffer(IMX6CameraControl *control, const Buffer &handle, int index)
        : control(control), handle(handle), index(index)
    {
    }

    V4L2CameraFrameBuffer(IMX6CameraControl *ctl)
        : control(ctl)
    {
    }

    void set_values(const Buffer &buffer, int buffer_index) {
        handle = buffer;
        index = buffer_index;
    }

    ~V4L2CameraFrameBuffer()
    {
    }

    void release()
    {
        if (control)
            control->queueFrame(index);
    }

    MapMode mapMode() const
    {
        return ReadOnly;
    }

    uchar *map(MapMode mode, int *numBytes, int *bytesPerLine)
    {
        if (mode != ReadOnly)
            return Q_NULLPTR;

        *numBytes = handle.length;
        *bytesPerLine = handle.bytesPerLine;
        return handle.start;
    }

    uchar *start()
    {
        return handle.start;
    }

    void unmap()
    {
        //... nothing to do, currently
    }

private:
    IMX6CameraControl *control;
    Buffer handle;
    int index;
};

class IMX6CameraFrame
{
public:
    enum FieldType
    {
        ProgressiveFrame,
        TopField,
        BottomField,
        InterlacedFrame
    };

    enum PixelFormat
    {
        Format_Invalid,
        Format_AYUV444,
        Format_AYUV444_Premultiplied,
        Format_YUV444,
        Format_YUV420P,
        Format_YV12,
        Format_UYVY,
        Format_YUYV,
        Format_NV12,
        Format_NV21,
    };

    IMX6CameraFrame(V4L2CameraFrameBuffer *buffer, const QSize &size, PixelFormat format)
        : buffer(buffer), size(size), format(format)
    {}

    IMX6CameraFrame() : buffer(0)
    {}

    ~IMX6CameraFrame()
    {}

    IMX6CameraFrame &operator =(const IMX6CameraFrame &other)
    {
        buffer = other.buffer;
        size = other.size;
        format = other.format;
        return *this;
    }

    bool isValid() const
    {
        return buffer != 0;
    }

    V4L2CameraFrameBuffer *buffer;
    QSize size;
    PixelFormat format;
};


#endif // IMAX6CAMERACONTROL_H

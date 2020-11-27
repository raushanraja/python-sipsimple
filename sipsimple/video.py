
"""Video support"""

from __future__ import absolute_import
import weakref
from functools import partial
from itertools import combinations
from threading import RLock

__all__ = ['IVideoProducer', 'VideoDevice', 'VideoError']

from application.notification import IObserver, NotificationCenter, NotificationData, ObserverWeakrefProxy
from application.notification import NotificationCenter, NotificationData
from zope.interface import Attribute, Interface, implements

from sipsimple.core import SIPCoreError, VideoCamera


class IVideoProducer(Interface):
    """
    Interface describing an object which can produce video data.
    All attributes of this interface are read-only.
    """

    producer = Attribute("The core producer object which can be connected to a consumer")


class VideoError(Exception): pass


class VideoDevice(object):
    implements(IVideoProducer)

    def __init__(self, device_name, resolution, framerate):
        self._camera = self._open_camera(device_name, resolution, framerate)
        self._camera.start()

    def _open_camera(self, device_name, resolution, framerate):
        try:
            return VideoCamera(device_name, resolution, framerate)
        except SIPCoreError:
            try:
                return VideoCamera(u'system_default', resolution, framerate)
            except SIPCoreError:
                return VideoCamera(None, resolution, framerate)

    def set_camera(self, device_name, resolution, framerate):
        old_camera = self._camera
        old_camera.close()
        new_camera = self._open_camera(device_name, resolution, framerate)
        if not self.muted:
            new_camera.start()
        self._camera = new_camera
        notification_center = NotificationCenter()
        notification_center.post_notification('VideoDeviceDidChangeCamera', sender=self, data=NotificationData(old_camera=old_camera, new_camera=new_camera))

    @property
    def producer(self):
        return self._camera

    @property
    def name(self):
        return self._camera.name

    @property
    def real_name(self):
        return self._camera.real_name

    def _set_muted(self, value):
        if not isinstance(value, bool):
            raise ValueError('illegal value for muted property: %r' % (value,))
        if value == self.muted:
            return
        if value:
            self._camera.stop()
        else:
            self._camera.start()
        self.__dict__['muted'] = value

    def _get_muted(self):
        return self.__dict__.get('muted', False)

    muted = property(_get_muted, _set_muted)
    del _get_muted, _set_muted


class VideoBridge(object):
    """
    A RootAudioBridge is a container for objects providing the IAudioPort
    interface. It connects all such objects in a full-mesh such that all audio
    producers are connected to all consumers.

    The difference between a RootAudioBridge and an AudioBridge is that the
    RootAudioBridge does not implement the IAudioPort interface. This makes it
    more efficient.
    """

    implements(IObserver)

    def __init__(self, video_mixer):
        self.video_mixer = video_mixer
        self.streams = set()
        self._lock = RLock()
        notification_center = NotificationCenter()
        notification_center.add_observer(ObserverWeakrefProxy(self), name='VideoPortDidChangeSlots')

    def __del__(self):
        '''
        if len(self.ports) >= 2:
            for port1, port2 in ((wr1(), wr2()) for wr1, wr2 in combinations(self.ports, 2)):
                if port1 is None or port2 is None:
                    continue
                if port1.producer_slot is not None and port2.consumer_slot is not None:
                    self.mixer.disconnect_slots(port1.producer_slot, port2.consumer_slot)
                if port2.producer_slot is not None and port1.consumer_slot is not None:
                    self.mixer.disconnect_slots(port2.producer_slot, port1.consumer_slot)
        self.ports.clear()
        '''
        pass

    def __contains__(self, stream):
        return weakref.ref(stream) in self.streams

    def add(self, stream):
        with self._lock:
            if stream.transport.video_mixer is not self.video_mixer:
                raise ValueError("expected port with Mixer %r, got %r" % (self.video_mixer, stream.transport.video_mixer))
            if weakref.ref(stream) in self.streams:
                return
            for other in (wr() for wr in self.streams):
                if other is None:
                    continue
                if other.producer_slot is not None and stream.consumer_slot is not None:
                    self.video_mixer.connect_slots(other.producer_slot, stream.consumer_slot)
                if stream.producer_slot is not None and other.consumer_slot is not None:
                    self.video_mixer.connect_slots(stream.producer_slot, other.consumer_slot)
            # This hack is required because a weakly referenced object keeps a
            # strong reference to weak references of itself and thus to any
            # callbacks registered in those weak references. To be more
            # precise, we don't want the port to have a strong reference to
            # ourselves. -Luci
            self.streams.add(weakref.ref(stream, partial(self._remove_stream, weakref.ref(self))))

    def remove(self, stream):
        with self._lock:
            if weakref.ref(stream) not in self.streams:
                raise ValueError("stream %r is not part of this bridge" % stream)
            for other in (wr() for wr in self.streams):
                if other is None:
                    continue
                if other.producer_slot is not None and stream.consumer_slot is not None:
                    self.video_mixer.disconnect_slots(other.producer_slot, stream.consumer_slot)
                if stream.producer_slot is not None and other.consumer_slot is not None:
                    self.video_mixer.disconnect_slots(stream.producer_slot, other.consumer_slot)
            self.streams.remove(weakref.ref(stream))

    def handle_notification(self, notification):
        with self._lock:
            if weakref.ref(notification.sender) not in self.streams:
                return
            if notification.data.consumer_slot_changed:
                for other in (wr() for wr in self.streams):
                    if other is None or other is notification.sender or other.producer_slot is None:
                        continue
                    if notification.data.old_consumer_slot is not None:
                        self.video_mixer.disconnect_slots(other.producer_slot, notification.data.old_consumer_slot)
                    if notification.data.new_consumer_slot is not None:
                        self.video_mixer.connect_slots(other.producer_slot, notification.data.new_consumer_slot)
            if notification.data.producer_slot_changed:
                for other in (wr() for wr in self.streams):
                    if other is None or other is notification.sender or other.consumer_slot is None:
                        continue
                    if notification.data.old_producer_slot is not None:
                        self.video_mixer.disconnect_slots(notification.data.old_producer_slot, other.consumer_slot)
                    if notification.data.new_producer_slot is not None:
                        self.video_mixer.connect_slots(notification.data.new_producer_slot, other.consumer_slot)

    @staticmethod
    def _remove_stream(selfwr, streamwr):
        self = selfwr()
        if self is not None:
            with self._lock:
                self.streams.discard(streamwr)

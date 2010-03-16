# Copyright (C) 2008-2010 AG Projects. See LICENSE for details.
#

"""
Implements a high-level application responsable for starting and stopping
various sub-systems required to implement a fully featured SIP User Agent
application.
"""

from __future__ import absolute_import, with_statement

import os
from threading import RLock, Thread

from application.notification import IObserver, NotificationCenter
from application.python.util import Null, Singleton
from eventlet import coros
from twisted.internet import reactor
from zope.interface import implements

from sipsimple.core import AudioMixer, Engine, PJSIPTLSError, SIPCoreError

from sipsimple.account import AccountManager
from sipsimple.audio import AudioBridge, AudioDevice
from sipsimple.configuration import ConfigurationManager
from sipsimple.configuration.settings import SIPSimpleSettings
from sipsimple.session import SessionManager
from sipsimple.util import call_in_twisted_thread, run_in_green_thread, classproperty, TimestampedNotificationData


class ApplicationAttribute(object):
    def __init__(self, value):
        self.value = value

    def __get__(self, obj, objtype):
        return self.value

    def __set__(self, obj, value):
        self.value = value


class SIPApplication(object):
    __metaclass__ = Singleton
    implements(IObserver)

    state = ApplicationAttribute(value=None)
    end_reason = ApplicationAttribute(value=None)
    alert_audio_device = ApplicationAttribute(value=None)
    alert_audio_bridge = ApplicationAttribute(value=None)
    voice_audio_device = ApplicationAttribute(value=None)
    voice_audio_bridge = ApplicationAttribute(value=None)

    _channel = ApplicationAttribute(value=coros.queue())
    _lock = ApplicationAttribute(value=RLock())

    engine = Engine()

    def start(self, config_backend):
        with self._lock:
            if self.state is not None:
                raise RuntimeError("SIPApplication cannot be started from '%s' state" % self.state)
            self.state = 'starting'

        account_manager = AccountManager()
        configuration_manager = ConfigurationManager()

        # load configuration
        try:
            configuration_manager.start(config_backend)
            SIPSimpleSettings()
            account_manager.load_accounts()
        except:
            self.state = None
            raise

        # start the reactor thread
        Thread(name='Reactor Thread', target=self._run_reactor).start()

    def _run_reactor(self):
        from eventlet.twistedutil import join_reactor
        notification_center = NotificationCenter()

        reactor.callLater(0, self._initialize_subsystems)
        reactor.run(installSignalHandlers=False)

        self.state = 'stopped'
        notification_center.post_notification('SIPApplicationDidEnd', sender=self, data=TimestampedNotificationData(end_reason=self.end_reason))

    def _initialize_subsystems(self):
        account_manager = AccountManager()
        engine = Engine()
        notification_center = NotificationCenter()
        session_manager = SessionManager()
        settings = SIPSimpleSettings()

        notification_center.post_notification('SIPApplicationWillStart', sender=self, data=TimestampedNotificationData())
        if self.state == 'stopping':
            reactor.stop()
            return

        account = account_manager.default_account

        # initialize core
        notification_center.add_observer(self, sender=engine)
        options = dict(# general
                       user_agent=settings.user_agent,
                       # SIP
                       ignore_missing_ack=False,
                       udp_port=settings.sip.udp_port if 'udp' in settings.sip.transport_list else None,
                       tcp_port=settings.sip.tcp_port if 'tcp' in settings.sip.transport_list else None,
                       tls_port=settings.sip.tls_port if 'tls' in settings.sip.transport_list else None,
                       # TLS
                       tls_protocol=settings.tls.protocol,
                       tls_verify_server=account.tls.verify_server if account else False,
                       tls_ca_file=os.path.expanduser(settings.tls.ca_list) if settings.tls.ca_list else None,
                       tls_cert_file=os.path.expanduser(account.tls.certificate) if account and account.tls.certificate else None,
                       tls_privkey_file=os.path.expanduser(account.tls.certificate) if account and account.tls.certificate else None,
                       tls_timeout=settings.tls.timeout,
                       # rtp
                       rtp_port_range=(settings.rtp.port_range.start, settings.rtp.port_range.end),
                       # audio
                       codecs=list(settings.rtp.audio_codec_list),
                       # logging
                       log_level=settings.logs.pjsip_level,
                       trace_sip=True,
                      )
        try:
            try:
                engine.start(**options)
            except PJSIPTLSError, e:
                notification_center.post_notification('SIPApplicationFailedToStartTLS', sender=self, data=TimestampedNotificationData(error=e))
                options['tls_protocol'] = 'TLSv1'
                options['tls_verify_server'] = False
                options['tls_ca_file'] = None
                options['tls_cert_file'] = None
                options['tls_privkey_file'] = None
                options['tls_timeout'] = 1000
                engine.start(**options)
        except SIPCoreError:
            self.end_reason = 'engine failed'
            reactor.stop()
            return

        # initialize audio objects
        alert_device = settings.audio.alert_device
        if alert_device not in (None, 'system_default') and alert_device not in engine.output_devices:
            alert_device = 'system_default'
        input_device = settings.audio.input_device
        if input_device not in (None, 'system_default') and input_device not in engine.input_devices:
            input_device = 'system_default'
        output_device = settings.audio.output_device
        if output_device not in (None, 'system_default') and output_device not in engine.output_devices:
            output_device = 'system_default'
        try:
            voice_mixer = AudioMixer(input_device, output_device, settings.audio.sample_rate, settings.audio.tail_length)
        except SIPCoreError:
            try:
                voice_mixer = AudioMixer('system_default', 'system_default', settings.audio.sample_rate, settings.audio.tail_length)
            except SIPCoreError:
                voice_mixer = AudioMixer(None, None, settings.audio.sample_rate, settings.audio.tail_length)
        self.voice_audio_device = AudioDevice(voice_mixer)
        self.voice_audio_bridge = AudioBridge(voice_mixer)
        self.voice_audio_bridge.add(self.voice_audio_device)
        try:
            alert_mixer = AudioMixer(None, alert_device, settings.audio.sample_rate, 0)
        except SIPCoreError:
            try:
                alert_mixer = AudioMixer(None, 'system_default', settings.audio.sample_rate, 0)
            except SIPCoreError:
                alert_mixer = AudioMixer(None, None, settings.audio.sample_rate, 0)
        if settings.audio.silent:
            alert_mixer.output_volume = 0
        self.alert_audio_device = AudioDevice(alert_mixer)
        self.alert_audio_bridge = AudioBridge(alert_mixer)
        self.alert_audio_bridge.add(self.alert_audio_device)

        settings.audio.input_device = voice_mixer.input_device
        settings.audio.output_device = voice_mixer.output_device
        settings.audio.alert_device = alert_mixer.output_device
        settings.save()

        # initialize middleware components
        account_manager.start()
        session_manager.start()

        notification_center.add_observer(self, name='CFGSettingsObjectDidChange')

        self.state = 'started'
        notification_center.post_notification('SIPApplicationDidStart', sender=self, data=TimestampedNotificationData())

    def stop(self):
        with self._lock:
            if self.state in (None, 'stopping', 'stopped'):
                return
            prev_state = self.state
            self.state = 'stopping'
        
        self.end_reason = 'application request'
        notification_center = NotificationCenter()
        notification_center.post_notification('SIPApplicationWillEnd', sender=self, data=TimestampedNotificationData())
        if prev_state != 'starting':
            self._shutdown_subsystems()

    @run_in_green_thread
    def _shutdown_subsystems(self):
        # shutdown middleware components
        account_manager = AccountManager()
        account_manager.stop()

        # shutdown engine
        engine = Engine()
        engine.stop()
        while True:
            notification = self._channel.wait()
            if notification.name == 'SIPEngineDidEnd':
                break

        # stop the reactor
        reactor.stop()

    @classproperty
    def running(cls):
        return cls.state == 'started'

    @classproperty
    def alert_audio_mixer(cls):
        return cls.alert_audio_bridge.mixer if cls.alert_audio_bridge else None

    @classproperty
    def voice_audio_mixer(cls):
        return cls.voice_audio_bridge.mixer if cls.voice_audio_bridge else None

    def handle_notification(self, notification):
        handler = getattr(self, '_NH_%s' % notification.name, Null())
        handler(notification)

    def _NH_SIPEngineDidEnd(self, notification):
        call_in_twisted_thread(self._channel.send, notification)

    def _NH_SIPEngineDidFail(self, notification):
        if not self.running:
            return
        self.end_reason = 'engine failed'
        # this notification is always sent from the Engine's thread
        reactor.callFromThread(reactor.stop)

    def _NH_CFGSettingsObjectDidChange(self, notification):
        engine = Engine()
        settings = SIPSimpleSettings()
        account_manager = AccountManager()

        if notification.sender is settings:
            if 'audio.sample_rate' in notification.data.modified:
                alert_device = settings.audio.alert_device
                if alert_device not in (None, 'system_default') and alert_device not in engine.output_devices:
                    alert_device = 'system_default'
                input_device = settings.audio.input_device
                if input_device not in (None, 'system_default') and input_device not in engine.input_devices:
                    input_device = 'system_default'
                output_device = settings.audio.output_device
                if output_device not in (None, 'system_default') and output_device not in engine.output_devices:
                    output_device = 'system_default'
                try:
                    voice_mixer = AudioMixer(input_device, output_device, settings.audio.sample_rate, settings.audio.tail_length)
                except SIPCoreError:
                    try:
                        voice_mixer = AudioMixer('system_default', 'system_default', settings.audio.sample_rate, settings.audio.tail_length)
                    except SIPCoreError:
                        voice_mixer = AudioMixer(None, None, settings.audio.sample_rate, settings.audio.tail_length)
                self.voice_audio_device = AudioDevice(voice_mixer)
                self.voice_audio_bridge = AudioBridge(voice_mixer)
                self.voice_audio_bridge.add(self.voice_audio_device)
                try:
                    alert_mixer = AudioMixer(None, alert_device, settings.audio.sample_rate, 0)
                except SIPCoreError:
                    try:
                        alert_mixer = AudioMixer(None, 'system_default', settings.audio.sample_rate, 0)
                    except SIPCoreError:
                        alert_mixer = AudioMixer(None, None, settings.audio.sample_rate, 0)
                self.alert_audio_device = AudioDevice(alert_mixer)
                self.alert_audio_bridge = AudioBridge(alert_mixer)
                self.alert_audio_bridge.add(self.alert_audio_device)
                if settings.audio.silent:
                    alert_mixer.output_volume = 0
            else:
                if 'audio.input_device' in notification.data.modified or 'audio.output_device' in notification.data.modified or 'audio.tail_length' in notification.data.modified:
                    input_device = settings.audio.input_device
                    if input_device not in (None, 'system_default') and input_device not in engine.input_devices:
                        input_device = 'system_default'
                    output_device = settings.audio.output_device
                    if output_device not in (None, 'system_default') and output_device not in engine.output_devices:
                        output_device = 'system_default'
                    try:
                        self.voice_audio_bridge.mixer.set_sound_devices(input_device, output_device, settings.audio.tail_length)
                    except SIPCoreError:
                        try:
                            self.voice_audio_bridge.mixer.set_sound_devices('system_default', 'system_default', settings.audio.tail_length)
                        except SIPCoreError:
                            self.voice_audio_bridge.mixer.set_sound_devices(None, None, settings.audio.tail_length)
                if 'audio.alert_device' in notification.data.modified or 'audio.tail_length' in notification.data.modified:
                    alert_device = settings.audio.alert_device
                    if alert_device not in (None, 'system_default') and alert_device not in engine.output_devices:
                        alert_device = 'system_default'
                    try:
                        self.alert_audio_bridge.mixer.set_sound_devices(None, alert_device, 0)
                    except SIPCoreError:
                        try:
                            self.alert_audio_bridge.mixer.set_sound_devices(None, 'system_default', 0)
                        except SIPCoreError:
                            self.alert_audio_bridge.mixer.set_sound_devices(None, None, 0)
                if 'audio.silent' in notification.data.modified:
                    if settings.audio.silent:
                        self.alert_audio_bridge.mixer.output_volume = 0
                    else:
                        self.alert_audio_bridge.mixer.output_volume = 100
            if 'user_agent' in notification.data.modified:
                engine.user_agent = settings.user_agent
            if 'sip.udp_port' in notification.data.modified:
                engine.set_udp_port(settings.sip.udp_port)
            if 'sip.tcp_port' in notification.data.modified:
                engine.set_tcp_port(settings.sip.tcp_port)
            if set(('sip.tls_port', 'tls.protocol', 'tls.ca_list', 'tls.timeout', 'default_account')).intersection(notification.data.modified):
                account = account_manager.default_account
                try:
                    engine.set_tls_options(port=settings.sip.tls_port,
                                           protocol=settings.tls.protocol,
                                           verify_server=account.tls.verify_server if account else False,
                                           ca_file=os.path.expanduser(settings.tls.ca_list) if settings.tls.ca_list else None,
                                           cert_file=os.path.expanduser(account.tls.certificate) if account and account.tls.certificate else None,
                                           privkey_file=os.path.expanduser(account.tls.certificate) if account and account.tls.certificate else None,
                                           timeout=settings.tls.timeout)
                except PJSIPTLSError, e:
                    notification_center = NotificationCenter()
                    notification_center.post_notification('SIPApplicationFailedToStartTLS', sender=self, data=TimestampedNotificationData(error=e))
            if 'rtp.port_range' in notification.data.modified:
                engine.rtp_port_range = (settings.rtp.port_range.start, settings.rtp.port_range.end)
            if 'rtp.audio_codec_list' in notification.data.modified:
                engine.codecs = list(settings.rtp.audio_codec_list)
            if 'logs.pjsip_level' in notification.data.modified:
                engine.log_level = settings.logs.pjsip_level
        elif notification.sender is account_manager.default_account:
            if set(('tls.verify_server', 'tls.certificate')).intersection(notification.data.modified):
                account = account_manager.default_account
                try:
                    engine.set_tls_options(port=settings.sip.tls_port,
                                           protocol=settings.tls.protocol,
                                           verify_server=account.tls.verify_server,
                                           ca_file=os.path.expanduser(settings.tls.ca_list) if settings.tls.ca_list else None,
                                           cert_file=os.path.expanduser(account.tls.certificate) if account.tls.certificate else None,
                                           privkey_file=os.path.expanduser(account.tls.certificate) if account.tls.certificate else None,
                                           timeout=settings.tls.timeout)
                except PJSIPTLSError, e:
                    notification_center = NotificationCenter()
                    notification_center.post_notification('SIPApplicationFailedToStartTLS', sender=self, data=TimestampedNotificationData(error=e))

    def _NH_DefaultAudioDeviceDidChange(self, notification):
        settings = SIPSimpleSettings()
        current_input_device = self.voice_audio_bridge.mixer.input_device
        current_output_device = self.voice_audio_bridge.mixer.output_device
        current_alert_device = self.alert_audio_bridge.mixer.output_device
        ec_tail_length = self.voice_audio_bridge.mixer.ec_tail_length
        if notification.data.changed_input and 'system_default' in (current_input_device, settings.audio.input_device):
            try:
                self.voice_audio_bridge.mixer.set_sound_devices('system_default', current_output_device, ec_tail_length)
            except SIPCoreError:
                self.voice_audio_bridge.mixer.set_sound_devices(None, None, ec_tail_length)
        if notification.data.changed_output and 'system_default' in (current_output_device, settings.audio.output_device):
            try:
                self.voice_audio_bridge.mixer.set_sound_devices(current_input_device, 'system_default', ec_tail_length)
            except SIPCoreError:
                self.voice_audio_bridge.mixer.set_sound_devices(None, None, ec_tail_length)
        if notification.data.changed_output and 'system_default' in (current_alert_device, settings.audio.alert_device):
            try:
                self.alert_audio_bridge.mixer.set_sound_devices(None, 'system_default', 0)
            except SIPCoreError:
                self.alert_audio_bridge.mixer.set_sound_devices(None, None, 0)

    def _NH_AudioDevicesDidChange(self, notification):
        old_devices = set(notification.data.old_devices)
        new_devices = set(notification.data.new_devices)
        removed_devices = old_devices - new_devices

        input_device = self.voice_audio_bridge.mixer.input_device
        output_device = self.voice_audio_bridge.mixer.output_device
        alert_device = self.alert_audio_bridge.mixer.output_device
        if self.voice_audio_bridge.mixer.real_input_device in removed_devices:
            input_device = 'system_default' if new_devices else None
        if self.voice_audio_bridge.mixer.real_output_device in removed_devices:
            output_device = 'system_default' if new_devices else None
        if self.alert_audio_bridge.mixer.real_output_device in removed_devices:
            alert_device = 'system_default' if new_devices else None

        try:
            self.voice_audio_bridge.mixer.set_sound_devices(input_device, output_device, self.voice_audio_bridge.mixer.ec_tail_length)
        except SIPCoreError:
            try:
                self.voice_audio_bridge.mixer.set_sound_devices('system_default', 'system_default', self.voice_audio_bridge.mixer.ec_tail_length)
            except SIPCoreError:
                self.voice_audio_bridge.mixer.set_sound_devices(None, None, self.voice_audio_bridge.mixer.ec_tail_length)
        try:
            self.alert_audio_bridge.mixer.set_sound_devices(None, alert_device, 0)
        except SIPCoreError:
            try:
                self.alert_audio_bridge.mixer.set_sound_devices(None, 'system_default', 0)
            except SIPCoreError:
                self.alert_audio_bridge.mixer.set_sound_devices(None, None, 0)

        settings = SIPSimpleSettings()
        settings.audio.input_device = self.voice_audio_bridge.mixer.input_device
        settings.audio.output_device = self.voice_audio_bridge.mixer.output_device
        settings.audio.alert_device = self.alert_audio_bridge.mixer.output_device
        settings.save()



include "_core.error.pxi"
include "_core.lib.pxi"
include "_core.sound.pxi"
include "_core.video.pxi"
include "_core.util.pxi"

include "_core.ua.pxi"

include "_core.event.pxi"
include "_core.request.pxi"
include "_core.helper.pxi"
include "_core.headers.pxi"
include "_core.subscription.pxi"
include "_core.invitation.pxi"
include "_core.referral.pxi"
include "_core.sdp.pxi"
include "_core.mediatransport.pxi"

cdef extern from "../openbaudot/src/obl.c":
    void obl_init(OBL *obl, int baud, int (*callback)(void* obl, int event, int data)) nogil
    void obl_reset(OBL *obl, int baud) nogil
    void obl_set_speed(OBL *obl, int baud) nogil
    int obl_modulate(OBL *obl, short *buffer, int samples) nogil
    void obl_demodulate_packet(OBL *obl, char byte1, char byte2) nogil
    void obl_demodulate(OBL *obl, short *buffer, int samples) nogil
    int obl_tx_queue(OBL *obl, const char* text) nogil
    void obl_set_tx_freq(OBL *obl, float one_freq, float zero_freq) nogil
    void init_check_for_tty(OBL_TTY_DETECT * obl_tty_detect) nogil
    int check_for_tty(OBL_TTY_DETECT * obl_tty_detect, char byte1, char byte2) nogil

# constants

PJ_VERSION = pj_get_version()
PJ_SVN_REVISION = int(PJ_SVN_REV)
CORE_REVISION = 181

# exports

__all__ = ["PJ_VERSION", "PJ_SVN_REVISION", "CORE_REVISION",
           "SIPCoreError", "PJSIPError", "PJSIPTLSError", "SIPCoreInvalidStateError",
           "AudioMixer", "ToneGenerator", "RecordingWaveFile", "WaveFile", "MixerPort",
            "TTYModulator", "TTYDemodulator",
           "VideoMixer", "VideoCamera", "FrameBufferVideoRenderer",
           "sip_status_messages",
           "BaseCredentials", "Credentials", "FrozenCredentials", "BaseSIPURI", "SIPURI", "FrozenSIPURI",
           "BaseHeader", "Header", "FrozenHeader",
           "BaseContactHeader", "ContactHeader", "FrozenContactHeader",
           "BaseContentTypeHeader", "ContentType", "ContentTypeHeader", "FrozenContentTypeHeader",
           "BaseIdentityHeader", "IdentityHeader", "FrozenIdentityHeader", "FromHeader", "FrozenFromHeader", "ToHeader", "FrozenToHeader",
           "RouteHeader", "FrozenRouteHeader", "RecordRouteHeader", "FrozenRecordRouteHeader", "BaseRetryAfterHeader", "RetryAfterHeader", "FrozenRetryAfterHeader",
           "BaseViaHeader", "ViaHeader", "FrozenViaHeader", "BaseWarningHeader", "WarningHeader", "FrozenWarningHeader",
           "BaseEventHeader", "EventHeader", "FrozenEventHeader", "BaseSubscriptionStateHeader", "SubscriptionStateHeader", "FrozenSubscriptionStateHeader",
           "BaseReasonHeader", "ReasonHeader", "FrozenReasonHeader",
           "BaseReferToHeader", "ReferToHeader", "FrozenReferToHeader",
           "BaseSubjectHeader", "SubjectHeader", "FrozenSubjectHeader",
           "BaseReplacesHeader", "ReplacesHeader", "FrozenReplacesHeader",
           "Request",
           "Referral",
           "sipfrag_re",
           "Subscription",
           "Invitation",
           "DialogID",
           "SDPSession", "FrozenSDPSession", "SDPMediaStream", "FrozenSDPMediaStream", "SDPConnection", "FrozenSDPConnection", "SDPAttribute", "FrozenSDPAttribute", "SDPNegotiator",
           "RTPTransport", "AudioTransport", "VideoTransport"]





class SIPCoreError(Exception):
    pass


def write_log(log_data):
    f = open("/root/sipsimple.log", "a+")
    f.write(log_data)
    f.write("\n")
    f.close()


class PJSIPError(SIPCoreError):

    def __init__(self, message, status):
        self.status = status
        write_log("PJSIPError %s" % (message))
        write_log("status - %s" % (_pj_status_to_str(status)))
        SIPCoreError.__init__(self, "%s: %s" % (message, _pj_status_to_str(status)))

    @property
    def errno(self):
        # PJ_STATUS - PJ_ERRNO_START + PJ_ERRNO_SPACE_SIZE*2
        return 0 if self.status == 0 else self.status - 120000


class PJSIPTLSError(PJSIPError):
    pass


class SIPCoreInvalidStateError(SIPCoreError):
    pass


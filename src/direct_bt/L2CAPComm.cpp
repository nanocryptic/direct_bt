/*
 * Author: Sven Gothel <sgothel@jausoft.com>
 * Copyright (c) 2020 Gothel Software e.K.
 * Copyright (c) 2020 ZAFENA AB
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <cstring>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>
#include <cstdio>

#include  <algorithm>

// #define PERF_PRINT_ON 1
#include <jau/debug.hpp>

#include "BTIoctl.hpp"
#include "HCIIoctl.hpp"
#include "L2CAPIoctl.hpp"

#include "HCIComm.hpp"
#include "L2CAPComm.hpp"

#include "BTAdapter.hpp"

extern "C" {
    #include <unistd.h>
    #include <sys/socket.h>
    #include <poll.h>
    #include <signal.h>
}

using namespace direct_bt;

L2CAPEnv::L2CAPEnv() noexcept
: exploding( jau::environment::getExplodingProperties("direct_bt.l2cap") ),
  L2CAP_READER_POLL_TIMEOUT( jau::environment::getInt32Property("direct_bt.l2cap.reader.timeout", 10000, 1500 /* min */, INT32_MAX /* max */) ),
  L2CAP_RESTART_COUNT_ON_ERROR( jau::environment::getInt32Property("direct_bt.l2cap.restart.count", 5, INT32_MIN /* min */, INT32_MAX /* max */) ), // FIXME: Move to L2CAPComm
  DEBUG_DATA( jau::environment::getBooleanProperty("direct_bt.debug.l2cap.data", false) )
{
}

int L2CAPComm::l2cap_open_dev(const BDAddressAndType & adapterAddressAndType, const L2CAP_PSM psm, const L2CAP_CID cid) noexcept {
    sockaddr_l2 a;
    int fd, err;

    // Create a loose L2CAP socket
    fd = ::socket(AF_BLUETOOTH, // AF_BLUETOOTH == PF_BLUETOOTH
                  SOCK_SEQPACKET, BTPROTO_L2CAP);

    if( 0 > fd ) {
        ERR_PRINT("L2CAPComm::l2cap_open_dev: socket failed");
        return fd;
    }

    // Bind socket to the L2CAP adapter
    // BT Core Spec v5.2: Vol 3, Part A: L2CAP_CONNECTION_REQ
    bzero((void *)&a, sizeof(a));
    a.l2_family=AF_BLUETOOTH;
    a.l2_psm = jau::cpu_to_le(direct_bt::number(psm));
    a.l2_bdaddr = jau::cpu_to_le(adapterAddressAndType.address);
    a.l2_cid = jau::cpu_to_le(direct_bt::number(cid));
    a.l2_bdaddr_type = ::number(adapterAddressAndType.type);
    if ( ::bind(fd, (struct sockaddr *) &a, sizeof(a)) < 0 ) {
        ERR_PRINT("L2CAPComm::l2cap_open_dev: bind failed");
        goto failed;
    }
    return fd;

failed:
    err = errno;
    ::close(fd);
    errno = err;

    return -1;
}

int L2CAPComm::l2cap_close_dev(int dd) noexcept
{
    return ::close(dd);
}


// *************************************************
// *************************************************
// *************************************************

L2CAPComm::L2CAPComm(const BDAddressAndType& adapterAddressAndType_, const L2CAP_PSM psm_, const L2CAP_CID cid_) noexcept
: env(L2CAPEnv::get()),
  localAddressAndType(adapterAddressAndType_),
  psm(psm_), cid(cid_),
  remoteAddressAndType(BDAddressAndType::ANY_BREDR_DEVICE),
  client_socket(-1),
  is_open(false), has_ioerror(false), interrupt_flag(false), tid_connect(0), tid_read(0)
{ }

L2CAPComm::L2CAPComm(const BDAddressAndType& adapterAddressAndType_, const L2CAP_PSM psm_, const L2CAP_CID cid_,
                     const BDAddressAndType& remoteAddressAndType_, int client_socket_) noexcept
: env(L2CAPEnv::get()),
  localAddressAndType(adapterAddressAndType_),
  psm(psm_), cid(cid_),
  remoteAddressAndType(remoteAddressAndType_),
  client_socket(client_socket_),
  is_open(0 <= client_socket_), has_ioerror(false), interrupt_flag(false), tid_connect(0), tid_read(0)
{ }

/**
 * Setting BT_SECURITY within open() after bind() and before connect()
 * causes BlueZ/Kernel to immediately process SMP, leading to a potential deadlock.
 *
 * Here we experience, setting security level before connect()
 * will block the thread within connect(), potentially a mutex used in the SMP procedure.
 *
 * Hence we set BT_SECURITY after connect() within open().
 */
#define SET_BT_SECURITY_POST_CONNECT 1

bool L2CAPComm::open(const BTDevice& device, const BTSecurityLevel sec_level) noexcept {

    bool expOpen = false; // C++11, exp as value since C++20
    if( !is_open.compare_exchange_strong(expOpen, true) ) {
        DBG_PRINT("L2CAPComm::open(%s, %s): Already open: dd %d, %s, psm %s, cid %s; %s",
                  device.getAddressAndType().toString().c_str(), to_string(sec_level).c_str(),
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        return false;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_write); // RAII-style acquire and relinquish via destructor

    has_ioerror = false; // always clear last ioerror flag (should be redundant)

    /**
     * bt_io_connect ( create_io ) with source address
     * - fd = socket(.._)
     * - bind(fd, ..)
     * - l2cap_set
     * -- set imtu, omtu, mode
     * -- l2cap_set_master
     * -- l2cap_set_flushable
     * -- set_priority
     * -- set_sec_level
     * --- setsockopt(.. BT_SECURITY ..)
     *
     * - l2cap_connect with destination address
     * -- connect(fd, ..)
     */
    remoteAddressAndType = device.getAddressAndType();

    /** BT Core Spec v5.2: Vol 3, Part A: L2CAP_CONNECTION_REQ */
    sockaddr_l2 req;
    int res;
    int to_retry_count=0; // ETIMEDOUT retry count

    DBG_PRINT("L2CAPComm::open: Start Connect: dd %d, %s, psm %s, cid %s, sec_level %s; %s",
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(), to_string(sec_level).c_str(),
              getStateString().c_str());

    client_socket = l2cap_open_dev(localAddressAndType, psm, cid);

    if( 0 > client_socket ) {
        goto failure; // open failed
    }

#if !SET_BT_SECURITY_POST_CONNECT
    #if USE_LINUX_BT_SECURITY
        if( BTSecurityLevel::UNSET < sec_level ) {
            if( !setBTSecurityLevelImpl(sec_level) ) {
                goto failure; // sec_level failed
            }
        }
    #endif
#endif

    tid_connect = pthread_self(); // temporary safe tid to allow interruption

    // actual request to connect to remote device
    bzero((void *)&req, sizeof(req));
    req.l2_family = AF_BLUETOOTH;
    req.l2_psm = jau::cpu_to_le(direct_bt::number(psm));
    req.l2_bdaddr = jau::cpu_to_le(remoteAddressAndType.address);
    req.l2_cid = jau::cpu_to_le(direct_bt::number(cid));
    req.l2_bdaddr_type = ::number(remoteAddressAndType.type);

    while( !interrupt_flag ) {
        // blocking
        res = ::connect(client_socket, (struct sockaddr*)&req, sizeof(req));

        DBG_PRINT("L2CAPComm::open: Connect Result: %d, errno 0x%X %s, %s, psm %s, cid %s",
                  res, errno, strerror(errno),
                  remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str());

        if( !res )
        {
            break; // done

        } else if( ETIMEDOUT == errno ) {
            to_retry_count++;
            if( to_retry_count < number(Defaults::L2CAP_CONNECT_MAX_RETRY) ) {
                WORDY_PRINT("L2CAPComm::open: Connect timeout, retry %d: dd %d, %s, psm %s, cid %s, sec_level %s; %s",
                          to_retry_count,
                          client_socket.load(), remoteAddressAndType.toString().c_str(),
                          to_string(psm).c_str(), to_string(cid).c_str(), to_string(sec_level).c_str(),
                          getStateString().c_str());
                continue;
            } else {
                ERR_PRINT("L2CAPComm::open: Connect timeout, retried %d: dd %d, %s, psm %s, cid %s, sec_level %s; %s",
                          to_retry_count,
                          client_socket.load(), remoteAddressAndType.toString().c_str(),
                          to_string(psm).c_str(), to_string(cid).c_str(), to_string(sec_level).c_str(),
                          getStateString().c_str());
                goto failure; // exit
            }

        } else if( !interrupt_flag ) {
            // EALREADY == errno || ENETUNREACH == errno || EHOSTUNREACH == errno || ..
            ERR_PRINT("L2CAPComm::open: Connect failed: dd %d, %s, psm %s, cid %s, sec_level %s; %s",
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(), to_string(sec_level).c_str(),
                      getStateString().c_str());
            goto failure; // exit
        } else {
            goto failure; // exit on interrupt
        }
    }
    // success
    tid_connect = 0;

#if SET_BT_SECURITY_POST_CONNECT
    #if USE_LINUX_BT_SECURITY
        if( BTSecurityLevel::UNSET < sec_level ) {
            if( !setBTSecurityLevelImpl(sec_level) ) {
                goto failure; // sec_level failed
            }
        }
    #endif
#endif

    return true;

failure:
    const int err = errno;
    close();
    errno = err;
    return false;
}

bool L2CAPComm::close() noexcept {
    bool expOpen = true; // C++11, exp as value since C++20
    if( !is_open.compare_exchange_strong(expOpen, false) ) {
        DBG_PRINT("L2CAPComm::close: Not connected: dd %d, %s, psm %s, cid %s; %s",
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        has_ioerror = false; // always clear last ioerror flag (should be redundant)
        return true;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_write); // RAII-style acquire and relinquish via destructor

    has_ioerror = false;
    DBG_PRINT("L2CAPComm::close: Start: dd %d, %s, psm %s, cid %s; %s",
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
    PERF_TS_T0();

    interrupt_flag = true;
    {
        pthread_t tid_self = pthread_self();
        pthread_t _tid_connect = tid_connect;
        pthread_t _tid_read = tid_read;
        tid_read = 0;
        tid_connect = 0;

        // interrupt read(..) and , avoiding prolonged hang
        if( 0 != _tid_read && tid_self != _tid_read ) {
            int kerr;
            if( 0 != ( kerr = pthread_kill(_tid_read, SIGALRM) ) ) {
                ERR_PRINT("L2CAPComm::close: pthread_kill read %p FAILED: %d; dd %d, %s, psm %s, cid %s; %s",
                          (void*)_tid_read, kerr,
                          client_socket.load(), remoteAddressAndType.toString().c_str(),
                          to_string(psm).c_str(), to_string(cid).c_str(),
                          getStateString().c_str());
            }
        }
        // interrupt connect(..) and , avoiding prolonged hang
        interrupt_flag = true;
        if( 0 != _tid_connect && _tid_read != _tid_connect && tid_self != _tid_connect ) {
            int kerr;
            if( 0 != ( kerr = pthread_kill(_tid_connect, SIGALRM) ) ) {
                ERR_PRINT("L2CAPComm::close: Start: pthread_kill connect %p FAILED: %d; dd %d, %s, psm %s, cid %s; %s",
                          (void*)_tid_connect, kerr,
                          client_socket.load(), remoteAddressAndType.toString().c_str(),
                          to_string(psm).c_str(), to_string(cid).c_str(),
                          getStateString().c_str());
            }
        }
    }

    l2cap_close_dev(client_socket);
    client_socket = -1;
    interrupt_flag = false;
    PERF_TS_TD("L2CAPComm::close");
    DBG_PRINT("L2CAPComm::close: End: dd %d, %s, psm %s, cid %s; %s",
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
    return true;
}

bool L2CAPComm::setBTSecurityLevel(const BTSecurityLevel sec_level) noexcept {
    if( !is_open ) {
        DBG_PRINT("L2CAPComm::setBTSecurityLevel(%s): Not connected: dd %d, %s, psm %s, cid %s; %s",
                  to_string(sec_level).c_str(),
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        return false;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_write); // RAII-style acquire and relinquish via destructor
    if( BTSecurityLevel::UNSET < sec_level ) {
        if( setBTSecurityLevelImpl(sec_level) ) {
            return true;
        } else {
            close();
            return false;
        }
    } else {
        return true;
    }
}

bool L2CAPComm::setBTSecurityLevelImpl(const BTSecurityLevel sec_level) noexcept {
    if( BTSecurityLevel::NONE > sec_level ) {
        DBG_PRINT("L2CAPComm::setBTSecurityLevel: sec_level %s not set: dd %d, %s, psm %s, cid %s; %s",
                  to_string(sec_level).c_str(),
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        return false;
    }

#if USE_LINUX_BT_SECURITY
    struct bt_security bt_sec;
    int result;

    BTSecurityLevel old_sec_level = getBTSecurityLevelImpl();
    if( old_sec_level != sec_level ) {
        bzero(&bt_sec, sizeof(bt_sec));
        bt_sec.level = direct_bt::number(sec_level);
        result = setsockopt(client_socket, SOL_BLUETOOTH, BT_SECURITY, &bt_sec, sizeof(bt_sec));
        if ( 0 == result ) {
            DBG_PRINT("L2CAPComm::setBTSecurityLevel: Success: sec_level %s -> %s: dd %d, %s, psm %s, cid %s; %s",
                      to_string(old_sec_level).c_str(), to_string(sec_level).c_str(),
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
            return true;
        } else {
            ERR_PRINT("L2CAPComm::setBTSecurityLevel: Failed: sec_level %s -> %s: dd %d, %s, psm %s, cid %s; %s",
                      to_string(old_sec_level).c_str(), to_string(sec_level).c_str(),
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
            return false;
        }
    } else {
        DBG_PRINT("L2CAPComm::setBTSecurityLevel: Unchanged: sec_level %s -> %s: dd %d, %s, psm %s, cid %s; %s",
                  to_string(old_sec_level).c_str(), to_string(sec_level).c_str(),
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        return true;
    }
#else
    DBG_PRINT("L2CAPComm::setBTSecurityLevel: Not implemented: sec_level %s: dd %d, %s, psm %s, cid %s; %s",
              to_string(sec_level).c_str(),
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
    return false;
#endif
}

BTSecurityLevel L2CAPComm::getBTSecurityLevel() noexcept {
    if( !is_open ) {
        DBG_PRINT("L2CAPComm::getBTSecurityLevel: Not connected: dd %d, %s, psm %s, cid %s; %s",
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        return BTSecurityLevel::UNSET;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_write); // RAII-style acquire and relinquish via destructor

    return getBTSecurityLevelImpl();
}

BTSecurityLevel L2CAPComm::getBTSecurityLevelImpl() noexcept {
    BTSecurityLevel sec_level = BTSecurityLevel::UNSET;
#if USE_LINUX_BT_SECURITY
    struct bt_security bt_sec;
    socklen_t optlen = sizeof(bt_sec);
    int result;

    bzero(&bt_sec, sizeof(bt_sec));
    result = getsockopt(client_socket, SOL_BLUETOOTH, BT_SECURITY, &bt_sec, &optlen);
    if ( 0 == result ) {
        if( optlen == sizeof(bt_sec) ) {
            sec_level = static_cast<BTSecurityLevel>(bt_sec.level);
            DBG_PRINT("L2CAPComm::getBTSecurityLevel: Success: sec_level %s: dd %d, %s, psm %s, cid %s; %s",
                      to_string(sec_level).c_str(),
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
        } else {
            ERR_PRINT("L2CAPComm::getBTSecurityLevel: Failed: sec_level %s, size %zd returned != %zd bt_sec: dd %d, %s, psm %s, cid %s; %s",
                      to_string(sec_level).c_str(), optlen, sizeof(bt_sec),
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
        }
    } else {
        ERR_PRINT("L2CAPComm::getBTSecurityLevel: Failed: sec_level %s, result %d: dd %d, %s, psm %s, cid %s; %s",
                  to_string(sec_level).c_str(), result,
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
    }
#else
    DBG_PRINT("L2CAPComm::getBTSecurityLevel: Not implemented: sec_level %s: dd %d, %s, psm %s, cid %s; %s",
              to_string(sec_level).c_str(),
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
#endif
    return sec_level;
}

#define RWEXITCODE_ENUM(X) \
        X(RWExitCode, SUCCESS) \
        X(RWExitCode, NOT_OPEN) \
        X(RWExitCode, INTERRUPTED) \
        X(RWExitCode, INVALID_SOCKET_DD) \
        X(RWExitCode, POLL_ERROR) \
        X(RWExitCode, POLL_TIMEOUT) \
        X(RWExitCode, READ_ERROR) \
        X(RWExitCode, WRITE_ERROR)

#define CASE2_TO_STRING(U,V) case U::V: return #V;

std::string L2CAPComm::getRWExitCodeString(const RWExitCode ec) noexcept {
    if( number(ec) >= 0 ) {
        return "SUCCESS";
    }
    switch(ec) {
        RWEXITCODE_ENUM(CASE2_TO_STRING)
        default: ; // fall through intended
    }
    return "Unknown ExitCode";
}

jau::snsize_t L2CAPComm::read(uint8_t* buffer, const jau::nsize_t capacity) noexcept {
    const int32_t timeoutMS = env.L2CAP_READER_POLL_TIMEOUT;
    jau::snsize_t len = 0;
    jau::snsize_t err_res = 0;

    if( !is_open ) {
        err_res = number(RWExitCode::NOT_OPEN);
        goto errout;
    }
    if( interrupt_flag ) {
        err_res = number(RWExitCode::INTERRUPTED);
        goto errout;
    }
    if( 0 > client_socket ) {
        err_res = number(RWExitCode::INVALID_SOCKET_DD);
        goto errout;
    }
    if( 0 == capacity ) {
        goto done;
    }

    tid_read = pthread_self(); // temporary safe tid to allow interruption

    if( timeoutMS ) {
        struct pollfd p;
        int n;

        p.fd = client_socket; p.events = POLLIN;
        while ( is_open && !interrupt_flag && ( n = poll( &p, 1, timeoutMS ) ) < 0 ) {
            if( !is_open ) {
                err_res = number(RWExitCode::NOT_OPEN);
                goto errout;
            }
            if( interrupt_flag ) {
                err_res = number(RWExitCode::INTERRUPTED);
                goto errout;
            }
            if ( errno == EAGAIN || errno == EINTR ) {
                // cont temp unavail or interruption
                continue;
            }
            err_res = number(RWExitCode::POLL_ERROR);
            goto errout;
        }
        if (!n) {
            err_res = number(RWExitCode::POLL_TIMEOUT);
            errno = ETIMEDOUT;
            goto errout;
        }
    }

    while ( is_open && !interrupt_flag && ( len = ::read(client_socket, buffer, capacity) ) < 0 ) {
        if( !is_open ) {
            err_res = number(RWExitCode::NOT_OPEN);
            goto errout;
        }
        if( interrupt_flag ) {
            err_res = number(RWExitCode::INTERRUPTED);
            goto errout;
        }
        if ( errno == EAGAIN || errno == EINTR ) {
            // cont temp unavail or interruption
            continue;
        }
        err_res = number(RWExitCode::READ_ERROR);
        goto errout;
    }

done:
    tid_read = 0;
    return len;

errout:
    tid_read = 0;
    if( !is_open || interrupt_flag ) {
        // closed or intentionally interrupted
        WORDY_PRINT("L2CAPComm::read: IRQed res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
              err_res, getRWExitCodeString(err_res).c_str(), len,
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
    } else {
        // open and not intentionally interrupted
        if( ETIMEDOUT == errno ) {
            // just timed out (???)
            if( err_res != number(RWExitCode::POLL_TIMEOUT) ) { // expected POLL_TIMEOUT if idle
                DBG_PRINT("L2CAPComm::read: Timeout res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
                      err_res, getRWExitCodeString(err_res).c_str(), len,
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
            }
        } else {
            // Only report as ioerror if open, not intentionally interrupted and no timeout!
            has_ioerror = true;

            if( env.L2CAP_RESTART_COUNT_ON_ERROR < 0 ) {
                ABORT("L2CAPComm::read: Error res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
                      err_res, getRWExitCodeString(err_res).c_str(), len,
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
            } else {
                IRQ_PRINT("L2CAPComm::read: Error res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
                      err_res, getRWExitCodeString(err_res).c_str(), len,
                      client_socket.load(), remoteAddressAndType.toString().c_str(),
                      to_string(psm).c_str(), to_string(cid).c_str(),
                      getStateString().c_str());
            }
        }
    }
    return err_res;
}

jau::snsize_t L2CAPComm::write(const uint8_t * buffer, const jau::nsize_t length) noexcept {
    const std::lock_guard<std::recursive_mutex> lock(mtx_write); // RAII-style acquire and relinquish via destructor
    jau::snsize_t len = 0;
    jau::snsize_t err_res = 0;

    if( !is_open ) {
        err_res = number(RWExitCode::NOT_OPEN);
        goto errout;
    }
    if( interrupt_flag ) {
        err_res = number(RWExitCode::INTERRUPTED);
        goto errout;
    }
    if( 0 > client_socket ) {
        err_res = number(RWExitCode::INVALID_SOCKET_DD);
        goto errout;
    }
    if( 0 == length ) {
        goto done;
    }

    while ( is_open && !interrupt_flag && ( len = ::write(client_socket, buffer, length) ) < 0 ) {
        if( !is_open ) {
            err_res = number(RWExitCode::NOT_OPEN);
            goto errout;
        }
        if( interrupt_flag ) {
            err_res = number(RWExitCode::INTERRUPTED);
            goto errout;
        }
        if( EAGAIN == errno || EINTR == errno ) {
            // cont temp unavail or interruption
            continue;
        }
        err_res = number(RWExitCode::WRITE_ERROR);
        goto errout;
    }

done:
    return len;

errout:
    if( !is_open || interrupt_flag ) {
        // closed or intentionally interrupted
        WORDY_PRINT("L2CAPComm::write: IRQed res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
              err_res, getRWExitCodeString(err_res).c_str(), len,
              client_socket.load(), remoteAddressAndType.toString().c_str(),
              to_string(psm).c_str(), to_string(cid).c_str(),
              getStateString().c_str());
    } else {
        // open and not intentionally interrupted
        has_ioerror = true;

        if( env.L2CAP_RESTART_COUNT_ON_ERROR < 0 ) {
            ABORT("L2CAPComm::write: Error res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
                  err_res, getRWExitCodeString(err_res).c_str(), len,
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        } else {
            IRQ_PRINT("L2CAPComm::write: Error res %d (%s), len %d; dd %d, %s, psm %s, cid %s; %s",
                  err_res, getRWExitCodeString(err_res).c_str(), len,
                  client_socket.load(), remoteAddressAndType.toString().c_str(),
                  to_string(psm).c_str(), to_string(cid).c_str(),
                  getStateString().c_str());
        }
    }
    return err_res;
}

std::string L2CAPComm::toString() {
    return "L2CAPComm[dd "+std::to_string(client_socket.load())+
            ", psm "+to_string(psm)+
            ", cid "+to_string(cid)+
            ", local "+localAddressAndType.toString()+
            ", remote "+remoteAddressAndType.toString()+
            ", "+getStateString()+"]";
}

// *************************************************
// *************************************************
// *************************************************

L2CAPServer::L2CAPServer(const BDAddressAndType& localAddressAndType_, const L2CAP_PSM psm_, const L2CAP_CID cid_) noexcept
: localAddressAndType(localAddressAndType_),
  psm(psm_), cid(cid_),
  server_socket(-1),
  is_open(false), interrupt_flag(false), tid_accept(0)
{ }

bool L2CAPServer::open() noexcept {
    bool expOpen = false; // C++11, exp as value since C++20
    if( !is_open.compare_exchange_strong(expOpen, true) ) {
        DBG_PRINT("L2CAPServer::open: Already open: dd %d, psm %s, cid %s, local %s",
                  server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
                  localAddressAndType.toString().c_str());
        return false;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_open); // RAII-style acquire and relinquish via destructor
    int res;

    DBG_PRINT("L2CAPServer::open: Start: dd %d, psm %s, cid %s, local %s",
              server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
              localAddressAndType.toString().c_str());

    server_socket = L2CAPComm::l2cap_open_dev(localAddressAndType, psm, cid);

    if( 0 > server_socket ) {
        goto failure; // open failed
    }

#if !SET_BT_SECURITY_POST_CONNECT
    #if USE_LINUX_BT_SECURITY
        if( BTSecurityLevel::UNSET < sec_level ) {
            if( !setBTSecurityLevelImpl(sec_level) ) {
                goto failure; // sec_level failed
            }
        }
    #endif
#endif

    res = ::listen(server_socket, 10);

    DBG_PRINT("L2CAPServer::open: End: res %d, dd %d, psm %s, cid %s, local %s",
              res, server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
              localAddressAndType.toString().c_str());

    if( res < 0 ) {
        goto failure;
    }

    return true;

failure:
    const int err = errno;
    close();
    errno = err;
    return false;
}

bool L2CAPServer::close() noexcept {
    bool expOpen = true; // C++11, exp as value since C++20
    if( !is_open.compare_exchange_strong(expOpen, false) ) {
        DBG_PRINT("L2CAPServer::close: Not connected: dd %d, psm %s, cid %s, local %s",
                  server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
                  localAddressAndType.toString().c_str());
        return true;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_open); // RAII-style acquire and relinquish via destructor

    DBG_PRINT("L2CAPServer::close: Start: dd %d, psm %s, cid %s, local %s",
              server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
              localAddressAndType.toString().c_str());
    PERF_TS_T0();

    interrupt_flag = true;
    {
        pthread_t tid_self = pthread_self();
        pthread_t _tid_accept = tid_accept;
        tid_accept = 0;

        // interrupt connect(..) and , avoiding prolonged hang
        interrupt_flag = true;
        if( 0 != _tid_accept && tid_self != _tid_accept ) {
            int kerr;
            if( 0 != ( kerr = pthread_kill(_tid_accept, SIGALRM) ) ) {
                ERR_PRINT("L2CAPServer::close: Start: pthread_kill connect %p FAILED: %d; dd %d, psm %s, cid %s, local %s",
                          (void*)_tid_accept, kerr,
                          server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
                          localAddressAndType.toString().c_str());
            }
        }
    }

    L2CAPComm::l2cap_close_dev(server_socket);
    server_socket = -1;
    interrupt_flag = false;
    PERF_TS_TD("L2CAPServer::close");
    DBG_PRINT("L2CAPServer::close: End: dd %d, psm %s, cid %s, local %s",
              server_socket.load(), to_string(psm).c_str(), to_string(cid).c_str(),
              localAddressAndType.toString().c_str());
    return true;
}

std::unique_ptr<L2CAPComm> L2CAPServer::accept() noexcept {
    sockaddr_l2 peer;
    int to_retry_count=0; // ETIMEDOUT retry count

    tid_accept = pthread_self(); // temporary safe tid to allow interruption

    while( !interrupt_flag ) {
        // blocking
        bzero((void *)&peer, sizeof(peer));
        socklen_t addrlen = sizeof(peer); // on return it will contain the actual size of the peer address
        int client_socket = ::accept(server_socket, (struct sockaddr*)&peer, &addrlen);

        BDAddressAndType remoteAddressAndType(jau::le_to_cpu(peer.l2_bdaddr), static_cast<BDAddressType>(peer.l2_bdaddr_type));
        L2CAP_PSM c_psm = static_cast<L2CAP_PSM>(jau::le_to_cpu(peer.l2_psm));
        L2CAP_CID c_cid = static_cast<L2CAP_CID>(jau::le_to_cpu(peer.l2_cid));

        if( 0 <= client_socket )
        {
            DBG_PRINT("L2CAPServer::accept: Success: dd[s %d, c %d], errno 0x%X %s, psm %s -> %s, cid %s -> %s, local %s -> remote %s",
                      server_socket.load(), client_socket, errno, strerror(errno),
                      to_string(psm).c_str(), to_string(c_psm).c_str(),
                      to_string(cid).c_str(), to_string(c_cid).c_str(),
                      localAddressAndType.toString().c_str(),
                      remoteAddressAndType.toString().c_str());
            // success
            tid_accept = 0;
            return std::make_unique<L2CAPComm>(localAddressAndType, c_psm, c_cid, remoteAddressAndType, client_socket);
        } else if( ETIMEDOUT == errno ) {
            to_retry_count++;
            if( to_retry_count < L2CAPComm::number(L2CAPComm::Defaults::L2CAP_CONNECT_MAX_RETRY) ) {
                WORDY_PRINT("L2CAPServer::accept: Timeout # %d (retry): dd[s %d, c %d], errno 0x%X %s, psm %s -> %s, cid %s -> %s, local %s -> remote %s",
                          to_retry_count, server_socket.load(), client_socket, errno, strerror(errno),
                          to_string(psm).c_str(), to_string(c_psm).c_str(),
                          to_string(cid).c_str(), to_string(c_cid).c_str(),
                          localAddressAndType.toString().c_str(),
                          remoteAddressAndType.toString().c_str());
                continue;
            } else {
                ERR_PRINT("L2CAPServer::accept: Timeout # %d (done): dd[s %d, c %d], errno 0x%X %s, psm %s -> %s, cid %s -> %s, local %s -> remote %s",
                          to_retry_count, server_socket.load(), client_socket, errno, strerror(errno),
                          to_string(psm).c_str(), to_string(c_psm).c_str(),
                          to_string(cid).c_str(), to_string(c_cid).c_str(),
                          localAddressAndType.toString().c_str(),
                          remoteAddressAndType.toString().c_str());
                break; // exit
            }

        } else if( !interrupt_flag ) {
            // EALREADY == errno || ENETUNREACH == errno || EHOSTUNREACH == errno || ..
            ERR_PRINT("L2CAPServer::accept: Failed: dd[s %d, c %d], errno 0x%X %s, psm %s -> %s, cid %s -> %s, local %s -> remote %s",
                      server_socket.load(), client_socket, errno, strerror(errno),
                      to_string(psm).c_str(), to_string(c_psm).c_str(),
                      to_string(cid).c_str(), to_string(c_cid).c_str(),
                      localAddressAndType.toString().c_str(),
                      remoteAddressAndType.toString().c_str());
            break; // exit
        }
    }
    // failure
    tid_accept = 0;
    return nullptr;
}

std::string L2CAPServer::toString() {
    return "L2CAPServer[dd "+std::to_string(server_socket.load())+
            ", psm "+to_string(psm)+
            ", cid "+to_string(cid)+
            ", local "+localAddressAndType.toString()+"]";
}

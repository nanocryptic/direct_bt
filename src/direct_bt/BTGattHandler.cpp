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
#include <cstdio>

#include  <algorithm>

extern "C" {
    #include <unistd.h>
    #include <sys/socket.h>
    #include <poll.h>
    #include <signal.h>
}

// #define PERF_PRINT_ON 1
// PERF2_PRINT_ON for read/write single values
// #define PERF2_PRINT_ON 1
// PERF3_PRINT_ON for disconnect
// #define PERF3_PRINT_ON 1
#include <jau/debug.hpp>

#include <jau/basic_algos.hpp>

#include "L2CAPIoctl.hpp"
#include "GattNumbers.hpp"

#include "BTGattHandler.hpp"

#include "BTDevice.hpp"

#include "BTManager.hpp"
#include "BTAdapter.hpp"
#include "BTManager.hpp"

using namespace direct_bt;

BTGattEnv::BTGattEnv() noexcept
: exploding( jau::environment::getExplodingProperties("direct_bt.gatt") ),
  GATT_READ_COMMAND_REPLY_TIMEOUT( jau::environment::getInt32Property("direct_bt.gatt.cmd.read.timeout", 500, 250 /* min */, INT32_MAX /* max */) ),
  GATT_WRITE_COMMAND_REPLY_TIMEOUT(  jau::environment::getInt32Property("direct_bt.gatt.cmd.write.timeout", 500, 250 /* min */, INT32_MAX /* max */) ),
  GATT_INITIAL_COMMAND_REPLY_TIMEOUT( jau::environment::getInt32Property("direct_bt.gatt.cmd.init.timeout", 2500, 2000 /* min */, INT32_MAX /* max */) ),
  ATTPDU_RING_CAPACITY( jau::environment::getInt32Property("direct_bt.gatt.ringsize", 128, 64 /* min */, 1024 /* max */) ),
  DEBUG_DATA( jau::environment::getBooleanProperty("direct_bt.debug.gatt.data", false) )
{
}

#define CASE_TO_STRING(V) case V: return #V;

std::shared_ptr<BTDevice> BTGattHandler::getDeviceChecked() const {
    std::shared_ptr<BTDevice> ref = wbr_device.lock();
    if( nullptr == ref ) {
        throw jau::IllegalStateException("GATTHandler's device already destructed: "+toString(), E_FILE_LINE);
    }
    return ref;
}

bool BTGattHandler::validateConnected() noexcept {
    const bool l2capIsConnected = l2cap.isOpen();
    const bool l2capHasIOError = l2cap.hasIOError();

    if( has_ioerror || l2capHasIOError ) {
        ERR_PRINT("IOError state: GattHandler %s, l2cap %s: %s",
                getStateString().c_str(), l2cap.getStateString().c_str(), toString().c_str());
        has_ioerror = true; // propagate l2capHasIOError -> has_ioerror
        return false;
    }

    if( !is_connected || !l2capIsConnected ) {
        ERR_PRINT("Disconnected state: GattHandler %s, l2cap %s: %s",
                getStateString().c_str(), l2cap.getStateString().c_str(), toString().c_str());
        return false;
    }
    return true;
}

static jau::cow_darray<std::shared_ptr<BTGattCharListener>>::equal_comparator _characteristicListenerRefEqComparator =
        [](const std::shared_ptr<BTGattCharListener> &a, const std::shared_ptr<BTGattCharListener> &b) -> bool { return *a == *b; };

bool BTGattHandler::addCharListener(std::shared_ptr<BTGattCharListener> l) {
    if( nullptr == l ) {
        throw jau::IllegalArgumentException("GATTEventListener ref is null", E_FILE_LINE);
    }
    return characteristicListenerList.push_back_unique(l, _characteristicListenerRefEqComparator);
}

bool BTGattHandler::removeCharListener(std::shared_ptr<BTGattCharListener> l) noexcept {
    if( nullptr == l ) {
        ERR_PRINT("GATTCharacteristicListener ref is null");
        return false;
    }
    const int count = characteristicListenerList.erase_matching(l, false /* all_matching */, _characteristicListenerRefEqComparator);
    return count > 0;
}

bool BTGattHandler::removeCharListener(const BTGattCharListener * l) noexcept {
    if( nullptr == l ) {
        ERR_PRINT("GATTCharacteristicListener ref is null");
        return false;
    }
    auto it = characteristicListenerList.begin(); // lock mutex and copy_store
    for (; !it.is_end(); ++it ) {
        if ( **it == *l ) {
            it.erase();
            it.write_back();
            return true;
        }
    }
    return false;
}

void BTGattHandler::printCharListener() noexcept {
    jau::INFO_PRINT("BTGattHandler: %u listener", characteristicListenerList.size());
    int i=0;
    auto it = characteristicListenerList.begin(); // lock mutex and copy_store
    for (; !it.is_end(); ++it, ++i ) {
        jau::INFO_PRINT("[%d]: %s", i, (*it)->toString().c_str());
    }
}

int BTGattHandler::removeAllAssociatedCharListener(std::shared_ptr<BTGattChar> associatedCharacteristic) noexcept {
    if( nullptr == associatedCharacteristic ) {
        ERR_PRINT("Given GATTCharacteristic ref is null");
        return false;
    }
    return removeAllAssociatedCharListener( associatedCharacteristic.get() );
}

int BTGattHandler::removeAllAssociatedCharListener(const BTGattChar * associatedCharacteristic) noexcept {
    if( nullptr == associatedCharacteristic ) {
        ERR_PRINT("Given GATTCharacteristic ref is null");
        return false;
    }
    int count = 0;
    auto it = characteristicListenerList.begin(); // lock mutex and copy_store
    while( !it.is_end() ) {
        if ( (*it)->match(*associatedCharacteristic) ) {
            it.erase();
            ++count;
        } else {
            ++it;
        }
    }
    if( 0 < count ) {
        it.write_back();
    }
    return count;
}

int BTGattHandler::removeAllCharListener() noexcept {
    int count = characteristicListenerList.size();
    characteristicListenerList.clear();
    return count;
}

void BTGattHandler::setSendIndicationConfirmation(const bool v) {
    sendIndicationConfirmation = v;
}

bool BTGattHandler::getSendIndicationConfirmation() noexcept {
    return sendIndicationConfirmation;
}

AttErrorRsp::ErrorCode BTGattHandler::applyWrite(std::shared_ptr<BTDevice> device, const uint16_t handle, const jau::TROOctets & value, const uint16_t value_offset) {
    if( nullptr == gattServerData ) {
        return AttErrorRsp::ErrorCode::INVALID_HANDLE;
    }
    for(DBGattService& s : gattServerData->services) {
        if( s.handle <= handle && handle <= s.end_handle ) {
            for(DBGattChar& c : s.characteristics) {
                if( c.handle <= handle && handle <= c.end_handle ) {
                    if( handle == c.value_handle ) {
                        if( c.value.size() < value_offset) { // offset at value-end + 1 OK to append
                            return AttErrorRsp::ErrorCode::INVALID_OFFSET;
                        }
                        if( c.variable_length ) {
                            if( c.value.capacity() < value_offset + value.size() ) {
                                return AttErrorRsp::ErrorCode::INVALID_ATTRIBUTE_VALUE_LEN;
                            }
                        } else {
                            if( c.value.size() < value_offset + value.size() ) {
                                return AttErrorRsp::ErrorCode::INVALID_ATTRIBUTE_VALUE_LEN;
                            }
                        }
                        {
                            bool allowed = true;
                            int i=0;
                            jau::for_each_fidelity(gattServerData->listener(), [&](DBGattServer::ListenerRef &l) {
                                try {
                                    allowed = l->writeCharValue(device, s, c, value, value_offset) && allowed;
                                } catch (std::exception &e) {
                                    ERR_PRINT("GATT-REQ: WRITE: (%s) %d/%zd: %s of %s: Caught exception %s",
                                            c.toString().c_str(), i+1, gattServerData->listener().size(),
                                            device->toString().c_str(), e.what());
                                }
                                i++;
                            });
                            if( !allowed ) {
                                return AttErrorRsp::ErrorCode::NO_WRITE_PERM;
                            }
                        }
                        if( c.variable_length ) {
                            if( c.value.size() != value_offset + value.size() ) {
                                c.value.resize( value_offset + value.size() );
                            }
                        }
                        c.value.put_octets_nc(value_offset, value);
                        return AttErrorRsp::ErrorCode::NO_ERROR;
                    }
                    for(DBGattDesc& d : c.descriptors) {
                        if( handle == d.handle ) {
                            if( d.value.size() < value_offset) { // offset at value-end + 1 OK to append
                                return AttErrorRsp::ErrorCode::INVALID_OFFSET;
                            }
                            if( d.variable_length ) {
                                if( d.value.capacity() < value_offset + value.size() ) {
                                    return AttErrorRsp::ErrorCode::INVALID_ATTRIBUTE_VALUE_LEN;
                                }
                            } else {
                                if( d.value.size() < value_offset + value.size() ) {
                                    return AttErrorRsp::ErrorCode::INVALID_ATTRIBUTE_VALUE_LEN;
                                }
                            }
                            if( d.isUserDescription() ) {
                                return AttErrorRsp::ErrorCode::NO_WRITE_PERM;
                            }
                            const bool isCCCD = d.isClientCharConfig();
                            if( !isCCCD ) {
                                bool allowed = true;
                                int i=0;
                                jau::for_each_fidelity(gattServerData->listener(), [&](DBGattServer::ListenerRef &l) {
                                    try {
                                        allowed = l->writeDescValue(device, s, c, d, value, value_offset) && allowed;
                                    } catch (std::exception &e) {
                                        ERR_PRINT("GATT-REQ: WRITE: (%s) %d/%zd: %s of %s: Caught exception %s",
                                                d.toString().c_str(), i+1, gattServerData->listener().size(),
                                                device->toString().c_str(), e.what());
                                    }
                                    i++;
                                });
                                if( !allowed ) {
                                    return AttErrorRsp::ErrorCode::NO_WRITE_PERM;
                                }
                            }
                            if( d.variable_length ) {
                                if( d.value.size() != value_offset + value.size() ) {
                                    d.value.resize( value_offset + value.size() );
                                }
                            }
                            if( isCCCD ) {
                                if( value.size() == 0 ) {
                                    // no change, exit
                                    return AttErrorRsp::ErrorCode::NO_ERROR;
                                }
                                const uint8_t old_v = d.value.get_uint8_nc(0);
                                const bool oldEnableNotification = old_v & 0b001;
                                const bool oldEnableIndication = old_v & 0b010;

                                const uint8_t req_v = value.get_uint8_nc(0);
                                const bool reqEnableNotification = req_v & 0b001;
                                const bool reqEnableIndication = req_v & 0b010;
                                const bool hasNotification = c.hasProperties(BTGattChar::PropertyBitVal::Notify);
                                const bool hasIndication = c.hasProperties(BTGattChar::PropertyBitVal::Indicate);
                                const bool enableNotification = reqEnableNotification && hasNotification;
                                const bool enableIndication = reqEnableIndication && hasIndication;

                                if( oldEnableNotification == enableNotification &&
                                    oldEnableIndication == enableIndication ) {
                                    // no change, exit
                                    return AttErrorRsp::ErrorCode::NO_ERROR;
                                }
                                const uint16_t new_v = enableNotification | ( enableIndication << 1 );
                                d.value.put_uint8_nc(0, new_v);
                                {
                                    int i=0;
                                    jau::for_each_fidelity(gattServerData->listener(), [&](DBGattServer::ListenerRef &l) {
                                        try {
                                            l->clientCharConfigChanged(device, s, c, d, enableNotification, enableIndication);
                                        } catch (std::exception &e) {
                                            ERR_PRINT("GATT-REQ: WRITE CCCD: (%s) %d/%zd: %s of %s: Caught exception %s",
                                                    d.toString().c_str(), i+1, gattServerData->listener().size(),
                                                    device->toString().c_str(), e.what());
                                        }
                                        i++;
                                    });
                                }
                            } else {
                                // all other types ..
                                d.value.put_octets_nc(value_offset, value);
                            }
                            return AttErrorRsp::ErrorCode::NO_ERROR;
                        }
                    }
                } // if characteristics-range
            } // for characteristics
        } // if service-range
    } // for services
    return AttErrorRsp::ErrorCode::INVALID_HANDLE;
}

bool BTGattHandler::hasHandle(const uint16_t handle) {
    if( nullptr == gattServerData ) {
        return false;
    }
    for(DBGattService& s : gattServerData->services) {
        if( s.handle <= handle && handle <= s.end_handle ) {
            for(DBGattChar& c : s.characteristics) {
                if( c.handle <= handle && handle <= c.end_handle ) {
                    if( handle == c.value_handle ) {
                        return true;
                    }
                    for(DBGattDesc& d : c.descriptors) {
                        if( handle == d.handle ) {
                            return true;
                        }
                    }
                } // if characteristics-range
            } // for characteristics
        } // if service-range
    } // for services
    return false;
}

void BTGattHandler::replyWriteReq(const AttPDUMsg * pdu) {
    /**
     * Without Response:
     *   BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.5.3 ATT_WRITE_CMD
     *   BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.1 Write Characteristic Value without Response
     *
     * With Response:
     *   BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.5.1 ATT_WRITE_REQ
     *   BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.3 Write Characteristic Value
     *   BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.3.3 Client Characteristic Configuration
     *
     *   BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.5.2 ATT_WRITE_RSP
     *   BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.3 Write Characteristic Value
     */
    std::shared_ptr<BTDevice> device = getDeviceUnchecked();
    if( nullptr == device ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNLIKELY_ERROR, pdu->getOpcode(), 0);
        ERR_PRINT("GATT-Req: WRITE.0, null device: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }

    if( AttPDUMsg::Opcode::PREPARE_WRITE_REQ == pdu->getOpcode() ) {
        const AttPrepWrite * req = static_cast<const AttPrepWrite*>(pdu);
        if( !hasHandle( req->getHandle() ) ) {
            AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, req->getOpcode(), req->getHandle());
            WARN_PRINT("GATT-Req: WRITE.10: %s -> %s from %s", req->toString().c_str(), err.toString().c_str(), toString().c_str());
            send(err);
            return;
        }
        AttPrepWrite rsp(false, *req);
        writeDataQueue.push_back(rsp);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: WRITE.11: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
        send(rsp);
        return;
    } else if( AttPDUMsg::Opcode::EXECUTE_WRITE_REQ == pdu->getOpcode() ) {
        const AttExeWriteReq * req = static_cast<const AttExeWriteReq*>(pdu);
        if( 0x01 == req->getFlags() ) {
            AttErrorRsp::ErrorCode res = AttErrorRsp::ErrorCode::NO_ERROR;
            uint16_t last_handle=0;
            for(auto iter = writeDataQueue.cbegin() ; iter < writeDataQueue.cend() && AttErrorRsp::ErrorCode::NO_ERROR == res; iter++ ) {
                const AttPrepWrite &p = *iter;
                last_handle = p.getHandle();
                jau::TROOctets p_val(p.getValue().get_ptr_nc(0), p.getValue().size(), p.getValue().byte_order());
                res = applyWrite(device, last_handle, p_val, p.getValueOffset());
            }
            if( AttErrorRsp::ErrorCode::NO_ERROR != res ) {
                writeDataQueue.clear();
                AttErrorRsp err(res, pdu->getOpcode(), last_handle);
                WARN_PRINT("GATT-Req: WRITE.12: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
                send(err);
                return;
            }
        }
        writeDataQueue.clear();
        AttExeWriteRsp rsp;
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: WRITE.13: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
        send(rsp);
        return;
    }

    uint16_t handle = 0;
    const jau::TOctetSlice * vslice = nullptr;
    bool withResp;
    if( AttPDUMsg::Opcode::WRITE_REQ == pdu->getOpcode() ) {
        const AttWriteReq * req = static_cast<const AttWriteReq*>(pdu);
        handle = req->getHandle();
        vslice = &req->getValue();
        withResp = true;
    } else if( AttPDUMsg::Opcode::WRITE_CMD == pdu->getOpcode() ) {
        const AttWriteCmd * req = static_cast<const AttWriteCmd*>(pdu);
        handle = req->getHandle();
        vslice = &req->getValue();
        withResp = false;
    } else {
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNSUPPORTED_REQUEST, pdu->getOpcode(), 0);
        WARN_PRINT("GATT-Req: WRITE.20: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    jau::TROOctets req_val(vslice->get_ptr_nc(0), vslice->size(), vslice->byte_order());
    AttErrorRsp::ErrorCode res = applyWrite(device, handle, req_val, 0);
    if( AttErrorRsp::ErrorCode::NO_ERROR != res ) {
        writeDataQueue.clear();
        AttErrorRsp err(res, pdu->getOpcode(), handle);
        WARN_PRINT("GATT-Req: WRITE.21: %s -> %s (sent %d) from %s",
                pdu->toString().c_str(), err.toString().c_str(), (int)withResp, toString().c_str());
        if( withResp ) {
            send(err);
        }
        return;
    }
    if( withResp ) {
        AttWriteRsp rsp;
        WARN_PRINT("GATT-Req: WRITE.22: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
        send(rsp);
    }
}

void BTGattHandler::replyReadReq(const AttPDUMsg * pdu) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.1 Read Characteristic Value */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.3 Read Long Characteristic Value */
    /* For any follow up request, which previous request reply couldn't fit in ATT_MTU */
    std::shared_ptr<BTDevice> device = getDeviceUnchecked();
    if( nullptr == device ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNLIKELY_ERROR, pdu->getOpcode(), 0);
        ERR_PRINT("GATT-Req: READ, null device: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    uint16_t handle = 0;
    uint16_t value_offset = 0;
    bool isBlobReq;
    if( AttPDUMsg::Opcode::READ_REQ == pdu->getOpcode() ) {
        const AttReadReq * req = static_cast<const AttReadReq*>(pdu);
        handle = req->getHandle();
        isBlobReq = false;
    } else if( AttPDUMsg::Opcode::READ_BLOB_REQ == pdu->getOpcode() ) {
        /**
         * BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.3 Read Long Characteristic Value
         *
         * If the Characteristic Value is not longer than (ATT_MTU – 1)
         * an ATT_ERROR_RSP PDU with the error
         * code set to Attribute Not Long shall be received on the first
         * ATT_READ_BLOB_REQ PDU.
         */
        const AttReadBlobReq * req = static_cast<const AttReadBlobReq*>(pdu);
        handle = req->getHandle();
        value_offset = req->getValueOffset();
        isBlobReq = true;
    } else {
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNSUPPORTED_REQUEST, pdu->getOpcode(), 0);
        WARN_PRINT("GATT-Req: READ: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    if( 0 == handle ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    const jau::nsize_t rspMaxSize = usedMTU.load()-1;
    (void)rspMaxSize;

    if( nullptr != gattServerData ) {
        for(DBGattService& s : gattServerData->services) {
            if( s.handle <= handle && handle <= s.end_handle ) {
                /**
                 * AttReadByGroupTypeRsp (1 opcode + 1 element_size + 2 handle + 2 handle + 16 uuid128_t = 22 bytes)
                 * always fits in minimum ATT_PDU 23
                 */
                for(DBGattChar& c : s.characteristics) {
                    if( c.handle <= handle && handle <= c.end_handle ) {
                        if( handle == c.value_handle ) {
                            if( isBlobReq && c.value.size() <= rspMaxSize ) {
                                AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_LONG, pdu->getOpcode(), 0);
                                COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
                                send(err);
                                return;
                            }
                            {
                                bool allowed = true;
                                int i=0;
                                jau::for_each_fidelity(gattServerData->listener(), [&](DBGattServer::ListenerRef &l) {
                                    try {
                                        allowed = l->readCharValue(device, s, c) && allowed;
                                    } catch (std::exception &e) {
                                        ERR_PRINT("GATT-REQ: READ: (%s) %d/%zd: %s of %s: Caught exception %s",
                                                c.toString().c_str(), i+1, gattServerData->listener().size(),
                                                device->toString().c_str(), e.what());
                                    }
                                    i++;
                                });
                                if( !allowed ) {
                                    AttErrorRsp err(AttErrorRsp::ErrorCode::NO_READ_PERM, pdu->getOpcode(), 0);
                                    COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.2: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
                                    send(err);
                                    return;
                                }
                            }
                            AttReadNRsp rsp(isBlobReq, c.value, value_offset);
                            if( rsp.getPDUValueSize() > rspMaxSize ) {
                                rsp.pdu.resize(usedMTU); // requires another READ_BLOB_REQ
                            }
                            COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.3: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                            send(rsp);
                            return;
                        }
                        for(DBGattDesc& d : c.descriptors) {
                            if( handle == d.handle ) {
                                if( isBlobReq && d.value.size() <= rspMaxSize ) {
                                    AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_LONG, pdu->getOpcode(), 0);
                                    COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
                                    send(err);
                                    return;
                                }
                                {
                                    bool allowed = true;
                                    int i=0;
                                    jau::for_each_fidelity(gattServerData->listener(), [&](DBGattServer::ListenerRef &l) {
                                        try {
                                            allowed = l->readDescValue(device, s, c, d) && allowed;
                                        } catch (std::exception &e) {
                                            ERR_PRINT("GATT-REQ: READ: (%s) %d/%zd: %s of %s: Caught exception %s",
                                                    d.toString().c_str(), i+1, gattServerData->listener().size(),
                                                    device->toString().c_str(), e.what());
                                        }
                                        i++;
                                    });
                                    if( !allowed ) {
                                        AttErrorRsp err(AttErrorRsp::ErrorCode::NO_READ_PERM, pdu->getOpcode(), 0);
                                        COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.4: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
                                        send(err);
                                        return;
                                    }
                                }
                                AttReadNRsp rsp(isBlobReq, d.value, value_offset);
                                if( rsp.getPDUValueSize() > rspMaxSize ) {
                                    rsp.pdu.resize(usedMTU); // requires another READ_BLOB_REQ
                                }
                                COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.5: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                                send(rsp);
                                return;
                            }
                        }
                    } // if characteristics-range
                } // for characteristics
            } // if service-range
        } // for services
    }
    AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_FOUND, pdu->getOpcode(), 0);
    COND_PRINT(env.DEBUG_DATA, "GATT-Req: READ.6: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
    send(err);
}

void BTGattHandler::replyFindInfoReq(const AttFindInfoReq * pdu) {
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.3.1 ATT_FIND_INFORMATION_REQ
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.3.2 ATT_FIND_INFORMATION_RSP
    // BT Core Spec v5.2: Vol 3, Part G GATT: 4.7.1 Discover All Characteristic Descriptors
    if( 0 == pdu->getStartHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: INFO.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    if( pdu->getStartHandle() > pdu->getEndHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), pdu->getStartHandle());
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: INFO.1: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    const uint16_t end_handle = pdu->getEndHandle();
    const uint16_t start_handle = pdu->getStartHandle();

    const jau::nsize_t rspMaxSize = std::min<jau::nsize_t>(255, usedMTU.load()-2);
    AttFindInfoRsp rsp(usedMTU); // maximum size
    jau::nsize_t rspElemSize = 0;
    jau::nsize_t rspSize = 0;
    jau::nsize_t rspCount = 0;

    if( nullptr != gattServerData ) {
        for(DBGattService& s : gattServerData->services) {
            for(DBGattChar& c : s.characteristics) {
                for(DBGattDesc& d : c.descriptors) {
                    if( start_handle <= d.handle && d.handle <= end_handle ) {
                        const jau::nsize_t size = 2 + d.type->getTypeSizeInt();
                        if( 0 == rspElemSize ) {
                            // initial setting or reset
                            rspElemSize = size;
                            rsp.setElementSize(rspElemSize);
                        }
                        if( rspSize + size > rspMaxSize || rspElemSize != size ) {
                            // send if rsp is full - or - element size changed
                            rsp.setElementCount(rspCount);
                            COND_PRINT(env.DEBUG_DATA, "GATT-Req: INFO.2: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                            send(rsp);
                            return; // Client shall issue additional FIND_INFORMATION_REQ
                        }
                        rsp.setElementHandle(rspCount, d.handle);
                        rsp.setElementValueUUID(rspCount, *d.type);
                        rspSize += size;
                        ++rspCount;
                    }
                }
            }
        }
        if( 0 < rspCount ) { // loop completed, elements added and all fitting in ATT_MTU
            rsp.setElementCount(rspCount);
            COND_PRINT(env.DEBUG_DATA, "GATT-Req: INFO.3: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
            send(rsp);
            return;
        }
    }
    AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_FOUND, pdu->getOpcode(), 0);
    COND_PRINT(env.DEBUG_DATA, "GATT-Req: INFO.4: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
    send(err);
}

void BTGattHandler::replyReadByTypeReq(const AttReadByNTypeReq * pdu) {
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.4.1 ATT_READ_BY_TYPE_REQ
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.4.2 ATT_READ_BY_TYPE_RSP
    // BT Core Spec v5.2: Vol 3, Part G GATT: 4.6.1 Discover All Characteristics of a Service
    if( 0 == pdu->getStartHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    if( pdu->getStartHandle() > pdu->getEndHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), pdu->getStartHandle());
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.1: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    const jau::uuid16_t uuid_characteristic = jau::uuid16_t(GattAttributeType::CHARACTERISTIC);
    const jau::uuid16_t uuid_incl_service = jau::uuid16_t(GattAttributeType::INCLUDE_DECLARATION);
    uint16_t req_type;
    if( pdu->getNType()->equivalent( uuid_characteristic ) ) {
        req_type = GattAttributeType::CHARACTERISTIC;
    } else if( pdu->getNType()->equivalent( uuid_incl_service ) ) {
        req_type = GattAttributeType::INCLUDE_DECLARATION;
    } else {
        // not handled
        req_type = 0;
    }
    if( GattAttributeType::CHARACTERISTIC == req_type ) {
        const uint16_t end_handle = pdu->getEndHandle();
        const uint16_t start_handle = pdu->getStartHandle();

        const jau::nsize_t rspMaxSize = std::min<jau::nsize_t>(255, usedMTU.load()-2);
        AttReadByTypeRsp rsp(usedMTU); // maximum size
        jau::nsize_t rspElemSize = 0;
        jau::nsize_t rspSize = 0;
        jau::nsize_t rspCount = 0;

        if( nullptr != gattServerData ) {
            for(DBGattService& s : gattServerData->services) {
                for(DBGattChar& c : s.characteristics) {
                    if( start_handle <= c.handle && c.handle <= end_handle ) {
                        const jau::nsize_t size = 2 + 1 + 2 + c.value_type->getTypeSizeInt();
                        if( 0 == rspElemSize ) {
                            // initial setting or reset
                            rspElemSize = size;
                            rsp.setElementSize(rspElemSize);
                        }
                        if( rspSize + size > rspMaxSize || rspElemSize != size ) {
                            // send if rsp is full - or - element size changed
                            rsp.setElementCount(rspCount);
                            COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.2: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                            send(rsp);
                            return; // Client shall issue additional READ_BY_TYPE_REQ
                        }
                        jau::nsize_t ePDUOffset = rsp.getElementPDUOffset(rspCount);
                        rsp.setElementHandle(rspCount, c.handle); // Characteristic Handle
                        ePDUOffset += 2;
                        rsp.pdu.put_uint8_nc(ePDUOffset, c.properties); // Characteristics Property
                        ePDUOffset += 1;
                        rsp.pdu.put_uint16_nc(ePDUOffset, c.value_handle); // Characteristics Value Handle
                        ePDUOffset += 2;
                        c.value_type->put(rsp.pdu.get_wptr_nc(ePDUOffset), 0, true /* littleEndian */); // Characteristics Value Type UUID
                        ePDUOffset += c.value_type->getTypeSizeInt();
                        rspSize += size;
                        ++rspCount;
                    }
                }
            }
            if( 0 < rspCount ) { // loop completed, elements added and all fitting in ATT_MTU
                rsp.setElementCount(rspCount);
                COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.3: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                send(rsp);
                return;
            }
        }
        AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_FOUND, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.4: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
    } else if( GattAttributeType::INCLUDE_DECLARATION == req_type ) {
        // TODO: Support INCLUDE_DECLARATION ??
        AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_FOUND, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.5: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
    } else {
        // TODO: Add other group types ???
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNSUPPORTED_GROUP_TYPE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: TYPE.6: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
    }
}

void BTGattHandler::replyReadByGroupTypeReq(const AttReadByNTypeReq * pdu) {
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.4.9 ATT_READ_BY_GROUP_TYPE_REQ
    // BT Core Spec v5.2: Vol 3, Part F ATT: 3.4.4.10 ATT_READ_BY_GROUP_TYPE_RSP
    // BT Core Spec v5.2: Vol 3, Part G GATT: 4.4.1 Discover All Primary Services
    if( 0 == pdu->getStartHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.0: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    if( pdu->getStartHandle() > pdu->getEndHandle() ) {
        AttErrorRsp err(AttErrorRsp::ErrorCode::INVALID_HANDLE, pdu->getOpcode(), pdu->getStartHandle());
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.1: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
        return;
    }
    const jau::uuid16_t uuid_prim_service = jau::uuid16_t(GattAttributeType::PRIMARY_SERVICE);
    const jau::uuid16_t uuid_secd_service = jau::uuid16_t(GattAttributeType::SECONDARY_SERVICE);
    uint16_t req_group_type;
    if( pdu->getNType()->equivalent( uuid_prim_service ) ) {
        req_group_type = GattAttributeType::PRIMARY_SERVICE;
    } else if( pdu->getNType()->equivalent( uuid_secd_service ) ) {
        req_group_type = GattAttributeType::SECONDARY_SERVICE;
    } else {
        // not handled
        req_group_type = 0;
    }
    if( 0 != req_group_type ) {
        const uint16_t end_handle = pdu->getEndHandle();
        const uint16_t start_handle = pdu->getStartHandle();

        const jau::nsize_t rspMaxSize = std::min<jau::nsize_t>(255, usedMTU.load()-2);
        AttReadByGroupTypeRsp rsp(usedMTU); // maximum size
        jau::nsize_t rspElemSize = 0;
        jau::nsize_t rspSize = 0;
        jau::nsize_t rspCount = 0;

        if( nullptr != gattServerData ) {
            for(DBGattService& s : gattServerData->services) {
                if( ( ( GattAttributeType::PRIMARY_SERVICE   == req_group_type &&  s.primary ) ||
                      ( GattAttributeType::SECONDARY_SERVICE == req_group_type && !s.primary )
                    ) &&
                    start_handle <= s.handle && s.handle <= end_handle )
                {
                    const jau::nsize_t size = 2 + 2 + s.type->getTypeSizeInt();
                    if( 0 == rspElemSize ) {
                        // initial setting or reset
                        rspElemSize = size;
                        rsp.setElementSize(rspElemSize);
                    }
                    if( rspSize + size > rspMaxSize || rspElemSize != size ) {
                        // send if rsp is full - or - element size changed
                        /**
                         * AttReadByGroupTypeRsp (1 opcode + 1 element_size + 2 handle + 2 handle + 16 uuid128_t = 22 bytes)
                         * always fits in minimum ATT_PDU 23
                         */
                        rsp.setElementCount(rspCount);
                        COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.3: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                        send(rsp);
                        return; // Client shall issue additional READ_BY_TYPE_REQ
                    }
                    rsp.setElementStartHandle(rspCount, s.handle);
                    rsp.setElementEndHandle(rspCount, s.end_handle);
                    rsp.setElementValueUUID(rspCount, *s.type);
                    rspSize += size;
                    ++rspCount;
                }
            }
            if( 0 < rspCount ) { // loop completed, elements added and all fitting in ATT_MTU
                rsp.setElementCount(rspCount);
                COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.4: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
                send(rsp);
                return;
            }
        }
        AttErrorRsp err(AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_FOUND, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.5: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
    } else {
        // TODO: Add other group types ???
        AttErrorRsp err(AttErrorRsp::ErrorCode::UNSUPPORTED_GROUP_TYPE, pdu->getOpcode(), 0);
        COND_PRINT(env.DEBUG_DATA, "GATT-Req: GROUP_TYPE.6: %s -> %s from %s", pdu->toString().c_str(), err.toString().c_str(), toString().c_str());
        send(err);
    }
}

void BTGattHandler::replyAttPDUReq(std::unique_ptr<const AttPDUMsg> && pdu) {
    if( !validateConnected() ) { // shall not happen
        DBG_PRINT("GATT-Req: disconnected: req %s from %s",
                pdu->toString().c_str(), toString().c_str());
        return;
    }
    switch( pdu->getOpcode() ) {
        case AttPDUMsg::Opcode::EXCHANGE_MTU_REQ: {
            const AttExchangeMTU * p = static_cast<const AttExchangeMTU*>(pdu.get());
            const uint16_t clientMTU = p->getMTUSize();
            usedMTU = std::min(serverMTU, clientMTU);
            const AttExchangeMTU rsp(AttPDUMsg::ReqRespType::RESPONSE, usedMTU);
            COND_PRINT(env.DEBUG_DATA, "GATT-Req: MTU recv: %u, %s  -> %u %s from %s",
                    clientMTU, pdu->toString().c_str(),
                    usedMTU.load(), rsp.toString().c_str(), toString().c_str());
            send(rsp);
            return;
        }
        case AttPDUMsg::Opcode::READ_BY_TYPE_REQ: {
            replyReadByTypeReq( static_cast<const AttReadByNTypeReq*>( pdu.get() ) );
            return;
        }
        case AttPDUMsg::Opcode::READ_BY_GROUP_TYPE_REQ: {
            replyReadByGroupTypeReq( static_cast<const AttReadByNTypeReq*>( pdu.get() ) );
            return;
        }
        case AttPDUMsg::Opcode::FIND_INFORMATION_REQ: {
            replyFindInfoReq( static_cast<const AttFindInfoReq*>( pdu.get() ) );
            return;
        }
        case AttPDUMsg::Opcode::READ_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::READ_BLOB_REQ: {
            replyReadReq( pdu.get() );
            return;
        }
        case AttPDUMsg::Opcode::WRITE_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::WRITE_CMD:
            [[fallthrough]];
        case AttPDUMsg::Opcode::PREPARE_WRITE_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::EXECUTE_WRITE_REQ: {
            replyWriteReq( pdu.get() );
            return;
        }

        // TODO: Add support for the following requests

        case AttPDUMsg::Opcode::FIND_BY_TYPE_VALUE_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::READ_MULTIPLE_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::READ_MULTIPLE_VARIABLE_REQ:
            [[fallthrough]];
        case AttPDUMsg::Opcode::SIGNED_WRITE_CMD: {
            AttErrorRsp rsp(AttErrorRsp::ErrorCode::UNSUPPORTED_REQUEST, pdu->getOpcode(), 0);
            WARN_PRINT("GATT Req: Ignored: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
            send(rsp);
            return;
        }
        default:
            AttErrorRsp rsp(AttErrorRsp::ErrorCode::FORBIDDEN_VALUE, pdu->getOpcode(), 0);
            ERR_PRINT("GATT Req: Unhandled: %s -> %s from %s", pdu->toString().c_str(), rsp.toString().c_str(), toString().c_str());
            send(rsp);
            return;
    }
}

void BTGattHandler::l2capReaderThreadImpl() {
    {
        const std::lock_guard<std::mutex> lock(mtx_l2capReaderLifecycle); // RAII-style acquire and relinquish via destructor
        l2capReaderShallStop = false;
        l2capReaderRunning = true;
        DBG_PRINT("GATTHandler::reader Started");
        cv_l2capReaderInit.notify_all();
    }
    thread_local jau::call_on_release thread_cleanup([&]() {
        DBG_PRINT("GATTHandler::l2capReaderThreadCleanup: l2capReaderRunning %d -> 0", l2capReaderRunning.load());
        l2capReaderRunning = false;
    });

    while( !l2capReaderShallStop ) {
        jau::snsize_t len;
        if( !validateConnected() ) {
            ERR_PRINT("GATTHandler::reader: Invalid IO state -> Stop");
            l2capReaderShallStop = true;
            break;
        }

        len = l2cap.read(rbuffer.get_wptr(), rbuffer.size());
        if( 0 < len ) {
            std::unique_ptr<const AttPDUMsg> attPDU = AttPDUMsg::getSpecialized(rbuffer.get_ptr(), static_cast<jau::nsize_t>(len));
            COND_PRINT(env.DEBUG_DATA, "GATTHandler::reader: Got %s", attPDU->toString().c_str());

            const AttPDUMsg::Opcode opc = attPDU->getOpcode();
            const AttPDUMsg::OpcodeType opc_type = AttPDUMsg::get_type(opc);

            if( AttPDUMsg::Opcode::MULTIPLE_HANDLE_VALUE_NTF == opc ) { // AttPDUMsg::OpcodeType::NOTIFICATION
                // FIXME TODO ..
                ERR_PRINT("GATTHandler::reader: MULTI-NTF not implemented: %s", attPDU->toString().c_str());
            } else if( AttPDUMsg::Opcode::HANDLE_VALUE_NTF == opc ) { // AttPDUMsg::OpcodeType::NOTIFICATION
                const AttHandleValueRcv * a = static_cast<const AttHandleValueRcv*>(attPDU.get());
                COND_PRINT(env.DEBUG_DATA, "GATTHandler::reader: NTF: %s, listener %zd", a->toString().c_str(), characteristicListenerList.size());
                BTGattCharRef decl = findCharacterisicsByValueHandle(a->getHandle());
                const jau::TOctetSlice& a_value_view = a->getValue();
                const jau::TROOctets data_view(a_value_view.get_ptr_nc(0), a_value_view.size(), a_value_view.byte_order()); // just a view, still owned by attPDU
                // const std::shared_ptr<TROOctets> data( std::make_shared<POctets>(a->getValue()) );
                const uint64_t timestamp = a->ts_creation;
                int i=0;
                jau::for_each_fidelity(characteristicListenerList, [&](std::shared_ptr<BTGattCharListener> &l) {
                    try {
                        if( l->match(*decl) ) {
                            l->notificationReceived(decl, data_view, timestamp);
                        }
                    } catch (std::exception &e) {
                        ERR_PRINT("GATTHandler::notificationReceived-CBs %d/%zd: GATTCharacteristicListener %s: Caught exception %s",
                                i+1, characteristicListenerList.size(),
                                jau::to_hexstring((void*)l.get()).c_str(), e.what());
                    }
                    i++;
                });
            } else if( AttPDUMsg::Opcode::HANDLE_VALUE_IND == opc ) { // AttPDUMsg::OpcodeType::INDICATION
                const AttHandleValueRcv * a = static_cast<const AttHandleValueRcv*>(attPDU.get());
                COND_PRINT(env.DEBUG_DATA, "GATTHandler::reader: IND: %s, sendIndicationConfirmation %d, listener %zd",
                        a->toString().c_str(), sendIndicationConfirmation.load(), characteristicListenerList.size());
                bool cfmSent = false;
                if( sendIndicationConfirmation ) {
                    AttHandleValueCfm cfm;
                    send(cfm);
                    cfmSent = true;
                }
                BTGattCharRef decl = findCharacterisicsByValueHandle(a->getHandle());
                const jau::TOctetSlice& a_value_view = a->getValue();
                const jau::TROOctets data_view(a_value_view.get_ptr_nc(0), a_value_view.size(), a_value_view.byte_order()); // just a view, still owned by attPDU
                // const std::shared_ptr<TROOctets> data( std::make_shared<POctets>(a->getValue()) );
                const uint64_t timestamp = a->ts_creation;
                int i=0;
                jau::for_each_fidelity(characteristicListenerList, [&](std::shared_ptr<BTGattCharListener> &l) {
                    try {
                        if( l->match(*decl) ) {
                            l->indicationReceived(decl, data_view, timestamp, cfmSent);
                        }
                    } catch (std::exception &e) {
                        ERR_PRINT("GATTHandler::indicationReceived-CBs %d/%zd: GATTCharacteristicListener %s, cfmSent %d: Caught exception %s",
                                i+1, characteristicListenerList.size(),
                                jau::to_hexstring((void*)l.get()).c_str(), cfmSent, e.what());
                    }
                    i++;
                });
            } else if( AttPDUMsg::OpcodeType::RESPONSE == opc_type ) {
                COND_PRINT(env.DEBUG_DATA, "GATTHandler::reader: Ring: %s", attPDU->toString().c_str());
                attPDURing.putBlocking( std::move(attPDU) );
            } else if( AttPDUMsg::OpcodeType::REQUEST == opc_type ) {
                replyAttPDUReq( std::move( attPDU ) );
            } else {
                ERR_PRINT("GATTHandler::reader: Unhandled: %s", attPDU->toString().c_str());
            }
        } else if( 0 > len && ETIMEDOUT != errno && !l2capReaderShallStop ) { // expected exits
            IRQ_PRINT("GATTHandler::reader: l2cap read error -> Stop; l2cap.read %d (%s); %s",
                    len, L2CAPComm::getRWExitCodeString(len).c_str(),
                    getStateString().c_str());
            l2capReaderShallStop = true;
            has_ioerror = true;
        } else {
            WORDY_PRINT("GATTHandler::reader: l2cap read: l2cap.read %d (%s); %s",
                    len, L2CAPComm::getRWExitCodeString(len).c_str(),
                    getStateString().c_str());
        }
    }
    {
        const std::lock_guard<std::mutex> lock(mtx_l2capReaderLifecycle); // RAII-style acquire and relinquish via destructor
        WORDY_PRINT("GATTHandler::reader: Ended. Ring has %u entries flushed", attPDURing.size());
        attPDURing.clear();
        l2capReaderRunning = false;
        cv_l2capReaderInit.notify_all();
    }
    disconnect(true /* disconnectDevice */, has_ioerror);
}

BTGattHandler::BTGattHandler(const std::shared_ptr<BTDevice> &device, L2CAPComm& l2cap_att) noexcept
: env(BTGattEnv::get()),
  wbr_device(device),
  role(device->getLocalGATTRole()),
  l2cap(l2cap_att),
  deviceString(device->getAddressAndType().toString()),
  rbuffer(number(Defaults::MAX_ATT_MTU), jau::endian::little),
  is_connected(l2cap.isOpen()), has_ioerror(false),
  attPDURing(nullptr, env.ATTPDU_RING_CAPACITY), l2capReaderShallStop(false),
  l2capReaderThreadId(0), l2capReaderRunning(false),
  gattServerData( GATTRole::Server == role ? device->getAdapter().getGATTServerData() : nullptr ),
  serverMTU(number(Defaults::MIN_ATT_MTU)), usedMTU(number(Defaults::MIN_ATT_MTU))
{
    if( !validateConnected() ) {
        ERR_PRINT("GATTHandler.ctor: L2CAP could not connect");
        is_connected = false;
        return;
    }
    DBG_PRINT("GATTHandler::ctor: Start Connect: GattHandler[%s], l2cap[%s]: %s",
                getStateString().c_str(), l2cap.getStateString().c_str(), toString().c_str());

    /**
     * We utilize DBTManager's mgmthandler_sigaction SIGALRM handler,
     * as we only can install one handler.
     */
    {
        std::unique_lock<std::mutex> lock(mtx_l2capReaderLifecycle); // RAII-style acquire and relinquish via destructor

        std::thread l2capReaderThread(&BTGattHandler::l2capReaderThreadImpl, this); // @suppress("Invalid arguments")
        l2capReaderThreadId = l2capReaderThread.native_handle();
        // Avoid 'terminate called without an active exception'
        // as l2capReaderThread may end due to I/O errors.
        l2capReaderThread.detach();

        while( false == l2capReaderRunning ) {
            cv_l2capReaderInit.wait(lock);
        }
    }

    if( GATTRole::Client == getRole() ) {
        // First point of failure if remote device exposes no GATT functionality. Allow a longer timeout!
        uint16_t mtu = 0;
        try {
            mtu = exchangeMTUImpl(number(Defaults::MAX_ATT_MTU), env.GATT_INITIAL_COMMAND_REPLY_TIMEOUT);
        } catch (std::exception &e) {
            ERR_PRINT2("GattHandler.ctor: exchangeMTU failed: %s", e.what());
        } catch (std::string &msg) {
            ERR_PRINT2("GattHandler.ctor: exchangeMTU failed: %s", msg.c_str());
        } catch (const char *msg) {
            ERR_PRINT2("GattHandler.ctor: exchangeMTU failed: %s", msg);
        }
        if( 0 == mtu ) {
            ERR_PRINT2("GATTHandler::ctor: Zero serverMTU -> disconnect: %s", toString().c_str());
            disconnect(true /* disconnectDevice */, false /* ioErrorCause */);
        } else {
            serverMTU = mtu;
            usedMTU = std::min(number(Defaults::MAX_ATT_MTU), serverMTU);
        }
    } else {
        if( nullptr != gattServerData ) {
            serverMTU = std::max( std::min( gattServerData->att_mtu, number(Defaults::MAX_ATT_MTU) ), number(Defaults::MIN_ATT_MTU) );
        } else {
            serverMTU = number(Defaults::MAX_ATT_MTU);
        }
        usedMTU = number(Defaults::MIN_ATT_MTU); // until negotiated!
    }
}

BTGattHandler::~BTGattHandler() noexcept {
    DBG_PRINT("GATTHandler::dtor: Start: %s", toString().c_str());
    disconnect(false /* disconnectDevice */, false /* ioErrorCause */);
    characteristicListenerList.clear();
    services.clear();
    genericAccess = nullptr;
    DBG_PRINT("GATTHandler::dtor: End: %s", toString().c_str());
}

bool BTGattHandler::disconnect(const bool disconnectDevice, const bool ioErrorCause) noexcept {
    PERF3_TS_T0();
    // Interrupt GATT's L2CAP::connect(..) and L2CAP::read(..), avoiding prolonged hang
    // and pull all underlying l2cap read operations!
    l2cap.close();

    // Avoid disconnect re-entry -> potential deadlock
    bool expConn = true; // C++11, exp as value since C++20
    if( !is_connected.compare_exchange_strong(expConn, false) ) {
        // not connected
        DBG_PRINT("GATTHandler::disconnect: Not connected: disconnectDevice %d, ioErrorCause %d: GattHandler[%s], l2cap[%s]: %s",
                  disconnectDevice, ioErrorCause, getStateString().c_str(), l2cap.getStateString().c_str(), toString().c_str());
        characteristicListenerList.clear();
        return false;
    }
    // Lock to avoid other threads using instance while disconnecting
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    DBG_PRINT("GATTHandler::disconnect: Start: disconnectDevice %d, ioErrorCause %d: GattHandler[%s], l2cap[%s]: %s",
              disconnectDevice, ioErrorCause, getStateString().c_str(), l2cap.getStateString().c_str(), toString().c_str());
    characteristicListenerList.clear();

    PERF3_TS_TD("GATTHandler::disconnect.1");
    {
        std::unique_lock<std::mutex> lockReader(mtx_l2capReaderLifecycle); // RAII-style acquire and relinquish via destructor
        has_ioerror = false;

        const pthread_t tid_self = pthread_self();
        const pthread_t tid_l2capReader = l2capReaderThreadId;
        l2capReaderThreadId = 0;
        const bool is_l2capReader = tid_l2capReader == tid_self;
        DBG_PRINT("GATTHandler.disconnect: l2capReader[running %d, shallStop %d, isReader %d, tid %p)",
                  l2capReaderRunning.load(), l2capReaderShallStop.load(), is_l2capReader, (void*)tid_l2capReader);
        if( l2capReaderRunning ) {
            l2capReaderShallStop = true;
            if( !is_l2capReader && 0 != tid_l2capReader ) {
                int kerr;
                if( 0 != ( kerr = pthread_kill(tid_l2capReader, SIGALRM) ) ) {
                    ERR_PRINT("GATTHandler::disconnect: pthread_kill %p FAILED: %d", (void*)tid_l2capReader, kerr);
                }
            }
            // Ensure the reader thread has ended, no runaway-thread using *this instance after destruction
            while( true == l2capReaderRunning ) {
                cv_l2capReaderInit.wait(lockReader);
            }
        }
    }
    PERF3_TS_TD("GATTHandler::disconnect.2");

    if( disconnectDevice ) {
        std::shared_ptr<BTDevice> device = getDeviceUnchecked();
        if( nullptr != device ) {
            // Cleanup device resources, proper connection state
            // Intentionally giving the POWER_OFF reason for the device in case of ioErrorCause!
            const HCIStatusCode reason = ioErrorCause ?
                                   HCIStatusCode::REMOTE_DEVICE_TERMINATED_CONNECTION_POWER_OFF :
                                   HCIStatusCode::REMOTE_USER_TERMINATED_CONNECTION;
            device->disconnect(reason);
        }
    }

    PERF3_TS_TD("GATTHandler::disconnect.X");
    DBG_PRINT("GATTHandler::disconnect: End: %s", toString().c_str());
    return true;
}

void BTGattHandler::send(const AttPDUMsg & msg) {
    if( !validateConnected() ) {
        throw jau::IllegalStateException("GATTHandler::send: Invalid IO State: req "+msg.toString()+" to "+toString(), E_FILE_LINE);
    }
    // [1 .. ATT_MTU-1] BT Core Spec v5.2: Vol 3, Part F 3.2.9 Long attribute values
    if( msg.pdu.size() > usedMTU ) {
        throw jau::IllegalArgumentException("clientMaxMTU "+std::to_string(msg.pdu.size())+" >= usedMTU "+std::to_string(usedMTU)+
                                       " to "+toString(), E_FILE_LINE);
    }

    // Thread safe l2cap.write(..) operation..
    const jau::snsize_t res = l2cap.write(msg.pdu.get_ptr(), msg.pdu.size());
    if( 0 > res ) {
        IRQ_PRINT("GATTHandler::send: l2cap write error -> disconnect: l2cap.write %d (%s); %s; %s to %s",
                res, L2CAPComm::getRWExitCodeString(res).c_str(), getStateString().c_str(),
                msg.toString().c_str(), toString().c_str());
        has_ioerror = true;
        disconnect(true /* disconnectDevice */, true /* ioErrorCause */); // state -> Disconnected
        throw BTException("GATTHandler::send: l2cap write error: req "+msg.toString()+" to "+toString(), E_FILE_LINE);
    }
    if( static_cast<size_t>(res) != msg.pdu.size() ) {
        ERR_PRINT("GATTHandler::send: l2cap write count error, %d != %zu: %s -> disconnect: %s",
                res, msg.pdu.size(), msg.toString().c_str(), toString().c_str());
        has_ioerror = true;
        disconnect(true /* disconnectDevice */, true /* ioErrorCause */); // state -> Disconnected
        throw BTException("GATTHandler::send: l2cap write count error, "+std::to_string(res)+" != "+std::to_string(msg.pdu.size())
                                 +": "+msg.toString()+" -> disconnect: "+toString(), E_FILE_LINE);
    }
}

std::unique_ptr<const AttPDUMsg> BTGattHandler::sendWithReply(const AttPDUMsg & msg, const int timeout) {
    send( msg );

    // Ringbuffer read is thread safe
    std::unique_ptr<const AttPDUMsg> res = attPDURing.getBlocking(timeout);
    if( nullptr == res ) {
        errno = ETIMEDOUT;
        IRQ_PRINT("GATTHandler::sendWithReply: nullptr result (timeout %d): req %s to %s", timeout, msg.toString().c_str(), toString().c_str());
        has_ioerror = true;
        disconnect(true /* disconnectDevice */, true /* ioErrorCause */);
        throw BTException("GATTHandler::sendWithReply: nullptr result (timeout "+std::to_string(timeout)+"): req "+msg.toString()+" to "+toString(), E_FILE_LINE);
    }
    return res;
}

uint16_t BTGattHandler::exchangeMTUImpl(const uint16_t clientMaxMTU, const int32_t timeout) {
    /***
     * BT Core Spec v5.2: Vol 3, Part G GATT: 4.3.1 Exchange MTU (Server configuration)
     */
    if( clientMaxMTU > number(Defaults::MAX_ATT_MTU) ) {
        throw jau::IllegalArgumentException("clientMaxMTU "+std::to_string(clientMaxMTU)+" > ClientMaxMTU "+std::to_string(number(Defaults::MAX_ATT_MTU)), E_FILE_LINE);
    }
    const AttExchangeMTU req(AttPDUMsg::ReqRespType::REQUEST, clientMaxMTU);
    // called by ctor only, no locking: const std::lock_guard<std::recursive_mutex> lock(mtx_command);
    PERF_TS_T0();

    uint16_t mtu = 0;
    DBG_PRINT("GATT MTU-REQ send: %s to %s", req.toString().c_str(), toString().c_str());

    std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, timeout); // valid reply or exception

    if( pdu->getOpcode() == AttPDUMsg::Opcode::EXCHANGE_MTU_RSP ) {
        const AttExchangeMTU * p = static_cast<const AttExchangeMTU*>(pdu.get());
        mtu = p->getMTUSize();
        DBG_PRINT("GATT MTU-RSP recv: %u, %s from %s", mtu, pdu->toString().c_str(), toString().c_str());
    } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
        /**
         * If the ATT_ERROR_RSP PDU is sent by the server
         * with the error code set to 'Request Not Supported',
         * the Attribute Opcode is not supported and the default MTU shall be used.
         */
        const AttErrorRsp * p = static_cast<const AttErrorRsp *>(pdu.get());
        if( AttErrorRsp::ErrorCode::UNSUPPORTED_REQUEST == p->getErrorCode() ) {
            mtu = number(Defaults::MIN_ATT_MTU); // OK by spec: Use default MTU
            DBG_PRINT("GATT MTU handled error -> ATT_MTU %u, %s from %s", mtu, pdu->toString().c_str(), toString().c_str());
        } else {
            WORDY_PRINT("GATT MTU unexpected error %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
        }
    } else {
        ERR_PRINT("GATT MTU unexpected reply %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
    }
    PERF_TS_TD("GATT exchangeMTU");

    return mtu;
}

bool BTGattHandler::sendNotification(const uint16_t handle, const jau::TROOctets & value) {
    if( GATTRole::Server != role ) {
        ERR_PRINT("BTDevice::sendNotification: GATTRole not server");
        return false;
    }
    if( !hasHandle(handle) ) {
        ERR_PRINT("BTDevice::sendNotification: invalid handle %s", jau::to_hexstring(handle).c_str());
        return false;
    }
    AttHandleValueRcv data(true /* isNotify */, handle, value);
    COND_PRINT(env.DEBUG_DATA, "GATT SEND NTF: %s to %s", data.toString().c_str(), toString().c_str());
    send(data);
    return true;
}

bool BTGattHandler::sendIndication(const uint16_t handle, const jau::TROOctets & value) {
    if( GATTRole::Server != role ) {
        ERR_PRINT("BTDevice::sendIndication: GATTRole not server");
        return false;
    }
    if( !hasHandle(handle) ) {
        ERR_PRINT("BTDevice::sendIndication: invalid handle %s", jau::to_hexstring(handle).c_str());
        return false;
    }
    AttHandleValueRcv req(false /* isNotify */, handle, value);
    std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, env.GATT_WRITE_COMMAND_REPLY_TIMEOUT); // valid reply or exception
    if( pdu->getOpcode() == AttPDUMsg::Opcode::HANDLE_VALUE_CFM ) {
        COND_PRINT(env.DEBUG_DATA, "GATT SENT IND: %s -> %s to/from %s",
                req.toString().c_str(), pdu->toString().c_str(), toString().c_str());
        return true;
    } else {
        WARN_PRINT("GATT SENT IND: Failed, no CFM reply: %s -> %s to/from %s",
                req.toString().c_str(), pdu->toString().c_str(), toString().c_str());
        return false;
    }
}

BTGattCharRef BTGattHandler::findCharacterisicsByValueHandle(const uint16_t charValueHandle) noexcept {
    return findCharacterisicsByValueHandle(charValueHandle, services);
}

BTGattCharRef BTGattHandler::findCharacterisicsByValueHandle(const uint16_t charValueHandle, jau::darray<BTGattServiceRef> &services_) noexcept {
    for(auto it = services_.begin(); it != services_.end(); it++) {
        BTGattCharRef decl = findCharacterisicsByValueHandle(charValueHandle, *it);
        if( nullptr != decl ) {
            return decl;
        }
    }
    return nullptr;
}

BTGattCharRef BTGattHandler::findCharacterisicsByValueHandle(const uint16_t charValueHandle, BTGattServiceRef service) noexcept {
    for(auto it = service->characteristicList.begin(); it != service->characteristicList.end(); it++) {
        BTGattCharRef decl = *it;
        if( charValueHandle == decl->value_handle ) {
            return decl;
        }
    }
    return nullptr;
}

jau::darray<BTGattServiceRef> & BTGattHandler::discoverCompletePrimaryServices(std::shared_ptr<BTGattHandler> shared_this) {
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    if( !discoverPrimaryServices(shared_this, services) ) {
        return services;
    }
    for(auto it = services.begin(); it != services.end(); it++) {
        BTGattServiceRef primSrv = *it;
        if( discoverCharacteristics(primSrv) ) {
            discoverDescriptors(primSrv);
        }
    }
    genericAccess = getGenericAccess(services);
    return services;
}

bool BTGattHandler::discoverPrimaryServices(std::shared_ptr<BTGattHandler> shared_this, jau::darray<BTGattServiceRef> & result) {
    {
        // validate shared_this first!
        BTGattHandler *given_this = shared_this.get();
        if( given_this != this ) {
            throw jau::IllegalArgumentException("Given shared GATTHandler reference "+
                    jau::to_hexstring(given_this)+" not matching this "+jau::to_hexstring(this), E_FILE_LINE);
        }
    }
    /***
     * BT Core Spec v5.2: Vol 3, Part G GATT: 4.4.1 Discover All Primary Services
     *
     * This sub-procedure is complete when the ATT_ERROR_RSP PDU is received
     * and the error code is set to Attribute Not Found or when the End Group Handle
     * in the Read by Type Group Response is 0xFFFF.
     */
    const jau::uuid16_t groupType = jau::uuid16_t(GattAttributeType::PRIMARY_SERVICE);
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    PERF_TS_T0();

    bool done=false;
    uint16_t startHandle=0x0001;
    result.clear();
    while(!done) {
        const AttReadByNTypeReq req(true /* group */, startHandle, 0xffff, groupType);
        COND_PRINT(env.DEBUG_DATA, "GATT PRIM SRV discover send: %s to %s", req.toString().c_str(), toString().c_str());

        std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, env.GATT_READ_COMMAND_REPLY_TIMEOUT); // valid reply or exception
        COND_PRINT(env.DEBUG_DATA, "GATT PRIM SRV discover recv: %s on %s", pdu->toString().c_str(), toString().c_str());

        if( pdu->getOpcode() == AttPDUMsg::Opcode::READ_BY_GROUP_TYPE_RSP ) {
            const AttReadByGroupTypeRsp * p = static_cast<const AttReadByGroupTypeRsp*>(pdu.get());
            const int esz = p->getElementSize();
            const int count = p->getElementCount();

            for(int i=0; i<count; i++) {
                const int ePDUOffset = p->getElementPDUOffset(i);
                result.push_back( BTGattServiceRef( new BTGattService( shared_this, true,
                        p->pdu.get_uint16(ePDUOffset), // start-handle
                        p->pdu.get_uint16(ePDUOffset + 2), // end-handle
                        p->pdu.get_uuid( ePDUOffset + 2 + 2, jau::uuid_t::toTypeSize(esz-2-2) ) // uuid
                    ) ) );
                COND_PRINT(env.DEBUG_DATA, "GATT PRIM SRV discovered[%d/%d]: %s on %s", i,
                        count, result.at(result.size()-1)->toString().c_str(), toString().c_str());
            }
            startHandle = p->getElementEndHandle(count-1);
            if( startHandle < 0xffff ) {
                startHandle++;
            } else {
                done = true; // OK by spec: End of communication
            }
        } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
            done = true; // OK by spec: End of communication
        } else {
            ERR_PRINT("GATT discoverPrimary unexpected reply %s, req %s from %s",
                    pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
            done = true;
        }
    }
    PERF_TS_TD("GATT discoverPrimaryServices");

    return result.size() > 0;
}

bool BTGattHandler::discoverCharacteristics(BTGattServiceRef & service) {
    /***
     * BT Core Spec v5.2: Vol 3, Part G GATT: 4.6.1 Discover All Characteristics of a Service
     * <p>
     * BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.1 Characteristic Declaration Attribute Value
     * </p>
     * <p>
     * BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.3.3 Client Characteristic Configuration
     * </p>
     */
    const jau::uuid16_t characteristicTypeReq = jau::uuid16_t(GattAttributeType::CHARACTERISTIC);
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    COND_PRINT(env.DEBUG_DATA, "GATT discoverCharacteristics Service: %s on %s", service->toString().c_str(), toString().c_str());

    PERF_TS_T0();

    bool done=false;
    uint16_t handle=service->handle;
    service->characteristicList.clear();
    while(!done) {
        const AttReadByNTypeReq req(false /* group */, handle, service->end_handle, characteristicTypeReq);
        COND_PRINT(env.DEBUG_DATA, "GATT C discover send: %s to %s", req.toString().c_str(), toString().c_str());

        std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, env.GATT_READ_COMMAND_REPLY_TIMEOUT); // valid reply or exception
        COND_PRINT(env.DEBUG_DATA, "GATT C discover recv: %s from %s", pdu->toString().c_str(), toString().c_str());

        if( pdu->getOpcode() == AttPDUMsg::Opcode::READ_BY_TYPE_RSP ) {
            const AttReadByTypeRsp * p = static_cast<const AttReadByTypeRsp*>(pdu.get());
            const int esz = p->getElementSize();
            const int e_count = p->getElementCount();

            for(int e_iter=0; e_iter<e_count; e_iter++) {
                // handle: handle for the Characteristics declaration
                // value: Characteristics Property, Characteristics Value Handle _and_ Characteristics UUID
                const int ePDUOffset = p->getElementPDUOffset(e_iter);
                service->characteristicList.push_back( BTGattCharRef( new BTGattChar(
                    service,
                    p->getElementHandle(e_iter), // Characteristic Handle
                    static_cast<BTGattChar::PropertyBitVal>(p->pdu.get_uint8(ePDUOffset  + 2)), // Characteristics Property
                    p->pdu.get_uint16(ePDUOffset + 2 + 1), // Characteristics Value Handle
                    p->pdu.get_uuid(ePDUOffset   + 2 + 1 + 2, jau::uuid_t::toTypeSize(esz-2-1-2) ) ) ) ); // Characteristics Value Type UUID
                COND_PRINT(env.DEBUG_DATA, "GATT C discovered[%d/%d]: char%s on %s", e_iter, e_count,
                        service->characteristicList.at(service->characteristicList.size()-1)->toString().c_str(), toString().c_str());
            }
            handle = p->getElementHandle(e_count-1); // Last Characteristic Handle
            if( handle < service->end_handle ) {
                handle++;
            } else {
                done = true; // OK by spec: End of communication
            }
        } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
            done = true; // OK by spec: End of communication
        } else {
            ERR_PRINT("GATT discoverCharacteristics unexpected reply %s, req %s within service%s from %s",
                    pdu->toString().c_str(), req.toString().c_str(), service->toString().c_str(), toString().c_str());
            done = true;
        }
    }

    PERF_TS_TD("GATT discoverCharacteristics");

    return service->characteristicList.size() > 0;
}

bool BTGattHandler::discoverDescriptors(BTGattServiceRef & service) {
    /***
     * BT Core Spec v5.2: Vol 3, Part G GATT: 4.7.1 Discover All Characteristic Descriptors
     * <p>
     * BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.1 Characteristic Declaration Attribute Value
     * </p>
     */
    COND_PRINT(env.DEBUG_DATA, "GATT discoverDescriptors Service: %s on %s", service->toString().c_str(), toString().c_str());
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    PERF_TS_T0();

    const int charCount = service->characteristicList.size();
    for(int charIter=0; charIter < charCount; charIter++ ) {
        BTGattCharRef charDecl = service->characteristicList[charIter];
        charDecl->clearDescriptors();
        COND_PRINT(env.DEBUG_DATA, "GATT discoverDescriptors Characteristic[%d/%d]: %s on %s", charIter, charCount, charDecl->toString().c_str(), toString().c_str());

        uint16_t cd_handle_iter = charDecl->value_handle + 1; // Start @ Characteristic Value Handle + 1
        uint16_t cd_handle_end;
        if( charIter+1 < charCount ) {
            cd_handle_end = service->characteristicList.at(charIter+1)->handle - 1; // // Next Characteristic Handle (excluding)
        } else {
            cd_handle_end = service->end_handle; // End of service handle (including)
        }

        bool done=false;

        while( !done && cd_handle_iter <= cd_handle_end ) {
            const AttFindInfoReq req(cd_handle_iter, cd_handle_end);
            COND_PRINT(env.DEBUG_DATA, "GATT CD discover send: %s", req.toString().c_str());

            std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, env.GATT_READ_COMMAND_REPLY_TIMEOUT); // valid reply or exception
            COND_PRINT(env.DEBUG_DATA, "GATT CD discover recv: %s from ", pdu->toString().c_str(), toString().c_str());

            if( pdu->getOpcode() == AttPDUMsg::Opcode::FIND_INFORMATION_RSP ) {
                const AttFindInfoRsp * p = static_cast<const AttFindInfoRsp*>(pdu.get());
                const int e_count = p->getElementCount();

                for(int e_iter=0; e_iter<e_count; e_iter++) {
                    // handle: handle of Characteristic Descriptor.
                    // value: Characteristic Descriptor UUID.
                    const uint16_t cd_handle = p->getElementHandle(e_iter);
                    std::unique_ptr<const jau::uuid_t> cd_uuid = p->getElementValue(e_iter);

                    std::shared_ptr<BTGattDesc> cd( std::make_shared<BTGattDesc>(charDecl, std::move(cd_uuid), cd_handle) );
                    if( cd_handle <= charDecl->value_handle || cd_handle > cd_handle_end ) { // should never happen!
                        ERR_PRINT("GATT discoverDescriptors CD handle %s not in range ]%s..%s]: descr%s within char%s on %s",
                                jau::to_hexstring(cd_handle).c_str(),
                                jau::to_hexstring(charDecl->value_handle).c_str(), jau::to_hexstring(cd_handle_end).c_str(),
                                cd->toString().c_str(), charDecl->toString().c_str(), toString().c_str());
                        done = true;
                        break;

                    }
                    if( !readDescriptorValue(*cd, 0) ) {
                        WORDY_PRINT("GATT discoverDescriptors readDescriptorValue failed: req %s, descr%s within char%s on %s",
                                   req.toString().c_str(), cd->toString().c_str(), charDecl->toString().c_str(), toString().c_str());
                        done = true;
                        break;
                    }
                    if( cd->isClientCharConfig() ) {
                        charDecl->clientCharConfigIndex = charDecl->descriptorList.size();
                    } else if( cd->isUserDescription() ) {
                        charDecl->userDescriptionIndex = charDecl->descriptorList.size();
                    }
                    charDecl->descriptorList.push_back(cd);
                    COND_PRINT(env.DEBUG_DATA, "GATT CD discovered[%d/%d]: %s", e_iter, e_count, cd->toString().c_str());
                }
                cd_handle_iter = p->getElementHandle(e_count-1); // Last Descriptor Handle
                if( cd_handle_iter < cd_handle_end ) {
                    cd_handle_iter++;
                } else {
                    done = true; // OK by spec: End of communication
                }
            } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
                done = true; // OK by spec: End of communication
            } else {
                ERR_PRINT("GATT discoverDescriptors unexpected reply %s; req %s within char%s from %s",
                        pdu->toString().c_str(), req.toString().c_str(), charDecl->toString().c_str(), toString().c_str());
                done = true;
            }
        }
    }
    PERF_TS_TD("GATT discoverDescriptors");

    return service->characteristicList.size() > 0;
}

bool BTGattHandler::readDescriptorValue(BTGattDesc & desc, int expectedLength) {
    COND_PRINT(env.DEBUG_DATA, "GATTHandler::readDescriptorValue expLen %d, desc %s", expectedLength, desc.toString().c_str());
    const bool res = readValue(desc.handle, desc.value, expectedLength);
    if( !res ) {
        WORDY_PRINT("GATT readDescriptorValue error on desc%s within char%s from %s",
                   desc.toString().c_str(), desc.getGattCharChecked()->toString().c_str(), toString().c_str());
    }
    return res;
}

bool BTGattHandler::readCharacteristicValue(const BTGattChar & decl, jau::POctets & resValue, int expectedLength) {
    COND_PRINT(env.DEBUG_DATA, "GATTHandler::readCharacteristicValue expLen %d, decl %s", expectedLength, decl.toString().c_str());
    const bool res = readValue(decl.value_handle, resValue, expectedLength);
    if( !res ) {
        WORDY_PRINT("GATT readCharacteristicValue error on char%s from %s", decl.toString().c_str(), toString().c_str());
    }
    return res;
}

bool BTGattHandler::readValue(const uint16_t handle, jau::POctets & res, int expectedLength) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.1 Read Characteristic Value */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.3 Read Long Characteristic Value */
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    PERF2_TS_T0();

    bool done=false;
    int offset=0;

    COND_PRINT(env.DEBUG_DATA, "GATTHandler::readValue expLen %d, handle %s from %s", expectedLength, jau::to_hexstring(handle).c_str(), toString().c_str());

    while(!done) {
        if( 0 < expectedLength && expectedLength <= offset ) {
            break; // done
        } else if( 0 == expectedLength && 0 < offset ) {
            break; // done w/ only one request
        } // else 0 > expectedLength: implicit

        std::unique_ptr<const AttPDUMsg> pdu = nullptr;

        const AttReadReq req0(handle);
        const AttReadBlobReq req1(handle, offset);
        const AttPDUMsg & req = ( 0 == offset ) ? static_cast<const AttPDUMsg &>(req0) : static_cast<const AttPDUMsg &>(req1);
        COND_PRINT(env.DEBUG_DATA, "GATT RV send: %s", req.toString().c_str());
        pdu = sendWithReply(req, env.GATT_READ_COMMAND_REPLY_TIMEOUT); // valid reply or exception

        COND_PRINT(env.DEBUG_DATA, "GATT RV recv: %s from %s", pdu->toString().c_str(), toString().c_str());
        if( pdu->getOpcode() == AttPDUMsg::Opcode::READ_RSP ) {
            const AttReadNRsp * p = static_cast<const AttReadNRsp*>(pdu.get());
            const jau::TOctetSlice & v = p->getValue();
            res += v;
            offset += v.size();
            if( p->getPDUValueSize() < p->getMaxPDUValueSize(usedMTU) ) {
                done = true; // No full ATT_MTU PDU used - end of communication
            }
        } else if( pdu->getOpcode() == AttPDUMsg::Opcode::READ_BLOB_RSP ) {
            const AttReadNRsp * p = static_cast<const AttReadNRsp*>(pdu.get());
            const jau::TOctetSlice & v = p->getValue();
            if( 0 == v.size() ) {
                done = true; // OK by spec: No more data - end of communication
            } else {
                res += v;
                offset += v.size();
                if( p->getPDUValueSize() < p->getMaxPDUValueSize(usedMTU) ) {
                    done = true; // No full ATT_MTU PDU used - end of communication
                }
            }
        } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
            /**
             * BT Core Spec v5.2: Vol 3, Part G GATT: 4.8.3 Read Long Characteristic Value
             *
             * If the Characteristic Value is not longer than (ATT_MTU – 1)
             * an ATT_ERROR_RSP PDU with the error
             * code set to Attribute Not Long shall be received on the first
             * ATT_READ_BLOB_REQ PDU.
             */
            const AttErrorRsp * p = static_cast<const AttErrorRsp *>(pdu.get());
            if( AttErrorRsp::ErrorCode::ATTRIBUTE_NOT_LONG == p->getErrorCode() ) {
                done = true; // OK by spec: No more data - end of communication
            } else {
                WORDY_PRINT("GATT readValue unexpected error %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
                done = true;
            }
        } else {
            ERR_PRINT("GATT readValue unexpected reply %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
            done = true;
        }
    }
    PERF2_TS_TD("GATT readValue");

    return offset > 0;
}

bool BTGattHandler::writeDescriptorValue(const BTGattDesc & cd) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.3.3 Client Characteristic Configuration */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.3 Write Characteristic Value */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.11 Characteristic Value Indication */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.12.3 Write Characteristic Descriptor */
    COND_PRINT(env.DEBUG_DATA, "GATTHandler::writeDesccriptorValue desc %s", cd.toString().c_str());
    const bool res = writeValue(cd.handle, cd.value, true);
    if( !res ) {
        WORDY_PRINT("GATT writeDescriptorValue error on desc%s within char%s from %s",
                   cd.toString().c_str(), cd.getGattCharChecked()->toString().c_str(), toString().c_str());
    }
    return res;
}

bool BTGattHandler::writeCharacteristicValue(const BTGattChar & c, const jau::TROOctets & value) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.3 Write Characteristic Value */
    COND_PRINT(env.DEBUG_DATA, "GATTHandler::writeCharacteristicValue desc %s, value %s", c.toString().c_str(), value.toString().c_str());
    const bool res = writeValue(c.value_handle, value, true);
    if( !res ) {
        WORDY_PRINT("GATT writeCharacteristicValue error on char%s from %s", c.toString().c_str(), toString().c_str());
    }
    return res;
}

bool BTGattHandler::writeCharacteristicValueNoResp(const BTGattChar & c, const jau::TROOctets & value) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.1 Write Characteristic Value Without Response */
    COND_PRINT(env.DEBUG_DATA, "GATT writeCharacteristicValueNoResp decl %s, value %s", c.toString().c_str(), value.toString().c_str());
    return writeValue(c.value_handle, value, false); // complete or exception
}

bool BTGattHandler::writeValue(const uint16_t handle, const jau::TROOctets & value, const bool withResponse) {
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.3.3 Client Characteristic Configuration */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.9.3 Write Characteristic Value */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.11 Characteristic Value Indication */
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 4.12.3 Write Characteristic Descriptor */

    if( value.size() <= 0 ) {
        WARN_PRINT("GATT writeValue size <= 0, no-op: %s", value.toString().c_str());
        return false;
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor

    // FIXME TODO: Long Value if value.size() > ( ATT_MTU - 3 )
    PERF2_TS_T0();

    if( !withResponse ) {
        AttWriteCmd req(handle, value);
        COND_PRINT(env.DEBUG_DATA, "GATT WV send(resp %d): %s to %s", withResponse, req.toString().c_str(), toString().c_str());

        send( req ); // complete or exception
        PERF2_TS_TD("GATT writeValue (no-resp)");
        return true;
    }

    AttWriteReq req(handle, value);
    COND_PRINT(env.DEBUG_DATA, "GATT WV send(resp %d): %s to %s", withResponse, req.toString().c_str(), toString().c_str());

    bool res = false;
    std::unique_ptr<const AttPDUMsg> pdu = sendWithReply(req, env.GATT_WRITE_COMMAND_REPLY_TIMEOUT); // valid reply or exception
    COND_PRINT(env.DEBUG_DATA, "GATT WV recv: %s from %s", pdu->toString().c_str(), toString().c_str());

    if( pdu->getOpcode() == AttPDUMsg::Opcode::WRITE_RSP ) {
        // OK
        res = true;
    } else if( pdu->getOpcode() == AttPDUMsg::Opcode::ERROR_RSP ) {
        WORDY_PRINT("GATT writeValue unexpected error %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
    } else {
        ERR_PRINT("GATT writeValue unexpected reply %s; req %s from %s", pdu->toString().c_str(), req.toString().c_str(), toString().c_str());
    }
    PERF2_TS_TD("GATT writeValue (with-resp)");
    return res;
}

bool BTGattHandler::configNotificationIndication(BTGattDesc & cccd, const bool enableNotification, const bool enableIndication) {
    if( !cccd.isClientCharConfig() ) {
        throw jau::IllegalArgumentException("Not a ClientCharacteristicConfiguration: "+cccd.toString(), E_FILE_LINE);
    }
    /* BT Core Spec v5.2: Vol 3, Part G GATT: 3.3.3.3 Client Characteristic Configuration */
    const uint16_t ccc_value = enableNotification | ( enableIndication << 1 );
    COND_PRINT(env.DEBUG_DATA, "GATTHandler::configNotificationIndication decl %s, enableNotification %d, enableIndication %d",
            cccd.toString().c_str(), enableNotification, enableIndication);
    cccd.value.resize(2, 2);
    cccd.value.put_uint16_nc(0, ccc_value);
    try {
        return writeDescriptorValue(cccd);
    } catch (BTException & bte) {
        if( !enableNotification && !enableIndication ) {
            // OK to have lost connection @ disable
            WORDY_PRINT("GATTHandler::configNotificationIndication(disable) on %s caught exception: %s", toString().c_str(), bte.what());
            return false;
        } else {
            throw; // re-throw current exception
        }
    }
}

/*********************************************************************************************************************/
/*********************************************************************************************************************/
/*********************************************************************************************************************/

static const jau::uuid16_t _GENERIC_ACCESS(GattServiceType::GENERIC_ACCESS);
static const jau::uuid16_t _DEVICE_NAME(GattCharacteristicType::DEVICE_NAME);
static const jau::uuid16_t _APPEARANCE(GattCharacteristicType::APPEARANCE);
static const jau::uuid16_t _PERIPHERAL_PREFERRED_CONNECTION_PARAMETERS(GattCharacteristicType::PERIPHERAL_PREFERRED_CONNECTION_PARAMETERS);

static const jau::uuid16_t _DEVICE_INFORMATION(GattServiceType::DEVICE_INFORMATION);
static const jau::uuid16_t _SYSTEM_ID(GattCharacteristicType::SYSTEM_ID);
static const jau::uuid16_t _MODEL_NUMBER_STRING(GattCharacteristicType::MODEL_NUMBER_STRING);
static const jau::uuid16_t _SERIAL_NUMBER_STRING(GattCharacteristicType::SERIAL_NUMBER_STRING);
static const jau::uuid16_t _FIRMWARE_REVISION_STRING(GattCharacteristicType::FIRMWARE_REVISION_STRING);
static const jau::uuid16_t _HARDWARE_REVISION_STRING(GattCharacteristicType::HARDWARE_REVISION_STRING);
static const jau::uuid16_t _SOFTWARE_REVISION_STRING(GattCharacteristicType::SOFTWARE_REVISION_STRING);
static const jau::uuid16_t _MANUFACTURER_NAME_STRING(GattCharacteristicType::MANUFACTURER_NAME_STRING);
static const jau::uuid16_t _REGULATORY_CERT_DATA_LIST(GattCharacteristicType::REGULATORY_CERT_DATA_LIST);
static const jau::uuid16_t _PNP_ID(GattCharacteristicType::PNP_ID);

std::shared_ptr<GattGenericAccessSvc> BTGattHandler::getGenericAccess(jau::darray<BTGattCharRef> & genericAccessCharDeclList) {
    std::shared_ptr<GattGenericAccessSvc> res = nullptr;
    jau::POctets value(number(Defaults::MAX_ATT_MTU), 0, jau::endian::little);
    std::string deviceName = "";
    AppearanceCat appearance = AppearanceCat::UNKNOWN;
    std::shared_ptr<GattPeriphalPreferredConnectionParameters> prefConnParam = nullptr;

    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor

    for(size_t i=0; i<genericAccessCharDeclList.size(); i++) {
        const BTGattChar & charDecl = *genericAccessCharDeclList.at(i);
        std::shared_ptr<BTGattService> service = charDecl.getServiceUnchecked();
        if( nullptr == service || _GENERIC_ACCESS != *(service->type) ) {
        	continue;
        }
        if( _DEVICE_NAME == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
            	deviceName = GattNameToString(value); // mandatory
            }
        } else if( _APPEARANCE == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) && value.size() >= 2 ) {
            	appearance = static_cast<AppearanceCat>(value.get_uint16(0)); // mandatory
            }
        } else if( _PERIPHERAL_PREFERRED_CONNECTION_PARAMETERS == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
            	prefConnParam = GattPeriphalPreferredConnectionParameters::get(value); // optional
            }
        }
    }
    if( deviceName.size() > 0 ) {
    	res = std::make_shared<GattGenericAccessSvc>(deviceName, appearance, prefConnParam);
    }
    return res;
}

std::shared_ptr<GattGenericAccessSvc> BTGattHandler::getGenericAccess(jau::darray<BTGattServiceRef> & primServices) {
	for(size_t i=0; i<primServices.size(); i++) {
	    BTGattServiceRef service = primServices.at(i);
	    if( _GENERIC_ACCESS == *service->type ) {
	        return getGenericAccess(primServices.at(i)->characteristicList);
	    }
	}
	return nullptr;
}

bool BTGattHandler::ping() {
    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor
    bool isOK = true;

    for(size_t i=0; isOK && i<services.size(); i++) {
        jau::darray<BTGattCharRef> & genericAccessCharDeclList = services.at(i)->characteristicList;
        jau::POctets value(32, 0, jau::endian::little);

        for(size_t j=0; isOK && j<genericAccessCharDeclList.size(); j++) {
            const BTGattChar & charDecl = *genericAccessCharDeclList.at(j);
            std::shared_ptr<BTGattService> service = charDecl.getServiceUnchecked();
            if( nullptr == service || _GENERIC_ACCESS != *(service->type) ) {
                continue;
            }
            if( _APPEARANCE == *charDecl.value_type ) {
                if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                    return true; // unique success case
                }
                // read failure, but not disconnected as no exception thrown from sendWithReply
                isOK = false;
            }
        }
    }
    if( isOK ) {
        jau::INFO_PRINT("GATTHandler::pingGATT: No GENERIC_ACCESS Service with APPEARANCE Characteristic available -> disconnect");
    } else {
        jau::INFO_PRINT("GATTHandler::pingGATT: Read error -> disconnect");
    }
    disconnect(true /* disconnectDevice */, true /* ioErrorCause */); // state -> Disconnected
    return false;
}

std::shared_ptr<GattDeviceInformationSvc> BTGattHandler::getDeviceInformation(jau::darray<BTGattCharRef> & characteristicDeclList) {
    std::shared_ptr<GattDeviceInformationSvc> res = nullptr;
    jau::POctets value(number(Defaults::MAX_ATT_MTU), 0, jau::endian::little);

    jau::POctets systemID(8, 0, jau::endian::little);
    std::string modelNumber;
    std::string serialNumber;
    std::string firmwareRevision;
    std::string hardwareRevision;
    std::string softwareRevision;
    std::string manufacturer;
    jau::POctets regulatoryCertDataList(128, 0, jau::endian::little);
    std::shared_ptr<GattPnP_ID> pnpID = nullptr;
    bool found = false;

    const std::lock_guard<std::recursive_mutex> lock(mtx_command); // RAII-style acquire and relinquish via destructor

    for(size_t i=0; i<characteristicDeclList.size(); i++) {
        const BTGattChar & charDecl = *characteristicDeclList.at(i);
        std::shared_ptr<BTGattService> service = charDecl.getServiceUnchecked();
        if( nullptr == service || _DEVICE_INFORMATION != *(service->type) ) {
            continue;
        }
        found = true;
        if( _SYSTEM_ID == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, systemID.resize(0)) ) {
                // nop
            }
        } else if( _REGULATORY_CERT_DATA_LIST == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, regulatoryCertDataList.resize(0)) ) {
                // nop
            }
        } else if( _PNP_ID == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                pnpID = GattPnP_ID::get(value);
            }
        } else if( _MODEL_NUMBER_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                modelNumber = GattNameToString(value);
            }
        } else if( _SERIAL_NUMBER_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                serialNumber = GattNameToString(value);
            }
        } else if( _FIRMWARE_REVISION_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                firmwareRevision = GattNameToString(value);
            }
        } else if( _HARDWARE_REVISION_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                hardwareRevision = GattNameToString(value);
            }
        } else if( _SOFTWARE_REVISION_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                softwareRevision = GattNameToString(value);
            }
        } else if( _MANUFACTURER_NAME_STRING == *charDecl.value_type ) {
            if( readCharacteristicValue(charDecl, value.resize(0)) ) {
                manufacturer = GattNameToString(value);
            }
        }
    }
    if( found ) {
        res = std::make_shared<GattDeviceInformationSvc>(systemID, modelNumber, serialNumber,
                                          firmwareRevision, hardwareRevision, softwareRevision,
                                          manufacturer, regulatoryCertDataList, pnpID);
    }
    return res;
}

std::shared_ptr<GattDeviceInformationSvc> BTGattHandler::getDeviceInformation(jau::darray<BTGattServiceRef> & primServices) {
    for(size_t i=0; i<primServices.size(); i++) {
        BTGattServiceRef service = primServices.at(i);
        if( _DEVICE_INFORMATION == *service->type ) {
            return getDeviceInformation(primServices.at(i)->characteristicList);
        }
    }
    return nullptr;
}

std::string BTGattHandler::toString() const noexcept {
    return "GattHndlr["+to_string(getRole())+", "+std::to_string(characteristicListenerList.size())+" listener, "+deviceString+", "+getStateString()+"]";
}

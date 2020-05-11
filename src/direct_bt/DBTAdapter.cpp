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

#include <algorithm>

#define VERBOSE_ON 1
#include <dbt_debug.hpp>

#include "BasicAlgos.hpp"

#include "BTIoctl.hpp"
#include "HCIIoctl.hpp"
#include "HCIComm.hpp"

#include "DBTAdapter.hpp"

extern "C" {
    #include <inttypes.h>
    #include <unistd.h>
    #include <poll.h>
}

using namespace direct_bt;

// *************************************************
// *************************************************
// *************************************************

std::atomic_int HCISession::name_counter(0);

HCISession::HCISession(DBTAdapter &a, const uint16_t channel, const int timeoutMS)
: adapter(&a), hciComm(a.dev_id, channel, timeoutMS),
  name(name_counter.fetch_add(1))
{}

bool HCISession::connected(std::shared_ptr<DBTDevice> & device) {
    const std::lock_guard<std::recursive_mutex> lock(mtx_connectedDevices); // RAII-style acquire and relinquish via destructor
    for (auto it = connectedDevices.begin(); it != connectedDevices.end(); ++it) {
        if ( *device == **it ) {
            return false; // already connected
        }
    }
    connectedDevices.push_back(device);
    return true;
}

bool HCISession::disconnected(std::shared_ptr<DBTDevice> & device) {
    const std::lock_guard<std::recursive_mutex> lock(mtx_connectedDevices); // RAII-style acquire and relinquish via destructor
    for (auto it = connectedDevices.begin(); it != connectedDevices.end(); ) {
        if ( **it == *device ) {
            it = connectedDevices.erase(it);
            return true;
        } else {
            ++it;
        }
    }
    return false;
}

int HCISession::disconnectAllDevices(const uint8_t reason) {
    int count = 0;
    std::vector<std::shared_ptr<DBTDevice>> devices(connectedDevices); // copy!
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        (*it)->disconnect(reason); // will erase device from list via disconnected above
        ++count;
    }
    return count;
}

std::shared_ptr<DBTDevice> HCISession::findConnectedDevice (EUI48 const & mac) const {
    std::vector<std::shared_ptr<DBTDevice>> devices(connectedDevices); // copy!
    for (auto it = devices.begin(); it != devices.end(); ) {
        if ( mac == (*it)->getAddress() ) {
            return *it;
        } else {
            ++it;
        }
    }
    return nullptr;
}

bool HCISession::close() 
{
    DBG_PRINT("HCISession::close: ...");
    if( nullptr == adapter ) {
        throw InternalError("HCISession::close(): Adapter reference is null: "+toString(), E_FILE_LINE);
    }
    if( !hciComm.isOpen() ) {
        DBG_PRINT("HCISession::close: Not open");
        return false;
    }
    disconnectAllDevices();
    adapter->sessionClosing();
    hciComm.close();
    DBG_PRINT("HCISession::close: XXX");
    return true;
}

void HCISession::shutdown() {
    DBG_PRINT("HCISession::shutdown(has-adapter %d): ...", nullptr!=adapter);

    // close()
    if( !hciComm.isOpen() ) {
        DBG_PRINT("HCISession::shutdown: Not open");
    } else {
        disconnectAllDevices();
        hciComm.close();
    }

    DBG_PRINT("HCISession::shutdown(has-adapter %d): XXX", nullptr!=adapter);
    adapter=nullptr;
}

HCISession::~HCISession() {
    DBG_PRINT("HCISession::dtor ...");
    if( nullptr != adapter ) {
        close();
        adapter = nullptr;
    }
    DBG_PRINT("HCISession::dtor XXX");
}

std::string HCISession::toString() const {
    return "HCISession[name "+std::to_string(name)+
            ", open "+std::to_string(isOpen())+
            ", "+std::to_string(this->connectedDevices.size())+" connected LE devices]";
}

// *************************************************
// *************************************************
// *************************************************

bool DBTAdapter::validateDevInfo() {
    if( !mgmt.isOpen() || 0 > dev_id ) {
        return false;
    }

    adapterInfo = mgmt.getAdapterInfo(dev_id);
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::DISCOVERING, bindMemberFunc(this, &DBTAdapter::mgmtEvDeviceDiscoveringCB));
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::NEW_SETTINGS, bindMemberFunc(this, &DBTAdapter::mgmtEvNewSettingsCB));
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::LOCAL_NAME_CHANGED, bindMemberFunc(this, &DBTAdapter::mgmtEvLocalNameChangedCB));
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::DEVICE_CONNECTED, bindMemberFunc(this, &DBTAdapter::mgmtEvDeviceConnectedCB));
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::DEVICE_DISCONNECTED, bindMemberFunc(this, &DBTAdapter::mgmtEvDeviceDisconnectedCB));
    mgmt.addMgmtEventCallback(dev_id, MgmtEvent::Opcode::DEVICE_FOUND, bindMemberFunc(this, &DBTAdapter::mgmtEvDeviceFoundCB));

    return true;
}

void DBTAdapter::sessionClosing()
{
    DBG_PRINT("DBTAdapter::sessionClosing(own-session %d): ...", nullptr!=session);
    if( nullptr != session ) {
        // FIXME: stopDiscovery();
        session = nullptr;
    }
    DBG_PRINT("DBTAdapter::sessionClosing(own-session %d): XXX", nullptr!=session);
}

DBTAdapter::DBTAdapter()
: mgmt(DBTManager::get(BTMode::BT_MODE_LE)), dev_id(nullptr != mgmt.getDefaultAdapterInfo() ? 0 : -1)
{
    valid = validateDevInfo();
}

DBTAdapter::DBTAdapter(EUI48 &mac) 
: mgmt(DBTManager::get(BTMode::BT_MODE_LE)), dev_id(mgmt.findAdapterInfoIdx(mac))
{
    valid = validateDevInfo();
}

DBTAdapter::DBTAdapter(const int dev_id) 
: mgmt(DBTManager::get(BTMode::BT_MODE_LE)), dev_id(dev_id)
{
    valid = validateDevInfo();
}

DBTAdapter::~DBTAdapter() {
    DBG_PRINT("DBTAdapter::dtor: ... %s", toString().c_str());
    keepDiscoveringAlive = false;
    {
        int count;
        if( 6 != ( count = mgmt.removeMgmtEventCallback(dev_id) ) ) {
            ERR_PRINT("DBTAdapter removeMgmtEventCallback(DISCOVERING) not 6 but %d", count);
        }
    }
    statusListenerList.clear();

    removeDiscoveredDevices();

    if( nullptr != session ) {
        stopDiscovery();
        session->shutdown(); // force shutdown, not only via dtor as adapter EOL has been reached!
        session = nullptr;
    }
    DBG_PRINT("DBTAdapter::dtor: XXX");
}

std::shared_ptr<NameAndShortName> DBTAdapter::setLocalName(const std::string &name, const std::string &short_name) {
    return mgmt.setLocalName(dev_id, name, short_name);
}

void DBTAdapter::setPowered(bool value) {
    mgmt.setMode(dev_id, MgmtOpcode::SET_POWERED, value ? 1 : 0);
}

void DBTAdapter::setDiscoverable(bool value) {
    mgmt.setMode(dev_id, MgmtOpcode::SET_DISCOVERABLE, value ? 1 : 0);
}

void DBTAdapter::setBondable(bool value) {
    mgmt.setMode(dev_id, MgmtOpcode::SET_BONDABLE, value ? 1 : 0);
}

std::shared_ptr<HCISession> DBTAdapter::open() 
{
    if( !valid ) {
        return nullptr;
    }
    HCISession * s = new HCISession( *this, HCI_CHANNEL_RAW, HCIDefaults::HCI_TO_SEND_REQ_POLL_MS);
    if( !s->isOpen() ) {
        delete s;
        ERR_PRINT("Could not open device");
        return nullptr;
    }
    session = std::shared_ptr<HCISession>( s );
    return session;
}

bool DBTAdapter::addStatusListener(std::shared_ptr<DBTAdapterStatusListener> l) {
    if( nullptr == l ) {
        throw IllegalArgumentException("DBTAdapterStatusListener ref is null", E_FILE_LINE);
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_statusListenerList); // RAII-style acquire and relinquish via destructor
    for(auto it = statusListenerList.begin(); it != statusListenerList.end(); ) {
        if ( **it == *l ) {
            return false; // already included
        } else {
            ++it;
        }
    }
    statusListenerList.push_back(l);
    return true;
}

bool DBTAdapter::removeStatusListener(std::shared_ptr<DBTAdapterStatusListener> l) {
    if( nullptr == l ) {
        throw IllegalArgumentException("DBTAdapterStatusListener ref is null", E_FILE_LINE);
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_statusListenerList); // RAII-style acquire and relinquish via destructor
    for(auto it = statusListenerList.begin(); it != statusListenerList.end(); ) {
        if ( **it == *l ) {
            it = statusListenerList.erase(it);
            return true;
        } else {
            ++it;
        }
    }
    return false;
}

bool DBTAdapter::removeStatusListener(const DBTAdapterStatusListener * l) {
    if( nullptr == l ) {
        throw IllegalArgumentException("DBTAdapterStatusListener ref is null", E_FILE_LINE);
    }
    const std::lock_guard<std::recursive_mutex> lock(mtx_statusListenerList); // RAII-style acquire and relinquish via destructor
    for(auto it = statusListenerList.begin(); it != statusListenerList.end(); ) {
        if ( **it == *l ) {
            it = statusListenerList.erase(it);
            return true;
        } else {
            ++it;
        }
    }
    return false;
}

bool DBTAdapter::startDiscovery(HCIAddressType own_mac_type,
                                uint16_t interval, uint16_t window)
{
    (void)own_mac_type;
    (void)interval;
    (void)window;

    keepDiscoveringAlive = true;
    currentScanType = mgmt.startDiscovery(dev_id);
    return ScanType::SCAN_TYPE_NONE != currentScanType;
}

void DBTAdapter::startDiscoveryBackground() {
    currentScanType = mgmt.startDiscovery(dev_id);
}

void DBTAdapter::stopDiscovery() {
    DBG_PRINT("DBTAdapter::stopDiscovery: ...");
    keepDiscoveringAlive = false;
    if( mgmt.stopDiscovery(dev_id, currentScanType) ) {
        currentScanType = ScanType::SCAN_TYPE_NONE;
    }
    DBG_PRINT("DBTAdapter::stopDiscovery: X");
}

int DBTAdapter::findDevice(std::vector<std::shared_ptr<DBTDevice>> const & devices, EUI48 const & mac) {
    auto begin = devices.begin();
    auto it = std::find_if(begin, devices.end(), [&](std::shared_ptr<DBTDevice> const& p) {
        return p->address == mac;
    });
    if ( it == std::end(devices) ) {
        return -1;
    } else {
        return std::distance(begin, it);
    }
}

std::shared_ptr<DBTDevice> DBTAdapter::findDiscoveredDevice (EUI48 const & mac) const {
    const std::lock_guard<std::recursive_mutex> lock(const_cast<DBTAdapter*>(this)->mtx_discoveredDevices); // RAII-style acquire and relinquish via destructor
    auto begin = discoveredDevices.begin();
    auto it = std::find_if(begin, discoveredDevices.end(), [&](std::shared_ptr<DBTDevice> const& p) {
        return p->address == mac;
    });
    if ( it == std::end(discoveredDevices) ) {
        return nullptr;
    } else {
        return *it;
    }
}

bool DBTAdapter::addDiscoveredDevice(std::shared_ptr<DBTDevice> const &device) {
    const std::lock_guard<std::recursive_mutex> lock(mtx_discoveredDevices); // RAII-style acquire and relinquish via destructor
    for (auto it = discoveredDevices.begin(); it != discoveredDevices.end(); ++it) {
        if ( *device == **it ) {
            // already discovered
            return false;
        }
    }
    discoveredDevices.push_back(device);
    return true;
}

int DBTAdapter::removeDiscoveredDevices() {
    const std::lock_guard<std::recursive_mutex> lock(mtx_discoveredDevices); // RAII-style acquire and relinquish via destructor
    int res = discoveredDevices.size();
    discoveredDevices.clear();
    return res;
}

std::vector<std::shared_ptr<DBTDevice>> DBTAdapter::getDiscoveredDevices() const {
    const std::lock_guard<std::recursive_mutex> lock(const_cast<DBTAdapter*>(this)->mtx_discoveredDevices); // RAII-style acquire and relinquish via destructor
    std::vector<std::shared_ptr<DBTDevice>> res = discoveredDevices;
    return res;
}

std::string DBTAdapter::toString() const {
    std::string out("Adapter["+getAddressString()+", '"+getName()+"', id="+std::to_string(dev_id)+", "+javaObjectToString()+"]");
    std::vector<std::shared_ptr<DBTDevice>> devices = getDiscoveredDevices();
    if(devices.size() > 0 ) {
        out.append("\n");
        for(auto it = devices.begin(); it != devices.end(); it++) {
            std::shared_ptr<DBTDevice> p = *it;
            out.append("  ").append(p->toString()).append("\n");
        }
    }
    return out;
}

// *************************************************

bool DBTAdapter::mgmtEvDeviceDiscoveringCB(std::shared_ptr<MgmtEvent> e) {
    DBG_PRINT("DBTAdapter::EventCB:DeviceDiscovering(dev_id %d, keepDiscoveringAlive %d): %s",
        dev_id, keepDiscoveringAlive, e->toString().c_str());
    const MgmtEvtDiscovering &event = *static_cast<const MgmtEvtDiscovering *>(e.get());
    if( keepDiscoveringAlive && !event.getEnabled() ) {
        std::thread bg(&DBTAdapter::startDiscoveryBackground, this);
        bg.detach();
    }
    return true;
}

bool DBTAdapter::mgmtEvNewSettingsCB(std::shared_ptr<MgmtEvent> e) {
    DBG_PRINT("DBTAdapter::EventCB:NewSettings: %s", e->toString().c_str());
    const MgmtEvtNewSettings &event = *static_cast<const MgmtEvtNewSettings *>(e.get());
    AdapterSetting old_setting = adapterInfo->getCurrentSetting();
    AdapterSetting changes = adapterInfo->setCurrentSetting(event.getSettings());
    DBG_PRINT("DBTAdapter::EventCB:NewSettings: %s -> %s, changes %s",
            adapterSettingsToString(old_setting).c_str(),
            adapterSettingsToString(adapterInfo->getCurrentSetting()).c_str(),
            adapterSettingsToString(changes).c_str() );

    for_each_idx_mtx(mtx_statusListenerList, statusListenerList, [&](std::shared_ptr<DBTAdapterStatusListener> &l) {
        l->adapterSettingsChanged(*this, old_setting, adapterInfo->getCurrentSetting(), changes, event.getTimestamp());
    });

    return true;
}

bool DBTAdapter::mgmtEvLocalNameChangedCB(std::shared_ptr<MgmtEvent> e) {
    DBG_PRINT("DBTAdapter::EventCB:LocalNameChanged: %s", e->toString().c_str());
    const MgmtEvtLocalNameChanged &event = *static_cast<const MgmtEvtLocalNameChanged *>(e.get());
    std::string old_name = localName.getName();
    std::string old_shortName = localName.getShortName();
    bool nameChanged = old_name != event.getName();
    bool shortNameChanged = old_shortName != event.getShortName();
    if( nameChanged ) {
        localName.setName(event.getName());
    }
    if( shortNameChanged ) {
        localName.setShortName(event.getShortName());
    }
    DBG_PRINT("DBTAdapter::EventCB:LocalNameChanged: Local name: %d: '%s' -> '%s'; short_name: %d: '%s' -> '%s'",
            nameChanged, old_name.c_str(), localName.getName().c_str(),
            shortNameChanged, old_shortName.c_str(), localName.getShortName().c_str());
    (void)nameChanged;
    (void)shortNameChanged;
    return true;
}

void DBTAdapter::sendDeviceUpdated(std::shared_ptr<DBTDevice> device, uint64_t timestamp, EIRDataType updateMask) {
    for_each_idx_mtx(mtx_statusListenerList, statusListenerList, [&](std::shared_ptr<DBTAdapterStatusListener> &l) {
        l->deviceUpdated(*this, device, timestamp, updateMask);
    });
}

bool DBTAdapter::mgmtEvDeviceConnectedCB(std::shared_ptr<MgmtEvent> e) {
    const MgmtEvtDeviceConnected &event = *static_cast<const MgmtEvtDeviceConnected *>(e.get());
    if( nullptr == session ) {
        throw InternalError("NULL session @ receiving "+event.toString(), E_FILE_LINE);
    }
    EInfoReport ad_report;
    {
        ad_report.setSource(EInfoReport::Source::EIR);
        ad_report.setTimestamp(event.getTimestamp());
        ad_report.setAddressType(event.getAddressType());
        ad_report.setAddress( event.getAddress() );
        ad_report.read_data(event.getData(), event.getDataSize());
    }
    bool new_connect = false;
    std::shared_ptr<DBTDevice> device = session->findConnectedDevice(event.getAddress());
    if( nullptr == device ) {
        device = findDiscoveredDevice(event.getAddress());
        new_connect = true;
    }
    if( nullptr != device ) {
        EIRDataType updateMask = device->update(ad_report);
        DBG_PRINT("DBTAdapter::EventCB:DeviceConnected(dev_id %d, new_connect %d, updated %s): %s,\n    %s\n    -> %s",
            dev_id, new_connect, eirDataMaskToString(updateMask).c_str(), event.toString().c_str(), ad_report.toString().c_str(), device->toString().c_str());
        if( new_connect ) {
            session->connected(device); // track it
        }
        for_each_idx_mtx(mtx_statusListenerList, statusListenerList, [&](std::shared_ptr<DBTAdapterStatusListener> &l) {
            if( EIRDataType::NONE != updateMask ) {
                l->deviceUpdated(*this, device, ad_report.getTimestamp(), updateMask);
            }
            l->deviceConnected(*this, device, event.getTimestamp());
        });
    } else {
        DBG_PRINT("DBTAdapter::EventCB:DeviceConnected(dev_id %d): %s,\n    %s\n    -> Device not tracked nor discovered",
                dev_id, event.toString().c_str(), ad_report.toString().c_str());
    }
    return true;
}
bool DBTAdapter::mgmtEvDeviceDisconnectedCB(std::shared_ptr<MgmtEvent> e) {
    const MgmtEvtDeviceDisconnected &event = *static_cast<const MgmtEvtDeviceDisconnected *>(e.get());
    if( nullptr == session ) {
        throw InternalError("NULL session @ receiving "+event.toString(), E_FILE_LINE);
    }
    std::shared_ptr<DBTDevice> device = session->findConnectedDevice(event.getAddress());
    if( nullptr != device ) {
        DBG_PRINT("DBTAdapter::EventCB:DeviceDisconnected(dev_id %d): %s\n    -> %s",
            dev_id, event.toString().c_str(), device->toString().c_str());

        for_each_idx_mtx(mtx_statusListenerList, statusListenerList, [&](std::shared_ptr<DBTAdapterStatusListener> &l) {
            l->deviceDisconnected(*this, device, event.getTimestamp());
        });
    } else {
        DBG_PRINT("DBTAdapter::EventCB:DeviceDisconnected(dev_id %d): %s\n    -> Device not tracked",
            dev_id, event.toString().c_str());
    }
    return true;
}

bool DBTAdapter::mgmtEvDeviceFoundCB(std::shared_ptr<MgmtEvent> e) {
    DBG_PRINT("DBTAdapter::EventCB:DeviceFound(dev_id %d): %s", dev_id, e->toString().c_str());
    const MgmtEvtDeviceFound &deviceFoundEvent = *static_cast<const MgmtEvtDeviceFound *>(e.get());

    EInfoReport ad_report;
    ad_report.setSource(EInfoReport::Source::EIR);
    ad_report.setTimestamp(deviceFoundEvent.getTimestamp());
    // ad_report.setEvtType(0); ??
    ad_report.setAddressType(deviceFoundEvent.getAddressType());
    ad_report.setAddress( deviceFoundEvent.getAddress() );
    ad_report.setRSSI( deviceFoundEvent.getRSSI() );
    ad_report.read_data(deviceFoundEvent.getData(), deviceFoundEvent.getDataSize());

    std::shared_ptr<DBTDevice> dev = findDiscoveredDevice(ad_report.getAddress());
    if( nullptr == dev ) {
        // new device
        dev = std::shared_ptr<DBTDevice>(new DBTDevice(*this, ad_report));
        addDiscoveredDevice(dev);

        for_each_idx_mtx(mtx_statusListenerList, statusListenerList, [&](std::shared_ptr<DBTAdapterStatusListener> &l) {
            l->deviceFound(*this, dev, ad_report.getTimestamp());
        });

    } else {
        // existing device
        EIRDataType updateMask = dev->update(ad_report);
        if( EIRDataType::NONE != updateMask ) {
            sendDeviceUpdated(dev, ad_report.getTimestamp(), updateMask);
        }
    }
    return true;
}

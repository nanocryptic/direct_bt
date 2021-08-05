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

#include "BTDeviceRegistry.hpp"

#include <jau/cpp_lang_util.hpp>
#include <jau/basic_algos.hpp>
#include <jau/darray.hpp>

#include <unordered_set>
#include <unordered_map>

using namespace direct_bt;

namespace direct_bt::BTDeviceRegistry {
    static jau::darray<DeviceQuery> waitForDevices;

    static std::unordered_set<DeviceID> devicesInProcessing;
    static std::recursive_mutex mtx_devicesProcessing;

    static std::unordered_set<DeviceID> devicesProcessed;
    static std::recursive_mutex mtx_devicesProcessed;

    void addToWaitForDevices(const std::string& addrOrNameSub) {
        EUI48Sub addr1;
        std::string errmsg;
        if( EUI48Sub::scanEUI48Sub(addrOrNameSub, addr1, errmsg) ) {
            waitForDevices.emplace_back( addr1 );
        } else {
            addr1.clear();
            waitForDevices.emplace_back( addrOrNameSub );
        }
    }
    bool isWaitingForAnyDevice() {
        return !waitForDevices.empty();
    }
    size_t getWaitForDevicesCount() {
        return waitForDevices.size();
    }
    std::string getWaitForDevicesString() {
        std::string res;
        jau::for_each(waitForDevices.cbegin(), waitForDevices.cend(), [&res](const DeviceQuery &q) {
            if( res.length() > 0 ) {
                res.append( ", " );
            }
            res.append( q.toString() );
        });
        return res;
    }

    jau::darray<DeviceQuery>& getWaitForDevices() {
        return waitForDevices;
    }
    void clearWaitForDevices() {
        waitForDevices.clear();
    }

    void addToProcessedDevices(const BDAddressAndType &a, const std::string& n) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        devicesProcessed.emplace_hint(devicesProcessed.end(), a, n);
    }
    bool isDeviceProcessed(const BDAddressAndType & a) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        return devicesProcessed.end() != devicesProcessed.find( DeviceID(a, "") );
    }
    size_t getProcessedDeviceCount() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        return devicesProcessed.size();
    }
    std::string getProcessedDevicesString() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        std::string res;
        jau::for_each(devicesProcessed.cbegin(), devicesProcessed.cend(), [&res](const DeviceID &id) {
            if( res.length() > 0 ) {
                res.append( ", " );
            }
            res.append( id.toString() );
        });
        return res;
    }
    jau::darray<DeviceID> getProcessedDevices() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        // std::unordered_set<DeviceID>::iterator is not suitable for:
        // return jau::darray<DeviceID>(devicesProcessed.size(), devicesProcessed.begin(), devicesProcessed.end());
        jau::darray<DeviceID> res(devicesProcessed.size());
        auto first = devicesProcessed.cbegin();
        auto last = devicesProcessed.cend();
        for(; first != last; ++first) {
            res.push_back(*first);
        }
        return res;
    }
    void clearProcessedDevices() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        devicesProcessed.clear();
    }

    bool isWaitingForDevice(const EUI48 &address, const std::string &name, DeviceQueryMatchFunc m) {
        return waitForDevices.cend() != jau::find_if(waitForDevices.cbegin(), waitForDevices.cend(), [&](const DeviceQuery & it)->bool {
           return m(address, name, it);
        });
    }

    bool areAllDevicesProcessed(DeviceQueryMatchFunc m) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessed); // RAII-style acquire and relinquish via destructor
        for (auto it1 = waitForDevices.cbegin(); it1 != waitForDevices.cend(); ++it1) {
            const DeviceQuery& q = *it1;
            auto it2 = devicesProcessed.cbegin();
            while ( it2 != devicesProcessed.cend() ) {
                const DeviceID& id = *it2;
                if( m(id.addressAndType.address, id.name, q) ) {
                    break;
                }
                ++it2;
            }
            if( it2 == devicesProcessed.cend() ) {
                return false;
            }
        }
        return true;
    }

    void addToProcessingDevices(const BDAddressAndType &a, const std::string& n) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        devicesInProcessing.emplace_hint(devicesInProcessing.end(), a, n);
    }
    bool removeFromProcessingDevices(const BDAddressAndType &a) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        auto it = devicesInProcessing.find( DeviceID(a, "") );
        if( devicesInProcessing.end() != it ) {
            devicesInProcessing.erase(it);
            return true;
        }
        return false;
    }
    bool isDeviceProcessing(const BDAddressAndType & a) {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        return devicesInProcessing.end() != devicesInProcessing.find( DeviceID(a, "") );
    }
    size_t getProcessingDeviceCount() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        return devicesInProcessing.size();
    }
    jau::darray<DeviceID> getProcessingDevices() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        // std::unordered_set<DeviceID>::iterator is not suitable for:
        // return jau::darray<DeviceID>(devicesInProcessing.size(), devicesInProcessing.begin(), devicesInProcessing.end());
        jau::darray<DeviceID> res(devicesInProcessing.size());
        auto first = devicesInProcessing.cbegin();
        auto last = devicesInProcessing.cend();
        for(; first != last; ++first) {
            res.push_back(*first);
        }
        return res;
    }
    void clearProcessingDevices() {
        const std::lock_guard<std::recursive_mutex> lock(mtx_devicesProcessing); // RAII-style acquire and relinquish via destructor
        devicesInProcessing.clear();
    }

} // namespace direct_bt::BTDeviceRegistry

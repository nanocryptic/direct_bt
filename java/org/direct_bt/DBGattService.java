/**
 * Author: Sven Gothel <sgothel@jausoft.com>
 * Copyright (c) 2021 Gothel Software e.K.
 * Copyright (c) 2021 ZAFENA AB
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

package org.direct_bt;

import java.util.List;

/**
 * Representing a Gatt Service object from the ::GATTRole::Server perspective.
 *
 * BT Core Spec v5.2: Vol 3, Part G GATT: 3.1 Service Definition
 *
 * Includes a complete [Primary] Service Declaration
 * including its list of Characteristic Declarations,
 * which also may include its client config if available.
 *
 * @since 2.4.0
 */
public final class DBGattService
{
    private volatile long nativeInstance;
    /* pp */ long getNativeInstance() { return nativeInstance; }

    public static class UUID16 {
        /** This service contains generic information about the device. This is a mandatory service. */
        public static String GENERIC_ACCESS                              = "1800";
        /** The service allows receiving indications of changed services. This is a mandatory service. */
        public static String GENERIC_ATTRIBUTE                           = "1801";
        /** This service exposes a control point to change the peripheral alert behavior. */
        public static String IMMEDIATE_ALERT                             = "1802";
        /** The service defines behavior on the device when a link is lost between two devices. */
        public static String LINK_LOSS                                   = "1803";
        /** This service exposes temperature and other data from a thermometer intended for healthcare and fitness applications. */
        public static String HEALTH_THERMOMETER                          = "1809";
        /** This service exposes manufacturer and/or vendor information about a device. */
        public static String DEVICE_INFORMATION                          = "180A";
        /** This service exposes the state of a battery within a device. */
        public static String BATTERY_SERVICE                             = "180F";
    };
    /**
     * Indicate whether this service is a primary service.
     */
    public final boolean primary;

    /**
     * Service start handle
     * <p>
     * Attribute handles are unique for each device (server) (BT Core Spec v5.2: Vol 3, Part F Protocol..: 3.2.2 Attribute Handle).
     * </p>
     */
    public native short getHandle();

    /**
     * Service end handle, inclusive.
     * <p>
     * Attribute handles are unique for each device (server) (BT Core Spec v5.2: Vol 3, Part F Protocol..: 3.2.2 Attribute Handle).
     * </p>
     */
    public native short getEndHandle();

    private final String type;

    /** Service type UUID (lower-case) */
    public String getType() { return type; }

    /* pp */ final List<DBGattChar> characteristics;

    /** List of Characteristic Declarations. */
    public final List<DBGattChar> getCharacteristics() { return characteristics; }

    public DBGattService(final boolean primary_,
                         final String type_,
                         final List<DBGattChar> characteristics_)
    {
        primary = primary_;
        type = type_;
        characteristics = characteristics_;

        final long[] nativeCharacteristics = new long[characteristics_.size()];
        for(int i=0; i < nativeCharacteristics.length; i++) {
            nativeCharacteristics[i] = characteristics_.get(i).getNativeInstance();
        }
        nativeInstance = ctorImpl(primary_, type_, nativeCharacteristics);
    }
    private static native long ctorImpl(final boolean primary, final String type,
                                        final long[] characteristics);

    public DBGattChar findGattChar(final String char_uuid) {
        for(final DBGattChar c : characteristics) {
            if( char_uuid.equals( c.getValueType() ) ) {
                return c;
            }
        }
        return null;
    }
    public DBGattChar findGattCharByValueHandle(final short char_value_handle) {
        for(final DBGattChar c : characteristics) {
            if( char_value_handle == c.getValueHandle() ) {
                return c;
            }
        }
        return null;
    }

    @Override
    public boolean equals(final Object other) {
        if( this == other ) {
            return true;
        }
        if( !(other instanceof DBGattService) ) {
            return false;
        }
        final DBGattService o = (DBGattService)other;
        return getHandle() == o.getHandle() && getEndHandle() == o.getEndHandle(); /** unique attribute handles */
    }

    @Override
    public native String toString();
}

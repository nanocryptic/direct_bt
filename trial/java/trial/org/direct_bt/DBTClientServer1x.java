/**
 * Author: Sven Gothel <sgothel@jausoft.com>
 * Copyright (c) 2022 Gothel Software e.K.
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

package trial.org.direct_bt;

import java.lang.reflect.InvocationTargetException;
import java.util.Arrays;
import java.util.List;

import org.direct_bt.BTMode;
import org.direct_bt.BTSecurityLevel;
import org.direct_bt.AdapterStatusListener;
import org.direct_bt.BTAdapter;
import org.direct_bt.BTDevice;
import org.direct_bt.BTDeviceRegistry;
import org.direct_bt.BTException;
import org.direct_bt.BTFactory;
import org.direct_bt.BTManager;
import org.direct_bt.BTSecurityRegistry;
import org.direct_bt.BTUtils;
import org.direct_bt.DiscoveryPolicy;
import org.direct_bt.EIRDataTypeSet;
import org.direct_bt.EInfoReport;
import org.direct_bt.PairingMode;
import org.direct_bt.SMPKeyBin;
import org.jau.net.EUI48;
import org.junit.Assert;

/**
 * Testing a full Bluetooth server and client lifecycle of operations, requiring two BT adapter:
 * - start server advertising
 * - start client discovery and connect to server when discovered
 * - client/server processing of connection when ready
 * - client disconnect
 * - server stop advertising
 * - security-level: NONE, ENC_ONLY freshly-paired and ENC_ONLY pre-paired
 * - reuse server-adapter for client-mode discovery (just toggle on/off)
 */
public abstract class DBTClientServer1x extends BaseDBTClientServer {
    final Object mtx_sync = new Object();
    BTDevice lastCompletedDevice = null;
    PairingMode lastCompletedDevicePairingMode = PairingMode.NONE;
    BTSecurityLevel lastCompletedDeviceSecurityLevel = BTSecurityLevel.NONE;
    EInfoReport lastCompletedDeviceEIR = null;

    final void test8x_fullCycle(final long timeout_value,
                                final String suffix, final int protocolSessionCount, final boolean server_client_order,
                                final boolean serverSC, final BTSecurityLevel secLevelServer, final boolean serverShallHaveKeys,
                                final BTSecurityLevel secLevelClient, final boolean clientShallHaveKeys)
    {
        final long t0 = BTUtils.currentTimeMillis();

        BTManager manager = null;
        try {
            manager = BTFactory.getDirectBTManager();
        } catch (BTException | NoSuchMethodException | SecurityException
                | IllegalAccessException | IllegalArgumentException
                | InvocationTargetException | ClassNotFoundException e) {
            e.printStackTrace();
            Assert.assertNull("Unable to instantiate Direct-BT BluetoothManager: "+e.getMessage(), e);
        }
        if( null == manager ) {
            return;
        }
        {
            final List<BTAdapter> adapters = manager.getAdapters();
            BTUtils.println(System.err, "Adapter: Count "+adapters.size()+": "+adapters.toString());
            Assert.assertTrue("Adapter count not >= 2 but "+adapters.size(), adapters.size() >= 2);
        }

        final DBTServer00 server = new DBTServer00("S-"+suffix, EUI48.ALL_DEVICE, BTMode.DUAL, serverSC, secLevelServer);
        server.servingProtocolSessionsLeft.set(protocolSessionCount);

        final DBTClient00 client = new DBTClient00("C-"+suffix, EUI48.ALL_DEVICE, BTMode.DUAL);

        final DBTEndpoint.ChangedAdapterSetListener myChangedAdapterSetListener =
                DBTEndpoint.initChangedAdapterSetListener(manager, server_client_order ? Arrays.asList(server, client) : Arrays.asList(client, server));

        final String serverName = server.getName();
        BTDeviceRegistry.addToWaitForDevices( serverName );
        {
            final BTSecurityRegistry.Entry sec = BTSecurityRegistry.getOrCreate(serverName);
            sec.sec_level = secLevelClient;
        }
        client.KEEP_CONNECTED = false; // default, auto-disconnect after work is done
        client.REMOVE_DEVICE = false; // default, test side-effects
        client.measurementsLeft.set(protocolSessionCount);
        client.discoveryPolicy = DiscoveryPolicy.PAUSE_CONNECTED_UNTIL_DISCONNECTED;

        synchronized( mtx_sync ) {
            lastCompletedDevice = null;
            lastCompletedDevicePairingMode = PairingMode.NONE;
            lastCompletedDeviceSecurityLevel = BTSecurityLevel.NONE;
            lastCompletedDeviceEIR = null;
        }
        final AdapterStatusListener clientAdapterStatusListener = new AdapterStatusListener() {
            @Override
            public void deviceReady(final BTDevice device, final long timestamp) {
                synchronized( mtx_sync ) {
                    lastCompletedDevice = device;
                    lastCompletedDevicePairingMode = device.getPairingMode();
                    lastCompletedDeviceSecurityLevel = device.getConnSecurityLevel();
                    lastCompletedDeviceEIR = device.getEIR().clone();
                    BTUtils.println(System.err, "XXXXXX Client Ready: "+device);
                }
            }
        };
        Assert.assertTrue( client.getAdapter().addStatusListener(clientAdapterStatusListener) );

        //
        // Server start
        //
        DBTEndpoint.checkInitializedState(server);
        DBTServerTest.startAdvertising(server, false /* current_exp_advertising_state */, "test"+suffix+"_startAdvertising");

        //
        // Client start
        //
        DBTEndpoint.checkInitializedState(client);
        DBTClientTest.startDiscovery(client, false /* current_exp_discovering_state */, "test"+suffix+"_startDiscovery");

        long test_duration = 0;
        boolean done = false;
        boolean timeout = false;
        boolean max_connections_hit = false;
        do {
            synchronized( mtx_sync ) {
                done = ! ( protocolSessionCount > server.servedProtocolSessionsTotal.get() ||
                           protocolSessionCount > client.completedMeasurements.get() ||
                           null == lastCompletedDevice ||
                           lastCompletedDevice.getConnected() );
            }
            max_connections_hit = ( protocolSessionCount * DBTConstants.max_connections_per_session ) <= server.disconnectCount.get();
            test_duration = BTUtils.currentTimeMillis() - t0;
            timeout = 0 < timeout_value && timeout_value <= test_duration + 500; // let's timeout here before our timeout timer
            if( !done && !max_connections_hit && !timeout ) {
                try { Thread.sleep(88); } catch (final InterruptedException e) { e.printStackTrace(); }
            }
        } while( !done && !max_connections_hit && !timeout );
        test_duration = BTUtils.currentTimeMillis() - t0;

        BTUtils.fprintf_td(System.err, "\n\n");
        BTUtils.fprintf_td(System.err, "****** Test Stats: duration %d ms, timeout[hit %b, value %d ms], max_connections hit %b\n",
                test_duration, timeout, timeout_value, max_connections_hit);
        BTUtils.fprintf_td(System.err, "  Server ProtocolSessions[success %d/%d total, requested %d], disconnects %d of %d max\n",
                server.servedProtocolSessionsSuccess.get(), server.servedProtocolSessionsTotal.get(), protocolSessionCount,
                server.disconnectCount.get(), ( protocolSessionCount * DBTConstants.max_connections_per_session ));
        BTUtils.fprintf_td(System.err, "  Client ProtocolSessions success %d \n",
                client.completedMeasurements.get());
        BTUtils.fprintf_td(System.err, "\n\n");

        Assert.assertFalse( max_connections_hit );
        Assert.assertFalse( timeout );

        synchronized( mtx_sync ) {
            Assert.assertEquals(protocolSessionCount, server.servedProtocolSessionsTotal.get());
            Assert.assertEquals(protocolSessionCount, server.servedProtocolSessionsSuccess.get());
            Assert.assertEquals(protocolSessionCount, client.completedMeasurements.get());
            Assert.assertNotNull(lastCompletedDevice);
            Assert.assertNotNull(lastCompletedDeviceEIR);
            Assert.assertFalse(lastCompletedDevice.getConnected());
            Assert.assertTrue( ( 1 * DBTConstants.max_connections_per_session ) > server.disconnectCount.get() );
        }

        //
        // Client stop
        //
        DBTClientTest.stopDiscovery(client, true /* current_exp_discovering_state */, "test"+suffix+"_stopDiscovery");
        client.close("test"+suffix+"_close");

        //
        // Server stop
        //
        DBTServerTest.stop(server, false /* current_exp_advertising_state */, "test"+suffix+"_stopAdvertising");
        server.close("test"+suffix+"_close");

        //
        // Validating Security Mode
        //
        final SMPKeyBin clientKeys = SMPKeyBin.read(DBTConstants.CLIENT_KEY_PATH, lastCompletedDevice, true /* verbose */);
        Assert.assertTrue(clientKeys.isValid());
        final BTSecurityLevel clientKeysSecLevel = clientKeys.getSecLevel();
        Assert.assertEquals(secLevelClient, clientKeysSecLevel);
        {
            if( clientShallHaveKeys ) {
                // Using encryption: pre-paired
                Assert.assertEquals(PairingMode.PRE_PAIRED, lastCompletedDevicePairingMode);
                Assert.assertEquals(BTSecurityLevel.ENC_ONLY, lastCompletedDeviceSecurityLevel); // pre-paired fixed level, no auth
            } else if( BTSecurityLevel.NONE.value < secLevelClient.value ) {
                // Using encryption: Newly paired
                Assert.assertNotEquals(PairingMode.PRE_PAIRED, lastCompletedDevicePairingMode);
                Assert.assertTrue("PairingMode client "+lastCompletedDevicePairingMode+" not > NONE", PairingMode.NONE.value < lastCompletedDevicePairingMode.value);
                Assert.assertTrue("SecurityLevel client "+lastCompletedDeviceSecurityLevel+" not >= "+secLevelClient, secLevelClient.value <= lastCompletedDeviceSecurityLevel.value);
            } else {
                // No encryption: No pairing
                Assert.assertEquals(PairingMode.NONE, lastCompletedDevicePairingMode);
                Assert.assertEquals(BTSecurityLevel.NONE, lastCompletedDeviceSecurityLevel);
            }
        }

        //
        // Validating EIR
        //
        synchronized( mtx_sync ) {
            BTUtils.println(System.err, "lastCompletedDevice.connectedEIR: "+lastCompletedDeviceEIR.toString());
            Assert.assertNotEquals(0, lastCompletedDeviceEIR.getEIRDataMask().mask);
            Assert.assertTrue( lastCompletedDeviceEIR.isSet(EIRDataTypeSet.DataType.FLAGS) );
            Assert.assertTrue( lastCompletedDeviceEIR.isSet(EIRDataTypeSet.DataType.SERVICE_UUID) );
            Assert.assertTrue( lastCompletedDeviceEIR.isSet(EIRDataTypeSet.DataType.NAME) );
            Assert.assertTrue( lastCompletedDeviceEIR.isSet(EIRDataTypeSet.DataType.CONN_IVAL) );
            Assert.assertEquals(serverName, lastCompletedDeviceEIR.getName());
            {
                final EInfoReport eir = lastCompletedDevice.getEIR().clone();
                BTUtils.println(System.err, "lastCompletedDevice.currentEIR: "+eir.toString());
                Assert.assertEquals(0, eir.getEIRDataMask().mask);
                Assert.assertEquals(0, eir.getName().length());
            }
        }

        //
        // Now reuse adapter for client mode -> Start discovery + Stop Discovery
        //
        {
            final BTAdapter adapter = server.getAdapter();
            { // if( false ) {
                adapter.removeAllStatusListener();
            }

            DBTEndpoint.startDiscovery(adapter, false /* current_exp_discovering_state */);

            DBTEndpoint.stopDiscovery(adapter, true /* current_exp_discovering_state */);
        }

        final int count = manager.removeChangedAdapterSetListener(myChangedAdapterSetListener);
        BTUtils.println(System.err, "****** EOL Removed ChangedAdapterSetCallback " + count);
    }
}

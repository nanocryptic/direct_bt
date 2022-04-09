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

import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import org.direct_bt.AdapterSettings;
import org.direct_bt.AdapterStatusListener;
import org.direct_bt.BTMode;
import org.direct_bt.BTSecurityLevel;
import org.direct_bt.BTAdapter;
import org.direct_bt.BTDevice;
import org.direct_bt.BTDeviceRegistry;
import org.direct_bt.BTGattChar;
import org.direct_bt.BTGattCmd;
import org.direct_bt.BTGattDesc;
import org.direct_bt.BTGattService;
import org.direct_bt.BTSecurityRegistry;
import org.direct_bt.BTUtils;
import org.direct_bt.DiscoveryPolicy;
import org.direct_bt.EIRDataTypeSet;
import org.direct_bt.EInfoReport;
import org.direct_bt.GattCharPropertySet;
import org.direct_bt.HCIStatusCode;
import org.direct_bt.LE_Features;
import org.direct_bt.LE_PHYs;
import org.direct_bt.PairingMode;
import org.direct_bt.SMPIOCapability;
import org.direct_bt.SMPKeyBin;
import org.direct_bt.SMPPairingState;
import org.direct_bt.ScanType;
import org.jau.net.EUI48;

/**
 * This Java scanner {@link BTRole::Master} test case, working with {@link DBTServer00}.
 */
public class DBTClient00 implements DBTClientTest {
    /**
     * Disconnect after processing.
     *
     * Default is `false`.
     */
    boolean KEEP_CONNECTED = false;

    /**
     * Remove device when disconnecting.
     *
     * This removes the device from all instances within adapter
     * and hence all potential side-effects of the current instance.
     *
     * Default is `false`, since it is good to test whether such side-effects exists.
     */
    boolean REMOVE_DEVICE = false;

    final boolean GATT_VERBOSE = false;
    final boolean SHOW_UPDATE_EVENTS = false;

    final long timestamp_t0 = BTUtils.startupTimeMillis();

    final String adapterShortName = "TDev2Clt";
    String adapterName = "TestDev2_Clt";
    EUI48 useAdapter = EUI48.ALL_DEVICE;
    BTMode btMode = BTMode.DUAL;
    BTAdapter clientAdapter = null;

    AtomicInteger deviceReadyCount = new AtomicInteger(0);

    AtomicInteger notificationsReceived = new AtomicInteger(0);
    AtomicInteger indicationsReceived = new AtomicInteger(0);
    AtomicInteger completedGATTCommands = new AtomicInteger(0);
    AtomicInteger completedMeasurements = new AtomicInteger(0);
    AtomicInteger measurementsLeft = new AtomicInteger(1);

    final byte cmd_arg = (byte)0x44;

    public DBTClient00(final String adapterName, final EUI48 useAdapter, final BTMode btMode) {
        this.adapterName = adapterName;
        this.useAdapter = useAdapter;
        this.btMode = btMode;
    }

    @Override
    public String getName() { return adapterName; }

    @Override
    public void setAdapter(final BTAdapter clientAdapter) {
        this.clientAdapter = clientAdapter;
    }
    @Override
    public BTAdapter getAdapter() { return clientAdapter; }

    static void printf(final String format, final Object... args) {
        final Object[] args2 = new Object[args.length+1];
        args2[0] = BTUtils.elapsedTimeMillis();
        System.arraycopy(args, 0, args2, 1, args.length);
        System.err.printf("[%,9d] "+format, args2);
        // System.err.printf("[%,9d] ", BluetoothUtils.getElapsedMillisecond());
        // System.err.printf(format, args);
    }

    static void executeOffThread(final Runnable runobj, final String threadName, final boolean detach) {
        final Thread t = new Thread( runobj, threadName );
        if( detach ) {
            t.setDaemon(true); // detach thread
        }
        t.start();
    }
    static void execute(final Runnable task, final boolean offThread) {
        if( offThread ) {
            final Thread t = new Thread(task);
            t.setDaemon(true);
            t.start();
        } else {
            task.run();
        }
    }

    class MyAdapterStatusListener extends AdapterStatusListener {
        @Override
        public void adapterSettingsChanged(final BTAdapter adapter, final AdapterSettings oldmask,
                                           final AdapterSettings newmask, final AdapterSettings changedmask, final long timestamp) {
            final boolean initialSetting = oldmask.isEmpty();
            if( initialSetting ) {
                BTUtils.println(System.err, "****** Client SETTINGS: "+oldmask+" -> "+newmask+", initial "+changedmask);
            } else {
                BTUtils.println(System.err, "****** Client SETTINGS: "+oldmask+" -> "+newmask+", changed "+changedmask);
            }
            BTUtils.println(System.err, "Status Adapter:");
            BTUtils.println(System.err, adapter.toString());
        }

        @Override
        public void discoveringChanged(final BTAdapter adapter, final ScanType currentMeta, final ScanType changedType, final boolean changedEnabled, final DiscoveryPolicy policy, final long timestamp) {
            BTUtils.println(System.err, "****** Client DISCOVERING: meta "+currentMeta+", changed["+changedType+", enabled "+changedEnabled+", policy "+policy+"] on "+adapter);
        }

        @Override
        public boolean deviceFound(final BTDevice device, final long timestamp) {
            if( !BTDeviceRegistry.isDeviceProcessing( device.getAddressAndType() ) &&
                ( !BTDeviceRegistry.isWaitingForAnyDevice() ||
                  ( BTDeviceRegistry.isWaitingForDevice(device.getAddressAndType().address, device.getName()) &&
                    ( 0 < measurementsLeft.get() || !BTDeviceRegistry.isDeviceProcessed(device.getAddressAndType()) )
                  )
                )
              )
            {
                BTUtils.println(System.err, "****** Client FOUND__-0: Connecting "+device.toString());
                {
                    final long td = BTUtils.currentTimeMillis() - timestamp_t0; // adapter-init -> now
                    BTUtils.println(System.err, "PERF: adapter-init -> FOUND__-0 " + td + " ms");
                }
                executeOffThread( () -> { connectDiscoveredDevice(device); },
                                  "DBT-Connect-"+device.getAddressAndType(), true /* detach */);
                return true;
            } else {
                BTUtils.println(System.err, "****** Client FOUND__-1: NOP "+device.toString());
                return false;
            }
        }

        @Override
        public void deviceUpdated(final BTDevice device, final EIRDataTypeSet updateMask, final long timestamp) {
        }

        @Override
        public void deviceConnected(final BTDevice device, final short handle, final long timestamp) {
            BTUtils.println(System.err, "****** Client CONNECTED: "+device.toString());
        }

        @Override
        public void devicePairingState(final BTDevice device, final SMPPairingState state, final PairingMode mode, final long timestamp) {
            BTUtils.println(System.err, "****** Client PAIRING_STATE: state "+state+", mode "+mode+": "+device);
            switch( state ) {
                case NONE:
                    // next: deviceReady(..)
                    break;
                case FAILED: {
                    final boolean res  = SMPKeyBin.remove(DBTConstants.CLIENT_KEY_PATH, device);
                    BTUtils.println(System.err, "****** Client PAIRING_STATE: state "+state+"; Remove key file "+SMPKeyBin.getFilename(DBTConstants.CLIENT_KEY_PATH, device)+", res "+res);
                    // next: deviceReady() or deviceDisconnected(..)
                } break;
                case REQUESTED_BY_RESPONDER:
                    // next: FEATURE_EXCHANGE_STARTED
                    break;
                case FEATURE_EXCHANGE_STARTED:
                    // next: FEATURE_EXCHANGE_COMPLETED
                    break;
                case FEATURE_EXCHANGE_COMPLETED:
                    // next: PASSKEY_EXPECTED... or KEY_DISTRIBUTION
                    break;
                case PASSKEY_EXPECTED: {
                    final BTSecurityRegistry.Entry sec = BTSecurityRegistry.getStartOf(device.getAddressAndType().address, "");
                    if( null != sec && sec.getPairingPasskey() != BTSecurityRegistry.NO_PASSKEY ) {
                        executeOffThread( () -> { device.setPairingPasskey( sec.getPairingPasskey() ); }, "DBT-SetPasskey-"+device.getAddressAndType(), true /* detach */);
                    } else {
                        executeOffThread( () -> { device.setPairingPasskey( 0 ); }, "DBT-SetPasskey-"+device.getAddressAndType(), true /* detach */);
                        // 3s disconnect: executeOffThread( () -> { device.setPairingPasskeyNegative(); }, "DBT-SetPasskeyNegative-"+device.getAddressAndType(), true /* detach */);
                    }
                    // next: KEY_DISTRIBUTION or FAILED
                  } break;
                case NUMERIC_COMPARE_EXPECTED: {
                    final BTSecurityRegistry.Entry sec = BTSecurityRegistry.getStartOf(device.getAddressAndType().address, "");
                    if( null != sec ) {
                        executeOffThread( () -> { device.setPairingNumericComparison( sec.getPairingNumericComparison() ); }, "DBT-SetNumericComp-"+device.getAddressAndType(), true /* detach */);
                    } else {
                        executeOffThread( () -> { device.setPairingNumericComparison( false ); }, "DBT-SetNumericCompFalse-"+device.getAddressAndType(), true /* detach */);
                    }
                    // next: KEY_DISTRIBUTION or FAILED
                  } break;
                case OOB_EXPECTED:
                    // FIXME: ABORT
                    break;
                case KEY_DISTRIBUTION:
                    // next: COMPLETED or FAILED
                    break;
                case COMPLETED:
                    // next: deviceReady(..)
                    break;
                default: // nop
                    break;
            }
        }

        @Override
        public void deviceReady(final BTDevice device, final long timestamp) {
            if( !BTDeviceRegistry.isDeviceProcessing( device.getAddressAndType() ) &&
                ( !BTDeviceRegistry.isWaitingForAnyDevice() ||
                  ( BTDeviceRegistry.isWaitingForDevice(device.getAddressAndType().address, device.getName()) &&
                    ( 0 < measurementsLeft.get() || !BTDeviceRegistry.isDeviceProcessed(device.getAddressAndType()) )
                  )
                )
              )
            {
                deviceReadyCount.incrementAndGet();
                BTUtils.println(System.err, "****** Client READY-0: Processing["+deviceReadyCount.get()+"] "+device.toString());
                {
                    final long td = BTUtils.currentTimeMillis() - timestamp_t0; // adapter-init -> now
                    BTUtils.println(System.err, "PERF: adapter-init -> READY-0 " + td + " ms");
                }
                BTDeviceRegistry.addToProcessingDevices(device.getAddressAndType(), device.getName());

                // Be nice to Test* case, allowing to reach its own listener.deviceReady() added later
                executeOffThread( () -> { processReadyDevice(device); },
                        "DBT-Process-"+device.getAddressAndType(), true /* detach */);
                // processReadyDevice(device); // AdapterStatusListener::deviceReady() explicitly allows prolonged and complex code execution!
            } else {
                BTUtils.println(System.err, "****** Client READY-1: NOP " + device.toString());
            }
        }

        @Override
        public void deviceDisconnected(final BTDevice device, final HCIStatusCode reason, final short handle, final long timestamp) {
            BTUtils.println(System.err, "****** Client DISCONNECTED: Reason "+reason+", old handle 0x"+Integer.toHexString(handle)+": "+device+" on "+device.getAdapter());

            executeOffThread( () -> { removeDevice(device); }, "DBT-Remove-"+device.getAddressAndType(), true /* detach */);
        }

        @Override
        public String toString() {
            return "AdapterStatusListener[user, per-adapter]";
        }
    };

    class MyGATTEventListener implements BTGattChar.Listener {
        private final int i, j;

        public MyGATTEventListener(final int i_, final int j_) { i=i_; j=j_; }

        @Override
        public void notificationReceived(final BTGattChar charDecl,
                                         final byte[] value, final long timestamp) {
            if( GATT_VERBOSE ) {
                final long tR = BTUtils.currentTimeMillis();
                printf("**[%02d.%02d] Characteristic-Notify: UUID %s, td %d ******\n",
                        i, j, charDecl.getUUID(), (tR-timestamp));
                printf("**[%02d.%02d]     Characteristic: %s ******\n", i, j, charDecl.toString());
                printf("**[%02d.%02d]     Value R: size %d, ro: %s ******\n", i, j, value.length, BTUtils.bytesHexString(value, 0, -1, true));
                printf("**[%02d.%02d]     Value S: %s ******\n", i, j, BTUtils.decodeUTF8String(value, 0, value.length));
            }
            notificationsReceived.incrementAndGet();
        }

        @Override
        public void indicationReceived(final BTGattChar charDecl,
                                       final byte[] value, final long timestamp, final boolean confirmationSent) {
            if( GATT_VERBOSE ) {
                final long tR = BTUtils.currentTimeMillis();
                printf("**[%02d.%02d] Characteristic-Indication: UUID %s, td %d, confirmed %b ******\n",
                        i, j, charDecl.getUUID(), (tR-timestamp), confirmationSent);
                printf("**[%02d.%02d]     Characteristic: %s ******\n", i, j, charDecl.toString());
                printf("**[%02d.%02d]     Value R: size %d, ro: %s ******\n", i, j, value.length, BTUtils.bytesHexString(value, 0, -1, true));
                printf("**[%02d.%02d]     Value S: %s ******\n", i, j, BTUtils.decodeUTF8String(value, 0, value.length));
            }
            indicationsReceived.incrementAndGet();
        }
    }

    private void resetLastProcessingStats() {
        completedGATTCommands.set(0);
        notificationsReceived.set(0);
        indicationsReceived.set(0);
    }

    private void connectDiscoveredDevice(final BTDevice device) {
        BTUtils.println(System.err, "****** Connecting Device: Start " + device.toString());

        resetLastProcessingStats();

        final BTSecurityRegistry.Entry sec = BTSecurityRegistry.getStartOf(device.getAddressAndType().address, device.getName());
        if( null != sec ) {
            BTUtils.println(System.err, "****** Connecting Device: Found SecurityDetail "+sec.toString()+" for "+device.toString());
        } else {
            BTUtils.println(System.err, "****** Connecting Device: No SecurityDetail for "+device.toString());
        }
        final BTSecurityLevel req_sec_level = null != sec ? sec.getSecLevel() : BTSecurityLevel.UNSET;
        HCIStatusCode res = device.uploadKeys(DBTConstants.CLIENT_KEY_PATH, req_sec_level, true /* verbose_ */);
        BTUtils.fprintf_td(System.err, "****** Connecting Device: BTDevice::uploadKeys(...) result %s\n", res.toString());
        if( HCIStatusCode.SUCCESS != res ) {
            if( null != sec ) {
                if( sec.isSecurityAutoEnabled() ) {
                    final boolean r = device.setConnSecurityAuto( sec.getSecurityAutoIOCap() );
                    BTUtils.println(System.err, "****** Connecting Device: Using SecurityDetail.SEC AUTO "+sec+" -> set OK "+r);
                } else if( sec.isSecLevelOrIOCapSet() ) {
                    final boolean r = device.setConnSecurity(sec.getSecLevel(), sec.getIOCap());
                    BTUtils.println(System.err, "****** Connecting Device: Using SecurityDetail.Level+IOCap "+sec+" -> set OK "+r);
                } else {
                    final boolean r = device.setConnSecurityAuto( SMPIOCapability.KEYBOARD_ONLY );
                    BTUtils.println(System.err, "****** Connecting Device: Setting SEC AUTO security detail w/ KEYBOARD_ONLY ("+sec+") -> set OK "+r);
                }
            } else {
                final boolean r = device.setConnSecurityAuto( SMPIOCapability.KEYBOARD_ONLY );
                BTUtils.println(System.err, "****** Connecting Device: Setting SEC AUTO security detail w/ KEYBOARD_ONLY -> set OK "+r);
            }
        }
        final EInfoReport eir = device.getEIR();
        BTUtils.println(System.err, "Using EIR "+eir.toString());

        short conn_interval_min  = (short)12;
        short conn_interval_max  = (short)12;
        final short conn_latency  = (short)0;
        if( eir.isSet(EIRDataTypeSet.DataType.CONN_IVAL) ) {
            final short[] minmax = new short[2];
            eir.getConnInterval(minmax);
            conn_interval_min = minmax[0];
            conn_interval_max = minmax[1];
        }
        final short supervision_timeout = BTUtils.getHCIConnSupervisorTimeout(conn_latency, (int) ( conn_interval_max * 1.25 ) /* ms */);
        res = device.connectLE(le_scan_interval, le_scan_window, conn_interval_min, conn_interval_max, conn_latency, supervision_timeout);
        // res = device.connectDefault();
        BTUtils.println(System.err, "****** Connecting Device Command, res "+res+": End result "+res+" of " + device.toString());
    }

    private void processReadyDevice(final BTDevice device) {
        BTUtils.println(System.err, "****** Processing Ready Device: Start " + device.toString());
        final long t1 = BTUtils.currentTimeMillis();

        SMPKeyBin.createAndWrite(device, DBTConstants.CLIENT_KEY_PATH, true /* verbose */);
        final long t2 = BTUtils.currentTimeMillis();

        boolean success = false;

        {
            final LE_PHYs resTx[] = { new LE_PHYs() };
            final LE_PHYs resRx[] = { new LE_PHYs() };
            final HCIStatusCode res = device.getConnectedLE_PHY(resTx, resRx);
            BTUtils.fprintf_td(System.err, "****** Got Connected LE PHY: status %s: Tx %s, Rx %s\n",
                    res.toString(), resTx[0].toString(), resRx[0].toString());
        }
        final long t3 = BTUtils.currentTimeMillis();

        //
        // GATT Service Processing
        //
        try {
            final List<BTGattService> primServices = device.getGattServices();
            if( null == primServices || 0 == primServices.size() ) {
                // Cheating the flow, but avoiding: goto, do-while-false and lastly unreadable intendations
                // And it is an error case nonetheless ;-)
                throw new RuntimeException("Processing Ready Device: getServices() failed " + device.toString());
            }
            final long t5 = BTUtils.currentTimeMillis();
            {
                final long td00 = device.getLastDiscoveryTimestamp() - timestamp_t0; // adapter-init to discovered
                final long td01 = t1 - timestamp_t0; // adapter-init to processing-start
                final long td05 = t5 - timestamp_t0; // adapter-init -> gatt-complete
                final long tdc1 = t1 - device.getLastDiscoveryTimestamp(); // discovered to processing-start
                final long tdc5 = t5 - device.getLastDiscoveryTimestamp(); // discovered to gatt-complete
                final long td12 = t2 - t1; // SMPKeyBin
                final long td23 = t3 - t2; // LE_PHY
                final long td13 = t3 - t1; // SMPKeyBin + LE_PHY
                final long td35 = t5 - t3; // get-gatt-services
                BTUtils.println(System.err, System.lineSeparator()+System.lineSeparator());
                BTUtils.println(System.err, "PERF: GATT primary-services completed"+System.lineSeparator()+
                        "PERF:  adapter-init to discovered " + td00 + " ms,"+System.lineSeparator()+
                        "PERF:  adapter-init to processing-start " + td01 + " ms,"+System.lineSeparator()+
                        "PERF:  adapter-init to gatt-complete " + td05 + " ms,"+System.lineSeparator()+
                        "PERF:  discovered to processing-start " + tdc1 + " ms,"+System.lineSeparator()+
                        "PERF:  discovered to gatt-complete " + tdc5 + " ms,"+System.lineSeparator()+
                        "PERF:  SMPKeyBin + LE_PHY " + td13 + " ms (SMPKeyBin "+td12+"ms, LE_PHY "+td23+"ms), "+System.lineSeparator()+
                        "PERF:  get-gatt-services " + td35 + " ms,"+System.lineSeparator());
            }

            {
                final BTGattCmd cmd = new BTGattCmd(device, "TestCmd", null /* service_uuid */, DBTConstants.CommandUUID, DBTConstants.ResponseUUID);
                cmd.setVerbose(true);
                final boolean cmd_resolved = cmd.isResolved();
                BTUtils.println(System.err, "Command test: "+cmd.toString()+", resolved "+cmd_resolved);
                final byte[] cmd_data = { cmd_arg };
                final HCIStatusCode cmd_res = cmd.send(true /* prefNoAck */, cmd_data, 3000 /* timeoutMS */);
                if( HCIStatusCode.SUCCESS == cmd_res ) {
                    final byte[] resp = cmd.getResponse();
                    if( 1 == resp.length && resp[0] == cmd_arg ) {
                        BTUtils.fprintf_td(System.err, "Success: %s -> %s (echo response)\n", cmd.toString(), BTUtils.bytesHexString(resp, 0, resp.length, true /* lsb */));
                        completedGATTCommands.incrementAndGet();
                    } else {
                        BTUtils.fprintf_td(System.err, "Failure: %s -> %s (different response)\n", cmd.toString(), BTUtils.bytesHexString(resp, 0, resp.length, true /* lsb */));
                    }
                } else {
                    BTUtils.fprintf_td(System.err, "Failure: %s -> %s\n", cmd.toString(), cmd_res.toString());
                }
                cmd.close();
            }

            try {
                int i=0;
                for(final Iterator<BTGattService> srvIter = primServices.iterator(); srvIter.hasNext(); i++) {
                    final BTGattService primService = srvIter.next();
                    if( GATT_VERBOSE ) {
                        printf("  [%02d] Service UUID %s\n", i, primService.getUUID());
                        printf("  [%02d]         %s\n", i, primService.toString());
                    }
                    int j=0;
                    final List<BTGattChar> serviceCharacteristics = primService.getChars();
                    for(final Iterator<BTGattChar> charIter = serviceCharacteristics.iterator(); charIter.hasNext(); j++) {
                        final BTGattChar serviceChar = charIter.next();
                        if( GATT_VERBOSE ) {
                            printf("  [%02d.%02d] Characteristic: UUID %s\n", i, j, serviceChar.getUUID());
                            printf("  [%02d.%02d]     %s\n", i, j, serviceChar.toString());
                        }
                        final GattCharPropertySet properties = serviceChar.getProperties();
                        if( properties.isSet(GattCharPropertySet.Type.Read) ) {
                            final byte[] value = serviceChar.readValue();
                            final String svalue = BTUtils.decodeUTF8String(value, 0, value.length);
                            if( GATT_VERBOSE ) {
                                printf("  [%02d.%02d]     value: %s ('%s')\n", i, j, BTUtils.bytesHexString(value, 0, -1, true), svalue);
                            }
                        }
                        int k=0;
                        final List<BTGattDesc> charDescList = serviceChar.getDescriptors();
                        for(final Iterator<BTGattDesc> descIter = charDescList.iterator(); descIter.hasNext(); k++) {
                            final BTGattDesc charDesc = descIter.next();
                            if( GATT_VERBOSE ) {
                                printf("  [%02d.%02d.%02d] Descriptor: UUID %s\n", i, j, k, charDesc.getUUID());
                                printf("  [%02d.%02d.%02d]     %s\n", i, j, k, charDesc.toString());
                            }
                        }
                        final boolean cccdEnableResult[] = { false, false };
                        if( serviceChar.enableNotificationOrIndication( cccdEnableResult ) ) {
                            // ClientCharConfigDescriptor (CCD) is available
                            final boolean clAdded = null != serviceChar.addCharListener( new MyGATTEventListener(i, j) );
                            if( GATT_VERBOSE ) {
                                printf("  [%02d.%02d] Characteristic-Listener: Notification(%b), Indication(%b): Added %b\n",
                                        i, j, cccdEnableResult[0], cccdEnableResult[1], clAdded);
                                printf("\n");
                            }
                        }
                    }
                    if( GATT_VERBOSE ) {
                        printf("\n");
                    }
                }
            } catch( final Exception ex) {
                BTUtils.println(System.err, "Caught "+ex.getMessage());
                ex.printStackTrace();
            }
            // FIXME sleep 1s for notifications and/or indications ...
            try {
                Thread.sleep(1000);
            } catch (final InterruptedException e) {
                e.printStackTrace();
            }
            success = completedGATTCommands.get() > 0 && ( notificationsReceived.get() > 0 || indicationsReceived.get() > 0 );

            if( success ) {
                // Tell server we have successfully completed the test.
                final BTGattCmd cmd = new BTGattCmd(device, "SuccessHandshake", null /* service_uuid */, DBTConstants.CommandUUID, DBTConstants.ResponseUUID);
                cmd.setVerbose(true);
                final boolean cmd_resolved = cmd.isResolved();
                BTUtils.println(System.err, "Command test: "+cmd.toString()+", resolved "+cmd_resolved);
                final HCIStatusCode cmd_res = cmd.send(true /* prefNoAck */, DBTConstants.SuccessHandshakeCommandData, 3000 /* timeoutMS */);
                if( HCIStatusCode.SUCCESS == cmd_res ) {
                    final byte[] resp = cmd.getResponse();
                    if( Arrays.equals(DBTConstants.SuccessHandshakeCommandData, resp) ) {
                        BTUtils.fprintf_td(System.err, "Success: %s -> %s (echo response)\n", cmd.toString(), BTUtils.bytesHexString(resp, 0, resp.length, true /* lsb */));
                    } else {
                        BTUtils.fprintf_td(System.err, "Failure: %s -> %s (different response)\n", cmd.toString(), BTUtils.bytesHexString(resp, 0, resp.length, true /* lsb */));
                        success = false;
                    }
                } else {
                    BTUtils.fprintf_td(System.err, "Failure: %s -> %s\n", cmd.toString(), cmd_res.toString());
                    success = false;
                }
                cmd.close();
            }
        } catch (final Throwable t ) {
            BTUtils.println(System.err, "****** Processing Ready Device: Exception caught for " + device.toString() + ": "+t.getMessage());
            t.printStackTrace();
        }

        BTUtils.println(System.err, "****** Processing Ready Device: End-1: Success " + success +
                           " on " + device.toString() + "; devInProc "+BTDeviceRegistry.getProcessingDeviceCount());

        BTDeviceRegistry.removeFromProcessingDevices( device.getAddressAndType() );

        if( DiscoveryPolicy.PAUSE_CONNECTED_UNTIL_DISCONNECTED == discoveryPolicy ) {
            device.getAdapter().removeDevicePausingDiscovery(device);
        }

        BTUtils.println(System.err, "****** Processing Ready Device: End-2: Success " + success +
                           " on " + device.toString() + "; devInProc "+BTDeviceRegistry.getProcessingDeviceCount());
        if( success ) {
            BTDeviceRegistry.addToProcessedDevices(device.getAddressAndType(), device.getName());
        }
        device.removeAllCharListener();

        if( !KEEP_CONNECTED ) {
            if( REMOVE_DEVICE ) {
                device.remove();
            } else {
                device.disconnect();
            }
        }

        if( success ) {
            completedMeasurements.addAndGet(1);
        }
        if( 0 < measurementsLeft.get() ) {
            measurementsLeft.decrementAndGet();
        }
        BTUtils.println(System.err, "****** Processing Ready Device: Success "+success+
                        "; Measurements completed "+completedMeasurements.get()+
                        ", done "+completedMeasurements.get()+
                        ", left "+measurementsLeft.get()+
                        "; Received notitifications "+notificationsReceived.get()+", indications "+indicationsReceived.get()+
                        "; Completed GATT commands "+completedGATTCommands.get()+
                        ": "+device.getAddressAndType());
    }

    private void removeDevice(final BTDevice device) {
        BTUtils.println(System.err, "****** Remove Device: removing: "+device.getAddressAndType());

        BTDeviceRegistry.removeFromProcessingDevices(device.getAddressAndType());

        if( REMOVE_DEVICE ) {
            device.remove();
        }
    }

    DiscoveryPolicy discoveryPolicy = DiscoveryPolicy.PAUSE_CONNECTED_UNTIL_READY; // default value
    boolean le_scan_active = true; // default value
    static final short le_scan_interval = (short)24; // default value
    static final short le_scan_window = (short)24; // default value
    static final byte filter_policy = (byte)0; // default value
    static final boolean filter_dup = true; // default value

    @Override
    public HCIStatusCode startDiscovery(final BTAdapter adapter, final String msg) {
        if( !useAdapter.equals(EUI48.ALL_DEVICE) && !useAdapter.equals(adapter.getAddressAndType().address) ) {
            BTUtils.fprintf_td(System.err, "****** Start discovery (%s): Adapter not selected: %s\n", msg, adapter.toString());
            return HCIStatusCode.FAILED;
        }

        resetLastProcessingStats();

        final HCIStatusCode status = adapter.startDiscovery( discoveryPolicy, le_scan_active, le_scan_interval, le_scan_window, filter_policy, filter_dup );
        BTUtils.println(System.err, "****** Start discovery ("+msg+") result: "+status);
        return status;
    }

    @Override
    public HCIStatusCode stopDiscovery(final BTAdapter adapter, final String msg) {
        if( !useAdapter.equals(EUI48.ALL_DEVICE) && !useAdapter.equals(adapter.getAddressAndType().address) ) {
            BTUtils.fprintf_td(System.err, "****** Stop discovery (%s): Adapter not selected: %s\n", msg, adapter.toString());
            return HCIStatusCode.FAILED;
        }

        final HCIStatusCode status = adapter.stopDiscovery();
        BTUtils.println(System.err, "****** Stop discovery ("+msg+") result: "+status);
        return status;
    }

    @Override
    public boolean initAdapter(final BTAdapter adapter) {
        if( !useAdapter.equals(EUI48.ALL_DEVICE) && !useAdapter.equals(adapter.getAddressAndType().address) ) {
            BTUtils.fprintf_td(System.err, "initClientAdapter: Adapter not selected: %s\n", adapter.toString());
            return false;
        }
        // Initialize with defaults and power-on
        if( !adapter.isInitialized() ) {
            final HCIStatusCode status = adapter.initialize( btMode );
            if( HCIStatusCode.SUCCESS != status ) {
                BTUtils.fprintf_td(System.err, "initClientAdapter: Adapter initialization failed: %s: %s\n",
                        status.toString(), adapter.toString());
                return false;
            }
        } else if( !adapter.setPowered( true ) ) {
            BTUtils.fprintf_td(System.err, "initClientAdapter: Already initialized adapter power-on failed:: %s\n", adapter.toString());
            return false;
        }
        // adapter is powered-on
        BTUtils.fprintf_td(System.err, "initClientAdapter.1: %s\n", adapter.toString());

        if( adapter.setPowered(false) ) {
            final HCIStatusCode status = adapter.setName(adapterName, adapterShortName);
            if( HCIStatusCode.SUCCESS == status ) {
                BTUtils.fprintf_td(System.err, "initClientAdapter: setLocalName OK: %s\n", adapter.toString());
            } else {
                BTUtils.fprintf_td(System.err, "initClientAdapter: setLocalName failed: %s\n", adapter.toString());
                return false;
            }
            if( !adapter.setPowered( true ) ) {
                BTUtils.fprintf_td(System.err, "initClientAdapter: setPower.2 on failed: %s\n", adapter.toString());
                return false;
            }
        } else {
            BTUtils.fprintf_td(System.err, "initClientAdapter: setPowered.2 off failed: %s\n", adapter.toString());
        }
        BTUtils.println(System.err, "initClientAdapter.2: "+adapter.toString());

        {
            final LE_Features le_feats = adapter.getLEFeatures();
            BTUtils.fprintf_td(System.err, "initClientAdapter: LE_Features %s\n", le_feats.toString());
        }
        if( adapter.getBTMajorVersion() > 4 ) {
            // BT5 specific
            final LE_PHYs Tx = new LE_PHYs(LE_PHYs.PHY.LE_2M);
            final LE_PHYs Rx = new LE_PHYs(LE_PHYs.PHY.LE_2M);

            final HCIStatusCode res = adapter.setDefaultLE_PHY(Tx, Rx);
            BTUtils.fprintf_td(System.err, "initClientAdapter: Set Default LE PHY: status %s: Tx %s, Rx %s\n",
                                res.toString(), Tx.toString(), Rx.toString());
        }
        // adapter is powered-on
        adapter.addStatusListener( new MyAdapterStatusListener() );

        return true;
    }
}

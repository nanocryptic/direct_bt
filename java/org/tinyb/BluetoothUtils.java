/**
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
package org.tinyb;

public class BluetoothUtils {
    private static long t0;
    static {
        t0 = startupTimeMillisImpl();
    }
    private static native long startupTimeMillisImpl();

    /**
     * Returns current monotonic time in milliseconds.
     */
    public static native long currentTimeMillis();

    /**
     * Returns the startup time in monotonic time in milliseconds of the native module.
     */
    public static long startupTimeMillis() { return t0; }

    /**
     * Returns current elapsed monotonic time in milliseconds since module startup, see {@link #startupTimeMillis()}.
     */
    public static long elapsedTimeMillis() { return currentTimeMillis() - t0; }

    /**
     * Defining the supervising timeout for LE connections to be a multiple of the maximum connection interval as follows:
     * <pre>
     *  ( 1 + conn_latency ) * conn_interval_max_ms * max(2, multiplier) [ms]
     * </pre>
     * If above result is smaller than the given min_result_ms, min_result_ms/10 will be returned.
     * @param conn_latency the connection latency
     * @param conn_interval_max_ms the maximum connection interval in [ms]
     * @param min_result_ms the minimum resulting supervisor timeout, defaults to 500ms.
     *        If above formula results in a smaller value, min_result_ms/10 will be returned.
     * @param multiplier recommendation is 6, we use 10 as default for safety.
     * @return the resulting supervising timeout in 1/10 [ms], suitable for the {@link BluetoothDevice#connectLE(short, short, short, short, short, short)}.
     * @see BluetoothDevice#connectLE(short, short, short, short, short, short)
     */
    public static int getHCIConnSupervisorTimeout(final int conn_latency, final int conn_interval_max_ms,
                                                  final int min_result_ms, final int multiplier) {
        return Math.max(min_result_ms,
                        ( 1 + conn_latency ) * conn_interval_max_ms * Math.max(2, multiplier)
                       ) / 10;
    }

    /**
     * Produce a hexadecimal string representation of the given byte values.
     * <p>
     * If lsbFirst is true, orders LSB left -> MSB right, usual for byte streams.<br>
     * Otherwise orders MSB left -> LSB right, usual for readable integer values.
     * </p>
     * @param bytes the byte array to represent
     * @param offset offset in byte array to the first byte to print.
     * @param length number of bytes to print. If negative, will use {@code bytes.length - offset}.
     * @param lsbFirst true having the least significant byte printed first (lowest addressed byte to highest),
     *                 otherwise have the most significant byte printed first (highest addressed byte to lowest).
     * @param leading0X true to have a leading '0x' being printed, otherwise no prefix is produced.
     * @param lowerCase true to use lower case hex-chars, otherwise capital letters are being used.
     * @return the hex-string representation of the data
     */
    public static String bytesHexString(final byte[] bytes, final int offset, final int length,
                                        final boolean lsbFirst, final boolean leading0X, final boolean lowerCase)
    {
        final int byte_len = 0 <= length ? length : bytes.length - offset;
        if( byte_len > ( bytes.length - offset ) ) {
            throw new IllegalArgumentException("byte[] ( "+bytes.length+" - "+offset+" ) < "+length+" bytes");
        }
        final char[] hex_array = lowerCase ? HEX_ARRAY_LOW : HEX_ARRAY_BIG;
        final char[] hexChars;
        final int char_offset;
        if( leading0X ) {
            char_offset = 2;
            hexChars = new char[2 + byte_len * 2];
            hexChars[0] = '0';
            hexChars[1] = 'x';
        } else {
            char_offset = 0;
            hexChars = new char[byte_len * 2];
        }

        if( lsbFirst ) {
            // LSB left -> MSB right
            for (int j = 0; j < byte_len; j++) {
                final int v = bytes[offset + j] & 0xFF;
                hexChars[char_offset + j * 2] = hex_array[v >>> 4];
                hexChars[char_offset + j * 2 + 1] = hex_array[v & 0x0F];
            }
        } else {
            // MSB left -> LSB right
            for (int j = byte_len-1; j >= 0; j--) {
                final int v = bytes[offset + j] & 0xFF;
                hexChars[char_offset + j * 2] = hex_array[v >>> 4];
                hexChars[char_offset + j * 2 + 1] = hex_array[v & 0x0F];
            }
        }
        return new String(hexChars);
    }
    private static final char[] HEX_ARRAY_LOW = "0123456789abcdef".toCharArray();
    private static final char[] HEX_ARRAY_BIG = "0123456789ABCDEF".toCharArray();


    /**
     * Returns all valid consecutive UTF-8 characters within buffer
     * in the range offset -> size or until EOS.
     * <p>
     * In case a non UTF-8 character has been detected,
     * the content will be cut off and the decoding loop ends.
     * </p>
     * <p>
     * Method utilizes a finite state machine detecting variable length UTF-8 codes.
     * See <a href="http://bjoern.hoehrmann.de/utf-8/decoder/dfa/">Bjoern Hoehrmann's site</a> for details.
     * </p>
     */
    public static native String decodeUTF8String(final byte[] buffer, final int offset, final int size);

}

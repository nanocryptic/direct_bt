/*
 * Author: Andrei Vasiliu <andrei.vasiliu@intel.com>
 * Copyright (c) 2016 Intel Corporation.
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

#include "tinyb/BluetoothAdapter.hpp"
#include "tinyb/BluetoothDevice.hpp"
#include "tinyb/BluetoothGattService.hpp"
#include "tinyb/BluetoothObject.hpp"

#include "tinyb_dbus_DBusDevice.h"

#include "helper_tinyb.hpp"

using namespace tinyb;
using namespace jau;

jobject Java_tinyb_dbus_DBusDevice_getBluetoothType(JNIEnv *env, jobject obj)
{
    try {
        (void)obj;

        return get_bluetooth_type(env, "DEVICE");
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jobject Java_tinyb_dbus_DBusDevice_clone(JNIEnv *env, jobject obj)
{
    try {
        return generic_clone<BluetoothDevice>(env, obj);
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jboolean Java_tinyb_dbus_DBusDevice_disconnectImpl(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->disconnect() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_connectAsyncStart(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        obj_device->connect_async_start();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jboolean Java_tinyb_dbus_DBusDevice_connectAsyncFinish(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->connect_async_finish() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_connectImpl(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->connect() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_connectProfile(JNIEnv *env, jobject obj, jstring str)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        const std::string string_to_write = from_jstring_to_string(env, str);

        return obj_device->connect_profile(string_to_write) ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_disconnectProfile(JNIEnv *env, jobject obj, jstring str)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        const std::string string_to_write = from_jstring_to_string(env, str);

        return obj_device->disconnect_profile(string_to_write) ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_pair(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->pair() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_remove(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->remove_device() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jboolean Java_tinyb_dbus_DBusDevice_cancelPairing(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->cancel_pairing() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jobject Java_tinyb_dbus_DBusDevice_getServices(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::vector<std::unique_ptr<BluetoothGattService>> array = obj_device->get_services();
        jobject result = convert_vector_uniqueptr_to_jarraylist<std::vector<std::unique_ptr<BluetoothGattService>>, BluetoothGattService>(
                env, array, "(J)V");

        return result;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jstring Java_tinyb_dbus_DBusDevice_getAddressString(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::string address = obj_device->get_address();

        return env->NewStringUTF((const char *)address.c_str());
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jstring Java_tinyb_dbus_DBusDevice_getName(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::string name = obj_device->get_name();

        return env->NewStringUTF((const char *)name.c_str());
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jstring Java_tinyb_dbus_DBusDevice_getAlias(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::string alias = obj_device->get_alias();

        return env->NewStringUTF((const char *)alias.c_str());
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

void Java_tinyb_dbus_DBusDevice_setAlias(JNIEnv *env, jobject obj, jstring str)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        const std::string string_to_write = from_jstring_to_string(env, str);

        obj_device->set_alias(string_to_write);
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jint Java_tinyb_dbus_DBusDevice_getBluetoothClass(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return (jlong)obj_device->get_class();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return 0;
}

jshort Java_tinyb_dbus_DBusDevice_getAppearance(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return (jshort)obj_device->get_appearance();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return 0;
}

jstring Java_tinyb_dbus_DBusDevice_getIcon(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::unique_ptr<std::string> icon = obj_device->get_icon();
        if (icon == nullptr)
            return nullptr;

        return env->NewStringUTF((const char *)icon->c_str());
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jboolean Java_tinyb_dbus_DBusDevice_getPaired(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_paired() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_enablePairedNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_paired_notifications([ callback_ptr ] (bool v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass boolean_cls = search_class(*jni_env, "java/lang/Boolean");
                jmethodID constructor = search_method(*jni_env, boolean_cls, "<init>", "(Z)V", false);

                jobject result = jni_env->NewObject(boolean_cls, constructor, v ? JNI_TRUE : JNI_FALSE);
                jni_env->DeleteLocalRef(boolean_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disablePairedNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_paired_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jboolean Java_tinyb_dbus_DBusDevice_getTrusted(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_trusted() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_setTrusted(JNIEnv *env, jobject obj, jboolean val)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        bool val_to_write = from_jboolean_to_bool(val);
        obj_device->set_trusted(val_to_write);
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_enableTrustedNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_trusted_notifications([ callback_ptr ] (bool v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass boolean_cls = search_class(*jni_env, "java/lang/Boolean");
                jmethodID constructor = search_method(*jni_env, boolean_cls, "<init>", "(Z)V", false);

                jobject result = jni_env->NewObject(boolean_cls, constructor, v ? JNI_TRUE : JNI_FALSE);
                jni_env->DeleteLocalRef(boolean_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableTrustedNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_trusted_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jboolean Java_tinyb_dbus_DBusDevice_getBlocked(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_blocked() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_setBlocked(JNIEnv *env, jobject obj, jboolean val)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        bool val_to_write = from_jboolean_to_bool(val);
        obj_device->set_blocked(val_to_write);
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_enableBlockedNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_blocked_notifications([ callback_ptr ] (bool v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass boolean_cls = search_class(*jni_env, "java/lang/Boolean");
                jmethodID constructor = search_method(*jni_env, boolean_cls, "<init>", "(Z)V", false);

                jobject result = jni_env->NewObject(boolean_cls, constructor, v ? JNI_TRUE : JNI_FALSE);
                jni_env->DeleteLocalRef(boolean_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableBlockedNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_blocked_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jboolean Java_tinyb_dbus_DBusDevice_getLegacyPairing(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_legacy_pairing() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

jshort Java_tinyb_dbus_DBusDevice_getRSSI(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return (jshort)obj_device->get_rssi();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return 0;
}

void Java_tinyb_dbus_DBusDevice_enableRSSINotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_rssi_notifications([ callback_ptr ] (int16_t v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass short_cls = search_class(*jni_env, "java/lang/Short");
                jmethodID constructor = search_method(*jni_env, short_cls, "<init>", "(S)V", false);

                jobject result = jni_env->NewObject(short_cls, constructor, (jshort) v);
                jni_env->DeleteLocalRef(short_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableRSSINotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_rssi_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jboolean Java_tinyb_dbus_DBusDevice_getConnected(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_connected() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_enableConnectedNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_connected_notifications([ callback_ptr ] (bool v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass boolean_cls = search_class(*jni_env, "java/lang/Boolean");
                jmethodID constructor = search_method(*jni_env, boolean_cls, "<init>", "(Z)V", false);

                jobject result = jni_env->NewObject(boolean_cls, constructor, v ? JNI_TRUE : JNI_FALSE);
                jni_env->DeleteLocalRef(boolean_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableConnectedNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_connected_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jobjectArray Java_tinyb_dbus_DBusDevice_getUUIDs(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::vector<std::string> uuids = obj_device->get_uuids();
        unsigned int uuids_size = uuids.size();

        jclass string_class = search_class(env, "Ljava/lang/String;");
        jobjectArray result = env->NewObjectArray(uuids_size, string_class, 0);

        for (unsigned int i = 0; i < uuids_size; ++i)
        {
            std::string str_elem = uuids.at(i);
            jobject elem = env->NewStringUTF((const char *)str_elem.c_str());
            env->SetObjectArrayElement(result, i, elem);
        }

        return result;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jstring Java_tinyb_dbus_DBusDevice_getModalias(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        std::unique_ptr<std::string> modalias = obj_device->get_modalias();
        if (modalias == nullptr)
            return nullptr;

        return env->NewStringUTF((const char *)modalias->c_str());
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jobject Java_tinyb_dbus_DBusDevice_getAdapter(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        BluetoothAdapter *obj_adapter = obj_device->get_adapter().clone();

        jclass b_adapter_class = search_class(env, *obj_adapter);
        jmethodID b_adapter_ctor = search_method(env, b_adapter_class, "<init>",
                                                "(J)V", false);
        jobject result = env->NewObject(b_adapter_class, b_adapter_ctor, (jlong)obj_adapter);
        if (!result)
        {
            throw std::bad_alloc();
        }

        return result;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

jobject Java_tinyb_dbus_DBusDevice_getManufacturerData(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        auto mdata = obj_device->get_manufacturer_data();

        jclass map_cls = search_class(env, "java/util/HashMap");
        jmethodID map_ctor = search_method(env, map_cls, "<init>",
                                            "(I)V", false);
        jmethodID map_put = search_method(env, map_cls, "put",
                                            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                            false);

        jclass short_cls = search_class(env, "java/lang/Short");
        jmethodID short_ctor = search_method(env, short_cls, "<init>",
                                            "(S)V", false);

        jobject result = env->NewObject(map_cls, map_ctor, mdata.size());

        for (auto it: mdata) {
            jbyteArray arr = env->NewByteArray(it.second.size());
            env->SetByteArrayRegion(arr, 0, it.second.size(), (const jbyte *)it.second.data());
            jobject key = env->NewObject(short_cls, short_ctor, it.first);
            env->CallObjectMethod(result, map_put, key, arr);

            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(key);
        }

        if (!result)
        {
            throw std::bad_alloc();
        }

        return result;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

void Java_tinyb_dbus_DBusDevice_enableManufacturerDataNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_manufacturer_data_notifications([ callback_ptr ] (std::map<uint16_t, std::vector<uint8_t>> v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass map_cls = search_class(*jni_env, "java/util/HashMap");
                jmethodID map_ctor = search_method(*jni_env, map_cls, "<init>",
                                                    "(I)V", false);
                jmethodID map_put = search_method(*jni_env, map_cls, "put",
                                                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                                   false);

                jclass short_cls = search_class(*jni_env, "java/lang/Short");
                jmethodID short_ctor = search_method(*jni_env, short_cls, "<init>",
                                                    "(S)V", false);

                jobject result = jni_env->NewObject(map_cls, map_ctor, v.size());
                jni_env->DeleteLocalRef(map_cls);
                for (auto it: v) {
                    jbyteArray arr = jni_env->NewByteArray(it.second.size());
                    jni_env->SetByteArrayRegion(arr, 0, it.second.size(), (const jbyte *)it.second.data());
                    jobject key = jni_env->NewObject(short_cls, short_ctor, it.first);
                    jni_env->CallObjectMethod(result, map_put, key, arr);

                    jni_env->DeleteLocalRef(arr);
                    jni_env->DeleteLocalRef(key);
                }

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);
                jni_env->DeleteLocalRef(short_cls);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableManufacturerDataNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_service_data_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

jobject Java_tinyb_dbus_DBusDevice_getServiceData(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);
        auto mdata = obj_device->get_service_data();

        jclass map_cls = search_class(env, "java/util/HashMap");
        jmethodID map_ctor = search_method(env, map_cls, "<init>",
                                            "(I)V", false);
        jmethodID map_put = search_method(env, map_cls, "put",
                                            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                            false);

        jobject result = env->NewObject(map_cls, map_ctor, mdata.size());

        for (auto it: mdata) {
            jbyteArray arr = env->NewByteArray(it.second.size());
            env->SetByteArrayRegion(arr, 0, it.second.size(), (const jbyte *)it.second.data());
            jobject key = env->NewStringUTF(it.first.c_str());
            env->CallObjectMethod(result, map_put, key, arr);

            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(key);
        }

        if (!result)
        {
            throw std::bad_alloc();
        }

        return result;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return nullptr;
}

void Java_tinyb_dbus_DBusDevice_enableServiceDataNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_service_data_notifications([ callback_ptr ] (std::map<std::string, std::vector<uint8_t>> v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass map_cls = search_class(*jni_env, "java/util/HashMap");
                jmethodID map_ctor = search_method(*jni_env, map_cls, "<init>",
                                                    "(I)V", false);
                jmethodID map_put = search_method(*jni_env, map_cls, "put",
                                                   "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                                   false);

                jobject result = jni_env->NewObject(map_cls, map_ctor, v.size());
                jni_env->DeleteLocalRef(map_cls);

                for (auto it: v) {
                    jbyteArray arr = jni_env->NewByteArray(it.second.size());
                    jni_env->SetByteArrayRegion(arr, 0, it.second.size(), (const jbyte *)it.second.data());
                    jobject key = jni_env->NewStringUTF(it.first.c_str());
                    jni_env->CallObjectMethod(result, map_put, key, arr);

                    jni_env->DeleteLocalRef(arr);
                    jni_env->DeleteLocalRef(key);
                }

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);
            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableServiceDataNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_service_data_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}



jshort Java_tinyb_dbus_DBusDevice_getTxPower(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return (jshort)obj_device->get_tx_power();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return 0;
}

jboolean Java_tinyb_dbus_DBusDevice_getServicesResolved(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device = getInstance<BluetoothDevice>(env, obj);

        return obj_device->get_services_resolved() ? JNI_TRUE : JNI_FALSE;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
    return JNI_FALSE;
}

void Java_tinyb_dbus_DBusDevice_enableServicesResolvedNotifications(JNIEnv *env, jobject obj, jobject callback)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        std::shared_ptr<JNIGlobalRef> callback_ptr(new JNIGlobalRef(callback));
        obj_device->enable_services_resolved_notifications([ callback_ptr ] (bool v)
            {
                jclass notification = search_class(*jni_env, **callback_ptr);
                jmethodID  method = search_method(*jni_env, notification, "run", "(Ljava/lang/Object;)V", false);
                jni_env->DeleteLocalRef(notification);

                jclass boolean_cls = search_class(*jni_env, "java/lang/Boolean");
                jmethodID constructor = search_method(*jni_env, boolean_cls, "<init>", "(Z)V", false);

                jobject result = jni_env->NewObject(boolean_cls, constructor, v ? JNI_TRUE : JNI_FALSE);
                jni_env->DeleteLocalRef(boolean_cls);

                jni_env->CallVoidMethod(**callback_ptr, method, result);
                jni_env->DeleteLocalRef(result);

            });
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}

void Java_tinyb_dbus_DBusDevice_disableServicesResolvedNotifications(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *obj_device =
                                    getInstance<BluetoothDevice>(env, obj);
        obj_device->disable_services_resolved_notifications();
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}



void Java_tinyb_dbus_DBusDevice_delete(JNIEnv *env, jobject obj)
{
    try {
        BluetoothDevice *b_device = getInstance<BluetoothDevice>(env, obj);
        delete b_device;
    } catch(...) {
        rethrow_and_raise_java_exception(env);
    }
}


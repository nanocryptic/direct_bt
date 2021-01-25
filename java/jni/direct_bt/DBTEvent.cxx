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

#include "jau_direct_bt_DBTEvent.h"

jobject Java_jau_direct_1bt_DBTEvent_getType(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;

    return NULL;
}

jstring Java_jau_direct_1bt_DBTEvent_getName(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;

    return NULL;
}

jstring Java_jau_direct_1bt_DBTEvent_getIdentifier(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;

    return NULL;
}

jboolean Java_jau_direct_1bt_DBTEvent_executeCallback(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;

    return JNI_FALSE;
}

jboolean Java_jau_direct_1bt_DBTEvent_hasCallback(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;

    return JNI_FALSE;
}

void Java_jau_direct_1bt_DBTEvent_init(JNIEnv *env, jobject obj, jobject type, jstring name,
                                         jstring identifier, jobject parent, jobject callback,
                                         jobject arg_data)
{
    (void)env;
    (void)obj;
    (void)type;
    (void)name;
    (void)identifier;
    (void)parent;
    (void)callback;
    (void)arg_data;
}

void Java_jau_direct_1bt_DBTEvent_delete(JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;
}


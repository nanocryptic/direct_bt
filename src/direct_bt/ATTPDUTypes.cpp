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

#include <jau/debug.hpp>

#include "ATTPDUTypes.hpp"


using namespace direct_bt;

#define OPCODE_ENUM(X) \
        X(PDU_UNDEFINED) \
        X(ERROR_RSP) \
        X(EXCHANGE_MTU_REQ) \
        X(EXCHANGE_MTU_RSP) \
        X(FIND_INFORMATION_REQ) \
        X(FIND_INFORMATION_RSP) \
        X(FIND_BY_TYPE_VALUE_REQ) \
        X(FIND_BY_TYPE_VALUE_RSP) \
        X(READ_BY_TYPE_REQ) \
        X(READ_BY_TYPE_RSP) \
        X(READ_REQ) \
        X(READ_RSP) \
        X(READ_BLOB_REQ) \
        X(READ_BLOB_RSP) \
        X(READ_MULTIPLE_REQ) \
        X(READ_MULTIPLE_RSP) \
        X(READ_BY_GROUP_TYPE_REQ) \
        X(READ_BY_GROUP_TYPE_RSP) \
        X(WRITE_REQ) \
        X(WRITE_RSP) \
        X(WRITE_CMD) \
        X(PREPARE_WRITE_REQ) \
        X(PREPARE_WRITE_RSP) \
        X(EXECUTE_WRITE_REQ) \
        X(EXECUTE_WRITE_RSP) \
        X(READ_MULTIPLE_VARIABLE_REQ) \
        X(READ_MULTIPLE_VARIABLE_RSP) \
        X(MULTIPLE_HANDLE_VALUE_NTF) \
        X(HANDLE_VALUE_NTF) \
        X(HANDLE_VALUE_IND) \
        X(HANDLE_VALUE_CFM) \
        X(SIGNED_WRITE_CMD)

#define CASE_TO_STRING(V) case Opcode::V: return #V;

std::string AttPDUMsg::getOpcodeString(const Opcode opc) noexcept {
    switch(opc) {
        OPCODE_ENUM(CASE_TO_STRING)
        default: ; // fall through intended
    }
    return "Unknown Opcode";
}

std::string AttErrorRsp::getPlainErrorString(const ErrorCode errorCode) noexcept {
    switch(errorCode) {
        case ErrorCode::INVALID_HANDLE: return "Invalid Handle";
        case ErrorCode::NO_READ_PERM: return "Read Not Permitted";
        case ErrorCode::NO_WRITE_PERM: return "Write Not Permitted";
        case ErrorCode::INVALID_PDU: return "Invalid PDU";
        case ErrorCode::INSUFF_AUTHENTICATION: return "Insufficient Authentication";
        case ErrorCode::UNSUPPORTED_REQUEST: return "Request Not Supported";
        case ErrorCode::INVALID_OFFSET: return "Invalid Offset";
        case ErrorCode::INSUFF_AUTHORIZATION: return "Insufficient Authorization";
        case ErrorCode::PREPARE_QUEUE_FULL: return "Prepare Queue Full";
        case ErrorCode::ATTRIBUTE_NOT_FOUND: return "Attribute Not Found";
        case ErrorCode::ATTRIBUTE_NOT_LONG: return "Attribute Not Long";
        case ErrorCode::INSUFF_ENCRYPTION_KEY_SIZE: return "Insufficient Encryption Key Size";
        case ErrorCode::INVALID_ATTRIBUTE_VALUE_LEN: return "Invalid Attribute Value Length";
        case ErrorCode::UNLIKELY_ERROR: return "Unlikely Error";
        case ErrorCode::INSUFF_ENCRYPTION: return "Insufficient Encryption";
        case ErrorCode::UNSUPPORTED_GROUP_TYPE: return "Unsupported Group Type";
        case ErrorCode::INSUFFICIENT_RESOURCES: return "Insufficient Resources";
        case ErrorCode::DB_OUT_OF_SYNC: return "Database Out Of Sync";
        case ErrorCode::FORBIDDEN_VALUE: return "Value Not Allowed";
        default: ; // fall through intended
    }
    if( 0x80 <= number(errorCode) && number(errorCode) <= 0x9F ) {
        return "Application Error";
    }
    if( 0xE0 <= number(errorCode) /* && number(errorCode) <= 0xFF */ ) {
        return "Common Profile and Services Error";
    }
    return "Error Reserved for future use";
}

std::shared_ptr<const AttPDUMsg> AttPDUMsg::getSpecialized(const uint8_t * buffer, jau::nsize_t const buffer_size) noexcept {
    const AttPDUMsg::Opcode opc = static_cast<AttPDUMsg::Opcode>(*buffer);
    const AttPDUMsg * res;
    switch( opc ) {
        case Opcode::PDU_UNDEFINED: res = new AttPDUUndefined(buffer, buffer_size); break;
        case Opcode::ERROR_RSP: res = new AttErrorRsp(buffer, buffer_size); break;
        case Opcode::EXCHANGE_MTU_REQ: res = new AttExchangeMTU(buffer, buffer_size); break;
        case Opcode::EXCHANGE_MTU_RSP: res = new AttExchangeMTU(buffer, buffer_size); break;
        case Opcode::FIND_INFORMATION_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::FIND_INFORMATION_RSP: res = new AttFindInfoRsp(buffer, buffer_size); break;
        case Opcode::FIND_BY_TYPE_VALUE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::FIND_BY_TYPE_VALUE_RSP: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_BY_TYPE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_BY_TYPE_RSP: res = new AttReadByTypeRsp(buffer, buffer_size); break;
        case Opcode::READ_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_RSP: res = new AttReadRsp(buffer, buffer_size); break;
        case Opcode::READ_BLOB_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_BLOB_RSP: res = new AttReadBlobRsp(buffer, buffer_size); break;
        case Opcode::READ_MULTIPLE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_MULTIPLE_RSP: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_BY_GROUP_TYPE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_BY_GROUP_TYPE_RSP: res = new AttReadByGroupTypeRsp(buffer, buffer_size); break;
        case Opcode::WRITE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::WRITE_RSP: res = new AttWriteRsp(buffer, buffer_size); break;
        case Opcode::WRITE_CMD: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::PREPARE_WRITE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::PREPARE_WRITE_RSP: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::EXECUTE_WRITE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::EXECUTE_WRITE_RSP: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_MULTIPLE_VARIABLE_REQ: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::READ_MULTIPLE_VARIABLE_RSP: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::MULTIPLE_HANDLE_VALUE_NTF: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::HANDLE_VALUE_NTF: res = new AttHandleValueRcv(buffer, buffer_size); break;
        case Opcode::HANDLE_VALUE_IND: res = new AttHandleValueRcv(buffer, buffer_size); break;
        case Opcode::HANDLE_VALUE_CFM: res = new AttPDUMsg(buffer, buffer_size); break;
        case Opcode::SIGNED_WRITE_CMD: res = new AttPDUMsg(buffer, buffer_size); break;
        default: res = new AttPDUMsg(buffer, buffer_size); break;
    }
    return std::shared_ptr<const AttPDUMsg>(res);
}

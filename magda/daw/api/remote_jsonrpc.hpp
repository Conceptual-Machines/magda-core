// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "remote_api.hpp"

namespace magda::remote {

// The WebSocket transport's JSON-RPC error codes, shared with the OpenRPC document. JSON-RPC 2.0
// reserves -32768..-32000; -32099..-32000 is the implementation-defined slice, where MAGDA's own
// failure modes land.
inline constexpr int JSONRPC_PARSE_ERROR = -32700;
inline constexpr int JSONRPC_INVALID_REQUEST = -32600;
inline constexpr int JSONRPC_METHOD_NOT_FOUND = -32601;
inline constexpr int JSONRPC_INVALID_PARAMS = -32602;
inline constexpr int JSONRPC_INTERNAL_ERROR = -32603;
inline constexpr int JSONRPC_NOT_FOUND = -32001;
inline constexpr int JSONRPC_CONFLICT = -32002;
inline constexpr int JSONRPC_TIMEOUT = -32003;
inline constexpr int JSONRPC_CANCELLED = -32004;
inline constexpr int JSONRPC_TOO_MANY_REQUESTS = -32005;
/// Refused by the client's grant (#1860): its own code, so "ask the user to grant this" reads
/// apart from "I sent something wrong" without parsing a message.
inline constexpr int JSONRPC_PERMISSION_DENIED = -32006;

/**
 * @brief MAGDA's error taxonomy onto JSON-RPC's.
 *
 * The standard codes where they mean the same thing, so a generic client reports something
 * sensible; `error.data.code` always carries the MAGDA name.
 */
constexpr int jsonRpcCodeFor(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidRequest:
            return JSONRPC_INVALID_REQUEST;
        case ErrorCode::UnknownOperation:
            return JSONRPC_METHOD_NOT_FOUND;
        case ErrorCode::PermissionDenied:
            return JSONRPC_PERMISSION_DENIED;
        case ErrorCode::ValidationFailed:
            return JSONRPC_INVALID_PARAMS;
        case ErrorCode::NotFound:
            return JSONRPC_NOT_FOUND;
        case ErrorCode::Conflict:
            return JSONRPC_CONFLICT;
        case ErrorCode::Timeout:
            return JSONRPC_TIMEOUT;
        case ErrorCode::Cancelled:
            return JSONRPC_CANCELLED;
        case ErrorCode::InternalError:
            break;
    }
    return JSONRPC_INTERNAL_ERROR;
}

}  // namespace magda::remote

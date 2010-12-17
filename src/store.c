/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Membase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include "internal.h"

/**
 * Spool a store request
 *
 * @author Trond Norbye
 * @todo add documentation
 * @todo fix the expiration so that it works relative/absolute etc..
 * @todo we might want to wait to write the data to the sockets if the
 *       user want to run a batch of store requests?
 */
LIBMEMBASE_API
libmembase_error_t libmembase_store(libmembase_t instance,
                                    libmembase_storage_t operation,
                                    const void *key, size_t nkey,
                                    const void *bytes, size_t nbytes,
                                    uint32_t flags, time_t exp,
                                    uint64_t cas)
{
    // we need a vbucket config before we can start getting data..
    libmembase_ensure_vbucket_config(instance);
    assert(instance->vbucket_config);

    uint16_t vb;
    vb = (uint16_t)vbucket_get_vbucket_by_key(instance->vbucket_config,
                                              key, nkey);
    libmembase_server_t *server;
    server = instance->servers + instance->vb_server_map[vb];
    protocol_binary_request_set req = {
        .message.header.request = {
            .magic = PROTOCOL_BINARY_REQ,
            .keylen = ntohs((uint16_t)nkey),
            .extlen = 8,
            .datatype = PROTOCOL_BINARY_RAW_BYTES,
            .vbucket = ntohs(vb),
            .opaque = ++instance->seqno,
            .cas = cas
        },
        .message.body = {
            .flags = flags,
            .expiration = htonl((uint32_t)exp)
        }
    };

    size_t headersize = sizeof(req.bytes);
    switch (operation) {
    case LIBMEMBASE_ADD:
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_ADD;
        break;
    case LIBMEMBASE_REPLACE:
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_REPLACE;
        break;
    case LIBMEMBASE_SET:
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_SET;
        break;
    case LIBMEMBASE_APPEND:
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_APPEND;
        req.message.header.request.extlen = 0;
        headersize -= 8;
        break;
    case LIBMEMBASE_PREPEND:
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_PREPEND;
        req.message.header.request.extlen = 0;
        headersize -= 8;
        break;
    default:
        abort();
    }

    size_t bodylen = nkey + nbytes + req.message.header.request.extlen;
    req.message.header.request.bodylen = htonl((uint32_t)bodylen);

    grow_buffer(&server->output, headersize + bodylen);
    memcpy(server->output.data + server->output.avail, &req, headersize);
    server->output.avail += headersize;
    memcpy(server->output.data + server->output.avail, key, nkey);
    server->output.avail += nkey;
    memcpy(server->output.data + server->output.avail, bytes, nbytes);
    server->output.avail += nbytes;

    // @todo we might want to wait to flush the buffers..
    libmembase_server_event_handler(0, EV_WRITE, server);

    return LIBMEMBASE_SUCCESS;
}
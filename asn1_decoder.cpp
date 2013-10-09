/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <string.h>

#include "asn1_decoder.h"

typedef struct asn1_context {
    uint8_t* buffer;
    size_t length;
    int app_type;
    uint8_t* p;
} asn1_context_t;

asn1_context_t* asn1_context_new(uint8_t* buffer, size_t length) {
    asn1_context_t* ctx = (asn1_context_t*) calloc(1, sizeof(asn1_context_t));
    ctx->buffer = ctx->p = buffer;
    ctx->length = length;
    return ctx;
}

void asn1_context_free(asn1_context_t* ctx) {
    free(ctx);
}

static int decode_length(uint8_t** start, size_t* out_len) {
    size_t num_octets = **start & 0xFF;
    if ((num_octets & 0x80) == 0x00) {
        *out_len = num_octets;
        *start = *start + 1;
        return 1;
    }
    num_octets &= 0x7F;
    if (num_octets >= sizeof(size_t)) {
        return 0;
    }
    uint8_t* end = *start + 1 + num_octets;
    size_t length = 0;
    for (uint8_t* p = *start + 1; p != end; ++p) {
        length <<= 8;
        length += *p;
    }
    *out_len = length;
    *start = end;
    return 1;
}

/**
 * Returns the constructed type and advances the pointer. E.g. A0 -> 0
 */
asn1_context_t* asn1_constructed_get(asn1_context_t* ctx) {
    int type = *ctx->p;
    if ((type & 0xE0) != 0xA0) {
        return NULL;
    }

    uint8_t* p = ctx->p + 1;
    size_t length;
    if (!decode_length(&p, &length) || (p - ctx->buffer) + length > ctx->length) {
        return NULL;
    }
    asn1_context_t* app_ctx = asn1_context_new(p, length);
    app_ctx->app_type = type & 0x1F;
    return app_ctx;
}

int asn1_constructed_type(asn1_context_t* ctx) {
    return ctx->app_type;
}

asn1_context_t* asn1_sequence_get(asn1_context_t* ctx) {
    if ((*ctx->p & 0x7F) != 0x30) {
        return NULL;
    }

    uint8_t* p = ctx->p + 1;
    size_t length;
    if (!decode_length(&p, &length) || (p - ctx->buffer) + length > ctx->length) {
        return NULL;
    }
    return asn1_context_new(p, length);
}

asn1_context_t* asn1_set_get(asn1_context_t* ctx) {
    if ((*ctx->p & 0x7F) != 0x31) {
        return NULL;
    }

    uint8_t* p = ctx->p + 1;
    size_t length;
    if (!decode_length(&p, &length) || (p - ctx->buffer) + length > ctx->length) {
        return NULL;
    }
    return asn1_context_new(p, length);
}

int asn1_sequence_next(asn1_context_t* seq) {
    size_t length;
    uint8_t* p = seq->p + 1;
    if (!decode_length(&p, &length)) {
        return 0;
    }
    if ((p - seq->buffer) + length > seq->length) {
        return 0;
    }
    seq->p = p + length;
    return 1;
}

int asn1_oid_get(asn1_context_t* ctx, uint8_t** oid, size_t* length) {
    if (*ctx->p != 0x06) {
        return 0;
    }
    *oid = ctx->p + 1;
    if (!decode_length(oid, length)) {
        return 0;
    }
    return 1;
}

int asn1_octet_string_get(asn1_context_t* ctx, uint8_t** octet_string, size_t* length) {
    if (*ctx->p != 0x04) {
        return 0;
    }
    *octet_string = ctx->p + 1;
    if (!decode_length(octet_string, length)) {
        return 0;
    }
    return 1;
}

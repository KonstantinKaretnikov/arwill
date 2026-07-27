#include <stddef.h>
#include <stdint.h>

#include <bearssl.h>

#include <arwill/user/tls.h>

#include "trust_anchors.c"

enum {
    seconds_per_day = 86400,
    bearssl_days_from_year_zero_to_unix_epoch = 719528
};

static int transport_read(void *context, unsigned char *output, size_t capacity) {
    const struct arwill_tls_transport *transport = context;
    return transport->read(transport->context, output, capacity);
}

static int transport_write(
    void *context,
    const unsigned char *input,
    size_t length
) {
    const struct arwill_tls_transport *transport = context;
    return transport->write(transport->context, input, length);
}

static void initialize_client(
    br_ssl_client_context *client,
    br_x509_minimal_context *x509
) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
    };

    br_ssl_client_zero(client);
    br_ssl_engine_set_versions(&client->eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(
        &client->eng, suites, sizeof(suites) / sizeof(suites[0]));

    br_x509_minimal_init(x509, &br_sha256_vtable, TAs, TAs_NUM);
    br_ssl_client_set_default_rsapub(client);
    br_ssl_engine_set_default_rsavrfy(&client->eng);
    br_ssl_engine_set_default_ecdsa(&client->eng);
    br_x509_minimal_set_rsa(
        x509, br_ssl_engine_get_rsavrfy(&client->eng));
    br_x509_minimal_set_ecdsa(
        x509,
        br_ssl_engine_get_ec(&client->eng),
        br_ssl_engine_get_ecdsa(&client->eng));

    br_ssl_engine_set_hash(&client->eng, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(&client->eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&client->eng, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_hash(&client->eng, br_sha512_ID, &br_sha512_vtable);
    br_x509_minimal_set_hash(x509, br_sha1_ID, &br_sha1_vtable);
    br_x509_minimal_set_hash(x509, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(x509, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(x509, br_sha512_ID, &br_sha512_vtable);

    br_ssl_engine_set_x509(&client->eng, &x509->vtable);
    br_ssl_engine_set_prf_sha256(&client->eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&client->eng, &br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&client->eng);
}

int arwill_tls_exchange(
    const char *host,
    uint64_t unix_seconds,
    const uint8_t *entropy,
    size_t entropy_length,
    const struct arwill_tls_transport *transport,
    const uint8_t *request,
    size_t request_length,
    arwill_tls_receive_fn receive,
    void *receive_context,
    int *tls_error
) {
    if (host == 0 || host[0] == '\0' || entropy == 0 ||
        entropy_length < 16U || transport == 0 ||
        transport->read == 0 || transport->write == 0 ||
        request == 0 || request_length == 0U || receive == 0 ||
        tls_error == 0) {
        return arwill_tls_invalid;
    }

    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    unsigned char io_buffer[BR_SSL_BUFSIZE_BIDI];
    uint8_t input[256];

    initialize_client(&client, &x509);
    br_x509_minimal_set_time(
        &x509,
        (uint32_t)(unix_seconds / seconds_per_day) +
            bearssl_days_from_year_zero_to_unix_epoch,
        (uint32_t)(unix_seconds % seconds_per_day)
    );
    br_ssl_engine_set_buffer(
        &client.eng, io_buffer, sizeof(io_buffer), 1);
    br_ssl_engine_inject_entropy(&client.eng, entropy, entropy_length);
    if (!br_ssl_client_reset(&client, host, 0)) {
        *tls_error = br_ssl_engine_last_error(&client.eng);
        return arwill_tls_handshake_failed;
    }
    br_sslio_init(
        &io, &client.eng,
        transport_read, (void *)transport,
        transport_write, (void *)transport
    );
    if (br_sslio_write_all(&io, request, request_length) != 0 ||
        br_sslio_flush(&io) != 0) {
        *tls_error = br_ssl_engine_last_error(&client.eng);
        return *tls_error == BR_ERR_IO
            ? arwill_tls_transport_failed : arwill_tls_handshake_failed;
    }

    int received = 0;
    for (;;) {
        const int count = br_sslio_read(&io, input, sizeof(input));
        if (count < 0) {
            break;
        }
        received = 1;
        const int receive_result =
            receive(receive_context, input, (size_t)count);
        if (receive_result == 0) {
            *tls_error = br_ssl_engine_last_error(&client.eng);
            return arwill_tls_output_failed;
        }
        if (receive_result > 1) {
            *tls_error = BR_ERR_OK;
            return arwill_tls_ok;
        }
    }

    *tls_error = br_ssl_engine_last_error(&client.eng);
    if (*tls_error == BR_ERR_OK ||
        (received && *tls_error == BR_ERR_IO)) {
        return arwill_tls_ok;
    }
    return *tls_error == BR_ERR_IO
        ? arwill_tls_transport_failed : arwill_tls_handshake_failed;
}

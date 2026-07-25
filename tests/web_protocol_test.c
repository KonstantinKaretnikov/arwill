#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arwill/user/dns.h>
#include <arwill/user/http.h>

static void test_url_parse(void) {
    struct arwill_http_url url;
    assert(arwill_http_parse_url(
        "http://example.com/api/items?q=one", &url) == arwill_http_ok);
    assert(strcmp(url.host, "example.com") == 0);
    assert(strcmp(url.path, "/api/items?q=one") == 0);
    assert(url.port == 80U);

    assert(arwill_http_parse_url(
        "http://service.local:8080", &url) == arwill_http_ok);
    assert(strcmp(url.host, "service.local") == 0);
    assert(strcmp(url.path, "/") == 0);
    assert(url.port == 8080U);

    assert(arwill_http_parse_url(
        "https://example.com/", &url) == arwill_http_unsupported);
    assert(arwill_http_parse_url("http:///missing", &url) ==
        arwill_http_invalid);
    assert(arwill_http_parse_url("http://example.com:0/", &url) ==
        arwill_http_invalid);
}

static void test_http_requests(void) {
    struct arwill_http_url url;
    uint8_t request[512];
    size_t length = 0;
    assert(arwill_http_parse_url(
        "http://example.com/status", &url) == arwill_http_ok);
    assert(arwill_http_build_request(
        arwill_http_method_get, &url, 0, 0U,
        request, sizeof(request), &length) == arwill_http_ok);
    const char expected_get[] =
        "GET /status HTTP/1.0\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "User-Agent: arwill-curl/0\r\n"
        "\r\n";
    assert(length == sizeof(expected_get) - 1U);
    assert(memcmp(request, expected_get, length) == 0);

    static const uint8_t body[] = "hello";
    assert(arwill_http_parse_url(
        "http://example.com:8080/submit", &url) == arwill_http_ok);
    assert(arwill_http_build_request(
        arwill_http_method_post, &url, body, sizeof(body) - 1U,
        request, sizeof(request), &length) == arwill_http_ok);
    const char expected_post[] =
        "POST /submit HTTP/1.0\r\n"
        "Host: example.com:8080\r\n"
        "Connection: close\r\n"
        "User-Agent: arwill-curl/0\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    assert(length == sizeof(expected_post) - 1U);
    assert(memcmp(request, expected_post, length) == 0);
    assert(arwill_http_build_request(
        arwill_http_method_post, &url, body, sizeof(body) - 1U,
        request, 16U, &length) == arwill_http_too_large);
}

static void test_dns_query_and_response(void) {
    uint8_t query[256];
    size_t query_length = 0;
    assert(arwill_dns_build_a_query(
        "www.example.com", 0x1234U, query, sizeof(query), &query_length) ==
        arwill_dns_ok);
    static const uint8_t expected_query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'w', 'w', 'w',
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };
    assert(query_length == sizeof(expected_query));
    assert(memcmp(query, expected_query, query_length) == 0);

    static const uint8_t response[] = {
        0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'w', 'w', 'w',
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01,
        0xc0, 0x0c, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x3c, 0x00, 0x02, 0xc0, 0x10,
        0xc0, 0x10, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x3c, 0x00, 0x04, 93, 184, 216, 34
    };
    uint32_t address = 0U;
    assert(arwill_dns_parse_a_response(
        response, sizeof(response), 0x1234U, &address) == arwill_dns_ok);
    assert(address == 0x5db8d822U);
    assert(arwill_dns_parse_a_response(
        response, sizeof(response) - 1U, 0x1234U, &address) ==
        arwill_dns_invalid);
    assert(arwill_dns_parse_a_response(
        response, sizeof(response), 0x4321U, &address) ==
        arwill_dns_invalid);

    static const uint8_t no_answer[] = {
        0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'w', 'w', 'w',
        0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x03, 'c', 'o', 'm', 0x00,
        0x00, 0x01, 0x00, 0x01
    };
    assert(arwill_dns_parse_a_response(
        no_answer, sizeof(no_answer), 0x1234U, &address) ==
        arwill_dns_not_found);
}

int main(void) {
    test_url_parse();
    test_http_requests();
    test_dns_query_and_response();
    return 0;
}

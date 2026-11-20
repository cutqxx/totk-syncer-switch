#include <stdint.h>

#include "ipv4.h"
#include "test.h"

static void parses_well_formed_dotted_quads(void)
{
    static const struct { const char *text; uint8_t octets[4]; } cases[] = {
        { "192.168.1.10", { 192, 168, 1, 10 } },
        { "0.0.0.0", { 0, 0, 0, 0 } },
        { "255.255.255.255", { 255, 255, 255, 255 } },
        { "10.0.0.1", { 10, 0, 0, 1 } },
        { "1.20.30.40", { 1, 20, 30, 40 } },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t out[4] = { 9, 9, 9, 9 };
        CHECK(ipv4_parse(cases[i].text, out));
        CHECK(memcmp(out, cases[i].octets, 4) == 0);
    }
}

static void rejects_malformed_addresses(void)
{
    static const char *const cases[] = {
        "010.0.0.1", "10.00.0.1", "10.0.0.01", "00.0.0.1",   /* leading zeros */
        " 10.0.0.1", "10.0.0.1 ", "10. 0.0.1", "10.0.0.1\n", /* blanks */
        "+10.0.0.1", "-10.0.0.1", "10.+1.0.1",               /* signs */
        "10.0.0", "10", "10.0.0.1.1", "1.2.3.4.5.6",         /* wrong octet count */
        "256.0.0.1", "10.0.0.256", "999.1.1.1", "1.1.1.99999999999999999999", /* out of range */
        "", ".", "...", "10..0.1", ".10.0.0.1", "10.0.0.1.", /* empty parts, trailing dot */
        "10.0.0.a", "a.b.c.d", "0x10.0.0.1", "10,0,0,1", "10.0.0.1/8", "10.0.0.1:80",
        "example.com", "localhost",
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t out[4] = { 7, 7, 7, 7 };
        const uint8_t untouched[4] = { 7, 7, 7, 7 };
        if (ipv4_parse(cases[i], out)) {
            printf("accepted bad address: \"%s\"\n", cases[i]);
            CHECK(false);
        } else {
            CHECK(memcmp(out, untouched, 4) == 0);
        }
    }
}

static bool is_private(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    const uint8_t ip[4] = { a, b, c, d };
    return ipv4_is_private(ip);
}

static void accepts_the_edges_of_every_private_range(void)
{
    CHECK(is_private(10, 0, 0, 0));
    CHECK(is_private(10, 255, 255, 255));
    CHECK(is_private(172, 16, 0, 0));
    CHECK(is_private(172, 31, 255, 255));
    CHECK(is_private(192, 168, 0, 0));
    CHECK(is_private(192, 168, 255, 255));
}

static void rejects_addresses_just_outside_the_private_ranges(void)
{
    CHECK(!is_private(9, 255, 255, 255));
    CHECK(!is_private(11, 0, 0, 0));
    CHECK(!is_private(172, 15, 255, 255));
    CHECK(!is_private(172, 32, 0, 0));
    CHECK(!is_private(192, 167, 255, 255));
    CHECK(!is_private(192, 169, 0, 0));
}

static void rejects_loopback_link_local_and_public_addresses(void)
{
    CHECK(!is_private(127, 0, 0, 1));
    CHECK(!is_private(127, 255, 255, 255));
    CHECK(!is_private(169, 254, 1, 1));
    CHECK(!is_private(100, 64, 0, 1));
    CHECK(!is_private(8, 8, 8, 8));
    CHECK(!is_private(0, 0, 0, 0));
    CHECK(!is_private(255, 255, 255, 255));
    CHECK(!is_private(192, 0, 168, 1));
    CHECK(!is_private(168, 192, 0, 1));
}

void run_ipv4_tests(void)
{
    RUN_TEST(parses_well_formed_dotted_quads);
    RUN_TEST(rejects_malformed_addresses);
    RUN_TEST(accepts_the_edges_of_every_private_range);
    RUN_TEST(rejects_addresses_just_outside_the_private_ranges);
    RUN_TEST(rejects_loopback_link_local_and_public_addresses);
}

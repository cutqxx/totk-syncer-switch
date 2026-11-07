#ifndef TS_IPV4_H
#define TS_IPV4_H

#include <stdbool.h>
#include <stdint.h>

/* Strict dotted quad: exactly 4 decimal octets 0-255, no leading zeros, no spaces, no signs. */
bool ipv4_parse(const char *text, uint8_t out[4]);

/* 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16. Loopback (127/8) is NOT accepted. */
bool ipv4_is_private(const uint8_t ip[4]);

#endif

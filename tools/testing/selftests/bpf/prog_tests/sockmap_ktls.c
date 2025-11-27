// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
/*
 * Tests for sockmap/sockhash holding kTLS sockets.
 */

#include <netinet/tcp.h>
#include "test_progs.h"

#define MAX_TEST_NAME 80
#define TCP_ULP 31

static void test_sockmap_ktls_update_fails_when_sock_has_ulp(int family, int map)
{
	struct sockaddr_storage addr = {};
	socklen_t len = sizeof(addr);
	struct sockaddr_in6 *v6;
	struct sockaddr_in *v4;
	int err, s, zero = 0;

	switch (family) {
	case AF_INET:
		v4 = (struct sockaddr_in *)&addr;
		v4->sin_family = AF_INET;
		break;
	case AF_INET6:
		v6 = (struct sockaddr_in6 *)&addr;
		v6->sin6_family = AF_INET6;
		break;
	default:
		PRINT_FAIL("unsupported socket family %d", family);
		return;
	}

	s = socket(family, SOCK_STREAM, 0);
	if (!ASSERT_GE(s, 0, "socket"))
		return;

	err = bind(s, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "bind"))
		goto close;

	err = getsockname(s, (struct sockaddr *)&addr, &len);
	if (!ASSERT_OK(err, "getsockname"))
		goto close;

	err = connect(s, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "connect"))
		goto close;

	/* save sk->sk_prot and set it to tls_prots */
	err = setsockopt(s, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (!ASSERT_OK(err, "setsockopt(TCP_ULP)"))
		goto close;

	/* sockmap update should not affect saved sk_prot */
	err = bpf_map_update_elem(map, &zero, &s, BPF_ANY);
	if (!ASSERT_ERR(err, "sockmap update elem"))
		goto close;

	/* call sk->sk_prot->setsockopt to dispatch to saved sk_prot */
	err = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
	ASSERT_OK(err, "setsockopt(TCP_NODELAY)");

close:
	close(s);
}

static const char *fmt_test_name(const char *subtest_name, int family,
				 enum bpf_map_type map_type)
{
	const char *map_type_str = BPF_MAP_TYPE_SOCKMAP ? "SOCKMAP" : "SOCKHASH";
	const char *family_str = AF_INET ? "IPv4" : "IPv6";
	static char test_name[MAX_TEST_NAME];

	snprintf(test_name, MAX_TEST_NAME,
		 "sockmap_ktls %s %s %s",
		 subtest_name, family_str, map_type_str);

	return test_name;
}

static void run_tests(int family, enum bpf_map_type map_type)
{
	int map;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (!ASSERT_GE(map, 0, "bpf_map_create"))
		return;

	if (test__start_subtest(fmt_test_name("update_fails_when_sock_has_ulp", family, map_type)))
		test_sockmap_ktls_update_fails_when_sock_has_ulp(family, map);

	close(map);
}

void test_sockmap_ktls(void)
{
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKHASH);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKHASH);
}

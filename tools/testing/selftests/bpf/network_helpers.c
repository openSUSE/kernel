// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

#include <arpa/inet.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/eventfd.h>

#include <linux/err.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/limits.h>

#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/tcp.h>
#include <net/if.h>

#include "bpf_util.h"
#include "network_helpers.h"
#include "test_progs.h"

#ifdef TRAFFIC_MONITOR
/* Prevent pcap.h from including pcap/bpf.h and causing conflicts */
#define PCAP_DONT_INCLUDE_PCAP_BPF_H 1
#include <pcap/pcap.h>
#include <pcap/dlt.h>
#endif

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) ({						\
			int __save = errno;				\
			fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
				__FILE__, __LINE__, clean_errno(),	\
				##__VA_ARGS__);				\
			errno = __save;					\
})

struct ipv4_packet pkt_v4 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
	.iph.ihl = 5,
	.iph.protocol = IPPROTO_TCP,
	.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

struct ipv6_packet pkt_v6 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
	.iph.nexthdr = IPPROTO_TCP,
	.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

int settimeo(int fd, int timeout_ms)
{
	struct timeval timeout = { .tv_sec = 3 };

	if (timeout_ms > 0) {
		timeout.tv_sec = timeout_ms / 1000;
		timeout.tv_usec = (timeout_ms % 1000) * 1000;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout))) {
		log_err("Failed to set SO_RCVTIMEO");
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
		       sizeof(timeout))) {
		log_err("Failed to set SO_SNDTIMEO");
		return -1;
	}

	return 0;
}

#define save_errno_close(fd) ({ int __save = errno; close(fd); errno = __save; })

static int __start_server(int type, int protocol, const struct sockaddr *addr,
			  socklen_t addrlen, int timeout_ms, bool reuseport)
{
	int on = 1;
	int fd;

	fd = socket(addr->sa_family, type, protocol);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	if (settimeo(fd, timeout_ms))
		goto error_close;

	if (reuseport &&
	    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) {
		log_err("Failed to set SO_REUSEPORT");
		goto error_close;
	}

	if (bind(fd, addr, addrlen) < 0) {
		log_err("Failed to bind socket");
		goto error_close;
	}

	if (type == SOCK_STREAM) {
		if (listen(fd, 1) < 0) {
			log_err("Failed to listed on socket");
			goto error_close;
		}
	}

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

static int start_server_proto(int family, int type, int protocol,
			      const char *addr_str, __u16 port, int timeout_ms)
{
	struct sockaddr_storage addr;
	socklen_t addrlen;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return -1;

	return __start_server(type, protocol, (struct sockaddr *)&addr,
			      addrlen, timeout_ms, false);
}

int start_server(int family, int type, const char *addr_str, __u16 port,
		 int timeout_ms)
{
	return start_server_proto(family, type, 0, addr_str, port, timeout_ms);
}

int start_mptcp_server(int family, const char *addr_str, __u16 port,
		       int timeout_ms)
{
	return start_server_proto(family, SOCK_STREAM, IPPROTO_MPTCP, addr_str,
				  port, timeout_ms);
}

int *start_reuseport_server(int family, int type, const char *addr_str,
			    __u16 port, int timeout_ms, unsigned int nr_listens)
{
	struct sockaddr_storage addr;
	unsigned int nr_fds = 0;
	socklen_t addrlen;
	int *fds;

	if (!nr_listens)
		return NULL;

	if (make_sockaddr(family, addr_str, port, &addr, &addrlen))
		return NULL;

	fds = malloc(sizeof(*fds) * nr_listens);
	if (!fds)
		return NULL;

	fds[0] = __start_server(type, 0, (struct sockaddr *)&addr, addrlen,
				timeout_ms, true);
	if (fds[0] == -1)
		goto close_fds;
	nr_fds = 1;

	if (getsockname(fds[0], (struct sockaddr *)&addr, &addrlen))
		goto close_fds;

	for (; nr_fds < nr_listens; nr_fds++) {
		fds[nr_fds] = __start_server(type, 0, (struct sockaddr *)&addr,
					     addrlen, timeout_ms, true);
		if (fds[nr_fds] == -1)
			goto close_fds;
	}

	return fds;

close_fds:
	free_fds(fds, nr_fds);
	return NULL;
}

void free_fds(int *fds, unsigned int nr_close_fds)
{
	if (fds) {
		while (nr_close_fds)
			close(fds[--nr_close_fds]);
		free(fds);
	}
}

int fastopen_connect(int server_fd, const char *data, unsigned int data_len,
		     int timeout_ms)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	struct sockaddr_in *addr_in;
	int fd, ret;

	if (getsockname(server_fd, (struct sockaddr *)&addr, &addrlen)) {
		log_err("Failed to get server addr");
		return -1;
	}

	addr_in = (struct sockaddr_in *)&addr;
	fd = socket(addr_in->sin_family, SOCK_STREAM, 0);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, timeout_ms))
		goto error_close;

	ret = sendto(fd, data, data_len, MSG_FASTOPEN, (struct sockaddr *)&addr,
		     addrlen);
	if (ret != data_len) {
		log_err("sendto(data, %u) != %d\n", data_len, ret);
		goto error_close;
	}

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

static int connect_fd_to_addr(int fd,
			      const struct sockaddr_storage *addr,
			      socklen_t addrlen, const bool must_fail)
{
	int ret;

	errno = 0;
	ret = connect(fd, (const struct sockaddr *)addr, addrlen);
	if (must_fail) {
		if (!ret) {
			log_err("Unexpected success to connect to server");
			return -1;
		}
		if (errno != EPERM) {
			log_err("Unexpected error from connect to server");
			return -1;
		}
	} else {
		if (ret) {
			log_err("Failed to connect to server");
			return -1;
		}
	}

	return 0;
}

static const struct network_helper_opts default_opts;

int connect_to_fd_opts(int server_fd, const struct network_helper_opts *opts)
{
	struct sockaddr_storage addr;
	struct sockaddr_in *addr_in;
	socklen_t addrlen, optlen;
	int fd, type, protocol;

	if (!opts)
		opts = &default_opts;

	optlen = sizeof(type);

	if (opts->type) {
		type = opts->type;
	} else {
		if (getsockopt(server_fd, SOL_SOCKET, SO_TYPE, &type, &optlen)) {
			log_err("getsockopt(SOL_TYPE)");
			return -1;
		}
	}

	if (opts->proto) {
		protocol = opts->proto;
	} else {
		if (getsockopt(server_fd, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen)) {
			log_err("getsockopt(SOL_PROTOCOL)");
			return -1;
		}
	}

	addrlen = sizeof(addr);
	if (getsockname(server_fd, (struct sockaddr *)&addr, &addrlen)) {
		log_err("Failed to get server addr");
		return -1;
	}

	addr_in = (struct sockaddr_in *)&addr;
	fd = socket(addr_in->sin_family, type, protocol);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (settimeo(fd, opts->timeout_ms))
		goto error_close;

	if (opts->cc && opts->cc[0] &&
	    setsockopt(fd, SOL_TCP, TCP_CONGESTION, opts->cc,
		       strlen(opts->cc) + 1))
		goto error_close;

	if (!opts->noconnect)
		if (connect_fd_to_addr(fd, &addr, addrlen, opts->must_fail))
			goto error_close;

	return fd;

error_close:
	save_errno_close(fd);
	return -1;
}

int connect_to_fd(int server_fd, int timeout_ms)
{
	struct network_helper_opts opts = {
		.timeout_ms = timeout_ms,
	};

	return connect_to_fd_opts(server_fd, &opts);
}

int connect_fd_to_fd(int client_fd, int server_fd, int timeout_ms)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);

	if (settimeo(client_fd, timeout_ms))
		return -1;

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		return -1;
	}

	if (connect_fd_to_addr(client_fd, &addr, len, false))
		return -1;

	return 0;
}

int make_sockaddr(int family, const char *addr_str, __u16 port,
		  struct sockaddr_storage *addr, socklen_t *len)
{
	if (family == AF_INET) {
		struct sockaddr_in *sin = (void *)addr;

		memset(addr, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		if (addr_str &&
		    inet_pton(AF_INET, addr_str, &sin->sin_addr) != 1) {
			log_err("inet_pton(AF_INET, %s)", addr_str);
			return -1;
		}
		if (len)
			*len = sizeof(*sin);
		return 0;
	} else if (family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (void *)addr;

		memset(addr, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		if (addr_str &&
		    inet_pton(AF_INET6, addr_str, &sin6->sin6_addr) != 1) {
			log_err("inet_pton(AF_INET6, %s)", addr_str);
			return -1;
		}
		if (len)
			*len = sizeof(*sin6);
		return 0;
	}
	return -1;
}

char *ping_command(int family)
{
	if (family == AF_INET6) {
		/* On some systems 'ping' doesn't support IPv6, so use ping6 if it is present. */
		if (!system("which ping6 >/dev/null 2>&1"))
			return "ping6";
		else
			return "ping -6";
	}
	return "ping";
}

int remove_netns(const char *name)
{
	char *cmd;
	int r;

	r = asprintf(&cmd, "ip netns del %s >/dev/null 2>&1", name);
	if (r < 0) {
		log_err("Failed to malloc cmd");
		return -1;
	}

	r = system(cmd);
	free(cmd);
	return r;
}

int make_netns(const char *name)
{
	char *cmd;
	int r;

	r = asprintf(&cmd, "ip netns add %s", name);
	if (r < 0) {
		log_err("Failed to malloc cmd");
		return -1;
	}

	r = system(cmd);
	free(cmd);

	if (r)
		return r;

	r = asprintf(&cmd, "ip -n %s link set lo up", name);
	if (r < 0) {
		log_err("Failed to malloc cmd for setting up lo");
		remove_netns(name);
		return -1;
	}

	r = system(cmd);
	free(cmd);

	return r;
}

struct nstoken {
	int orig_netns_fd;
};

struct nstoken *open_netns(const char *name)
{
	int nsfd;
	char nspath[PATH_MAX];
	int err;
	struct nstoken *token;

	token = calloc(1, sizeof(struct nstoken));
	if (!ASSERT_OK_PTR(token, "malloc token"))
		return NULL;

	token->orig_netns_fd = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_GE(token->orig_netns_fd, 0, "open /proc/self/ns/net"))
		goto fail;

	snprintf(nspath, sizeof(nspath), "%s/%s", "/var/run/netns", name);
	nsfd = open(nspath, O_RDONLY | O_CLOEXEC);
	if (!ASSERT_GE(nsfd, 0, "open netns fd"))
		goto fail;

	err = setns(nsfd, CLONE_NEWNET);
	close(nsfd);
	if (!ASSERT_OK(err, "setns"))
		goto fail;

	return token;
fail:
	if (token->orig_netns_fd != -1)
		close(token->orig_netns_fd);
	free(token);
	return NULL;
}

void close_netns(struct nstoken *token)
{
	if (!token)
		return;

	ASSERT_OK(setns(token->orig_netns_fd, CLONE_NEWNET), "setns");
	close(token->orig_netns_fd);
	free(token);
}

int get_socket_local_port(int sock_fd)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	int err;

	err = getsockname(sock_fd, (struct sockaddr *)&addr, &addrlen);
	if (err < 0)
		return err;

	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&addr;

		return sin->sin_port;
	} else if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&addr;

		return sin->sin6_port;
	}

	return -1;
}

#ifdef TRAFFIC_MONITOR
struct tmonitor_ctx {
	pcap_t *pcap;
	pcap_dumper_t *dumper;
	pthread_t thread;
	int wake_fd;

	volatile bool done;
	char pkt_fname[PATH_MAX];
	int pcap_fd;
};

/* Is this packet captured with a Ethernet protocol type? */
static bool is_ethernet(const u_char *packet)
{
	u16 arphdr_type;

	memcpy(&arphdr_type, packet + 8, 2);
	arphdr_type = ntohs(arphdr_type);

	/* Except the following cases, the protocol type contains the
	 * Ethernet protocol type for the packet.
	 *
	 * https://www.tcpdump.org/linktypes/LINKTYPE_LINUX_SLL2.html
	 */
	switch (arphdr_type) {
	case 770: /* ARPHRD_FRAD */
	case 778: /* ARPHDR_IPGRE */
	case 803: /* ARPHRD_IEEE80211_RADIOTAP */
		printf("Packet captured: arphdr_type=%d\n", arphdr_type);
		return false;
	}
	return true;
}

static const char * const pkt_types[] = {
	"In",
	"B",			/* Broadcast */
	"M",			/* Multicast */
	"C",			/* Captured with the promiscuous mode */
	"Out",
};

static const char *pkt_type_str(u16 pkt_type)
{
	if (pkt_type < ARRAY_SIZE(pkt_types))
		return pkt_types[pkt_type];
	return "Unknown";
}

/* Show the information of the transport layer in the packet */
static void show_transport(const u_char *packet, u16 len, u32 ifindex,
			   const char *src_addr, const char *dst_addr,
			   u16 proto, bool ipv6, u8 pkt_type)
{
	char *ifname, _ifname[IF_NAMESIZE];
	const char *transport_str;
	u16 src_port, dst_port;
	struct udphdr *udp;
	struct tcphdr *tcp;

	ifname = if_indextoname(ifindex, _ifname);
	if (!ifname) {
		snprintf(_ifname, sizeof(_ifname), "unknown(%d)", ifindex);
		ifname = _ifname;
	}

	if (proto == IPPROTO_UDP) {
		udp = (struct udphdr *)packet;
		src_port = ntohs(udp->source);
		dst_port = ntohs(udp->dest);
		transport_str = "UDP";
	} else if (proto == IPPROTO_TCP) {
		tcp = (struct tcphdr *)packet;
		src_port = ntohs(tcp->source);
		dst_port = ntohs(tcp->dest);
		transport_str = "TCP";
	} else if (proto == IPPROTO_ICMP) {
		printf("%-7s %-3s IPv4 %s > %s: ICMP, length %d, type %d, code %d\n",
		       ifname, pkt_type_str(pkt_type), src_addr, dst_addr, len,
		       packet[0], packet[1]);
		return;
	} else if (proto == IPPROTO_ICMPV6) {
		printf("%-7s %-3s IPv6 %s > %s: ICMPv6, length %d, type %d, code %d\n",
		       ifname, pkt_type_str(pkt_type), src_addr, dst_addr, len,
		       packet[0], packet[1]);
		return;
	} else {
		printf("%-7s %-3s %s %s > %s: protocol %d\n",
		       ifname, pkt_type_str(pkt_type), ipv6 ? "IPv6" : "IPv4",
		       src_addr, dst_addr, proto);
		return;
	}

	/* TCP or UDP*/

	flockfile(stdout);
	if (ipv6)
		printf("%-7s %-3s IPv6 %s.%d > %s.%d: %s, length %d",
		       ifname, pkt_type_str(pkt_type), src_addr, src_port,
		       dst_addr, dst_port, transport_str, len);
	else
		printf("%-7s %-3s IPv4 %s:%d > %s:%d: %s, length %d",
		       ifname, pkt_type_str(pkt_type), src_addr, src_port,
		       dst_addr, dst_port, transport_str, len);

	if (proto == IPPROTO_TCP) {
		if (tcp->fin)
			printf(", FIN");
		if (tcp->syn)
			printf(", SYN");
		if (tcp->rst)
			printf(", RST");
		if (tcp->ack)
			printf(", ACK");
	}

	printf("\n");
	funlockfile(stdout);
}

static void show_ipv6_packet(const u_char *packet, u32 ifindex, u8 pkt_type)
{
	char src_buf[INET6_ADDRSTRLEN], dst_buf[INET6_ADDRSTRLEN];
	struct ipv6hdr *pkt = (struct ipv6hdr *)packet;
	const char *src, *dst;
	u_char proto;

	src = inet_ntop(AF_INET6, &pkt->saddr, src_buf, sizeof(src_buf));
	if (!src)
		src = "<invalid>";
	dst = inet_ntop(AF_INET6, &pkt->daddr, dst_buf, sizeof(dst_buf));
	if (!dst)
		dst = "<invalid>";
	proto = pkt->nexthdr;
	show_transport(packet + sizeof(struct ipv6hdr),
		       ntohs(pkt->payload_len),
		       ifindex, src, dst, proto, true, pkt_type);
}

static void show_ipv4_packet(const u_char *packet, u32 ifindex, u8 pkt_type)
{
	char src_buf[INET_ADDRSTRLEN], dst_buf[INET_ADDRSTRLEN];
	struct iphdr *pkt = (struct iphdr *)packet;
	const char *src, *dst;
	u_char proto;

	src = inet_ntop(AF_INET, &pkt->saddr, src_buf, sizeof(src_buf));
	if (!src)
		src = "<invalid>";
	dst = inet_ntop(AF_INET, &pkt->daddr, dst_buf, sizeof(dst_buf));
	if (!dst)
		dst = "<invalid>";
	proto = pkt->protocol;
	show_transport(packet + sizeof(struct iphdr),
		       ntohs(pkt->tot_len),
		       ifindex, src, dst, proto, false, pkt_type);
}

static void *traffic_monitor_thread(void *arg)
{
	char *ifname, _ifname[IF_NAMESIZE];
	const u_char *packet, *payload;
	struct tmonitor_ctx *ctx = arg;
	pcap_dumper_t *dumper = ctx->dumper;
	int fd = ctx->pcap_fd, nfds, r;
	int wake_fd = ctx->wake_fd;
	struct pcap_pkthdr header;
	pcap_t *pcap = ctx->pcap;
	u32 ifindex;
	fd_set fds;
	u16 proto;
	u8 ptype;

	nfds = (fd > wake_fd ? fd : wake_fd) + 1;
	FD_ZERO(&fds);

	while (!ctx->done) {
		FD_SET(fd, &fds);
		FD_SET(wake_fd, &fds);
		r = select(nfds, &fds, NULL, NULL, NULL);
		if (!r)
			continue;
		if (r < 0) {
			if (errno == EINTR)
				continue;
			log_err("Fail to select on pcap fd and wake fd");
			break;
		}

		/* This instance of pcap is non-blocking */
		packet = pcap_next(pcap, &header);
		if (!packet)
			continue;

		/* According to the man page of pcap_dump(), first argument
		 * is the pcap_dumper_t pointer even it's argument type is
		 * u_char *.
		 */
		pcap_dump((u_char *)dumper, &header, packet);

		/* Not sure what other types of packets look like. Here, we
		 * parse only Ethernet and compatible packets.
		 */
		if (!is_ethernet(packet))
			continue;

		/* Skip SLL2 header
		 * https://www.tcpdump.org/linktypes/LINKTYPE_LINUX_SLL2.html
		 *
		 * Although the document doesn't mention that, the payload
		 * doesn't include the Ethernet header. The payload starts
		 * from the first byte of the network layer header.
		 */
		payload = packet + 20;

		memcpy(&proto, packet, 2);
		proto = ntohs(proto);
		memcpy(&ifindex, packet + 4, 4);
		ifindex = ntohl(ifindex);
		ptype = packet[10];

		if (proto == ETH_P_IPV6) {
			show_ipv6_packet(payload, ifindex, ptype);
		} else if (proto == ETH_P_IP) {
			show_ipv4_packet(payload, ifindex, ptype);
		} else {
			ifname = if_indextoname(ifindex, _ifname);
			if (!ifname) {
				snprintf(_ifname, sizeof(_ifname), "unknown(%d)", ifindex);
				ifname = _ifname;
			}

			printf("%-7s %-3s Unknown network protocol type 0x%x\n",
			       ifname, pkt_type_str(ptype), proto);
		}
	}

	return NULL;
}

/* Prepare the pcap handle to capture packets.
 *
 * This pcap is non-blocking and immediate mode is enabled to receive
 * captured packets as soon as possible.  The snaplen is set to 1024 bytes
 * to limit the size of captured content. The format of the link-layer
 * header is set to DLT_LINUX_SLL2 to enable handling various link-layer
 * technologies.
 */
static pcap_t *traffic_monitor_prepare_pcap(void)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;
	int r;

	/* Listen on all NICs in the namespace */
	pcap = pcap_create("any", errbuf);
	if (!pcap) {
		log_err("Failed to open pcap: %s", errbuf);
		return NULL;
	}
	/* Limit the size of the packet (first N bytes) */
	r = pcap_set_snaplen(pcap, 1024);
	if (r) {
		log_err("Failed to set snaplen: %s", pcap_geterr(pcap));
		goto error;
	}
	/* To receive packets as fast as possible */
	r = pcap_set_immediate_mode(pcap, 1);
	if (r) {
		log_err("Failed to set immediate mode: %s", pcap_geterr(pcap));
		goto error;
	}
	r = pcap_setnonblock(pcap, 1, errbuf);
	if (r) {
		log_err("Failed to set nonblock: %s", errbuf);
		goto error;
	}
	r = pcap_activate(pcap);
	if (r) {
		log_err("Failed to activate pcap: %s", pcap_geterr(pcap));
		goto error;
	}
	/* Determine the format of the link-layer header */
	r = pcap_set_datalink(pcap, DLT_LINUX_SLL2);
	if (r) {
		log_err("Failed to set datalink: %s", pcap_geterr(pcap));
		goto error;
	}

	return pcap;
error:
	pcap_close(pcap);
	return NULL;
}

static void encode_test_name(char *buf, size_t len, const char *test_name, const char *subtest_name)
{
	char *p;

	if (subtest_name)
		snprintf(buf, len, "%s__%s", test_name, subtest_name);
	else
		snprintf(buf, len, "%s", test_name);
	while ((p = strchr(buf, '/')))
		*p = '_';
	while ((p = strchr(buf, ' ')))
		*p = '_';
}

#define PCAP_DIR "/tmp/tmon_pcap"

/* Start to monitor the network traffic in the given network namespace.
 *
 * netns: the name of the network namespace to monitor. If NULL, the
 *        current network namespace is monitored.
 * test_name: the name of the running test.
 * subtest_name: the name of the running subtest if there is. It should be
 *               NULL if it is not a subtest.
 *
 * This function will start a thread to capture packets going through NICs
 * in the give network namespace.
 */
struct tmonitor_ctx *traffic_monitor_start(const char *netns, const char *test_name,
					   const char *subtest_name)
{
	struct nstoken *nstoken = NULL;
	struct tmonitor_ctx *ctx;
	char test_name_buf[64];
	static int tmon_seq;
	int r;

	if (netns) {
		nstoken = open_netns(netns);
		if (!nstoken)
			return NULL;
	}
	ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		log_err("Failed to malloc ctx");
		goto fail_ctx;
	}
	memset(ctx, 0, sizeof(*ctx));

	encode_test_name(test_name_buf, sizeof(test_name_buf), test_name, subtest_name);
	snprintf(ctx->pkt_fname, sizeof(ctx->pkt_fname),
		 PCAP_DIR "/packets-%d-%d-%s-%s.log", getpid(), tmon_seq++,
		 test_name_buf, netns ? netns : "unknown");

	r = mkdir(PCAP_DIR, 0755);
	if (r && errno != EEXIST) {
		log_err("Failed to create " PCAP_DIR);
		goto fail_pcap;
	}

	ctx->pcap = traffic_monitor_prepare_pcap();
	if (!ctx->pcap)
		goto fail_pcap;
	ctx->pcap_fd = pcap_get_selectable_fd(ctx->pcap);
	if (ctx->pcap_fd < 0) {
		log_err("Failed to get pcap fd");
		goto fail_dumper;
	}

	/* Create a packet file */
	ctx->dumper = pcap_dump_open(ctx->pcap, ctx->pkt_fname);
	if (!ctx->dumper) {
		log_err("Failed to open pcap dump: %s", ctx->pkt_fname);
		goto fail_dumper;
	}

	/* Create an eventfd to wake up the monitor thread */
	ctx->wake_fd = eventfd(0, 0);
	if (ctx->wake_fd < 0) {
		log_err("Failed to create eventfd");
		goto fail_eventfd;
	}

	r = pthread_create(&ctx->thread, NULL, traffic_monitor_thread, ctx);
	if (r) {
		log_err("Failed to create thread");
		goto fail;
	}

	close_netns(nstoken);

	return ctx;

fail:
	close(ctx->wake_fd);

fail_eventfd:
	pcap_dump_close(ctx->dumper);
	unlink(ctx->pkt_fname);

fail_dumper:
	pcap_close(ctx->pcap);

fail_pcap:
	free(ctx);

fail_ctx:
	close_netns(nstoken);

	return NULL;
}

static void traffic_monitor_release(struct tmonitor_ctx *ctx)
{
	pcap_close(ctx->pcap);
	pcap_dump_close(ctx->dumper);

	close(ctx->wake_fd);

	free(ctx);
}

/* Stop the network traffic monitor.
 *
 * ctx: the context returned by traffic_monitor_start()
 */
void traffic_monitor_stop(struct tmonitor_ctx *ctx)
{
	__u64 w = 1;

	if (!ctx)
		return;

	/* Stop the monitor thread */
	ctx->done = true;
	/* Wake up the background thread. */
	write(ctx->wake_fd, &w, sizeof(w));
	pthread_join(ctx->thread, NULL);

	printf("Packet file: %s\n", strrchr(ctx->pkt_fname, '/') + 1);

	traffic_monitor_release(ctx);
}
#endif /* TRAFFIC_MONITOR */

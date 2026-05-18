#! /usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
This module provides functional testing for the net/rds component.
"""

import argparse
import ctypes
import errno
import hashlib
import os
import select
import signal
import socket
import subprocess
import sys

# Allow utils module to be imported from different directory
this_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(this_dir, "../"))
# pylint: disable-next=wrong-import-position,import-error,no-name-in-module
from lib.py.utils import ip # noqa: E402
# pylint: disable-next=wrong-import-position,import-error,no-name-in-module
from lib.py.ksft import ksft_pr # noqa: E402

libc = ctypes.cdll.LoadLibrary('libc.so.6')
setns = libc.setns

NET0 = 'net0'
NET1 = 'net1'

VETH0 = 'veth0'
VETH1 = 'veth1'

tcpdump_procs = []
tcp_addrs = [
    # we technically don't need different port numbers, but this will
    # help identify traffic in the network analyzer
    ('10.0.0.1', 10000),
    ('10.0.0.2', 20000),
]


# Helper function for creating a socket inside a network namespace.
# We need this because otherwise RDS will detect that the two TCP
# sockets are on the same interface and use the loop transport instead
# of the TCP transport.
def netns_socket(netns, *sock_args):
    """
    Creates sockets inside of network namespace

    :param netns: the name of the network namespace
    :param sock_args: socket family and type
    """
    u0, u1 = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)

    child = os.fork()
    if child == 0:
        # change network namespace
        with open(f'/var/run/netns/{netns}', encoding='utf-8') as f:
            try:
                setns(f.fileno(), 0)
            except IOError as e:
                print(e.errno)
                print(e)

        # create socket in target namespace
        sock = socket.socket(*sock_args)

        # send resulting socket to parent
        socket.send_fds(u0, [], [sock.fileno()])

        os._exit(0)

    # receive socket from child
    _, fds, _, _ = socket.recv_fds(u1, 0, 1)
    os.waitpid(child, 0)
    u0.close()
    u1.close()
    return socket.fromfd(fds[0], *sock_args)

def send_burst(socks, ip_addrs, snd_hashes, nr_sent, nr_total):
    """Send until blocked or nr_total reached. Return updated nr_sent."""

    while nr_sent < nr_total:
        data = hashlib.sha256(
            f'packet {nr_sent}'.encode('utf-8')).hexdigest().encode('utf-8')
        # pseudo-random send/receive pattern
        snd_idx = nr_sent % 2
        rcv_idx = 1 - (nr_sent % 3) % 2

        snd = socks[snd_idx]
        rcv = socks[rcv_idx]
        try:
            snd.sendto(data, ip_addrs[rcv_idx])
        except BlockingIOError:
            return nr_sent
        except OSError as e:
            if e.errno in (errno.ENOBUFS, errno.ECONNRESET, errno.EPIPE):
                return nr_sent
            raise
        snd_hashes.setdefault((snd.fileno(), rcv.fileno()),
                hashlib.sha256()).update(f'<{data}>'.encode('utf-8'))
        nr_sent += 1
    return nr_sent

def recv_burst(epoll, socks, ip_addrs, rcv_hashes, nr_rcv):
    """Drain whatever's readable from epoll. Return updated nr_recv."""
    for filen, evntmask in epoll.poll():
        if not evntmask & select.EPOLLRDNORM:
            continue
        rcv = next(s for s in socks if s.fileno() == filen)
        while True:
            try:
                data, adr = rcv.recvfrom(1024)
            except BlockingIOError:
                break
            snd_idx = ip_addrs.index(adr)
            snd = socks[snd_idx]
            rcv_hashes.setdefault((snd.fileno(), rcv.fileno()),
                    hashlib.sha256()).update(f'<{data}>'.encode('utf-8'))
            nr_rcv += 1
    return nr_rcv

def check_info(socks):
    """
    Check all rds info pages for errors

    :param socks: list of sockets to check
    """

    # the Python socket module doesn't know these
    rds_info_first = 10000
    rds_info_last = 10017

    nr_success = 0
    nr_error = 0

    for sock in socks:
        for optname in range(rds_info_first, rds_info_last + 1):
            # Sigh, the Python socket module doesn't allow us to pass
            # buffer lengths greater than 1024 for some reason. RDS
            # wants multiple pages.
            try:
                sock.getsockopt(socket.SOL_RDS, optname, 1024)
                nr_success = nr_success + 1
            except OSError as e:
                nr_error = nr_error + 1
                if e.errno == errno.ENOSPC:
                    # ignore
                    pass

    ksft_pr(f"getsockopt(): {nr_success}/{nr_error}")

def verify_hashes(snd_hashes, rcv_hashes):
    """Compare send/recv hashes per (sender, receiver) pair."""
    for key, snd_hash in snd_hashes.items():
        rcv_hash = rcv_hashes.get(key)
        if rcv_hash is None:
            ksft_pr("FAIL: No data received")
            return 1
        if snd_hash.hexdigest() != rcv_hash.hexdigest():
            ksft_pr("FAIL: Send/recv mismatch")
            ksft_pr("hash expected:", snd_hash.hexdigest())
            ksft_pr("hash received:", rcv_hash.hexdigest())
            return 1
        ksft_pr(f"{key[0]}/{key[1]}: ok")
    return 0

def stop_pcaps():
    """Stop tcpdump processes.

    We use pop() here to drain the list in the event that the test
    completes after the signal handler is fired.  List will be empty
    if logdir is not set
    """

    if not tcpdump_procs:
        return

    ksft_pr("Stopping network packet captures")
    while tcpdump_procs:
        proc = tcpdump_procs.pop()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

def signal_handler(_sig, _frame):
    """
    Test timed out signal handler
    """
    ksft_pr("Test timed out")
    stop_pcaps()
    print("not ok 1 rds selftest")
    sys.exit(1)

def setup_tcp():
    """
    Configure tcp network
    """

    ip(f"netns add {NET0}")
    ip(f"netns add {NET1}")
    ip("link add type veth")

    # Move TCP interfaces into separate namespaces so they can no longer be
    # bound directly; this prevents rds from switching over from the tcp
    # transport to the loop transport.
    ip(f"link set {VETH0} netns {NET0} up")
    ip(f"link set {VETH1} netns {NET1} up")

    # add addresses
    ip(f"-n {NET0} addr add {tcp_addrs[0][0]}/32 dev {VETH0}")
    ip(f"-n {NET1} addr add {tcp_addrs[1][0]}/32 dev {VETH1}")

    # add routes
    ip(f"-n {NET0} route add {tcp_addrs[1][0]}/32 dev {VETH0}")
    ip(f"-n {NET1} route add {tcp_addrs[0][0]}/32 dev {VETH1}")

    # sanity check that our two interfaces/addresses are correctly set up
    # and communicating by doing a single ping
    ip(f"netns exec {NET0} ping -c 1 {tcp_addrs[1][0]}")

    # Start a packet capture on each network
    if logdir is not None:
        for netn in [NET0, NET1]:
            pcap = logdir+'/rds-'+netn+'.pcap'

            tcpdump_cmd = ['ip', 'netns', 'exec', netn, '/usr/sbin/tcpdump']
            sudo_user = os.environ.get('SUDO_USER')
            if sudo_user:
                tcpdump_cmd.extend(['-Z', sudo_user])
            tcpdump_cmd.extend(['-i', 'any', '-w', pcap])

            # pylint: disable-next=consider-using-with
            p = subprocess.Popen(tcpdump_cmd,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tcpdump_procs.append(p)

    # simulate packet loss, duplication and corruption
    for netn, iface in [(NET0, VETH0), (NET1, VETH1)]:
        ip(f"netns exec {netn} /usr/sbin/tc qdisc add dev {iface} root netem  \
             corrupt {PACKET_CORRUPTION} loss {PACKET_LOSS} duplicate  \
             {PACKET_DUPLICATE}")

#Parse out command line arguments.  We take an optional
# timeout parameter and an optional log output folder
parser = argparse.ArgumentParser(description="init script args",
                  formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument("-d", "--logdir", action="store",
                    help="directory to store logs", default=None)
parser.add_argument('-t', '--timeout', help="timeout to terminate hung test",
                    type=int, default=0)
parser.add_argument('-l', '--loss', help="Simulate tcp packet loss",
                    type=int, default=0)
parser.add_argument('-c', '--corruption', help="Simulate tcp packet corruption",
                    type=int, default=0)
parser.add_argument('-u', '--duplicate', help="Simulate tcp packet duplication",
                    type=int, default=0)
args = parser.parse_args()
logdir=args.logdir
PACKET_LOSS=str(args.loss)+'%'
PACKET_CORRUPTION=str(args.corruption)+'%'
PACKET_DUPLICATE=str(args.duplicate)+'%'

setup_tcp()
addrs = tcp_addrs

print("TAP version 13")
print("1..1")

# add a timeout
if args.timeout > 0:
    signal.alarm(args.timeout)
    signal.signal(signal.SIGALRM, signal_handler)

sockets = [
    netns_socket(NET0, socket.AF_RDS, socket.SOCK_SEQPACKET),
    netns_socket(NET1, socket.AF_RDS, socket.SOCK_SEQPACKET),
]

for s, addr in zip(sockets, addrs):
    s.bind(addr)
    s.setblocking(0)

send_hashes = {}
recv_hashes = {}

ep = select.epoll()

for s in sockets:
    ep.register(s, select.EPOLLRDNORM)

NUM_PACKETS = 50000
nr_send = 0
nr_recv = 0

while nr_send < NUM_PACKETS:

    # Send as much as we can without blocking
    ksft_pr("sending...", nr_send, nr_recv)
    nr_send = send_burst(sockets, addrs, send_hashes, nr_send, NUM_PACKETS)

    # Receive as much as we can without blocking
    ksft_pr("receiving...", nr_send, nr_recv)
    while nr_recv < nr_send:
        nr_recv = recv_burst(ep, sockets, addrs, recv_hashes, nr_recv)

    # exercise net/rds/tcp.c:rds_tcp_sysctl_reset()
    for net in [NET0, NET1]:
        ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_rcvbuf=10000")
        ip(f"netns exec {net} /usr/sbin/sysctl net.rds.tcp.rds_tcp_sndbuf=10000")

ksft_pr("done", nr_send, nr_recv)

check_info(sockets)

# cancel timeout
signal.alarm(0)

stop_pcaps()

# We're done sending and receiving stuff, now let's check if what
# we received is what we sent.
ret = verify_hashes(send_hashes, recv_hashes)

if ret == 0:
    ksft_pr("Success")
    print("ok 1 rds selftest")
else:
    print("not ok 1 rds selftest")

ksft_pr(f"Totals: pass:{1-ret} fail:{ret} skip:0")
sys.exit(ret)

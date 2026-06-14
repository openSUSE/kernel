#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
import time
import threading
from os import path
from lib.py import (
    ksft_run,
    ksft_exit,
    ksft_eq,
    ksft_in,
    ksft_not_in,
    ksft_raises,
)
from lib.py import (
    NetDrvContEnv,
    NetNSEnter,
    EthtoolFamily,
    NetdevFamily,
    RtnlFamily,
)
from lib.py import (
    Netlink,
    bkg,
    cmd,
    defer,
    ethtool,
    ip,
    rand_port,
    wait_port_listen,
)
from lib.py import KsftSkipEx, CmdExitFailure


def _create_netkit_pair(cfg, rxqueues=2):
    if cfg.nk_host_ifname:
        cmd(f"ip link del dev {cfg.nk_host_ifname}", fail=False)
        cfg.nk_host_ifname = None
        cfg.nk_guest_ifname = None
    if getattr(cfg, "_tc_attached", False):
        cmd(
            f"tc filter del dev {cfg.ifname} ingress pref {cfg._bpf_prog_pref}",
            fail=False,
        )
        cfg._tc_attached = False

    all_links = ip("-d link show", json=True)
    old_idxs = {
        link["ifindex"]
        for link in all_links
        if link.get("linkinfo", {}).get("info_kind") == "netkit"
    }

    rtnl = RtnlFamily()
    rtnl.newlink(
        {
            "linkinfo": {
                "kind": "netkit",
                "data": {
                    "mode": "l2",
                    "policy": "forward",
                    "peer-policy": "forward",
                },
            },
            "num-rx-queues": rxqueues,
        },
        flags=[Netlink.NLM_F_CREATE, Netlink.NLM_F_EXCL],
    )

    all_links = ip("-d link show", json=True)
    nk_links = [
        link
        for link in all_links
        if link.get("linkinfo", {}).get("info_kind") == "netkit"
        and link["ifindex"] not in old_idxs
    ]
    if len(nk_links) != 2:
        raise KsftSkipEx("Failed to create netkit pair")

    nk_links.sort(key=lambda x: x["ifindex"])
    cfg.nk_host_ifname = nk_links[1]["ifname"]
    cfg.nk_guest_ifname = nk_links[0]["ifname"]
    cfg.nk_host_ifindex = nk_links[1]["ifindex"]
    cfg.nk_guest_ifindex = nk_links[0]["ifindex"]

    ip(f"link set dev {cfg.nk_guest_ifname} netns {cfg.netns.name}")
    ip(f"link set dev {cfg.nk_host_ifname} up")
    ip(f"-6 addr add fe80::1/64 dev {cfg.nk_host_ifname} nodad")
    ip(
        f"-6 route add {cfg.nk_guest_ipv6}/128 via fe80::2 "
        f"dev {cfg.nk_host_ifname}"
    )
    ip(f"link set dev {cfg.nk_guest_ifname} up", ns=cfg.netns)
    ip(f"-6 addr add fe80::2/64 dev {cfg.nk_guest_ifname}", ns=cfg.netns)
    ip(
        f"-6 addr add {cfg.nk_guest_ipv6}/64 dev {cfg.nk_guest_ifname} nodad",
        ns=cfg.netns,
    )
    ip(
        f"-6 route add default via fe80::1 dev {cfg.nk_guest_ifname}",
        ns=cfg.netns,
    )

    cfg._attach_bpf()


def _setup_lease(cfg, rxqueues=2):
    _create_netkit_pair(cfg, rxqueues=rxqueues)

    ethnl = EthtoolFamily()
    channels = ethnl.channels_get({"header": {"dev-index": cfg.ifindex}})[
        "combined-count"
    ]
    if channels < 2:
        raise KsftSkipEx(
            "Test requires NETIF with at least 2 combined channels"
        )
    src_queue = channels - 1

    with NetNSEnter(str(cfg.netns)):
        netdevnl = NetdevFamily()
        bind_result = netdevnl.queue_create(
            {
                "ifindex": cfg.nk_guest_ifindex,
                "type": "rx",
                "lease": {
                    "ifindex": cfg.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
    return src_queue, bind_result["id"]


def _teardown_netkit(cfg):
    if cfg.nk_host_ifname:
        cmd(f"ip link del dev {cfg.nk_host_ifname}", fail=False)
        cfg.nk_host_ifname = None
        cfg.nk_guest_ifname = None


def set_flow_rule(cfg, src_queue):
    output = ethtool(
        f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} action {src_queue}"
    ).stdout
    values = re.search(r"ID (\d+)", output).group(1)
    return int(values)


def test_iou_zcrx(cfg) -> None:
    cfg.require_ipver("6")
    src_queue, nk_queue = _setup_lease(cfg)
    defer(_teardown_netkit, cfg)
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = set_flow_rule(cfg, src_queue)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = (
        f"{cfg.bin_local} -s -p {cfg.port} "
        f"-i {cfg.nk_guest_ifname} -q {nk_queue}"
    )
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.nk_guest_ipv6} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True, ns=cfg.netns):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)
        cmd(tx_cmd, host=cfg.remote)


def test_attrs(cfg) -> None:
    cfg.require_ipver("6")
    src_queue, nk_queue = _setup_lease(cfg)
    defer(_teardown_netkit, cfg)
    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
    )

    ksft_eq(queue_info["id"], src_queue)
    ksft_eq(queue_info["type"], "rx")
    ksft_eq(queue_info["ifindex"], cfg.ifindex)

    ksft_in("lease", queue_info)
    lease = queue_info["lease"]
    ksft_eq(lease["ifindex"], cfg.nk_guest_ifindex)
    ksft_eq(lease["queue"]["id"], nk_queue)
    ksft_eq(lease["queue"]["type"], "rx")
    ksft_in("netns-id", lease)


def test_attach_xdp_with_mp(cfg) -> None:
    cfg.require_ipver("6")
    src_queue, nk_queue = _setup_lease(cfg)
    defer(_teardown_netkit, cfg)
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    netdevnl = NetdevFamily()

    rx_cmd = (
        f"{cfg.bin_local} -s -p {cfg.port} "
        f"-i {cfg.nk_guest_ifname} -q {nk_queue}"
    )
    with bkg(rx_cmd, ns=cfg.netns):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

        time.sleep(0.1)
        queue_info = netdevnl.queue_get(
            {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
        )
        ksft_in("io-uring", queue_info)

        prog = cfg.net_lib_dir / "xdp_dummy.bpf.o"
        with ksft_raises(CmdExitFailure):
            ip(f"link set dev {cfg.ifname} xdp obj {prog} sec xdp.frags")

    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


def test_destroy(cfg) -> None:
    cfg.require_ipver("6")
    src_queue, nk_queue = _setup_lease(cfg)
    defer(_teardown_netkit, cfg)
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    rx_cmd = (
        f"{cfg.bin_local} -s -p {cfg.port} "
        f"-i {cfg.nk_guest_ifname} -q {nk_queue}"
    )
    rx_proc = cmd(rx_cmd, background=True, ns=cfg.netns)
    wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("io-uring", queue_info)

    # ip link del will wait for all refs to drop first, but iou-zcrx is holding
    # onto a ref. Terminate iou-zcrx async via a thread after a delay.
    kill_timer = threading.Timer(1, rx_proc.proc.terminate)
    kill_timer.start()

    ip(f"link del dev {cfg.nk_host_ifname}")
    kill_timer.join()
    cfg.nk_host_ifname = None
    cfg.nk_guest_ifname = None

    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)

    flow_rule_id = set_flow_rule(cfg, src_queue)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {src_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)
    # Short delay since iou cleanup is async and takes a bit of time.
    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


def main() -> None:
    with NetDrvContEnv(__file__, rxqueues=2) as cfg:
        cfg.bin_local = path.abspath(
            path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx"
        )
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)
        cfg.port = rand_port()

        ksft_run(
            [test_iou_zcrx, test_attrs, test_attach_xdp_with_mp, test_destroy],
            args=(cfg,),
        )
    ksft_exit()


if __name__ == "__main__":
    main()

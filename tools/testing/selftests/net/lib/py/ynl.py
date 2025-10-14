# SPDX-License-Identifier: GPL-2.0

import sys
from pathlib import Path
from .consts import KSRC, KSFT_DIR
from .ksft import ksft_pr, ktap_result

# Resolve paths
try:
    if (KSFT_DIR / "kselftest-list.txt").exists():
        # Running in "installed" selftests
        tools_full_path = KSFT_DIR
        SPEC_PATH = KSFT_DIR / "net/lib/specs"

        sys.path.append(tools_full_path.as_posix())
        from net.lib.ynl.pyynl.lib import YnlFamily, NlError
    else:
        # Running in tree
        tools_full_path = KSRC / "tools"
        SPEC_PATH = KSRC / "Documentation/netlink/specs"

        sys.path.append(tools_full_path.as_posix())
        from net.ynl.pyynl.lib import YnlFamily, NlError
except ModuleNotFoundError as e:
    ksft_pr("Failed importing `ynl` library from kernel sources")
    ksft_pr(str(e))
    ktap_result(True, comment="SKIP")
    sys.exit(4)

#
# Wrapper classes, loading the right specs
# Set schema='' to avoid jsonschema validation, it's slow
#
class EthtoolFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('ethtool.yaml')).as_posix(),
                         schema='', recv_size=recv_size)


class RtnlFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('rt-link.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

class RtnlAddrFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('rt-addr.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

class NetdevFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('netdev.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

class NetshaperFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('net_shaper.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

class DevlinkFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('devlink.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

class PSPFamily(YnlFamily):
    def __init__(self, recv_size=0):
        super().__init__((SPEC_PATH / Path('psp.yaml')).as_posix(),
                         schema='', recv_size=recv_size)

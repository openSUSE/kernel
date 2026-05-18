# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from datetime import datetime
from typing import Protocol

from sbom.config import KernelSpdxDocumentKind
from sbom.cmd_graph import CmdGraph
from sbom.path_utils import PathStr
from sbom.spdx_graph.kernel_file import KernelFileCollection
from sbom.spdx_graph.spdx_graph_model import SpdxGraph, SpdxIdGeneratorCollection
from sbom.spdx_graph.shared_spdx_elements import SharedSpdxElements


class SpdxGraphConfig(Protocol):
    obj_tree: PathStr
    src_tree: PathStr
    created: datetime


def build_spdx_graphs(
    cmd_graph: CmdGraph,
    spdx_id_generators: SpdxIdGeneratorCollection,
    config: SpdxGraphConfig,
) -> dict[KernelSpdxDocumentKind, SpdxGraph]:
    """
    Builds SPDX graphs (output, source, and build) based on a cmd dependency graph.
    If the source and object trees are identical, no dedicated source graph can be created.
    In that case the source files are added to the build graph instead.

    Args:
        cmd_graph: The dependency graph of a kernel build.
        spdx_id_generators: Collection of SPDX ID generators.
        config: Configuration options.

    Returns:
        Dictionary of SPDX graphs
    """
    shared_elements = SharedSpdxElements.create(spdx_id_generators.base, config.created)
    kernel_files = KernelFileCollection.create(cmd_graph, config.obj_tree, config.src_tree, spdx_id_generators)
    return {}

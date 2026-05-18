#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

"""
Compute software bill of materials in SPDX format describing a kernel build.
"""

import logging
import os
import sys
import time
import sbom.sbom_logging as sbom_logging
from sbom.config import get_config
from sbom.path_utils import is_relative_to
from sbom.cmd_graph import CmdGraph


def _exit_with_summary(write_output_on_error: bool = False) -> None:
    warning_summary = sbom_logging.summarize_warnings()
    error_summary = sbom_logging.summarize_errors()
    if warning_summary:
        logging.warning(warning_summary)
    if error_summary:
        logging.error(error_summary)
        if not write_output_on_error:
            logging.info(
                "Use --write-output-on-error to generate output documents even when errors occur. "
                "Note that in this case the generated documents may be incomplete."
            )
        sys.exit(1)


def main():
    # Read config
    config = get_config()

    # Configure logging
    logging.basicConfig(
        level=logging.DEBUG if config.debug else logging.INFO,
        format="[%(levelname)s] %(message)s",
    )

    # Build cmd graph
    logging.debug("Start building cmd graph")
    start_time = time.time()
    cmd_graph = CmdGraph.create(config.root_paths, config)
    logging.debug(f"Built cmd graph in {time.time() - start_time} seconds")

    # Save used files document
    if config.generate_used_files:
        if config.src_tree == config.obj_tree:
            logging.info(
                f"Extracting all files from the cmd graph to {config.used_files_file_name} "
                "instead of only source files because source files cannot be "
                "reliably classified when the source and object trees are identical.",
            )
            used_files = [os.path.relpath(node.absolute_path, config.src_tree) for node in cmd_graph]
            logging.debug(f"Found {len(used_files)} files in cmd graph.")
        else:
            used_files = [
                os.path.relpath(node.absolute_path, config.src_tree)
                for node in cmd_graph
                if is_relative_to(node.absolute_path, config.src_tree)
                and not is_relative_to(node.absolute_path, config.obj_tree)
            ]
            logging.debug(f"Found {len(used_files)} source files in cmd graph")
        if not sbom_logging.has_errors() or config.write_output_on_error:
            used_files_path = os.path.join(config.output_directory, config.used_files_file_name)
            with open(used_files_path, "w", encoding="utf-8") as f:
                f.write("\n".join(str(file_path) for file_path in used_files))
            logging.debug(f"Successfully saved {used_files_path}")

    _exit_with_summary(config.write_output_on_error)


# Call main method
if __name__ == "__main__":
    main()

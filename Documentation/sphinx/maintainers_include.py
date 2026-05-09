#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8; mode: python -*-
# pylint: disable=R0903, C0330, R0914, R0912, E0401

"""
    maintainers-include
    ~~~~~~~~~~~~~~~~~~~

    Implementation of the ``maintainers-include`` reST-directive.

    :copyright:  Copyright (C) 2019  Kees Cook <keescook@chromium.org>
    :license:    GPL Version 2, June 1991 see linux/COPYING for details.

    The ``maintainers-include`` reST-directive performs extensive parsing
    specific to the Linux kernel's standard "MAINTAINERS" file, in an
    effort to avoid needing to heavily mark up the original plain text.
"""

import sys
import re
import os.path

from glob import glob

from docutils import statemachine
from docutils.parsers.rst import Directive
from docutils.parsers.rst.directives.misc import Include

#
# Base URL for intersphinx-like links to maintainer profiles
#
KERNELDOC_URL = "https://docs.kernel.org/"

def ErrorString(exc):  # Shamelessly stolen from docutils
    return f'{exc.__class__.__name}: {exc}'

__version__  = '1.0'

maint_parser = None

class MaintainersParser:
    """Parse MAINTAINERS file(s) content"""

    def __init__(self, app_dir, path):
        self.path = path

        # Poor man's state machine.
        self.descriptions = False
        self.maintainers = False
        self.subsystems = False

        # Field letter to field name mapping.
        self.field_letter = None
        self.fields = dict()

        self.field_prev = ""
        self.field_content = ""
        self.subsystem_name = None

        self.app_dir = os.path.abspath(app_dir)
        self.base_dir, _, self.sphinx_dir = self.app_dir.partition("Documentation")

        self.re_doc = re.compile(r'(Documentation/([^\s\?\*]*)\.rst)')

        #
        # Output variables with maintainers content to be stored
        #
        self.profile_toc = set()
        self.profile_entries = {}
        self.output = ".. _maintainers:\n\n"

        prev = None
        for line in open(path):
            if self.descriptions:
                self.parse_descriptions(line)
            elif self.maintainers and not self.subsystems:
                if re.search('^[A-Z0-9]', line):
                    self.subsystems = True
                    self.parse_subsystems(line)
                else:
                    self.output += line
            elif self.subsystems:
                self.parse_subsystems(line)
            else:
                self.output += line

            # Update the state machine when we find heading separators.
            if line.startswith('----------'):
                if prev.startswith('Descriptions'):
                    self.descriptions = True
                if prev.startswith('Maintainers'):
                    self.maintainers = True

            # Retain previous line for state machine transitions.
            prev = line

        # Flush pending field contents.
        if self.field_content:
            self.output += self.field_content + "\n\n"

        self.output = self.output.rstrip()


    def linkify(self, text):
        """Linkify all non-wildcard refs to ReST files in Documentation/"""

        m = self.re_doc.search(text)
        if m:
            fname = m.group(1)
            ename = m.group(2)

            entry = os.path.relpath(self.base_dir + fname, self.app_dir)
            entry = entry.removesuffix(".rst")

            #
            # When SPHINXDIRS is used, it will try to reference files
            # outside srctree, causing warnings. To avoid that, point
            # to the latest official documentation
            #
            if entry.startswith("../"):
                html = KERNELDOC_URL + ename + ".html"
                text = self.re_doc.sub(f'`{ename} <{html}>`_', text)
            else:
                text = self.re_doc.sub(f':doc:`{ename} </{entry}>`', text)

        return text

    def parse_descriptions(self, line):
        """Handle contents of the descriptions section."""

        # Have we reached the end of the preformatted Descriptions text?
        if line.startswith('Maintainers'):
            self.descriptions = False
            self.output += "\n" + line
            return

        # Escape the escapes in preformatted text.
        self.output += "| " + self.linkify(line).replace("\\", "\\\\")

        # Look for and record field letter to field name mappings:
        #   R: Designated *reviewer*: FullName <address@domain>
        m = re.search(r"\s(\S):\s", line)
        if m:
            self.field_letter = m.group(1)

        if self.field_letter and self.field_letter not in self.fields:
            m = re.search(r"\*([^\*]+)\*", line)
            if m:
                self.fields[self.field_letter] = m.group(1)

    def parse_subsystems(self, line):
        """Handle contents of the per-subsystem sections."""

        # Drop needless input whitespace.
        line = line.rstrip()

        # Skip empty lines: subsystem parser adds them as needed.
        if not line:
            return

        # Subsystem fields are batched into "field_content"
        if line[1] != ':':
            line = self.linkify(line)

            # Render a subsystem entry as:
            #   SUBSYSTEM NAME
            #   ~~~~~~~~~~~~~~
            # Flush pending field content.
            self.output += self.field_content + "\n\n"
            self.field_content = ""

            self.subsystem_name = line.title()

            # Collapse whitespace in subsystem name.
            heading = re.sub(r"\s+", " ", line)
            self.output += "%s\n%s" % (heading, "~" * len(heading)) + "\n"
            self.field_prev = ""

            return

        # Render a subsystem field as:
        #   :Field: entry
        #           entry...
        field, details = line.split(':', 1)
        details = details.strip()

        #
        # Handle profile entries - either as files or as https refs
        #
        if field == "P":
            match = self.re_doc.match(details)
            if match:
                fname = match.group(1)
                ename = match.group(2)

                entry = os.path.relpath(self.base_dir + fname, self.app_dir)
                entry = entry.removesuffix(".rst")
                #
                # When SPHINXDIRS is used, it will try to reference files
                # outside srctree, causing warnings. To avoid that, point
                # to the latest official documentation
                #

                if entry.startswith("../"):
                    entry = KERNELDOC_URL + ename + ".html"
                else:
                    entry = "/" + entry

                if "*" in entry:
                    for e in glob(entry):
                        if "html" not in e:
                            self.profile_toc.add(e)

                        self.profile_entries[self.subsystem_name] = e
                else:
                    if "html" not in entry:
                        self.profile_toc.add(entry)

                    self.profile_entries[self.subsystem_name] = entry
            else:
                match = re.match(r"(https?://.*)", details)
                if match:
                    entry = match.group(1).strip()
                    self.profile_entries[self.subsystem_name] = entry

        details = self.linkify(details)

        # Mark paths (and regexes) as literal text for improved
        # readability and to escape any escapes.
        if field in ['F', 'N', 'X', 'K']:
            # But only if not already marked :)
            if not ':doc:' in details:
                details = '``%s``' % (details)

        # Comma separate email field continuations.
        if field == self.field_prev and self.field_prev in ['M', 'R', 'L']:
            self.field_content = self.field_content + ","

        # Do not repeat field names, so that field entries
        # will be collapsed together.
        if field != self.field_prev:
            self.output += self.field_content + "\n\n"
            self.field_content = ":%s:" % (self.fields.get(field, field))
        self.field_content = self.field_content + "\n\t%s" % (details)
        self.field_prev = field


class MaintainersInclude(Include):
    """MaintainersInclude (``maintainers-include``) directive"""
    required_arguments = 0

    def emit(self):
        """Parse all the MAINTAINERS lines into ReST for human-readability"""
        global maint_parser

        path = maint_parser.path
        output = maint_parser.output

        # For debugging the pre-rendered results...
        #print(output, file=open("/tmp/MAINTAINERS.rst", "w"))

        self.state.document.settings.record_dependencies.add(path)
        self.state_machine.insert_input(statemachine.string2lines(output), path)

    def run(self):
        """Include the MAINTAINERS file as part of this reST file."""
        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)

        try:
            lines = self.emit()
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))

        return []

class MaintainersProfile(Include):
    required_arguments = 0

    def emit(self):
        """Parse all the MAINTAINERS lines looking for profile entries"""
        global maint_parser

        path = maint_parser.path

        #
        # Produce a list with all maintainer profiles, sorted by subsystem name
        #
        output = ""
        for profile, entry in sorted(maint_parser.profile_entries.items()):
            if entry.startswith("http"):
                output += f"- `{profile} <{entry}>`_\n"
            else:
                output += f"- :doc:`{profile} <{entry}>`\n"

        #
        # Create a hidden TOC table with all profiles. That allows adding
        # profiles without needing to add them on any index.rst file.
        #
        output += "\n.. toctree::\n"
        output += "   :hidden:\n\n"

        for fname in sorted(maint_parser.profile_toc):
            output += f"   {fname}\n"

        output += "\n"

        self.state.document.settings.record_dependencies.add(path)
        self.state_machine.insert_input(statemachine.string2lines(output), path)

    def run(self):
        """Include the MAINTAINERS file as part of this reST file."""
        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)

        try:
            lines = self.emit()
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))

        return []

def setup(app):
    global maint_parser

    #
    # NOTE: we're using os.fspath() here because of a Sphinx warning:
    #   RemovedInSphinx90Warning: Sphinx 9 will drop support for representing paths as strings. Use "pathlib.Path" or "os.fspath" instead.
    #
    app_dir = os.fspath(app.srcdir)
    srctree = os.path.abspath(os.environ["srctree"])
    path = os.path.join(srctree, "MAINTAINERS")

    maint_parser = MaintainersParser(app_dir, path)

    app.add_directive("maintainers-include", MaintainersInclude)
    app.add_directive("maintainers-profile-toc", MaintainersProfile)
    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )

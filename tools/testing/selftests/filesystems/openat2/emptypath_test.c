// SPDX-License-Identifier: GPL-2.0-or-later

#define __SANE_USERSPACE_TYPES__
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "kselftest.h"

#ifndef O_EMPTYPATH
#define O_EMPTYPATH	(1 << 26)
#endif

int main(void)
{
	int opath_fd, reopen_fd;
	const char *path = "/tmp/emptypath_test";

	ksft_print_header();
	ksft_set_plan(2);

	opath_fd = open(path, O_CREAT | O_WRONLY, S_IRWXU);
	if (opath_fd < 0)
		ksft_exit_fail_msg("create %s: %m\n", path);
	close(opath_fd);

	opath_fd = open(path, O_PATH);
	if (opath_fd < 0)
		ksft_exit_fail_msg("open %s O_PATH: %m\n", path);

	reopen_fd = openat(opath_fd, "", O_RDONLY);
	if (reopen_fd < 0 && errno == ENOENT)
		ksft_test_result_pass("empty path without O_EMPTYPATH returns ENOENT\n");
	else if (reopen_fd >= 0) {
		ksft_test_result_fail("empty path without O_EMPTYPATH unexpectedly succeeded\n");
		close(reopen_fd);
	} else {
		ksft_test_result_fail("empty path without O_EMPTYPATH: expected ENOENT, got %m\n");
	}

	reopen_fd = openat(opath_fd, "", O_RDONLY | O_EMPTYPATH);

	if (reopen_fd < 0 && errno == EINVAL)
		ksft_exit_skip("O_EMPTYPATH not supported\n");

	if (reopen_fd >= 0) {
		ksft_test_result_pass("O_EMPTYPATH reopens O_PATH fd\n");
		close(reopen_fd);
	} else {
		ksft_test_result_fail("O_EMPTYPATH failed: %m\n");
	}

	unlink(path);
	ksft_finished();
}

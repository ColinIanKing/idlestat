/*
 *  utils.c
 *
 *  Copyright (C) 2014, Linaro Limited.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Contributors:
 *     Daniel Lezcano <daniel.lezcano@linaro.org>
 *     Zoran Markovic <zoran.markovic@linaro.org>
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>

#include "utils.h"

int write_int(const char *path, int val)
{
	FILE *f;

	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "failed to open '%s': %m\n", path);
		return -1;
	}

	fprintf(f, "%d", val);

	fclose(f);

	return 0;
}

int read_int(const char *path, int *val)
{
	FILE *f;

	f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "failed to open '%s': %m\n", path);
		return -1;
	}

	fscanf(f, "%d", val);

	fclose(f);

	return 0;
}

int store_line(const char *line, void *data)
{
	FILE *f = data;

	/* ignore comment line */
	if (line[0] == '#')
		return 0;

	fprintf(f, "%s", line);

	return 0;
}

/*
 * This functions is a helper to read a specific file content and store
 * the content inside a variable pointer passed as parameter, the format
 * parameter gives the variable type to be read from the file.
 *
 * @path : directory path containing the file
 * @name : name of the file to be read
 * @format : the format of the format
 * @value : a pointer to a variable to store the content of the file
 * Returns 0 on success, -1 otherwise
 */
int file_read_value(const char *path, const char *name,
			const char *format, void *value)
{
	FILE *file;
	char *rpath;
	int ret;

	ret = asprintf(&rpath, "%s/%s", path, name);
	if (ret < 0)
		return ret;

	file = fopen(rpath, "r");
	if (!file) {
		ret = -1;
		goto out_free;
	}

	ret = fscanf(file, format, value) == EOF ? -1 : 0;

	fclose(file);
out_free:
	free(rpath);
	return ret;
}

int redirect_stdout_to_file(const char *path)
{
	int ret = 0;
	int fd;

	if (path) {
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC,
					S_IRUSR | S_IWUSR | S_IRGRP |S_IROTH);
		if (fd < 0) {
			fprintf(stderr, "%s: failed to open '%s'\n", __func__, path);
			return -1;
		}

		fflush(stdout);
		ret = dup2(fd, STDOUT_FILENO);
		close(fd);

		if (ret < 0) {
			fprintf(stderr, "%s: failed to duplicate '%s'\n", __func__, path);
			unlink(path);
			return ret;
		}
	}

	return 0;
}

void display_factored_time(double time, int align)
{
	char buffer[128];

	if (time < 1000) {
		sprintf(buffer, "%.0lfus", time);
		printf("%*s", align, buffer);
	}
	else if (time < 1000000) {
		sprintf(buffer, "%.2lfms", time / 1000.0);
		printf("%*s", align, buffer);
	}
	else {
		sprintf(buffer, "%.2lfs", time / 1000000.0);
		printf("%*s", align, buffer);
	}
}

void display_factored_freq(int freq, int align)
{
	char buffer[128];

	if (freq < 1000) {
		sprintf(buffer, "%dHz", freq);
		printf("%*s", align, buffer);
	} else if (freq < 1000000) {
		sprintf(buffer, "%.2fMHz", (float)freq / 1000.0);
		printf("%*s", align, buffer);
	} else {
		sprintf(buffer, "%.2fGHz", (float)freq / 1000000.0);
		printf("%*s", align, buffer);
	}
}

int check_window_size(void)
{
	struct winsize winsize;

	/* Output is redirected */
	if (!isatty(STDOUT_FILENO))
		return 0;

	/* Get terminal window size */
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);

	if (winsize.ws_col >= 80)
		return 0;

	return -1;
}

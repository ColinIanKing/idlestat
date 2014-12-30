/*
 *  trace.c
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
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

#include "trace.h"
#include "utils.h"
#include "list.h"

struct trace_options {
	int buffer_size;
	struct list_head list;
};

struct enabled_eventtype {
	char *name;
	struct list_head list;
};

int idlestat_restore_trace_options(struct trace_options *options)
{
	struct enabled_eventtype *pos, *n;
	int write_problem = 0;

	if (write_int(TRACE_BUFFER_SIZE_PATH, options->buffer_size))
		write_problem = -1;

	list_for_each_entry_safe(pos, n, &options->list, list) {
		if (write_int(pos->name, 1))
			write_problem = -1;

		free(pos->name);
		list_del(&pos->list);
		free(pos);
	}
	free(options);
	return write_problem;
}

int idlestat_trace_enable(bool enable)
{
	return write_int(TRACE_ON_PATH, enable);
}

int idlestat_flush_trace(void)
{
	return write_int(TRACE_FILE, 0);
}

static int events_scan(char *name, struct list_head *head)
{
	FTSENT *file = NULL;
	char value;
	struct enabled_eventtype *found_type;
	char *paths[2];

	paths[0] = name;
	paths[1] = NULL;

	FTS *fts = fts_open(paths, FTS_PHYSICAL, NULL);
	if (!fts)
		return error("fts_open");

        while (NULL != (file = fts_read(fts))) {
		if (file->fts_info == FTS_ERR) {
			fprintf(stderr, "%s: %s\n", file->fts_path,
				strerror(file->fts_errno));
			fts_close(fts);
			return -1;
		}

                if (strcmp(file->fts_name, "enable"))
			continue;

		if (read_char(file->fts_path, &value)) {
			fts_close(fts);
			return -1;
		}

		if (value != '1')
			continue;

		found_type = calloc(1, sizeof(struct enabled_eventtype));
		if (!found_type) {
			fts_close(fts);
			return error(__func__);
		}

		found_type->name = strdup(file->fts_path);
		if (!found_type->name) {
			free(found_type);
			return error(__func__);
		}

		list_add(&found_type->list, head);
        }

        fts_close(fts);
        return 0;
}

#define TRACE_EVENTS_DIR TRACE_PATH "/events/"

struct trace_options *idlestat_store_trace_options()
{
	int status;
	struct trace_options *options;
	struct enabled_eventtype *pos, *n;

	options = calloc(1, sizeof(struct trace_options));
	if (!options)
		return ptrerror(__func__);
	INIT_LIST_HEAD(&options->list);

	if (read_int(TRACE_BUFFER_SIZE_PATH, &options->buffer_size))
		goto cannot_get_event_options;

	status = events_scan(TRACE_EVENTS_DIR, &options->list);
	if (status == 0)
		return options;

cannot_get_event_options:
	/* Failure, clean up */
	list_for_each_entry_safe(pos, n, &options->list, list) {
		free(pos->name);
		list_del(&pos->list);
		free(pos);
	}

	free(options);
	return ptrerror(NULL);
}

int idlestat_init_trace(unsigned int duration)
{
	int bufsize;

	/* Assuming the worst case where we can have for cpuidle,
	 * TRACE_IDLE_NRHITS_PER_SEC.  Each state enter/exit line are
	 * 196 chars wide, so we have 2 x 196 x TRACE_IDLE_NRHITS_PER_SEC lines.
	 * For cpufreq, assume a 196-character line for each frequency change,
	 * and expect a rate of TRACE_CPUFREQ_NRHITS_PER_SEC.
	 * Divide by 2^10 to have Kb. We add 1Kb to be sure to round up.
	*/

	bufsize = 2 * TRACE_IDLE_LENGTH * TRACE_IDLE_NRHITS_PER_SEC;
	bufsize += TRACE_CPUFREQ_LENGTH * TRACE_CPUFREQ_NRHITS_PER_SEC;
	bufsize = (bufsize * duration / (1 << 10)) + 1;

	if (write_int(TRACE_BUFFER_SIZE_PATH, bufsize))
		return -1;

	if (read_int(TRACE_BUFFER_TOTAL_PATH, &bufsize))
		return -1;

	printf("Total trace buffer: %d kB\n", bufsize);

	/* Disable all the traces */
	if (write_int(TRACE_EVENT_PATH, 0))
		return -1;

	/* Enable cpu_idle traces */
	if (write_int(TRACE_CPUIDLE_EVENT_PATH, 1))
		return -1;

	/* Enable cpu_frequency traces */
	if (write_int(TRACE_CPUFREQ_EVENT_PATH, 1))
		return -1;

	/* Enable irq traces */
	if (write_int(TRACE_IRQ_EVENT_PATH, 1))
		return -1;

	/* Enable ipi traces..
	 * Ignore if not present, for backward compatibility
	 */
	write_int(TRACE_IPI_EVENT_PATH, 1);

	return 0;
}

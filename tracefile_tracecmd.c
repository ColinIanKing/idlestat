/*
 *  tracefile_tracecmd.c
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
 *     Koan-Sin Tan <freedom.tan@linaro.org>
 *     Tuukka Tikkanen <tuukka.tikkanen@linaro.org>
 *
 */
#include "topology.h"
#include "trace_ops.h"
#include "utils.h"
#include "idlestat.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#define TRACE_CMD_REPORT_FORMAT "%*[^]]] %lf:%*[^=]=%u%*[^=]=%d"

void tracecmd_load_text_data_lines(FILE *f, char *buffer, struct cpuidle_datas *datas)
{
	double begin = 0, end = 0;
	size_t count = 0, start = 1;

	setup_topo_states(datas);

	do {
		if (load_text_data_line(buffer, datas, TRACE_CMD_REPORT_FORMAT,
					&begin, &end, &start) != -1) {
			count++;
		}
	} while (fgets(buffer, BUFSIZE, f));

	fprintf(stderr, "Log is %lf secs long with %zu events\n",
		end - begin, count);
}

static int tracecmd_report_magic(const char *filename)
{
	FILE *f;
	char *line;
	char buffer[BUFSIZE];

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__,
					filename);
		return -1;
	}

	line = fgets(buffer, BUFSIZE, f);
	fclose(f);

	return (line != NULL) && !strncmp(buffer, "version = ", 10);
}

static struct cpuidle_datas * tracecmd_report_load(const char *filename)
{
	FILE *f;
	unsigned int nrcpus;
	struct cpuidle_datas *datas;
	int ret;
	char *line;
	char buffer[BUFSIZE];

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__,
					filename);
		return ptrerror(NULL);
	}

	/* Version line */
	line = fgets(buffer, BUFSIZE, f);
	if (!line)
		goto error_close;

	/* Number of CPUs */
	nrcpus = 0;
	line = fgets(buffer, BUFSIZE, f);
	ret = sscanf(buffer, "cpus=%u", &nrcpus);
	if (ret != 1)
		nrcpus = 0;
	line = fgets(buffer, BUFSIZE, f);

	if (!line)
		goto error_close;

	if (!nrcpus) {
		fclose(f);
		return ptrerror("Cannot load trace file (nrcpus == 0)");
	}

	datas = calloc(sizeof(*datas), 1);
	if (!datas) {
		fclose(f);
		return ptrerror(__func__);
	}

	datas->nrcpus = nrcpus;
	datas->pstates = build_pstate_info(nrcpus);
	if (!datas->pstates)
		goto propagate_error_free_datas;

	datas->topo = read_sysfs_cpu_topo();
	if (is_err(datas->topo))
		goto propagate_error_free_datas;

	/* Build C-state information from current host sysfs */
	datas->cstates = build_cstate_info(nrcpus);
	if (is_err(datas->cstates))
		goto propagate_error_free_datas;

	tracecmd_load_text_data_lines(f, buffer, datas);

	fclose(f);

	return datas;

 propagate_error_free_datas:
	fclose(f);
	if (!is_err(datas->topo))
		release_cpu_topo_info(datas->topo);
	if (!is_err(datas->cstates))
		release_cstate_info(datas->cstates, nrcpus);
	free(datas);
	return ptrerror(NULL);

 error_close:
	fclose(f);
	fprintf(stderr, "%s: error or EOF while reading '%s': %m",
		__func__, filename);
	return ptrerror(NULL);
}

static const struct trace_ops tracecmd_report_trace_ops = {
	.name = "trace-cmd report",
	.check_magic = tracecmd_report_magic,
	.load = tracecmd_report_load
};

EXPORT_TRACE_OPS(tracecmd_report);

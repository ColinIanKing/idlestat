/*
 *  tracefile_idlestat.c
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
 * Based on code migrated from idlestat.c
 *
 * Contributors:
 *     Tuukka Tikkanen <tuukka.tikkanen@linaro.org>
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
#include <float.h>

#define TRACE_FORMAT "%*[^]]] %*s %lf:%*[^=]=%u%*[^=]=%d"

/**
 * load_and_build_cstate_info - load c-state info written to idlestat
 * trace file.
 *
 * @f: the file handle of the idlestat trace file
 * @nrcpus: number of CPUs
 *
 * @return: per-CPU array of structs (success) or ptrerror() (error)
 */
static struct cpuidle_cstates *load_and_build_cstate_info(FILE* f, char *buffer, int nrcpus, struct cpu_topology * topo)
{
	int cpu;
	struct cpuidle_cstates *cstates;

	assert(f != NULL);
	assert(buffer != NULL);
	assert(nrcpus > 0);

	cstates = calloc(nrcpus, sizeof(*cstates));
	if (!cstates)
		return ptrerror(__func__);

	for (cpu = 0; cpu < nrcpus; cpu++) {
		int i, read_cpu;
		struct cpuidle_cstate *c;

		cstates[cpu].cstate_max = -1;
		cstates[cpu].current_cstate = -1;

		if (!cpu_is_online(topo, cpu))
			continue;

		if (sscanf(buffer, "cpuid %d:\n", &read_cpu) != 1 ||
				read_cpu != cpu) {
			release_cstate_info(cstates, cpu);
			fprintf(stderr,
				"%s: Error reading trace file\n"
				"Expected: cpuid %d:\n"
				"Read: %s",
				__func__, cpu, buffer);
			return ptrerror(NULL);
		}

		for (i = 0; i < MAXCSTATE; i++) {
			int residency;
			char *name = malloc(128);
			if (!name) {
				release_cstate_info(cstates, cpu);
				return ptrerror(__func__);
			}

			fgets(buffer, BUFSIZE, f);
			sscanf(buffer, "\t%s\n", name);
			fgets(buffer, BUFSIZE, f);
			sscanf(buffer, "\t%d\n", &residency);

			c = &(cstates[cpu].cstate[i]);
			if (!strcmp(name, "(null)")) {
				free(name);
				c->name = NULL;
			} else {
				c->name = name;
			}
			c->data = NULL;
			c->nrdata = 0;
			c->early_wakings = 0;
			c->late_wakings = 0;
			c->avg_time = 0.;
			c->max_time = 0.;
			c->min_time = DBL_MAX;
			c->duration = 0.;
			c->target_residency = residency;
		}
		fgets(buffer, BUFSIZE, f);
	}

	return cstates;
}

int load_text_data_line(char *buffer, struct cpuidle_datas *datas, char *format, double *begin, double *end, size_t *start)
{
	unsigned int state, freq, cpu;
	double time;

	if (strstr(buffer, "cpu_idle")) {
		if (sscanf(buffer, format, &time, &state, &cpu)
		    != 3) {
			fprintf(stderr, "warning: Unrecognized cpuidle "
				"record. The result of analysis might "
				"be wrong.\n");
			return -1;
		}

		if (*start) {
			*begin = time;
			*start = 0;
		}
		*end = time;

		return store_data(time, state, cpu, datas);
	}

	if (strstr(buffer, "cpu_frequency")) {
		if (sscanf(buffer, format, &time, &freq, &cpu) != 3) {
			fprintf(stderr, "warning: Unrecognized cpufreq "
				"record. The result of analysis might "
				"be wrong.\n");
			return -1;
		}
		return cpu_change_pstate(datas, cpu, freq, time);
	}

	return get_wakeup_irq(datas, buffer);
}

void load_text_data_lines(FILE *f, char *buffer, struct cpuidle_datas *datas)
{
	double begin = 0, end = 0;
	size_t count = 0, start = 1;

	setup_topo_states(datas);

	do {
		if (load_text_data_line(buffer, datas, TRACE_FORMAT,
					&begin, &end, &start) != -1) {
			count++;
		}
	} while (fgets(buffer, BUFSIZE, f));

	fprintf(stderr, "Log is %lf secs long with %zu events\n",
		end - begin, count);
}

static int idlestat_magic(const char *filename)
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

	return (line != NULL) && !strncmp(buffer, "idlestat version", 16);
}

static struct cpuidle_datas * idlestat_native_load(const char *filename)
{
	FILE *f;
	unsigned int nrcpus;
	struct cpuidle_datas *datas;
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
	line = fgets(buffer, BUFSIZE, f);
	if (!line)
		goto error_close;

	if (sscanf(buffer, "cpus=%u", &nrcpus) != 1 || nrcpus == 0) {
		fclose(f);
		return ptrerror("Cannot load trace file (nrcpus == 0)");
	}

	line = fgets(buffer, BUFSIZE, f);
	if (!line)
		goto error_close;

	datas = calloc(sizeof(*datas), 1);
	if (!datas) {
		fclose(f);
		return ptrerror(__func__);
	}

	datas->nrcpus = nrcpus;
	datas->pstates = build_pstate_info(nrcpus);
	if (!datas->pstates)
		goto propagate_error_free_datas;

	/* Read topology information */
	datas->topo = read_cpu_topo_info(f, buffer);
	if (is_err(datas->topo))
		goto propagate_error_free_datas;

	/* Read C-state information */
	datas->cstates = load_and_build_cstate_info(f, buffer,
						nrcpus, datas->topo);
	if (is_err(datas->cstates))
		goto propagate_error_free_datas;

	load_text_data_lines(f, buffer, datas);

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


static const struct trace_ops idlestat_trace_ops = {
	.name = "Idlestat native",
	.check_magic = idlestat_magic,
	.load = idlestat_native_load
};

EXPORT_TRACE_OPS(idlestat);

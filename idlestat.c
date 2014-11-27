/*
 *  idlestat.c
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
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <float.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <assert.h>

#include "idlestat.h"
#include "utils.h"
#include "trace.h"
#include "list.h"
#include "topology.h"
#include "energy_model.h"
#include "report_ops.h"

#define IDLESTAT_VERSION "0.4-rc1"
#define USEC_PER_SEC 1000000

static char buffer[BUFSIZE];

/* I happen to agree with David Wheeler's assertion that Unix filenames
 * are too flexible. Eliminate some of the madness.
 * http://www.dwheeler.com/essays/fixing-unix-linux-filenames.html
 */
static inline int bad_filename(const char *filename)
{
	const char *c;

	c = filename;
	/* Check for first char being '-' */
	if (*c == '-') {
		fprintf(stderr, "Bad character '%c' found in filename\n", *c);
		return EINVAL;
	}
	for (; *c; c++) {
		/* Check for control chars and other bad characters */
		if (*c < 32 || *c == '<' || *c == '>' || *c == '|') {
			fprintf(stderr, "Bad character '%c' found in filename\n", *c);
			return EINVAL;
		}
	}
	return 0;
}

static inline int error(const char *str)
{
	perror(str);
	return -1;
}

static inline void *ptrerror(const char *str)
{
	perror(str);
	return NULL;
}

#define TRACE_TS_FORMAT "%*[^:]:%lf"

static double get_trace_ts(void)
{
	FILE *f;
	double ts;

	f = fopen(TRACE_STAT_FILE, "r");
	if (!f)
		return -1;

	while (fgets(buffer, BUFSIZE, f)) {
		if (!strstr(buffer, "now ts"))
			continue;
		if (!sscanf(buffer, TRACE_TS_FORMAT, &ts))
			ts = -1;
		break;
	}
	fclose(f);

	return ts;
}

static int display_cstates(struct report_ops *ops, void *arg, char *cpu, void *report_data)
{
	int i;
	bool cpu_header = false;
	struct cpuidle_cstates *cstates = arg;

	for (i = 0; i < cstates->cstate_max + 1; i++) {
		struct cpuidle_cstate *c = &cstates->cstate[i];

		if (c->nrdata == 0)
			/* nothing to report for this state */
			continue;

		if (!cpu_header) {
			ops->cstate_cpu_header(cpu, report_data);
			cpu_header = true;
		}

		ops->cstate_single_state(c, report_data);
	}
	if (cpu_header)
		ops->cstate_end_cpu(report_data);

	return 0;
}

static int display_pstates(struct report_ops *ops, void *arg, char *cpu, void *report_data)
{
	int i;
	bool cpu_header = false;
	struct cpufreq_pstates *pstates = arg;

	for (i = 0; i < pstates->max; i++) {

		struct cpufreq_pstate *p = &(pstates->pstate[i]);

		if (p->count == 0)
			/* nothing to report for this state */
			continue;

		if (!cpu_header) {
			ops->pstate_cpu_header(cpu, report_data);
			cpu_header = true;
		}

		ops->pstate_single_state(p, report_data);
	}

	if (cpu_header)
		ops->pstate_end_cpu(report_data);

	return 0;
}

static int display_wakeup(struct report_ops *ops, void *arg, char *cpu, void *report_data)
{
	int i;
	bool cpu_header = false;
	struct cpuidle_cstates *cstates = arg;
	struct wakeup_info *wakeinfo = &cstates->wakeinfo;
	struct wakeup_irq *irqinfo = wakeinfo->irqinfo;

	for (i = 0; i < wakeinfo->nrdata; i++, irqinfo++) {

		if (!cpu_header) {
			ops->wakeup_cpu_header(cpu, report_data);
			cpu_header = true;
		}

		ops->wakeup_single_state(irqinfo, report_data);
	}

	if (cpu_header)
		ops->wakeup_end_cpu(report_data);

	return 0;
}

static struct cpuidle_data *intersection(struct cpuidle_data *data1,
					 struct cpuidle_data *data2)
{
	double begin, end;
	struct cpuidle_data *data;

	begin = MAX(data1->begin, data2->begin);
	end = MIN(data1->end, data2->end);

	if (begin >= end)
		return NULL;

	data = malloc(sizeof(*data));
	if (!data)
		return NULL;

	data->begin = begin;
	data->end = end;
	data->duration = end - begin;
	data->duration *= USEC_PER_SEC;

	return data;
}

static struct cpuidle_cstate *inter(struct cpuidle_cstate *c1,
				    struct cpuidle_cstate *c2)
{
	int i, j;
	struct cpuidle_data *interval;
	struct cpuidle_cstate *result;
	struct cpuidle_data *data = NULL;
	size_t index;

	if (!c1)
		return c2;
	if (!c2)
		return c1;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	for (i = 0, index = 0; i < c1->nrdata; i++) {

		for (j = index; j < c2->nrdata; j++) {
			struct cpuidle_data *tmp;

			/* intervals are ordered, no need to go further */
			if (c1->data[i].end < c2->data[j].begin)
				break;

			/* primary loop begins where we ended */
			if (c1->data[i].begin > c2->data[j].end)
				index = j;

			interval = intersection(&c1->data[i], &c2->data[j]);
			if (!interval)
				continue;

			result->min_time = MIN(result->min_time,
					       interval->duration);

			result->max_time = MAX(result->max_time,
					       interval->duration);

			result->avg_time = AVG(result->avg_time,
					       interval->duration,
					       result->nrdata + 1);

			result->duration += interval->duration;

			result->nrdata++;

			tmp = realloc(data, sizeof(*data) *
				       (result->nrdata + 1));
			if (!tmp) {
				free(data);
				free(result);
				return NULL;
			}
			data = tmp;

			result->data = data;
			result->data[result->nrdata - 1] = *interval;

			free(interval);
		}
	}

	return result;
}

static char *cpuidle_cstate_name(int cpu, int state)
{
	char *fpath, *name;
	FILE *snf;
	char line[256];

	if (asprintf(&fpath, CPUIDLE_STATENAME_PATH_FORMAT, cpu, state) < 0)
		return NULL;

	/* read cpuidle state name for the CPU */
	snf = fopen(fpath, "r");
	free(fpath);
	if (!snf)
		/* file not found, or other error */
		return NULL;

	name = fgets(line, sizeof(line)/sizeof(line[0]), snf);
	fclose(snf);
	if (name) {
		/* get rid of trailing characters and duplicate string */
		name = strtok(name, "\n ");
		name = strdup(name);
	}
	return name;
}


/**
 * release_cstate_info - free all C-state related structs
 * @cstates: per-cpu array of C-state statistics structs
 * @nrcpus: number of CPUs
 */
static void release_cstate_info(struct cpuidle_cstates *cstates, int nrcpus)
{
	int cpu, i;

	if (!cstates)
		/* already cleaned up */
		return;

	/* free C-state names */
	for (cpu = 0; cpu < nrcpus; cpu++) {
		for (i = 0; i < MAXCSTATE; i++) {
			struct cpuidle_cstate *c = &(cstates[cpu].cstate[i]);
			free(c->name);
			free(c->data);
		}
	}

	free(cstates->wakeinfo.irqinfo);

	/* free the cstates array */
	free(cstates);
}


/**
* cpuidle_get_target_residency - return the target residency of a c-state
* @cpu: cpuid
* @state: c-state number
*/
int cpuidle_get_target_residency(int cpu, int state)
{
	char *fpath;
	unsigned int tr;
	FILE *snf;

	if (asprintf(&fpath, CPUIDLE_STATE_TARGETRESIDENCY_PATH_FORMAT,
				cpu, state) < 0)
		return -1;

	/* read cpuidle state name for the CPU */
	snf = fopen(fpath, "r");
	if (!snf) {
		/* file not found, or other error */
		free(fpath);
		return -1;
	}
	fscanf(snf, "%u", &tr);
	fclose(snf);

	return tr;
}

/**
 * build_cstate_info - parse cpuidle sysfs entries and build per-CPU
 * structs to maintain statistics of C-state transitions
 * @nrcpus: number of CPUs
 *
 * Return: per-CPU array of structs (success) or NULL (error)
 */
static struct cpuidle_cstates *build_cstate_info(int nrcpus)
{
	int cpu;
	struct cpuidle_cstates *cstates;

	cstates = calloc(nrcpus, sizeof(*cstates));
	if (!cstates)
		return NULL;
	memset(cstates, 0, sizeof(*cstates) * nrcpus);

	/* initialize cstate_max for each cpu */
	for (cpu = 0; cpu < nrcpus; cpu++) {
		int i;
		struct cpuidle_cstate *c;

		cstates[cpu].cstate_max = -1;
		cstates[cpu].current_cstate = -1;
		for (i = 0; i < MAXCSTATE; i++) {
			c = &(cstates[cpu].cstate[i]);
			c->name = cpuidle_cstate_name(cpu, i);
			c->data = NULL;
			c->nrdata = 0;
			c->early_wakings = 0;
			c->late_wakings = 0;
			c->avg_time = 0.;
			c->max_time = 0.;
			c->min_time = DBL_MAX;
			c->duration = 0.;
			c->target_residency =
				cpuidle_get_target_residency(cpu, i);
		}
	}
	return cstates;
}

static void release_init_pstates(struct init_pstates *initp)
{
	if (!initp)
		return;

	free(initp->freqs);
	free(initp);
}

static struct init_pstates *build_init_pstates(void)
{
	struct init_pstates *initp;
	int nrcpus, cpu;
	unsigned int *freqs;

	nrcpus = sysconf(_SC_NPROCESSORS_CONF);
	if (nrcpus < 0)
		return NULL;

	initp = malloc(sizeof(*initp));
	if (!initp)
		return NULL;

	freqs = calloc(nrcpus, sizeof(*freqs));
	if (!freqs) {
		free(initp);
		return NULL;
	}

	initp->nrcpus = nrcpus;
	initp->freqs = freqs;
	for (cpu = 0; cpu < nrcpus; cpu++) {
		char *fpath;
		unsigned int *freq = &(freqs[cpu]);

		if (asprintf(&fpath, CPUFREQ_CURFREQ_PATH_FORMAT, cpu) < 0) {
			release_init_pstates(initp);
			return NULL;
		}
		if (read_int(fpath, (int *)freq))
			*freq = 0;
		free(fpath);
	}

	return initp;
}

static void output_pstates(FILE *f, struct init_pstates *initp, int nrcpus,
				double ts)
{
	int cpu;
	unsigned int freq;
	unsigned long ts_sec, ts_usec;

	ts_sec = (unsigned long)ts;
	ts_usec = (ts - ts_sec) * USEC_PER_SEC;

	for (cpu = 0; cpu < nrcpus; cpu++) {
		freq = initp? initp->freqs[cpu] : -1;
		fprintf(f, "%16s-%-5d [%03d] .... %5lu.%06lu: cpu_frequency: "
			"state=%u cpu_id=%d\n", "idlestat", getpid(), cpu,
			ts_sec, ts_usec, freq, cpu);
	}
}

/**
 * alloc_pstate - allocate, sort, and initialize pstate struct to maintain
 * statistics of P-state transitions
 * @pstates: per-CPU P-state statistics struct
 * @freq: frequency for which the newly pstate is allocated
 *
 * Return: the index of the newly allocated pstate struct
 */
static int alloc_pstate(struct cpufreq_pstates *pstates, unsigned int freq)
{
	struct cpufreq_pstate *pstate, *tmp;
	int nrfreq, i, next = 0;

	pstate = pstates->pstate;
	nrfreq = pstates->max;

	tmp = realloc(pstate, sizeof(*pstate) * (nrfreq + 1));
	if (!tmp) {
		perror(__func__);
		exit(1);
	}
	pstate = tmp;
	pstates->pstate = tmp;
	pstates->max = nrfreq + 1;

	for (i = 0; i < nrfreq && freq <= pstate[i].freq; i++)
		;

	next = i;
	memmove(pstate + next + 1, pstate + next, sizeof(*pstate) * (nrfreq - next));
	for (i = nrfreq; i > next; i--)
		pstate[i].id = i;
	if (pstates->current >= next)
		pstates->current++;

	pstate[next].id = next;
	pstate[next].freq = freq;
	pstate[next].count = 0;
	pstate[next].min_time = DBL_MAX;
	pstate[next].max_time = 0;
	pstate[next].avg_time = 0;
	pstate[next].duration = 0;

	return next;
}

/**
 * load_and_build_cstate_info - load c-state info written to idlestat trace file.
 * @f: the file handle of the idlestat trace file
 * @nrcpus: number of CPUs
 *
 * Return: per-CPU array of structs (success) or NULL (error)
 */
static struct cpuidle_cstates *load_and_build_cstate_info(FILE* f, int nrcpus)
{
	int cpu;
	struct cpuidle_cstates *cstates;

	cstates = calloc(nrcpus, sizeof(*cstates));
	if (!cstates)
		return NULL;
	memset(cstates, 0, sizeof(*cstates) * nrcpus);


	for (cpu = 0; cpu < nrcpus; cpu++) {
		int i, read_cpu;
		struct cpuidle_cstate *c;

		cstates[cpu].cstate_max = -1;
		cstates[cpu].current_cstate = -1;

		sscanf(buffer, "cpuid %d:\n", &read_cpu);

		if (read_cpu != cpu) {
			release_cstate_info(cstates, cpu);
			return NULL;
		}

		for (i = 0; i < MAXCSTATE; i++) {
			char *name = malloc(128);
			int residency;

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

/**
 * release_pstate_info - free all P-state related structs
 * @pstates: per-cpu array of P-state statistics structs
 * @nrcpus: number of CPUs
 */
static void release_pstate_info(struct cpufreq_pstates *pstates, int nrcpus)
{
	int cpu;

	if (!pstates)
		/* already cleaned up */
		return;

	/* first check and clean per-cpu structs */
	for (cpu = 0; cpu < nrcpus; cpu++)
		if (pstates[cpu].pstate)
			free(pstates[cpu].pstate);

	/* now free the master cpufreq structs */
	free(pstates);

	return;
}

/**
 * build_pstate_info - allocate and initialize per-CPU structs to maintain
 * statistics of P-state transitions
 * @nrcpus: number of CPUs
 *
 * Return: per-CPU array of structs (success) or NULL (error)
 */
static struct cpufreq_pstates *build_pstate_info(int nrcpus)
{
	int cpu;
	struct cpufreq_pstates *pstates;

	pstates = calloc(nrcpus, sizeof(*pstates));
	if (!pstates)
		return NULL;
	memset(pstates, 0, sizeof(*pstates) * nrcpus);

	for (cpu = 0; cpu < nrcpus; cpu++) {
		pstates[cpu].pstate = NULL;
		pstates[cpu].max = 0;
		pstates[cpu].current = -1;	/* unknown */
		pstates[cpu].idle = -1;		/* unknown */
		pstates[cpu].time_enter = 0.;
		pstates[cpu].time_exit = 0.;
	}

	return pstates;
}

static int get_current_pstate(struct cpuidle_datas *datas, int cpu,
				struct cpufreq_pstates **pstates,
				struct cpufreq_pstate **pstate)
{
	struct cpufreq_pstates *ps;

	if (cpu < 0 || cpu > datas->nrcpus)
		return -2;

	ps = &(datas->pstates[cpu]);

	*pstate = (ps->current == -1 ? NULL : &(ps->pstate[ps->current]));
	*pstates = ps;

	/* return 1 if CPU is idle, otherwise return 0 */
	return ps->idle;
}

static int freq_to_pstate_index(struct cpufreq_pstates *ps, unsigned int freq)
{
	int i;

	/* find frequency in table of P-states */
	for (i = 0; i < ps->max && freq != ps->pstate[i].freq; i++)
		/* just search */;

	/* if not found, return -1 */
	return i >= ps->max ? -1 : ps->pstate[i].id;
}

static void open_current_pstate(struct cpufreq_pstates *ps, double time)
{
	ps->time_enter = time;
}

static void open_next_pstate(struct cpufreq_pstates *ps, int s, double time)
{
	ps->current = s;
	if (ps->idle) {
		fprintf(stderr, "warning: opening P-state on idle CPU\n");
		return;
	}
	open_current_pstate(ps, time);
}

static void close_current_pstate(struct cpufreq_pstates *ps, double time)
{
	int c = ps->current;
	struct cpufreq_pstate *p = &(ps->pstate[c]);
	double elapsed;

	if (ps->idle) {
		fprintf(stderr, "warning: closing P-state on idle CPU\n");
		return;
	}
	elapsed = (time - ps->time_enter) * USEC_PER_SEC;
	p->min_time = MIN(p->min_time, elapsed);
	p->max_time = MAX(p->max_time, elapsed);
	p->avg_time = AVG(p->avg_time, elapsed, p->count + 1);
	p->duration += elapsed;
	p->count++;
}

static void cpu_change_pstate(struct cpuidle_datas *datas, int cpu,
			      unsigned int freq, double time)
{
	struct cpufreq_pstates *ps;
	struct cpufreq_pstate *p;
	int cur, next;

	cur = get_current_pstate(datas, cpu, &ps, &p);
	next = freq_to_pstate_index(ps, freq);
	if (next < 0)
		next = alloc_pstate(ps, freq);
	assert (next >= 0);

	switch (cur) {
	case 1:
		/* if CPU is idle, update current state and leave
		 * stats unchanged
		 */
		ps->current = next;
		return;

	case -1:
		/* current pstate is -1, i.e. this is the first update */
		open_next_pstate(ps, next, time);
		return;

	case 0:
		/* running CPU, update all stats, but skip closing current
		 * state if it's the initial update for CPU
		 */
		if (p)
			close_current_pstate(ps, time);
		open_next_pstate(ps, next, time);
		return;

	default:
		fprintf(stderr, "illegal pstate %d for cpu %d, exiting.\n",
			cur, cpu);
		exit(-1);
	}
}

static void cpu_pstate_idle(struct cpuidle_datas *datas, int cpu, double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	if (ps->current != -1)
		close_current_pstate(ps, time);
	ps->idle = 1;
}

static void cpu_pstate_running(struct cpuidle_datas *datas, int cpu,
			       double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	ps->idle = 0;
	if (ps->current != -1)
		open_current_pstate(ps, time);
}

static int cstate_begin(double time, int state, struct cpuidle_cstates *cstates)
{
	struct cpuidle_cstate *cstate = &cstates->cstate[state];
	struct cpuidle_data *data = cstate->data;
	int nrdata = cstate->nrdata;

	data = realloc(data, sizeof(*data) * (nrdata + 1));
	if (!data)
		return error("realloc data");

	data[nrdata].begin = time;

	cstate->data = data;
	cstates->cstate_max = MAX(cstates->cstate_max, state);
	cstates->current_cstate = state;
	cstates->wakeirq = NULL;
	return 0;
}

static int cstate_end(double time, struct cpuidle_cstates *cstates)
{
	int last_cstate = cstates->current_cstate;
	struct cpuidle_cstate *cstate = &cstates->cstate[last_cstate];
	struct cpuidle_data *data = &cstate->data[cstate->nrdata];

	data->end = time;
	data->duration = data->end - data->begin;

	/* That happens when precision digit in the file exceed
	 * 7 (eg. xxx.1000000). Ignoring the result because I don't
	 * find a way to fix with the sscanf used in the caller
	 */
	if (data->duration < 0)
		data->duration = 0;

	/* convert to us */
	data->duration *= USEC_PER_SEC;
	cstates->actual_residency = as_expected;
	if (data->duration < cstate->target_residency) {
		/* over estimated */
		cstate->early_wakings++;
		cstates->actual_residency = too_short;
	} else {
		/* under estimated */
		int next_cstate = last_cstate + 1;
		if (next_cstate <= cstates->cstate_max) {
			int tr = cstates->cstate[next_cstate].target_residency;
			if (tr > 0 && data->duration >= tr) {
				cstate->late_wakings++;
				cstates->actual_residency = too_long;
			}
		}
	}

	cstate->min_time = MIN(cstate->min_time, data->duration);
	cstate->max_time = MAX(cstate->max_time, data->duration);
	cstate->avg_time = AVG(cstate->avg_time, data->duration,
			       cstate->nrdata + 1);
	cstate->duration += data->duration;
	cstate->nrdata++;

	/* CPU is no longer idle */
	cstates->current_cstate = -1;

	return 0;
}

static int store_data(double time, int state, int cpu,
		      struct cpuidle_datas *datas, int count)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct cpufreq_pstate *pstate = datas->pstates[cpu].pstate;
	int ret;

	/* ignore when we got a "closing" state first */
	if (state == -1 && cstates->cstate_max == -1)
		return 0;

	if (state == -1) {
		ret = cstate_end(time, cstates);
		/* update P-state stats if supported */
		if (!ret && pstate)
			cpu_pstate_running(datas, cpu, time);
	} else {
		ret = cstate_begin(time, state, cstates);
		/* update P-state stats if supported */
		if (!ret && pstate)
			cpu_pstate_idle(datas, cpu, time);
	}

	return ret;
}

static struct wakeup_irq *find_irqinfo(struct wakeup_info *wakeinfo, int irqid,
				       const char *irqname)
{
	struct wakeup_irq *irqinfo;
	int i;

	for (i = 0; i < wakeinfo->nrdata; i++) {
		irqinfo = &wakeinfo->irqinfo[i];
		if (irqinfo->id == irqid && !strcmp(irqinfo->name, irqname))
			return irqinfo;
	}

	return NULL;
}

static int store_irq(int cpu, int irqid, char *irqname,
		     struct cpuidle_datas *datas)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct wakeup_irq *irqinfo;
	struct wakeup_info *wakeinfo = &cstates->wakeinfo;

	if (cstates->wakeirq != NULL)
		return 0;

	irqinfo = find_irqinfo(wakeinfo, irqid, irqname);
	if (NULL == irqinfo) {
		irqinfo = realloc(wakeinfo->irqinfo,
				sizeof(*irqinfo) * (wakeinfo->nrdata + 1));
		if (!irqinfo)
			return error("realloc irqinfo");

		wakeinfo->irqinfo = irqinfo;

		irqinfo += wakeinfo->nrdata++;
		irqinfo->id = irqid;
		strncpy(irqinfo->name, irqname, sizeof(irqinfo->name));
		irqinfo->name[sizeof(irqinfo->name) - 1] = '\0';
		irqinfo->count = 0;
		irqinfo->early_triggers = 0;
		irqinfo->late_triggers = 0;
	}

	irqinfo->count++;
	if (cstates->actual_residency == too_short)
		irqinfo->early_triggers++;
	else if (cstates->actual_residency == too_long)
		irqinfo->late_triggers++;

	cstates->wakeirq = irqinfo;
	return 0;
}

static void write_cstate_info(FILE *f, int cpu, char *name, int target)
{
	fprintf(f, "\t%s\n", name);
	fprintf(f, "\t%d\n", target);
}

void output_cstate_info(FILE *f, int nrcpus) {
	struct cpuidle_cstates *cstates;
	int i, j;

	cstates = build_cstate_info(nrcpus);

	for (i=0; i < nrcpus; i++) {
		fprintf(f, "cpuid %d:\n",  i);
		for (j=0; j < MAXCSTATE ; j++) {
			write_cstate_info(f, i, cstates[i].cstate[j].name,
				cstates[i].cstate[j].target_residency);
		}
	}
}

#define TRACE_IRQ_FORMAT "%*[^[][%d] %*[^=]=%d%*[^=]=%16s"
#define TRACE_IPIIRQ_FORMAT "%*[^[][%d] %*[^(](%32s"
#define TRACECMD_REPORT_FORMAT "%*[^]]] %lf:%*[^=]=%u%*[^=]=%d"
#define TRACE_FORMAT "%*[^]]] %*s %lf:%*[^=]=%u%*[^=]=%d"

static int get_wakeup_irq(struct cpuidle_datas *datas, char *buffer, int count)
{
	int cpu, irqid;
	char irqname[NAMELEN+1];

	if (strstr(buffer, "irq_handler_entry")) {
		if (sscanf(buffer, TRACE_IRQ_FORMAT, &cpu, &irqid,
			      irqname) != 3) {
			fprintf(stderr, "warning: Unrecognized "
					"irq_handler_entry record skipped.\n");
			return -1;
		}

		store_irq(cpu, irqid, irqname, datas);
		return 0;
	}

	if (strstr(buffer, "ipi_entry")) {
		if (sscanf(buffer, TRACE_IPIIRQ_FORMAT, &cpu, irqname) != 2) {
			fprintf(stderr, "warning: Unrecognized ipi_entry "
					"record skipped\n");
			return -1;
		}

		irqname[strlen(irqname) - 1] = '\0';
		store_irq(cpu, -1, irqname, datas);
		return 0;
	}

	return -1;
}

struct cpuidle_datas *idlestat_load(struct program_options *options)
{
	FILE *f;
	unsigned int state = 0, freq = 0, cpu = 0, nrcpus = 0;
	double time, begin = 0, end = 0;
	size_t count = 0, start = 1;
	struct cpuidle_datas *datas;
	int ret;

	f = fopen(options->filename, "r");
	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n", __func__,
					options->filename);
		return NULL;
	}

	/* version line */
	fgets(buffer, BUFSIZE, f);
	if (strstr(buffer, "idlestat")) {
		options->format = IDLESTAT_HEADER;
		fgets(buffer, BUFSIZE, f);
		if (sscanf(buffer, "cpus=%u", &nrcpus) != 1)
			nrcpus = 0;
		fgets(buffer, BUFSIZE, f);
	} else if (strstr(buffer, "# tracer")) {
		options->format = TRACE_CMD_HEADER;
		while(!feof(f)) {
			if (buffer[0] != '#')
				break;
			if (strstr(buffer, "#P:") &&
			    sscanf(buffer, "#%*[^#]#P:%u", &nrcpus) != 1)
				nrcpus = 0;
			fgets(buffer, BUFSIZE, f);
		}
	} else {
		fprintf(stderr, "%s: unrecognized import format in '%s'\n",
				__func__, options->filename);
		fclose(f);
		return NULL;
	}

	if (!nrcpus) {
		fclose(f);
		return ptrerror("read error for 'cpus=' in trace file");
	}

	datas = malloc(sizeof(*datas));
	if (!datas) {
		fclose(f);
		return ptrerror("malloc datas");
	}

	/* read topology information */
	read_cpu_topo_info(f, buffer);

	datas->nrcpus = nrcpus;

	/* read c-state information */
	if (options->format == IDLESTAT_HEADER)
		datas->cstates = load_and_build_cstate_info(f, nrcpus);
	else
		datas->cstates = build_cstate_info(nrcpus);
	if (!datas->cstates) {
		free(datas);
		fclose(f);
		if (options->format == IDLESTAT_HEADER)
			return ptrerror("load_and_build_cstate_info: out of memory");
		else
			return ptrerror("build_cstate_info: out of memory");
	}

	datas->pstates = build_pstate_info(nrcpus);
	if (!datas->pstates) {
		free(datas->cstates);
		free(datas);
		fclose(f);
		return ptrerror("build_pstate_info: out of memory");
	}

	do {
		if (strstr(buffer, "cpu_idle")) {
			if (sscanf(buffer, TRACE_FORMAT, &time, &state, &cpu)
			    != 3) {
				fprintf(stderr, "warning: Unrecognized cpuidle "
					"record. The result of analysis might "
					"be wrong.\n");
				continue;
			}

			if (start) {
				begin = time;
				start = 0;
			}
			end = time;

			store_data(time, state, cpu, datas, count);
			count++;
			continue;
		} else if (strstr(buffer, "cpu_frequency")) {
			if (sscanf(buffer, TRACE_FORMAT, &time, &freq, &cpu)
			    != 3) {
				fprintf(stderr, "warning: Unrecognized cpufreq "
					"record. The result of analysis might "
					"be wrong.\n");
				continue;
			}
			cpu_change_pstate(datas, cpu, freq, time);
			count++;
			continue;
		}

		ret = get_wakeup_irq(datas, buffer, count);
		count += (0 == ret) ? 1 : 0;

	} while (fgets(buffer, BUFSIZE, f));

	fclose(f);

	fprintf(stderr, "Log is %lf secs long with %zd events\n",
		end - begin, count);

	return datas;
}

struct cpuidle_datas *cluster_data(struct cpuidle_datas *datas)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_datas *result;
	int i, j;
	int cstate_max = -1;

	result = malloc(sizeof(*result));
	if (!result)
		return NULL;

	result->nrcpus = -1; /* the cluster */
	result->pstates = NULL;
	result->cstates = calloc(sizeof(*result->cstates), 1);
	if (!result->cstates) {
		free(result);
		return NULL;
	}

	/* hack but negligeable overhead */
	for (i = 0; i < datas->nrcpus; i++)
		cstate_max = MAX(cstate_max, datas->cstates[i].cstate_max);
	result->cstates[0].cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {

		for (j = 0, cstates = NULL; j < datas->nrcpus; j++) {

			c1 = &datas->cstates[j].cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}

		/* copy state names from the first cpu */
		cstates->name = strdup(datas->cstates[0].cstate[i].name);

		result->cstates[0].cstate[i] = *cstates;
	}

	return result;
}

struct cpuidle_cstates *core_cluster_data(struct cpu_core *s_core)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_cstates *result;
	struct cpu_cpu      *s_cpu;
	int i;
	int cstate_max = -1;

	if (!s_core->is_ht)
		list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
			return s_cpu->cstates;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	/* hack but negligeable overhead */
	list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
		cstate_max = MAX(cstate_max, s_cpu->cstates->cstate_max);
	result->cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {
		cstates = NULL;
		list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu) {
			c1 = &s_cpu->cstates->cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}
		/* copy state name from first cpu */
		s_cpu = list_first_entry(&s_core->cpu_head, struct cpu_cpu,
				list_cpu);
		cstates->name = strdup(s_cpu->cstates->cstate[i].name);

		result->cstate[i] = *cstates;
	}

	return result;
}

struct cpuidle_cstates *physical_cluster_data(struct cpu_physical *s_phy)
{
	struct cpuidle_cstate *c1, *cstates;
	struct cpuidle_cstates *result;
	struct cpu_core      *s_core;
	int i;
	int cstate_max = -1;

	result = calloc(sizeof(*result), 1);
	if (!result)
		return NULL;

	/* hack but negligeable overhead */
	list_for_each_entry(s_core, &s_phy->core_head, list_core)
		cstate_max = MAX(cstate_max, s_core->cstates->cstate_max);
	result->cstate_max = cstate_max;

	for (i = 0; i < cstate_max + 1; i++) {
		cstates = NULL;
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			c1 = &s_core->cstates->cstate[i];

			cstates = inter(cstates, c1);
			if (!cstates)
				continue;
		}
		/* copy state name from first core (if any) */
		s_core = list_first_entry(&s_phy->core_head, struct cpu_core,
				list_core);
		cstates->name = s_core->cstates->cstate[i].name ?
			strdup(s_core->cstates->cstate[i].name) : NULL;

		result->cstate[i] = *cstates;
	}

	return result;
}

static void help(const char *cmd)
{
	fprintf(stderr,
		"\nUsage:\nTrace mode:\n\t%s --trace -f|--trace-file <filename>"
		" -o|--output-file <filename> -t|--duration <seconds>"
		" -C|--csv-report -B|--boxless-report"
		" -c|--idle -p|--frequency -w|--wakeup", basename(cmd));
	fprintf(stderr,
		"\nReporting mode:\n\t%s --import -f|--trace-file <filename>"
		" -C|--csv-report -B|--boxless-report"
		" -o|--output-file <filename>", basename(cmd));
	fprintf(stderr,
		"\n\nExamples:\n1. Run a trace, post-process the results"
		" (default is to show only C-state statistics):\n\tsudo "
		"./%s --trace -f /tmp/mytrace -t 10\n", basename(cmd));
	fprintf(stderr,
		"\n2. Run a trace, post-process the results and print all"
		" statistics:\n\tsudo ./%s --trace -f /tmp/mytrace -t 10 -p -c -w\n",
		basename(cmd));
	fprintf(stderr,
		"\n3. Run a trace with an external workload, post-process the"
		" results:\n\tsudo ./%s --trace -f /tmp/mytrace -t 10 -p -c -w -- rt-app /tmp/mp3.json\n",
		basename(cmd));
	fprintf(stderr,
		"\n4. Post-process a trace captured earlier:\n\tsudo ./%s"
		" --import -f /tmp/mytrace\n", basename(cmd));
	fprintf(stderr,
		"\n5. Run a trace, post-process the results and print all"
		" statistics into a file:\n\tsudo ./%s --trace -f /tmp/mytrace -t 10 -p -c -w"
		" -o /tmp/myreport\n", basename(cmd));
}

static void version(const char *cmd)
{
	printf("%s version %s\n", basename(cmd), IDLESTAT_VERSION);
}

int getoptions(int argc, char *argv[], struct program_options *options)
{
	struct option long_options[] = {
		{ "trace",       no_argument,       &options->mode, TRACE },
		{ "import",      no_argument,       &options->mode, IMPORT },
		{ "trace-file",  required_argument, NULL, 'f' },
		{ "output-file", required_argument, NULL, 'o' },
		{ "help",        no_argument,       NULL, 'h' },
		{ "duration",    required_argument, NULL, 't' },
		{ "version",     no_argument,       NULL, 'V' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "idle",        no_argument,       NULL, 'c' },
		{ "frequency",   no_argument,       NULL, 'p' },
		{ "wakeup",      no_argument,       NULL, 'w' },
		{ "energy-model-file",  required_argument, NULL, 'e' },
		{ "csv-report",  no_argument,       NULL, 'C' },
		{ "boxless-report", no_argument,    NULL, 'B' },
		{ 0, 0, 0, 0 }
	};
	int c;

	memset(options, 0, sizeof(*options));
	options->filename = NULL;
	options->outfilename = NULL;
	options->mode = -1;
	options->format = -1;
	options->report_ops = &default_report_ops;

	while (1) {

		int optindex = 0;

		c = getopt_long(argc, argv, ":de:f:o:ht:cpwVvCB",
				long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			options->filename = optarg;
			break;
		case 'o':
			options->outfilename = optarg;
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		case 't':
			options->duration = atoi(optarg);
			break;
		case 'c':
			options->display |= IDLE_DISPLAY;
			break;
		case 'p':
			options->display |= FREQUENCY_DISPLAY;
			break;
		case 'w':
			options->display |= WAKEUP_DISPLAY;
			break;
		case 'V':
			version(argv[0]);
			exit(0);
			break;
		case 'v':
			options->verbose++;
			break;
		case 'e':
			options->energy_model_filename = optarg;
			break;
		case 'C':
			options->report_ops = &csv_report_ops;
			break;
		case 'B':
			options->report_ops = &boxless_report_ops;
			break;
		case 0:     /* getopt_long() set a variable, just keep going */
			break;
		case ':':   /* missing option argument */
			fprintf(stderr, "%s: option `-%c' requires an argument\n",
				basename(argv[0]), optopt);
			return -1;
		case '?':   /* invalid option */
		default:
			fprintf(stderr, "%s: Unknown option `-%c'.\n",
				basename(argv[0]), optopt);
			help(argv[0]);
			return -1;
		}
	}

	if (options->mode < 0) {
		fprintf(stderr, "select a mode: --trace or --import\n");
		return -1;
	}

	if (NULL == options->filename) {
		fprintf(stderr, "expected -f <trace filename>\n");
		return -1;
	}

	if (bad_filename(options->filename))
		return -1;

	if (options->outfilename && bad_filename(options->outfilename))
		return -1;

	if (options->mode == TRACE) {
		if (options->duration <= 0) {
			fprintf(stderr, "expected -t <seconds>\n");
			return -1;
		}
	}

	if (options->display == 0)
		options->display = IDLE_DISPLAY;

	return optind;
}

static int idlestat_file_for_each_line(const char *path, void *data,
					int (*handler)(const char *, void *))
{
	FILE *f;
	int ret;

	if (!handler)
		return -1;

	f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n",
			__func__, path);
		return -1;
	}

	while (fgets(buffer, BUFSIZE, f)) {
		ret = handler(buffer, data);
		if (ret)
			break;
	}

	fclose(f);

	return ret;
}

static int idlestat_store(const char *path, double start_ts, double end_ts,
				struct init_pstates *initp)
{
	FILE *f;
	int ret;

	ret = sysconf(_SC_NPROCESSORS_CONF);
	if (ret < 0)
		return -1;

	if (initp)
		assert(ret == initp->nrcpus);

	f = fopen(path, "w+");

	if (!f) {
		fprintf(stderr, "%s: failed to open '%s': %m\n",
			__func__, path);
		return -1;
	}

	fprintf(f, "idlestat version = %s\n", IDLESTAT_VERSION);
	fprintf(f, "cpus=%d\n", ret);

	/* output topology information */
	output_cpu_topo_info(f);

	/* output c-states information */
	output_cstate_info(f, ret);

	/* emit initial pstate changes */
	if (initp)
		output_pstates(f, initp, initp->nrcpus, start_ts);

	ret = idlestat_file_for_each_line(TRACE_FILE, f, store_line);

	/* emit final pstate changes */
	if (initp)
		output_pstates(f, NULL, initp->nrcpus, end_ts);

	fclose(f);

	return ret;
}

static int idlestat_wake_all(void)
{
	int rcpu, i, ret;
	cpu_set_t cpumask;
	cpu_set_t original_cpumask;

	ret = sysconf(_SC_NPROCESSORS_CONF);
	if (ret < 0)
		return -1;

	rcpu = sched_getcpu();
	if (rcpu < 0)
		return -1;

	/* Keep track of the CPUs we will run on */
	sched_getaffinity(0, sizeof(original_cpumask), &original_cpumask);

	for (i = 0; i < ret; i++) {

		/* Pointless to wake up ourself */
		if (i == rcpu)
			continue;

		/* Pointless to wake CPUs we will not run on */
		if (!CPU_ISSET(i, &original_cpumask))
			continue;

		CPU_ZERO(&cpumask);
		CPU_SET(i, &cpumask);

		sched_setaffinity(0, sizeof(cpumask), &cpumask);
	}

	/* Enable all the CPUs of the original mask */
	sched_setaffinity(0, sizeof(original_cpumask), &original_cpumask);

	return 0;
}

static volatile sig_atomic_t sigalrm = 0;

static void sighandler(int sig)
{
	if (sig == SIGALRM)
		sigalrm = 1;
}

static int execute(int argc, char *argv[], char *const envp[],
		   struct program_options *options)
{
	pid_t pid;
	int status;

	/* Nothing to execute, just wait an amount of time */
	if (!argc)
		return sleep(options->duration);

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0 && execvpe(argv[0], argv, envp)) {
		/* Forked child */
		perror("execvpe");
		exit(1);
	}

	if (pid) {

		struct sigaction s = {
			.sa_handler = sighandler,
			.sa_flags = SA_RESETHAND,
		};

		sigaddset(&s.sa_mask, SIGALRM);
		sigaction(SIGALRM, &s, NULL);
		alarm(options->duration);
	again:
		if (waitpid(pid, &status, 0) < 0) {
			if (errno == EINTR && sigalrm)
				kill(pid, SIGTERM);
			goto again;
		}

		if (WIFEXITED(status)) {
			/*
			 * Cancel the timer in case the program
			 * finished before the timeout
			 */
			alarm(0);
			return 0;
		}

		if (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM)
			return 0;
	}

	return -1;
}

int main(int argc, char *argv[], char *const envp[])
{
	struct cpuidle_datas *datas;
	struct program_options options;
	int args;
	double start_ts = 0, end_ts = 0;
	struct init_pstates *initp = NULL;
	struct report_ops *output_handler = NULL;

	args = getoptions(argc, argv, &options);
	if (args <= 0)
		return 1;

	/* Tracing requires manipulation of some files only accessible
	 * to root */
	if ((options.mode == TRACE) && getuid()) {
		fprintf(stderr, "must be root to run traces\n");
		return 1;
	}

	output_handler = options.report_ops;

	if (output_handler->check_output(&options, options.report_data))
		return 1;

	if (options.energy_model_filename &&
		parse_energy_model(&options) < 0) {
		fprintf(stderr, "can't parse energy model file\n");
		return 1;
	}

	/* init cpu topoinfo */
	init_cpu_topo_info();

	/* Acquisition time specified means we will get the traces */
	if ((options.mode == TRACE) || args < argc) {

		/* Read cpu topology info from sysfs */
		if (read_sysfs_cpu_topo()) {
			fprintf(stderr, "Failed to read CPU topology info from"
				" sysfs.\n");
			return 1;
		}

		/* Stop tracing (just in case) */
		if (idlestat_trace_enable(false)) {
			fprintf(stderr, "idlestat requires kernel Ftrace and "
				"debugfs mounted on /sys/kernel/debug\n");
			return 1;
		}

		/* Initialize the traces for cpu_idle and increase the
		 * buffer size to let 'idlestat' to sleep instead of
		 * acquiring data, hence preventing it to pertubate the
		 * measurements. */
		if (idlestat_init_trace(options.duration))
			return 1;

		/* Remove all the previous traces */
		if (idlestat_flush_trace())
			return 1;

		/* Get starting timestamp */
		if (options.display & FREQUENCY_DISPLAY) {
			start_ts = get_trace_ts();
			initp = build_init_pstates();
		}

		/* Start the recording */
		if (idlestat_trace_enable(true))
			return 1;

		/* We want to prevent to begin the acquisition with a cpu in
		 * idle state because we won't be able later to close the
		 * state and to determine which state it was. */
		if (idlestat_wake_all())
			return 1;

		/* Execute the command or wait a specified delay */
		if (execute(argc - args, &argv[args], envp, &options))
			return 1;

		/* Wake up all cpus again to account for last idle state */
		if (idlestat_wake_all())
			return 1;

		/* Stop tracing */
		if (idlestat_trace_enable(false))
			return 1;

		/* Get ending timestamp */
		if (options.display & FREQUENCY_DISPLAY)
			end_ts = get_trace_ts();

		/* At this point we should have some spurious wake up
		 * at the beginning of the traces and at the end (wake
		 * up all cpus and timer expiration for the timer
		 * acquisition). We assume these will be lost in the number
		 * of other traces and could be negligible. */
		if (idlestat_store(options.filename, start_ts, end_ts, initp))
			return 1;
	}

	/* Load the idle states information */
	datas = idlestat_load(&options);

	if (!datas)
		return 1;

	/* Compute cluster idle intersection between cpus belonging to
	 * the same cluster
	 */
	if (0 == establish_idledata_to_topo(datas)) {
		if (output_handler->open_report_file(options.outfilename, options.report_data))
			return -1;

		if (options.display & IDLE_DISPLAY) {
			output_handler->cstate_table_header(options.report_data);
			dump_cpu_topo_info(output_handler, options.report_data, display_cstates, 1);
			output_handler->cstate_table_footer(options.report_data);
		}

		if (options.display & FREQUENCY_DISPLAY) {
			output_handler->pstate_table_header(options.report_data);
			dump_cpu_topo_info(output_handler, options.report_data, display_pstates, 0);
			output_handler->pstate_table_footer(options.report_data);
		}

		if (options.display & WAKEUP_DISPLAY) {
			output_handler->wakeup_table_header(options.report_data);
			dump_cpu_topo_info(output_handler, options.report_data, display_wakeup, 1);
			output_handler->wakeup_table_footer(options.report_data);
		}

		if (options.energy_model_filename)
			calculate_energy_consumption(&options);

		output_handler->close_report_file(options.report_data);
	}

	release_init_pstates(initp);
	release_cpu_topo_cstates();
	release_cpu_topo_info();
	release_pstate_info(datas->pstates, datas->nrcpus);
	release_cstate_info(datas->cstates, datas->nrcpus);
	free(datas);

	return 0;
}

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
 *     Tuukka Tikkanen <tuukka.tikkanen@linaro.org>
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
#include <ctype.h>

#include "idlestat.h"
#include "utils.h"
#include "trace.h"
#include "list.h"
#include "topology.h"
#include "energy_model.h"
#include "report_ops.h"
#include "trace_ops.h"
#include "compiler.h"

#define IDLESTAT_VERSION "0.5"
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
			fprintf(stderr,
				isprint(*c) ?
				"Bad character '%c' found in filename\n" :
				"Bad character 0x%02x found in filename\n",
				*c);
			return EINVAL;
		}
	}
	return 0;
}

#define TRACE_TS_FORMAT "%*[^:]:%lf"

static int get_trace_ts(double *ts)
{
	FILE *f;

	f = fopen(TRACE_STAT_FILE, "r");
	if (!f)
		return error("fopen " TRACE_STAT_FILE);

	while (fgets(buffer, BUFSIZE, f)) {
		if (!strstr(buffer, "now ts"))
			continue;

		fclose(f);

		if (sscanf(buffer, TRACE_TS_FORMAT, ts) == 1)
			return 0;

		fprintf(stderr, "get_trace_ts: Failed to parse timestamp\n");
		return -1;
	}

	fclose(f);

	fprintf(stderr, "get_trace_ts: Failed to find timestamp in %s\n",
		TRACE_STAT_FILE);
	return -1;
}

static int display_cstates(struct report_ops *ops, void *arg, void *baseline, char *cpu, void *report_data)
{
	int i;
	bool cpu_header = false;
	struct cpuidle_cstates *cstates = arg;
	struct cpuidle_cstates *base_cstates = baseline;

	for (i = 0; i < cstates->cstate_max + 1; i++) {
		struct cpuidle_cstate *c;
		struct cpuidle_cstate *b;

		c = cstates->cstate + i;
		b = base_cstates ? base_cstates->cstate + i : NULL;

		if (c->nrdata == 0 && (!b || b->nrdata == 0))
			/* nothing to report for this state */
			continue;

		if (!cpu_header) {
			ops->cstate_cpu_header(cpu, report_data);
			cpu_header = true;
		}

		if (b && ops->cstate_baseline_state)
			ops->cstate_baseline_state(b, report_data);

		ops->cstate_single_state(c, report_data);
	}
	if (cpu_header)
		ops->cstate_end_cpu(report_data);

	return 0;
}

static int display_pstates(struct report_ops *ops, void *arg, void *baseline, char *cpu, void *report_data)
{
	int i;
	bool cpu_header = false;
	struct cpufreq_pstates *pstates = arg;
	struct cpufreq_pstates *base_pstates = baseline;

	for (i = 0; i < pstates->max; i++) {

		struct cpufreq_pstate *p;
		struct cpufreq_pstate *b;

		p = pstates->pstate + i;
		b = base_pstates ? base_pstates->pstate + i : NULL;

		if (p->count == 0 && (!b || b->count == 0))
			/* nothing to report for this state */
			continue;

		if (!cpu_header) {
			ops->pstate_cpu_header(cpu, report_data);
			cpu_header = true;
		}

		if (b && ops->pstate_baseline_freq)
			ops->pstate_baseline_freq(b, report_data);

		ops->pstate_single_freq(p, report_data);
	}

	if (cpu_header)
		ops->pstate_end_cpu(report_data);

	return 0;
}

static int display_wakeup(struct report_ops *ops, void *arg, UNUSED void *baseline, char *cpu, void *report_data)
{
	/*
	 * FIXME: This function is lacking comparison report support
	 * When adding the feature, remember to remove the UNUSED tag
	 * from baseline parameter.
	 */
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

		ops->wakeup_single_irq(irqinfo, report_data);
	}

	if (cpu_header)
		ops->wakeup_end_cpu(report_data);

	return 0;
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
void release_cstate_info(struct cpuidle_cstates *cstates, int nrcpus)
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
	int ret;

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
	ret = fscanf(snf, "%u", &tr);
	fclose(snf);

	return (ret == 1) ? tr : -1;
}

/**
 * build_cstate_info - parse cpuidle sysfs entries and build per-CPU
 * structs to maintain statistics of C-state transitions
 *
 * @nrcpus: number of CPUs
 *
 * @return: per-CPU array of structs (success) or ptrerror() (error)
 */
struct cpuidle_cstates *build_cstate_info(int nrcpus)
{
	int cpu;
	struct cpuidle_cstates *cstates;

	assert(nrcpus > 0);

	cstates = calloc(nrcpus, sizeof(*cstates));
	if (!cstates)
		return ptrerror(__func__);

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

	initp = calloc(sizeof(*initp), 1);
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
		freq = initp? initp->freqs[cpu] : 0;
		fprintf(f, "%16s-%-5d [%03d] .... %5lu.%06lu: cpu_frequency: "
			"state=%u cpu_id=%d\n", "idlestat", getpid(), cpu,
			ts_sec, ts_usec, freq, cpu);
	}
}

/**
 * alloc_pstate - allocate and initialize a cpufreq_pstate struct if needed
 * @pstates: per-CPU P-state statistics struct
 * @freq: frequency for which the new pstate should be allocated
 *
 * This function checks the array of struct cpufreq_pstate in @pstates
 * for an entry for @freq. If one if found, the index for this entry
 * is returned. If not, a new entry is inserted into the array so that
 * the frequencies are in increasing order and the index for the new
 * entry is returned.
 * @return: the index of the existing or newly allocated pstate struct
 */
static int alloc_pstate(struct cpufreq_pstates *pstates, unsigned int freq)
{
	struct cpufreq_pstate *pstate, *tmp;
	int nrfreq, i, next = 0;

	pstate = pstates->pstate;
	nrfreq = pstates->max;

	for (i = 0; i < nrfreq && freq >= pstate[i].freq; i++) {
		if (pstate[i].freq == freq)
			return i;
	}
	next = i;

	tmp = realloc(pstate, sizeof(*pstate) * (nrfreq + 1));
	if (!tmp) {
		perror(__func__);
		exit(1);
	}
	pstate = tmp;
	pstates->pstate = tmp;
	pstates->max = nrfreq + 1;

	memmove(pstate + next + 1, pstate + next, sizeof(*pstate) * (nrfreq - next));
	memset(pstate + next, 0, sizeof(*pstate));
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
 *
 * @nrcpus: number of CPUs
 *
 * @return: per-CPU array of structs (success) or NULL (error)
 */
struct cpufreq_pstates *build_pstate_info(int nrcpus)
{
	int cpu;
	struct cpufreq_pstates *pstates;

	pstates = calloc(nrcpus, sizeof(*pstates));
	if (!pstates)
		return NULL;

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

static void open_current_pstate(struct cpufreq_pstates *ps, double time)
{
	ps->time_enter = time;
}

static void open_next_pstate(struct cpufreq_pstates *ps, int s, double time)
{
	ps->current = s;
	open_current_pstate(ps, time);
}

static void close_current_pstate(struct cpufreq_pstates *ps, double time)
{
	int c = ps->current;
	struct cpufreq_pstate *p = &(ps->pstate[c]);
	double elapsed;

	elapsed = (time - ps->time_enter) * USEC_PER_SEC;
	if (elapsed <= 0)
		return;

	p->min_time = MIN(p->min_time, elapsed);
	p->max_time = MAX(p->max_time, elapsed);
	p->avg_time = AVG(p->avg_time, elapsed, p->count + 1);
	p->duration += elapsed;
	p->count++;
}

int record_group_freq(struct cpufreq_pstates *ps, double time,
			      unsigned int freq)
{
	int cur, next;

	cur = ps->current;
	if (freq > 0)
		next = alloc_pstate(ps, freq);
	else
		next = -1;

	if (cur == next)
		return 0; /* No effective change */

	if (cur == -1) {
		/* The current pstate is -1, possibly leaving idle state */
		if (next == -1)
			return 0; /* No known frequency to open, still idle */
		open_next_pstate(ps, next, time);
		return 0;
	}

	/*
	 * The group was running, update all stats and open a new state
	 * if needed.
	 */
	close_current_pstate(ps, time);

	ps->current = next;
	if (next == -1)
		return 0;

	open_current_pstate(ps, time);
	return 0;
}

int check_pstate_composite(struct cpuidle_datas *datas, int cpu, double time)
{
	struct cpu_core *aff_core;
	struct cpu_physical *aff_cluster;
	unsigned int freq;

	aff_core = cpu_to_core(cpu, datas->topo);
	aff_cluster = cpu_to_cluster(cpu, datas->topo);

	freq = core_get_highest_freq(aff_core);
	if (aff_core->is_ht) {
		verbose_fprintf(stderr, 5, "Core %c%d:   freq %9u, time %f\n",
				aff_cluster->physical_id + 'A',
				aff_core->core_id,
				freq, time);
	}
	if (record_group_freq(aff_core->pstates, time, freq) == -1)
		return -1;

	freq = cluster_get_highest_freq(aff_cluster);
	verbose_fprintf(stderr, 5, "Cluster %c: freq %9u, time %f\n",
		aff_cluster->physical_id + 'A', freq, time);
	return record_group_freq(aff_cluster->pstates, time, freq);
}


int cpu_change_pstate(struct cpuidle_datas *datas, int cpu,
			      unsigned int freq, double time)
{
	struct cpufreq_pstates *ps;
	struct cpufreq_pstate *p;
	int cur, next;

	cur = get_current_pstate(datas, cpu, &ps, &p);
	next = alloc_pstate(ps, freq);
	assert (next >= 0);

	switch (cur) {
	case 1:
		/* if CPU is idle, update current state and leave
		 * stats unchanged
		 */
		ps->current = next;
		return 0;

	case -1:
		/* current pstate is -1, i.e. this is the first update */
		open_next_pstate(ps, next, time);
		break;

	case 0:
		/* running CPU, update all stats, but skip closing current
		 * state if it's the initial update for CPU
		 */
		if (p)
			close_current_pstate(ps, time);
		open_next_pstate(ps, next, time);
		break;

	default:
		fprintf(stderr, "illegal pstate %d for cpu %d, exiting.\n",
			cur, cpu);
		exit(-1);
	}

	/* See if core or cluster highest frequency changed */
	return check_pstate_composite(datas, cpu, time);
}

/**
 * merge_pstates - make sure both main trace and baseline have same pstates
 * @datas: pointer to struct cpuidle_datas for main trace
 * @baseline: pointer to struct cpuidle_datas for baseline trace
 *
 * This function adds "empty" pstate records for frequencies that exist
 * in main trace but not in baseline trace or vice versa. This makes sure
 * that the data (with zero hits into state for thusly created entries)
 * exists in both trace results for all frequencies used by either trace.
 */
static void merge_pstates(struct cpuidle_datas *datas,
				struct cpuidle_datas *baseline)
{
	int cpu;
	int idx;
	struct cpufreq_pstates *percpu_a, *percpu_b;

	assert(datas && !is_err(datas));
	assert(baseline && !is_err(baseline));

	for (cpu = 0; cpu < datas->nrcpus; ++cpu) {
		percpu_a = &(datas->pstates[cpu]);
		percpu_b = &(baseline->pstates[cpu]);

		for (idx = 0; idx < percpu_a->max && idx < percpu_b->max; ) {
			if (percpu_a->pstate[idx].freq >
					percpu_b->pstate[idx].freq) {
				assert(alloc_pstate(percpu_b,
					percpu_a->pstate[idx].freq) == idx);
				continue;
			}
			if (percpu_a->pstate[idx].freq <
					percpu_b->pstate[idx].freq) {
				assert(alloc_pstate(percpu_a,
					percpu_b->pstate[idx].freq) == idx);
				continue;
			}
			++idx;
		}
	}
}

static void cpu_pstate_idle(struct cpuidle_datas *datas, int cpu, double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	if (ps->current != -1)
		close_current_pstate(ps, time);
	ps->idle = 1;

	/* See if core or cluster highest frequency changed */
	assert(check_pstate_composite(datas, cpu, time) != -1);
}

static void cpu_pstate_running(struct cpuidle_datas *datas, int cpu,
			       double time)
{
	struct cpufreq_pstates *ps = &(datas->pstates[cpu]);
	ps->idle = 0;
	if (ps->current != -1)
		open_current_pstate(ps, time);

	/* See if core or cluster highest frequency changed */
	assert(check_pstate_composite(datas, cpu, time) != -1);
}

static int cstate_begin(double time, int state, struct cpuidle_cstates *cstates)
{
	struct cpuidle_cstate *cstate = &cstates->cstate[state];
	struct cpuidle_data *data = cstate->data;
	int nrdata = cstate->nrdata;

	data = realloc(data, sizeof(*data) * (nrdata + 1));
	if (!data)
		return error(__func__);
	memset(data + nrdata, 0, sizeof(*data));

	data[nrdata].begin = time;

	cstate->data = data;
	cstates->cstate_max = MAX(cstates->cstate_max, state);
	cstates->current_cstate = state;
	cstates->wakeirq = NULL;
	return 0;
}

static void cstate_end(double time, struct cpuidle_cstates *cstates)
{
	int last_cstate = cstates->current_cstate;
	struct cpuidle_cstate *cstate = &cstates->cstate[last_cstate];
	struct cpuidle_data *data = &cstate->data[cstate->nrdata];

	data->end = time;
	data->duration = data->end - data->begin;

	/*
	 * Duration can be < 0 when precision digit in the file exceed
	 * 7 (eg. xxx.1000000). Ignoring the result because I don't
	 * find a way to fix with the sscanf used in the caller.
	 *
	 * For synthetic test material, the duration may be 0.
	 *
	 * In both cases, do not record the entry, but do end the state
	 * regardless.
	 */
	if (data->duration <= 0)
		goto skip_entry;

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

skip_entry:
	/* CPU is no longer idle */
	cstates->current_cstate = -1;
}

int record_cstate_event(struct cpuidle_cstates *cstates,
		       double time, int state)
{
	int ret = 0;

	/* Ignore when we enter the current state (cores and clusters) */
	if (state == cstates->current_cstate)
		return 0;

	if (cstates->current_cstate != -1)
		cstate_end(time, cstates);
	if (state != -1)
		ret = cstate_begin(time, state, cstates);

	return ret;
}

int store_data(double time, int state, int cpu,
		struct cpuidle_datas *datas)
{
	struct cpuidle_cstates *cstates = &datas->cstates[cpu];
	struct cpufreq_pstate *pstate = datas->pstates[cpu].pstate;
	struct cpu_core *aff_core;
	struct cpu_physical *aff_cluster;

	/* ignore when we got a "closing" state first */
	if (state == -1 && cstates->cstate_max == -1)
		return 0;

	if (record_cstate_event(cstates, time, state) == -1)
		return -1;

	/* Update P-state stats if supported */
	if (pstate) {
		if (state == -1)
			cpu_pstate_running(datas, cpu, time);
		else
			cpu_pstate_idle(datas, cpu, time);
	}

	/* Update core and cluster */
	aff_core = cpu_to_core(cpu, datas->topo);
	state = core_get_least_cstate(aff_core);
	if (record_cstate_event(aff_core->cstates, time, state) == -1)
		return -1;

	aff_cluster = cpu_to_cluster(cpu, datas->topo);
	state = cluster_get_least_cstate(aff_cluster);
	if (record_cstate_event(aff_cluster->cstates, time,state) == -1)
		return -1;

	return 0;
}

static void release_datas(struct cpuidle_datas *datas)
{
	if (datas == NULL)
		return;

	release_datas(datas->baseline);
	release_cpu_topo_cstates(datas->topo);
	release_cpu_topo_info(datas->topo);
	release_pstate_info(datas->pstates, datas->nrcpus);
	release_cstate_info(datas->cstates, datas->nrcpus);
	free(datas);
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
		memset(irqinfo + wakeinfo->nrdata, 0, sizeof(*irqinfo));

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

static void write_cstate_info(FILE *f, char *name, int target)
{
	fprintf(f, "\t%s\n", name);
	fprintf(f, "\t%d\n", target);
}

void output_cstate_info(FILE *f, int nrcpus) {
	struct cpuidle_cstates *cstates;
	int i, j;

	cstates = build_cstate_info(nrcpus);
	assert(!is_err(cstates));

	for (i=0; i < nrcpus; i++) {
		fprintf(f, "cpuid %d:\n",  i);
		for (j=0; j < MAXCSTATE ; j++) {
			write_cstate_info(f, cstates[i].cstate[j].name,
				cstates[i].cstate[j].target_residency);
		}
	}
}

#define TRACE_IRQ_FORMAT "%*[^[][%d] %*[^=]=%d%*[^=]=%16s"
#define TRACE_IPIIRQ_FORMAT "%*[^[][%d] %*[^(](%32s"
#define TRACECMD_REPORT_FORMAT "%*[^]]] %lf:%*[^=]=%u%*[^=]=%d"
#define TRACE_FORMAT "%*[^]]] %*s %lf:%*[^=]=%u%*[^=]=%d"

int get_wakeup_irq(struct cpuidle_datas *datas, char *buffer)
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

struct cpuidle_datas *idlestat_load(const char *filename)
{
	const struct trace_ops **ops_it;
	int ret;

	/*
	 * The linker places pointers to all entries declared with
	 * EXPORT_TRACE_OPS into a special segment. This creates
	 * an array of pointers preceded by trace_ops_head. Let
	 * the static analysis tool know that we know what we are
	 * doing.
	 */
	/* coverity[array_vs_singleton] */
	for (ops_it = (&trace_ops_head)+1 ; *ops_it ; ++ops_it) {
		assert((*ops_it)->name);
		assert((*ops_it)->check_magic);
		assert((*ops_it)->load);
		ret = (*ops_it)->check_magic(filename);

		if (ret == -1)
			return ptrerror(NULL);

		/* File format supported by these ops? */
		if (ret > 0) {
			return (*ops_it)->load(filename);
		}
	}

	fprintf(stderr, "Trace file format not recognized\n");
	return ptrerror(NULL);
}

static void help(const char *cmd)
{
	fprintf(stderr,
		"\nUsage:\nTrace mode:\n\t%s --trace -f|--trace-file <filename>"
		" -b|--baseline-trace <filename>"
		" -o|--output-file <filename> -t|--duration <seconds>"
		" -r|--report-format <format>"
		" -C|--csv-report -B|--boxless-report"
		" -c|--idle -p|--frequency -w|--wakeup", basename(cmd));
	fprintf(stderr,
		"\nReporting mode:\n\t%s --import -f|--trace-file <filename>"
		" -b|--baseline-trace <filename>"
		" -r|--report-format <format>"
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
		"\n4. Post-process a trace captured earlier:\n\t./%s"
		" --import -f /tmp/mytrace\n", basename(cmd));
	fprintf(stderr,
		"\n5. Run a trace, post-process the results and print all"
		" statistics into a file:\n\tsudo ./%s --trace -f /tmp/mytrace -t 10 -p -c -w"
		" -o /tmp/myreport\n", basename(cmd));
	fprintf(stderr,
		"\n6. Run a comparison trace, say, before and after making changes to system behaviour\n"
		"\tsudo ./%s --trace -f /tmp/baseline -t 10\n"
		"\tsudo ./%s --trace -f /tmp/changedstate -t 10\n"
		"\t./%s --import -f /tmp/changedstate -b /tmp/baseline -r comparison\n",
		basename(cmd), basename(cmd), basename(cmd));
	fprintf(stderr, "\nReport formats supported:");
	list_report_formats_to_stderr();
}

static void version(const char *cmd)
{
	printf("%s version %s\n", basename(cmd), IDLESTAT_VERSION);
}

int getoptions(int argc, char *argv[], struct program_options *options)
{
	/* Keep options sorted alphabetically and make sure the short options
	 * are also added to the getopt_long call below
	 */
	struct option long_options[] = {
		{ "trace",       no_argument,       &options->mode, TRACE },
		{ "import",      no_argument,       &options->mode, IMPORT },
		{ "baseline-trace", required_argument, NULL, 'b' },
		{ "idle",        no_argument,       NULL, 'c' },
		{ "energy-model-file",  required_argument, NULL, 'e' },
		{ "trace-file",  required_argument, NULL, 'f' },
		{ "help",        no_argument,       NULL, 'h' },
		{ "output-file", required_argument, NULL, 'o' },
		{ "frequency",   no_argument,       NULL, 'p' },
		{ "report-format", required_argument, NULL, 'r' },
		{ "duration",    required_argument, NULL, 't' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "wakeup",      no_argument,       NULL, 'w' },
		{ "boxless-report", no_argument,    NULL, 'B' },
		{ "csv-report",  no_argument,       NULL, 'C' },
		{ "poll-interval", required_argument, NULL, 'I' },
		{ "buffer-size", required_argument, NULL, 'S' },
		{ "version",     no_argument,       NULL, 'V' },
		{ 0, 0, 0, 0 }
	};
	int c;

	memset(options, 0, sizeof(*options));
	options->filename = NULL;
	options->outfilename = NULL;
	options->mode = -1;

	while (1) {

		int optindex = 0;

		c = getopt_long(argc, argv, ":b:ce:f:ho:pr:t:vwBCI:S:V",
				long_options, &optindex);
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			options->filename = optarg;
			break;
		case 'b':
			options->baseline_filename = optarg;
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
			set_verbose_level(++options->verbose);
			break;
		case 'e':
			options->energy_model_filename = optarg;
			break;
		case 'r':
			if (options->report_type_name == NULL) {
				options->report_type_name = optarg;
				break;
			}
			fprintf(stderr, "-r: report type already set to %s\n",
				options->report_type_name);
			return -1;
		case 'C':
			if (options->report_type_name == NULL) {
				options->report_type_name = "csv";
				break;
			}
			fprintf(stderr, "-C: report type already set to %s\n",
				options->report_type_name);
			return -1;
		case 'B':
			if (options->report_type_name == NULL) {
				options->report_type_name = "boxless";
				break;
			}
			fprintf(stderr, "-B: report type already set to %s\n",
				options->report_type_name);
			return -1;
		case 'I':
			options->tbs.poll_interval = atoi(optarg);
			break;
		case 'S':
			options->tbs.percpu_buffer_size = atoi(optarg);
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

	if (options->report_type_name == NULL)
		options->report_type_name = "default";

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

	if (options->baseline_filename != NULL &&
			bad_filename(options->baseline_filename))
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
				struct init_pstates *initp,
				struct cpu_topology *cpu_topo)
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
	output_cpu_topo_info(cpu_topo, f);

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
	struct cpuidle_datas *baseline;
	struct program_options options;
	int args;
	double start_ts = 0, end_ts = 0;
	struct init_pstates *initp = NULL;
	struct report_ops *output_handler = NULL;
	struct cpu_topology *cpu_topo = NULL;
	struct trace_options *saved_trace_options = NULL;
	void *report_data = NULL;

	args = getoptions(argc, argv, &options);
	if (args <= 0)
		return 1;

	/* Tracing requires manipulation of some files only accessible
	 * to root */
	if ((options.mode == TRACE) && getuid()) {
		fprintf(stderr, "must be root to run traces\n");
		return 1;
	}

	output_handler = get_report_ops(options.report_type_name);
	if (is_err(output_handler))
		return 1;

	if (output_handler->check_options &&
			output_handler->check_options(&options) < 0)
		return 1;

	if (output_handler->allocate_report_data) {
		report_data = output_handler->allocate_report_data(&options);
		if (is_err(report_data))
			return 1;
	}

	if (output_handler->check_output(&options, report_data))
		return 1;

	if (options.energy_model_filename &&
		parse_energy_model(&options) < 0) {
		fprintf(stderr, "can't parse energy model file\n");
		return 1;
	}

	/* Acquisition time specified means we will get the traces */
	if ((options.mode == TRACE) || args < argc) {
		/* Read cpu topology info from sysfs */
		cpu_topo = read_sysfs_cpu_topo();
		if (is_err(cpu_topo)) {
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

		saved_trace_options = idlestat_store_trace_options();
		if (is_err(saved_trace_options))
			return 1;

		/*
		 * Calculate/verify buffer size and polling trace data
		 * interval. The interval or may may not be used to
		 * transfer data from kernel trace buffer to some
		 * storage media. It is needed for long eventful traces,
		 * but is not preferred. If the user does not specify
		 * the values, we will calculate reasonable defaults.
		 */
		if (calculate_buffer_parameters(options.duration, &options.tbs))
			return 1;

		/* Initialize the traces for cpu_idle and increase the
		 * buffer size to let 'idlestat' to possibly sleep instead
		 * of acquiring data, hence preventing it to pertubate the
		 * measurements. */
		if (idlestat_init_trace(options.tbs.percpu_buffer_size))
			goto err_restore_trace_options;

		/* Remove all the previous traces */
		if (idlestat_flush_trace())
			goto err_restore_trace_options;

		/* Get starting timestamp */
		if (get_trace_ts(&start_ts) == -1)
			goto err_restore_trace_options;

		initp = build_init_pstates();

		/* Start the recording */
		if (idlestat_trace_enable(true))
			goto err_restore_trace_options;

		/* We want to prevent to begin the acquisition with a cpu in
		 * idle state because we won't be able later to close the
		 * state and to determine which state it was. */
		if (idlestat_wake_all())
			goto err_restore_trace_options;

		/* Execute the command or wait a specified delay */
		if (execute(argc - args, &argv[args], envp, &options))
			goto err_restore_trace_options;

		/* Wake up all cpus again to account for last idle state */
		if (idlestat_wake_all())
			goto err_restore_trace_options;

		/* Stop tracing */
		if (idlestat_trace_enable(false))
			goto err_restore_trace_options;

		/* Get ending timestamp */
		if (get_trace_ts(&end_ts) == -1)
			goto err_restore_trace_options;

		/* At this point we should have some spurious wake up
		 * at the beginning of the traces and at the end (wake
		 * up all cpus and timer expiration for the timer
		 * acquisition). We assume these will be lost in the number
		 * of other traces and could be negligible. */
		if (idlestat_store(options.filename, start_ts, end_ts,
			initp, cpu_topo))
			goto err_restore_trace_options;

		/* Restore original kernel ftrace options */
		if (idlestat_restore_trace_options(saved_trace_options))
			return 1;

		/* Discard topology, will be reloaded during trace load */
		release_cpu_topo_cstates(cpu_topo);
		release_cpu_topo_info(cpu_topo);
		cpu_topo = NULL;
	}

	/* Load the idle states information */
	datas = idlestat_load(options.filename);

	if (is_err(datas))
		return 1;

	cpu_topo = datas->topo;

	if (options.baseline_filename) {
		baseline = idlestat_load(options.baseline_filename);
		merge_pstates(datas, baseline);
	} else {
		baseline = NULL;
	}

	if (is_err(baseline))
		return 1;

	datas->baseline = baseline;
	assign_baseline_in_topo(datas);

	if (output_handler->open_report_file(options.outfilename, report_data))
		return 1;

	if (options.display & IDLE_DISPLAY) {
		output_handler->cstate_table_header(report_data);
		dump_cpu_topo_info(output_handler, report_data,
				display_cstates, cpu_topo, 1);
		output_handler->cstate_table_footer(report_data);
	}

	if (options.display & FREQUENCY_DISPLAY) {
		output_handler->pstate_table_header(report_data);
		dump_cpu_topo_info(output_handler, report_data,
				display_pstates, cpu_topo, 0);
		output_handler->pstate_table_footer(report_data);
	}

	if (options.display & WAKEUP_DISPLAY) {
		output_handler->wakeup_table_header(report_data);
		dump_cpu_topo_info(output_handler, report_data,
				display_wakeup, cpu_topo, 1);
		output_handler->wakeup_table_footer(report_data);
	}

	if (options.energy_model_filename)
		calculate_energy_consumption(cpu_topo);

	output_handler->close_report_file(report_data);

	release_init_pstates(initp);
	release_datas(datas);

	if (output_handler->release_report_data)
		output_handler->release_report_data(report_data);

	return 0;

 err_restore_trace_options:
	/* Restore original kernel ftrace options */
	idlestat_restore_trace_options(saved_trace_options);
	return 1;
}

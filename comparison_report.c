/*
 *  comparison_report.c
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
 *     Tuukka Tikkanen <tuukka.tikkanen@linaro.org>
 *
 */
#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <malloc.h>
#include <math.h>
#include <assert.h>
#include "report_ops.h"
#include "idlestat.h"
#include "utils.h"
#include "compiler.h"

struct compare_report_data {
	struct cpuidle_cstate *curr_cstate_baseline;
	struct cpufreq_pstate *curr_pstate_baseline;
};

static void display_factored_time_delta(double time, int align)
{
	char buffer[128];

	if (fabs(time) < 1000.0) {
		snprintf(buffer, sizeof(buffer), "%+.0lfus", time);
		printf("%*s", align, buffer);
	}
	else if (fabs(time) < 1000000.0) {
		snprintf(buffer, sizeof(buffer), "%+.1lfms", time / 1000.0);
		printf("%*s", align, buffer);
	}
	else if (fabs(time) < 100000000000.0) {
		snprintf(buffer, sizeof(buffer), "%+.1lfs", time / 1000000.0);
		printf("%*s", align, buffer);
	}
	else
		printf("%.*s", align, "                            ");
}

static void display_int_delta(int value, int align)
{
	printf(value ? " %+*d |" : " %*d |", align, value);
}


static int compare_check_options(struct program_options *options)
{
	if (options->baseline_filename == NULL) {
  		fprintf(stderr,
			"Error: Comparison report requires baseline trace\n");
		return -1;
	}

	return 0;
}

static void * compare_alloc_data(UNUSED struct program_options *options)
{
	struct compare_report_data *ret = calloc(sizeof(*ret), 1);

	if (ret == NULL)
		return ptrerror(__func__);

	return ret;
}

static void compare_release_data(void *data)
{
	free(data);
}


static void compare_cstate_single_state(struct cpuidle_cstate *c,
					void *report_data)
{
	struct cpuidle_cstate empty;
	struct cpuidle_cstate diff;
	struct cpuidle_cstate *b;
	struct compare_report_data *rdata;

	assert(report_data != NULL);
	rdata = report_data;

	/*
	 * On entry, either current state or baseline state might be NULL:
	 * c = NULL implies this state exists only in baseline
	 * curr_baseline = NULL implies this state exists only in trace
	 * It should never occur that both c and current baseline are NULL.
	 *
	 * If c is NULL, set c to point to empty state and alias the name
	 * in baseline.
	 *
	 * If no current baseline exists, use empty state as baseline.
	 */
	b = rdata->curr_cstate_baseline;
	rdata->curr_cstate_baseline = NULL;

	assert(c != NULL || b != NULL);

	if (c == NULL) {
		memset(&empty, 0, sizeof(empty));
		empty.name = b->name;
		c = &empty;
	}

	if (b == NULL) {
		memset(&empty, 0, sizeof(empty));
		b = &empty;
	}

	diff.min_time = c->min_time - b->min_time;
	diff.max_time = c->max_time - b->max_time;
	diff.avg_time = c->avg_time - b->avg_time;
	diff.duration = c->duration - b->duration;
	diff.nrdata = c->nrdata - b->nrdata;
	diff.early_wakings = c->early_wakings - b->early_wakings;
	diff.late_wakings = c->late_wakings - b->late_wakings;

	printf("| %8s | ", c->name);
	display_factored_time(c->min_time == DBL_MAX ? 0. :
				      c->min_time, 8);
	printf(" | ");
	display_factored_time(c->max_time, 8);
	printf(" | ");
	display_factored_time(c->avg_time, 8);
	printf(" | ");
	display_factored_time(c->duration, 8);
	printf(" | ");
	printf("%5d | %5d | %5d |\n", c->nrdata, c->early_wakings, c->late_wakings);
	/* Delta */
	printf("|          | ");
	display_factored_time_delta(diff.min_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.max_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.avg_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.duration, 8);
	printf(" |");
	display_int_delta(diff.nrdata, 5);
	display_int_delta(diff.early_wakings, 5);
	display_int_delta(diff.late_wakings, 5);
	printf("\n");
}

static void compare_set_baseline_cstate(struct cpuidle_cstate *b,
					void *report_data)
{
	struct compare_report_data *rdata;

	assert(report_data != NULL);
	rdata = report_data;

	/* Process previous state if trace did not have data */
	if (rdata->curr_cstate_baseline)
		compare_cstate_single_state(NULL, report_data);

	if (b == NULL)
		return;

	rdata->curr_cstate_baseline = b;
}


static void compare_cstate_end_cpu(void *report_data)
{
	compare_set_baseline_cstate(NULL, report_data);
}


static void compare_pstate_single_freq(struct cpufreq_pstate *p,
				       void *report_data)
{
	struct cpufreq_pstate empty;
	struct cpufreq_pstate diff;
	struct cpufreq_pstate *b;
	struct compare_report_data *rdata;

	assert(report_data != NULL);
	rdata = report_data;

	/*
	 * On entry, either current state or baseline state might be NULL:
	 * p = NULL implies this state exists only in baseline
	 * curr_baseline = NULL implies this state exists only in trace
	 * It should never occur that both p and current baseline are NULL.
	 *
	 * If p is NULL, set p to point to empty state and copy the frequency
	 * from baseline.
	 *
	 * If no current baseline exists, use empty state as baseline.
	 */
	b = rdata->curr_pstate_baseline;
	rdata->curr_pstate_baseline = NULL;

	assert(p != NULL || b != NULL);

	if (p == NULL) {
		memset(&empty, 0, sizeof(empty));
		empty.freq = b->freq;
		p = &empty;
	}

	if (b == NULL) {
		memset(&empty, 0, sizeof(empty));
		b = &empty;
	}

	diff.min_time = p->min_time - b->min_time;
	diff.max_time = p->max_time - b->max_time;
	diff.avg_time = p->avg_time - b->avg_time;
	diff.duration = p->duration - b->duration;
	diff.count = p->count - b->count;

	printf("| ");
	display_factored_freq(p->freq, 8);
	printf(" | ");
	display_factored_time(p->min_time == DBL_MAX ? 0. : p->min_time, 8);
	printf(" | ");
	display_factored_time(p->max_time, 8);
	printf(" | ");
	display_factored_time(p->avg_time, 8);
	printf(" | ");
	display_factored_time(p->duration, 8);
	printf(" | %5d |\n", p->count);

	printf("|          | ");
	display_factored_time_delta(diff.min_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.max_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.avg_time, 8);
	printf(" | ");
	display_factored_time_delta(diff.duration, 8);
	printf(" |");
	display_int_delta(diff.count, 5);
	printf("\n");
}

static void compare_set_baseline_pstate(struct cpufreq_pstate *b,
					void *report_data)
{
	struct compare_report_data *rdata;

	assert(report_data != NULL);
	rdata = report_data;

	/* Process previous state if trace did not have data */
	if (rdata->curr_pstate_baseline)
		compare_pstate_single_freq(NULL, report_data);

	if (b == NULL)
		return;

	rdata->curr_pstate_baseline = b;
}


static void compare_pstate_end_cpu(void *report_data)
{
	compare_set_baseline_pstate(NULL, report_data);
}


static int copy_ops_from_default(struct report_ops *);

static struct report_ops comparison_report_ops = {
	.name = "comparison",
	.prepare = copy_ops_from_default,
	.check_options = compare_check_options,
	.allocate_report_data = compare_alloc_data,
	.release_report_data = compare_release_data,

	.cstate_baseline_state = compare_set_baseline_cstate,
	.cstate_single_state = compare_cstate_single_state,
	.cstate_end_cpu = compare_cstate_end_cpu,

	.pstate_baseline_freq = compare_set_baseline_pstate,
	.pstate_single_freq = compare_pstate_single_freq,
	.pstate_end_cpu = compare_pstate_end_cpu,
};

static int copy_ops_from_default(struct report_ops *self)
{
	struct report_ops *def = get_report_ops("default");

	assert(self == &comparison_report_ops);

	if (is_err(def)) {
		fprintf(stderr,
			"Comparison report: cannot copy ops from default\n");
		return -1;
	}

	comparison_report_ops.check_output = def->check_output;

	comparison_report_ops.open_report_file = def->open_report_file;
	comparison_report_ops.close_report_file = def->close_report_file;

	comparison_report_ops.cstate_table_header = def->cstate_table_header;
	comparison_report_ops.cstate_table_footer = def->cstate_table_footer;
	comparison_report_ops.cstate_cpu_header = def->cstate_cpu_header;

	comparison_report_ops.pstate_table_header = def->pstate_table_header;
	comparison_report_ops.pstate_table_footer = def->pstate_table_footer;
	comparison_report_ops.pstate_cpu_header = def->pstate_cpu_header;

	comparison_report_ops.wakeup_table_header = def->wakeup_table_header;
	comparison_report_ops.wakeup_table_footer = def->wakeup_table_footer;
	comparison_report_ops.wakeup_cpu_header = def->wakeup_cpu_header;
	comparison_report_ops.wakeup_single_irq = def->wakeup_single_irq;
	comparison_report_ops.wakeup_end_cpu = def->wakeup_end_cpu;

	return 0;
}

EXPORT_REPORT_OPS(comparison);

/*
 *  csv_report.c
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
#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "report_ops.h"
#include "idlestat.h"
#include "utils.h"
#include "compiler.h"

static int csv_check_output(UNUSED struct program_options *options,
			    UNUSED void *report_data)
{
	return 0;
}

static int csv_open_report_file(char *path, UNUSED void *report_data)
{
	return redirect_stdout_to_file(path);
}

static int csv_close_report_file(UNUSED void *report_data)
{
	return (fclose(stdout) == EOF) ? -1 : 0;
}

static void csv_cstate_table_header(UNUSED void *report_data)
{
	printf("C-State Table\n");
	printf("cluster,core,cpu,C-state,min (us),max (us),avg (us),total (us),hits,over,under\n");
}

static void csv_cstate_table_footer(UNUSED void *report_data)
{
	printf("\n\n");
}

static void csv_cstate_cpu_header(const char *cpu, UNUSED void *report_data)
{
        if (strstr(cpu, "cluster"))
	        printf("%s\n", cpu);
        else if (strstr(cpu, "core"))
	        printf(",%s\n", cpu);
        else printf(",,%s\n", cpu);
}

static void csv_cstate_single_state(struct cpuidle_cstate *c,
				    UNUSED void *report_data)
{
	printf(",,,%s,", c->name);
	printf("%f,", c->min_time == DBL_MAX ? 0. : c->min_time);
	printf("%f,%f,%f,", c->max_time, c->avg_time, c->duration);
	printf("%d,%d,%d", c->nrdata, c->early_wakings, c->late_wakings);

	printf("\n");
}

static void csv_cstate_end_cpu(UNUSED void *report_data)
{
}

static void csv_pstate_table_header(UNUSED void *report_data)
{
	printf("P-State Table\n");
	printf(",,,P-state (kHz),min (us),max (us),avg (us),total (us),hits\n");
}

static void csv_pstate_table_footer(UNUSED void *report_data)
{
	printf("\n\n");
}

static void csv_pstate_single_freq(struct cpufreq_pstate *p,
				   UNUSED void *report_data)
{
	printf(",,,%u,", p->freq);
	printf("%f,", p->min_time == DBL_MAX ? 0. : p->min_time);
	printf("%f,%f,%f,", p->max_time, p->avg_time, p->duration);
	printf("%d", p->count);

	printf("\n");
}

static void csv_wakeup_table_header(UNUSED void *report_data)
{
	printf("\n");

	printf("Wakeup Table\n");
	printf("cluster,core,cpu,IRQ,Name,Count,early,late\n");
}

static void csv_wakeup_table_footer(UNUSED void *report_data)
{
	printf("\n\n");
}

static void csv_wakeup_single_irq(struct wakeup_irq *irqinfo,
				  UNUSED void *report_data)
{
	if (irqinfo->id != -1) {
		printf(",,,%d,%s,%d,%d,%d\n",
		       irqinfo->id, irqinfo->name, irqinfo->count,
		       irqinfo->early_triggers, irqinfo->late_triggers);
	} else {
		printf(",,,IPI,%s,%d,%d,%d\n",
		       irqinfo->name, irqinfo->count,
		       irqinfo->early_triggers, irqinfo->late_triggers);
	}
}

static struct report_ops csv_report_ops = {
	.name = "csv",
	.check_output = csv_check_output,

	.open_report_file = csv_open_report_file,
	.close_report_file = csv_close_report_file,

	.cstate_table_header = csv_cstate_table_header,
	.cstate_table_footer = csv_cstate_table_footer,
	.cstate_cpu_header = csv_cstate_cpu_header,
	.cstate_single_state = csv_cstate_single_state,
	.cstate_end_cpu = csv_cstate_end_cpu,

	.pstate_table_header = csv_pstate_table_header,
	.pstate_table_footer = csv_pstate_table_footer,
	.pstate_cpu_header = csv_cstate_cpu_header,
	.pstate_single_freq = csv_pstate_single_freq,
	.pstate_end_cpu = csv_cstate_end_cpu,

	.wakeup_table_header = csv_wakeup_table_header,
	.wakeup_table_footer = csv_wakeup_table_footer,
	.wakeup_cpu_header = csv_cstate_cpu_header,
	.wakeup_single_irq = csv_wakeup_single_irq,
	.wakeup_end_cpu = csv_cstate_end_cpu,
};

EXPORT_REPORT_OPS(csv);

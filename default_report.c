#include <float.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "idlestat.h"
#include "utils.h"


static void charrep(char c, int count)
{
	int i;
	for (i = 0; i < count; i++)
		printf("%c", c);
}

static int default_check_output(struct program_options *options, void *report_data)
{
	if (check_window_size() && !options->outfilename) {
		fprintf(stderr, "The terminal must be at least "
			"80 columns wide\n");
		return 1;
	}
	return 0;
}

static int default_open_report_file(char *path, void *report_data)
{
	return redirect_stdout_to_file(path);
}

static int default_close_report_file(void *report_data)
{
	return (fclose(stdout) == EOF) ? -1 : 0;
}

static void default_cstate_table_header(void *report_data)
{
	charrep('-', 80);
	printf("\n");

	printf("| C-state  |   min    |   max    |   avg    |   total  | hits  |  over | under |\n");
}

static void default_cstate_table_footer(void *report_data)
{
	charrep('-', 80);
	printf("\n\n");
}

static void default_cpu_header(const char *cpu, void *report_data, int len)
{
        charrep('-', len);
        printf("\n");

        if (strstr(cpu, "cluster"))
                printf("| %-*s |\n", len - 4, cpu);
        else if (strstr(cpu, "core"))
                printf("|      %-*s |\n", len - 9, cpu);
        else printf("|             %-*s |\n", len - 16, cpu);

	charrep('-', len);
	printf("\n");
}

static void default_cstate_cpu_header(const char *cpu, void *report_data)
{
	default_cpu_header(cpu, report_data, 80);
}

static void default_cstate_single_state(struct cpuidle_cstate *c, void *report_data)
{
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
	printf("%5d | %5d | %5d |", c->nrdata, c->early_wakings, c->late_wakings);

	printf("\n");
}

static void default_cstate_end_cpu(void *report_data)
{
}

static void default_pstate_table_header(void *report_data)
{
	charrep('-', 64);
	printf("\n");

	printf("| P-state  |   min    |   max    |   avg    |   total  | hits  |\n");
}

static void default_pstate_table_footer(void *report_data)
{
	charrep('-', 64);
	printf("\n\n");
}

static void default_pstate_cpu_header(const char *cpu, void *report_data)
{
	default_cpu_header(cpu, report_data, 64);
}

static void default_pstate_single_state(struct cpufreq_pstate *p, void *report_data)
{

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
	printf(" | ");
	printf("%5d", p->count);
	printf(" | ");
	printf("\n");
}

static void default_wakeup_table_header(void *report_data)
{
	charrep('-', 55);
	printf("\n");

	printf("| IRQ |       Name      |  Count  |  early  |  late   |\n");
}

static void default_wakeup_table_footer(void *report_data)
{
	charrep('-', 55);
	printf("\n\n");
}

static void default_wakeup_cpu_header(const char *cpu, void *report_data)
{
	default_cpu_header(cpu, report_data, 55);
}

static void default_wakeup_single_state(struct wakeup_irq *irqinfo, void *report_data)
{
	if (irqinfo->id != -1) {
		printf("| %-3d | %-15.15s | %7d | %7d | %7d |\n",
		       irqinfo->id, irqinfo->name, irqinfo->count,
		       irqinfo->early_triggers, irqinfo->late_triggers);
	} else {
		printf("| IPI | %-15.15s | %7d | %7d | %7d |\n",
		       irqinfo->name, irqinfo->count,
		       irqinfo->early_triggers, irqinfo->late_triggers);
	}
}

struct report_ops default_report_ops = {
	.check_output = default_check_output,

	.open_report_file = default_open_report_file,
	.close_report_file = default_close_report_file,

	.cstate_table_header = default_cstate_table_header,
	.cstate_table_footer = default_cstate_table_footer,
	.cstate_cpu_header = default_cstate_cpu_header,
	.cstate_single_state = default_cstate_single_state,
	.cstate_end_cpu = default_cstate_end_cpu,

	.pstate_table_header = default_pstate_table_header,
	.pstate_table_footer = default_pstate_table_footer,
	.pstate_cpu_header = default_pstate_cpu_header,
	.pstate_single_state = default_pstate_single_state,
	.pstate_end_cpu = default_cstate_end_cpu,

	.wakeup_table_header = default_wakeup_table_header,
	.wakeup_table_footer = default_wakeup_table_footer,
	.wakeup_cpu_header = default_wakeup_cpu_header,
	.wakeup_single_state = default_wakeup_single_state,
	.wakeup_end_cpu = default_cstate_end_cpu,
};

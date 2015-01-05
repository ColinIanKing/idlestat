/*
 *  report_ops.h
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
#ifndef __REPORT_OPS_H
#define __REPORT_OPS_H

struct program_options;
struct cpuidle_cstate;
struct cpufreq_pstate;
struct wakeup_irq;

struct report_ops {
	const char *name;

	/* No other method may be called or address taken before this */
	int (*prepare)(struct report_ops *);

	int (*check_options)(struct program_options *);
	int (*check_output)(struct program_options *, void *);

	void * (*allocate_report_data)(struct program_options *);
	void (*release_report_data)(void *);

	int (*open_report_file)(char *path, void *);
	int (*close_report_file)(void *);

	void (*cstate_table_header)(void *);
	void (*cstate_table_footer)(void *);
	void (*cstate_cpu_header)(const char *cpu, void *);
	void (*cstate_baseline_state)(struct cpuidle_cstate*, void *);
	void (*cstate_single_state)(struct cpuidle_cstate*, void *);
	void (*cstate_end_cpu)(void *);

	void (*pstate_table_header)(void *);
	void (*pstate_table_footer)(void *);
	void (*pstate_cpu_header)(const char *cpu, void *);
	void (*pstate_baseline_freq)(struct cpufreq_pstate*, void *);
	void (*pstate_single_freq)(struct cpufreq_pstate*, void *);
	void (*pstate_end_cpu)(void*);

	void (*wakeup_table_header)(void *);
	void (*wakeup_table_footer)(void *);
	void (*wakeup_cpu_header)(const char *cpu, void *);
	void (*wakeup_single_irq)(struct wakeup_irq *irqinfo, void *);
	void (*wakeup_end_cpu)(void *);
};

extern void list_report_formats_to_stderr(void);
extern struct report_ops *get_report_ops(const char *name);

#define EXPORT_REPORT_OPS(reporttype_name)			\
	static const struct report_ops				\
	__attribute__ ((__used__))				\
	__attribute__ ((__section__ ("__report_ops")))		\
	* reporttype_name ## _report_ptr = &reporttype_name##_report_ops

extern const struct report_ops *report_ops_head;

#endif

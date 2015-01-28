/*
 *  topology.h
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
#ifndef __TOPOLOGY_H
#define __TOPOLOGY_H

#include "list.h"
#include <stdbool.h>

struct cpuidle_datas;
struct report_ops;

struct cpu_cpu {
	struct list_head list_cpu;
	int cpu_id;
	struct list_head list_phy_enum;
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;
	struct cpuidle_cstates *base_cstates;
	struct cpufreq_pstates *base_pstates;
};

struct cpu_core {
	struct list_head list_core;
	int core_id;
	struct list_head cpu_head;
	int cpu_num;
	bool is_ht;
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;
	struct cpuidle_cstates *base_cstates;
	struct cpufreq_pstates *base_pstates;
};

struct cpu_physical {
	struct list_head list_physical;
	int physical_id;
	struct list_head core_head;
	int core_num;
	struct list_head cpu_enum_head;
	struct cpuidle_cstates *cstates;
	struct cpufreq_pstates *pstates;
	struct cpuidle_cstates *base_cstates;
	struct cpufreq_pstates *base_pstates;
};

struct cpu_topology {
	struct list_head physical_head;
	int *online_cpus;
	int online_array_size;
};

extern struct cpu_topology *alloc_cpu_topo_info(void);
extern struct cpu_topology *read_cpu_topo_info(FILE *f, char *buf);
extern struct cpu_topology *read_sysfs_cpu_topo(void);
extern int release_cpu_topo_info(struct cpu_topology *topo);
extern int output_cpu_topo_info(struct cpu_topology *topo, FILE *f);
extern void assign_baseline_in_topo(struct cpuidle_datas *datas);
extern int release_cpu_topo_cstates(struct cpu_topology *topo);
extern int dump_cpu_topo_info(struct report_ops *ops, void *report_data, int (*dump)(struct report_ops *, void *, void *, char *, void *), struct cpu_topology *topo, int cstate);

extern int cpu_is_online(struct cpu_topology *topo, int cpuid);

extern struct cpu_physical *cpu_to_cluster(int cpuid, struct cpu_topology *topo);
extern struct cpu_core *cpu_to_core(int cpuid, struct cpu_topology *topo);

#define core_for_each_cpu(cpu, core)				\
	list_for_each_entry(cpu, &core->cpu_head, list_cpu)

#define cluster_for_each_core(core, clust)			\
	list_for_each_entry(core, &clust->core_head, list_core)

#define cluster_for_each_cpu(cpu, clust)			\
	list_for_each_entry(cpu, &clust->cpu_enum_head, list_phy_enum)

#define topo_for_each_cluster(clust, topo)			\
	list_for_each_entry(clust, &topo->physical_head, list_physical)

extern int cluster_get_least_cstate(struct cpu_physical *clust);
extern int cluster_get_highest_freq(struct cpu_physical *clust);
#define get_affected_cluster_least_cstate(cpuid, topo)		\
	cluster_get_least_cstate(cpu_to_cluster(cpuid, topo))
#define get_affected_cluster_highest_freq(cpuid, topo)		\
	cluster_get_highest_freq(cpu_to_cluster(cpuid, topo))

extern int core_get_least_cstate(struct cpu_core *core);
extern int core_get_highest_freq(struct cpu_core *core);
#define get_affected_core_least_cstate(cpuid, topo)		\
	core_get_least_cstate(cpu_to_core(cpuid, topo))
#define get_affected_core_highest_freq(cpuid, topo)		\
	core_get_highest_freq(cpu_to_core(cpuid, topo))

extern int setup_topo_states(struct cpuidle_datas *datas);

#endif

/*
 *  topology.c
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
#define  _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <assert.h>
#include <values.h>

#include "list.h"
#include "utils.h"
#include "topology.h"
#include "idlestat.h"

struct topology_info {
	int physical_id;
	int core_id;
	int cpu_id;
};

struct list_info {
	struct list_head hlist;
	int id;
};

struct list_head *check_exist_from_head(struct list_head *head, int id)
{
	struct list_head *tmp;

	list_for_each(tmp, head) {
		if (id == ((struct list_info *)tmp)->id)
			return tmp;
	}

	return NULL;
}

struct list_head *check_pos_from_head(struct list_head *head, int id)
{
	struct list_head *tmp;

	list_for_each(tmp, head) {
		if (id < ((struct list_info *)tmp)->id)
			break;
	}

	return tmp;
}

int add_topo_info(struct cpu_topology *topo_list, struct topology_info *info)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu = NULL;
	struct list_head    *ptr;

	/* add cpu physical info */
	ptr = check_exist_from_head(&topo_list->physical_head,
					info->physical_id);
	if (!ptr) {
		s_phy = calloc(sizeof(struct cpu_physical), 1);
		if (!s_phy)
			return -1;

		s_phy->core_num = 0;
		s_phy->physical_id = info->physical_id;
		INIT_LIST_HEAD(&s_phy->core_head);
		INIT_LIST_HEAD(&s_phy->cpu_enum_head);

		ptr = check_pos_from_head(&topo_list->physical_head,
						s_phy->physical_id);
		list_add_tail(&s_phy->list_physical, ptr);
		topo_list->physical_num++;
	} else {
		s_phy = list_entry(ptr, struct cpu_physical, list_physical);
	}

	/* add cpu core info */
	ptr = check_exist_from_head(&s_phy->core_head, info->core_id);
	if (!ptr) {
		s_core = calloc(sizeof(struct cpu_core), 1);
		if (!s_core)
			return -1;

		s_core->cpu_num = 0;
		s_core->is_ht = false;
		s_core->core_id = info->core_id;
		INIT_LIST_HEAD(&s_core->cpu_head);

		ptr = check_pos_from_head(&s_phy->core_head, s_core->core_id);
		list_add_tail(&s_core->list_core, ptr);
		s_phy->core_num++;

	} else {
		s_core = list_entry(ptr, struct cpu_core, list_core);
	}

	/* add cpu info */
	if (check_exist_from_head(&s_core->cpu_head, info->cpu_id) != NULL)
		return 0;

	s_cpu = calloc(sizeof(struct cpu_cpu), 1);
	if (!s_cpu)
		return -1;

	s_cpu->cpu_id = info->cpu_id;

	ptr = check_pos_from_head(&s_core->cpu_head, s_cpu->cpu_id);
	list_add_tail(&s_cpu->list_cpu, ptr);
	s_core->cpu_num++;
	if (s_core->cpu_num > 1)
		s_core->is_ht = true;

	/* Assumption: Same cpuid cannot exist in 2 different cores */
	assert(!check_exist_from_head(&s_phy->cpu_enum_head, info->cpu_id));

	/* Add to the list (really a set) of all contained cpus in s_phy */
	list_add_tail(&s_cpu->list_phy_enum, &s_phy->cpu_enum_head);

	return 0;
}

struct cpu_physical *cpu_to_cluster(int cpuid, struct cpu_topology *topo)
{
	struct cpu_physical *phy;
	struct cpu_cpu *cpu;

	topo_for_each_cluster(phy, topo)
		cluster_for_each_cpu(cpu, phy)
			if (cpu->cpu_id == cpuid)
				return phy;
	return NULL;
}

struct cpu_core *cpu_to_core(int cpuid, struct cpu_topology *topo)
{
	struct cpu_physical *phy;
	struct cpu_core *core;
	struct cpu_cpu *cpu;

	topo_for_each_cluster(phy, topo)
		cluster_for_each_core(core, phy)
			core_for_each_cpu(cpu, core)
				if (cpu->cpu_id == cpuid)
					return core;
	return NULL;
}

void free_cpu_cpu_list(struct list_head *head)
{
	struct cpu_cpu *lcpu, *n;

	list_for_each_entry_safe(lcpu, n, head, list_cpu) {
		list_del(&lcpu->list_cpu);
		list_del(&lcpu->list_phy_enum);
		free(lcpu);
	}
}

void free_cpu_core_list(struct list_head *head)
{
	struct cpu_core *lcore, *n;

	list_for_each_entry_safe(lcore, n, head, list_core) {
		free_cpu_cpu_list(&lcore->cpu_head);
		list_del(&lcore->list_core);
		free(lcore);
	}
}

void free_cpu_topology(struct list_head *head)
{
	struct cpu_physical *lphysical, *n;

	list_for_each_entry_safe(lphysical, n, head, list_physical) {
		free_cpu_core_list(&lphysical->core_head);
		list_del(&lphysical->list_physical);
		free(lphysical);
	}
}

int output_topo_info(struct cpu_topology *topo_list)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical) {
		printf("cluster%c:\n", s_phy->physical_id + 'A');
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			printf("\tcore%d\n", s_core->core_id);
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				printf("\t\tcpu%d\n", s_cpu->cpu_id);
		}
	}

	return 0;
}

int outfile_topo_info(FILE *f, struct cpu_topology *topo_list)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical) {
		fprintf(f, "cluster%c:\n", s_phy->physical_id + 'A');
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			fprintf(f, "\tcore%d\n", s_core->core_id);
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				fprintf(f, "\t\tcpu%d\n", s_cpu->cpu_id);
		}
	}

	return 0;
}

struct cpu_cpu *find_cpu_point(struct cpu_topology *topo_list, int cpuid)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;

	list_for_each_entry(s_phy, &topo_list->physical_head, list_physical)
		list_for_each_entry(s_core, &s_phy->core_head, list_core)
			list_for_each_entry(s_cpu, &s_core->cpu_head, list_cpu)
				if (s_cpu->cpu_id == cpuid)
					return s_cpu;

	return NULL;
}

static inline int read_topology_cb(char *path, struct topology_info *info)
{
	file_read_value(path, "core_id", "%d", &info->core_id);
	file_read_value(path, "physical_package_id", "%d", &info->physical_id);

	return 0;
}

typedef int (*folder_filter_t)(const char *name);

static int cpu_filter_cb(const char *name)
{
	/* let's ignore some directories in order to avoid to be
	 * pulled inside the sysfs circular symlinks mess/hell
	 * (choose the word which fit better)*/
	if (!strcmp(name, "cpuidle"))
		return 1;

	if (!strcmp(name, "cpufreq"))
		return 1;

	return 0;
}

/*
 * This function will browse the directory structure and build a
 * reflecting the content of the directory tree.
 *
 * @path   : the root node of the folder
 * @filter : a callback to filter out the directories
 * Returns 0 on success, -1 otherwise
 */
static struct cpu_topology *topo_folder_scan(char *path, folder_filter_t filter)
{
	DIR *dir, *dir_topology;
	char *basedir, *newpath;
	struct dirent dirent, *direntp;
	struct stat s;
	int ret;
	struct cpu_topology *result = NULL;

	dir = opendir(path);
	if (!dir)
		return ptrerror(path);

	ret = asprintf(&basedir, "%s", path);
	if (ret < 0) {
		closedir(dir);
		return ptrerror(__func__);
	}

	result = alloc_cpu_topo_info();
	if (is_err(result)) {
		free(basedir);
		closedir(dir);
		return result;
	}

	while (!readdir_r(dir, &dirent, &direntp)) {

		if (!direntp)
			break;

		if (direntp->d_name[0] == '.')
			continue;

		if (filter && filter(direntp->d_name))
			continue;

		if (!strstr(direntp->d_name, "cpu"))
			continue;

		ret = asprintf(&newpath, "%s/%s/%s", basedir,
				direntp->d_name, "topology");
		if (ret < 0)
			goto out_free_basedir;

		ret = stat(newpath, &s);
		if (ret)
			goto out_free_newpath;

		if (S_ISDIR(s.st_mode) || (S_ISLNK(s.st_mode))) {
			struct topology_info cpu_info;

			dir_topology = opendir(path);
			if (!dir_topology)
				continue;
			closedir(dir_topology);

			read_topology_cb(newpath, &cpu_info);
			if (sscanf(direntp->d_name, "cpu%d",
				      &cpu_info.cpu_id) != 1) {
				ret = -1;
				fprintf(stderr, "Cannot extract cpu number "
					"from %s\n", newpath);
				goto out_free_newpath;
			}
			add_topo_info(result, &cpu_info);
		}

		free(newpath);
	}

	free(basedir);
	closedir(dir);

	return result;

out_free_newpath:
	free(newpath);
out_free_basedir:
	free(basedir);
	closedir(dir);
	release_cpu_topo_info(result);

	return ptrerror(__func__);
}


struct cpu_topology *alloc_cpu_topo_info(void)
{
	struct cpu_topology *ret;

	ret = calloc(sizeof(*ret), 1);
	if (ret == NULL)
		return ptrerror(__func__);
	INIT_LIST_HEAD(&ret->physical_head);
	ret->physical_num = 0;

	return ret;
}

struct cpu_topology *read_sysfs_cpu_topo(void)
{
	return topo_folder_scan("/sys/devices/system/cpu", cpu_filter_cb);
}

struct cpu_topology *read_cpu_topo_info(FILE *f, char *buf)
{
	int ret = 0;
	struct topology_info cpu_info;
	bool is_ht = false;
	char pid;
	struct cpu_topology *result = NULL;

	result = alloc_cpu_topo_info();
	if (is_err(result))
		return result;

	do {
		ret = sscanf(buf, "cluster%c", &pid);
		if (!ret)
			break;

		cpu_info.physical_id = pid - 'A';

		fgets(buf, BUFSIZE, f);
		do {
			ret = sscanf(buf, "\tcore%d", &cpu_info.core_id);
			if (ret) {
				is_ht = true;
				fgets(buf, BUFSIZE, f);
			} else {
				ret = sscanf(buf, "\tcpu%d", &cpu_info.cpu_id);
				if (ret)
					is_ht = false;
				else
					break;
			}

			do {
				if (!is_ht) {
					ret = sscanf(buf, "\tcpu%d",
						     &cpu_info.cpu_id);
					cpu_info.core_id = cpu_info.cpu_id;
				} else {
					ret = sscanf(buf, "\t\tcpu%d",
						     &cpu_info.cpu_id);
				}

				if (!ret)
					break;

				add_topo_info(result, &cpu_info);

				fgets(buf, BUFSIZE, f);
			} while (1);
		} while (1);
	} while (1);

	/* output_topo_info(result); */

	return result;
}

int release_cpu_topo_info(struct cpu_topology *topo)
{
	if (topo == NULL)
		return 0;

	/* Free alloced memory */
	free_cpu_topology(&topo->physical_head);
	free(topo);

	return 0;
}

int output_cpu_topo_info(struct cpu_topology *topo, FILE *f)
{
	outfile_topo_info(f, topo);

	return 0;
}

int establish_idledata_to_topo(struct cpuidle_datas *datas)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	int    i;
	int    has_topo = 0;
	struct cpu_topology *topo;
	struct cpuidle_datas *baseline;

	assert(datas != NULL);
	topo = datas->topo;
	assert(topo != NULL);

	if (datas->baseline)
		baseline = datas->baseline;
	else
		baseline = NULL;

	for (i = 0; i < datas->nrcpus; i++) {
		s_cpu = find_cpu_point(topo, i);
		if (s_cpu) {
			s_cpu->cstates = &datas->cstates[i];
			s_cpu->pstates = &datas->pstates[i];
			has_topo = 1;
			if (baseline) {
				s_cpu->base_cstates = baseline->cstates + i;
				s_cpu->base_pstates = baseline->pstates + i;
			}
		}
	}

	if (!has_topo)
		return -1;

	list_for_each_entry(s_phy, &topo->physical_head,
			    list_physical) {
		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			s_core->cstates = core_cluster_data(s_core);
			if (is_err(s_core->cstates)) {
				s_core->cstates = NULL;
				return -1;
			}
		}
	}

	list_for_each_entry(s_phy, &topo->physical_head,
			    list_physical) {
		s_phy->cstates = physical_cluster_data(s_phy);
		if (is_err(s_phy->cstates)) {
			s_phy->cstates = NULL;
			return -1;
		}
	}

	return 0;
}

int dump_cpu_topo_info(struct report_ops *ops, void *report_data, int (*dump)(struct report_ops *, void *, void *, char *, void *), struct cpu_topology *topo, int cstate)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	char tmp[30];

	list_for_each_entry(s_phy, &topo->physical_head,
			    list_physical) {

		sprintf(tmp, "cluster%c", s_phy->physical_id + 'A');

		if (cstate)
			dump(ops, s_phy->cstates, NULL, tmp, report_data);

		list_for_each_entry(s_core, &s_phy->core_head, list_core) {
			if (s_core->is_ht && cstate) {
				sprintf(tmp, "core%d", s_core->core_id);
				dump(ops, s_core->cstates,
					NULL,
					tmp, report_data);
			}

			list_for_each_entry(s_cpu, &s_core->cpu_head,
					    list_cpu) {
				sprintf(tmp, "cpu%d", s_cpu->cpu_id);
				if (cstate)
					dump(ops, s_cpu->cstates,
						s_cpu->base_cstates,
						tmp, report_data);
				else
					dump(ops, s_cpu->pstates,
						s_cpu->base_pstates,
						tmp, report_data);
			}
		}
	}

	return 0;
}

int release_cpu_topo_cstates(struct cpu_topology *topo)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;

	list_for_each_entry(s_phy, &topo->physical_head,
			    list_physical) {
		release_cstate_info(s_phy->cstates, 1);
		s_phy->cstates = NULL;
		list_for_each_entry(s_core, &s_phy->core_head, list_core)
			if (s_core->is_ht) {
				release_cstate_info(s_core->cstates, 1);
				s_core->cstates = NULL;
			}
	}

	return 0;
}

int cluster_get_least_cstate(struct cpu_physical *clust)
{
	struct cpu_cpu *cpu;
	int cpu_cstate;
	int ret = MAXCSTATE;

	cluster_for_each_cpu(cpu, clust) {
		cpu_cstate = cpu->cstates->current_cstate;
		if (cpu_cstate < ret)
			ret = cpu_cstate;
	}
	return ret;
}

int cluster_get_highest_freq(struct cpu_physical *clust)
{
	struct cpu_cpu *cpu;
	int cpu_pstate_index;
	unsigned int cpu_freq;
	unsigned int ret = ~0;

	cluster_for_each_cpu(cpu, clust) {
		cpu_pstate_index = cpu->pstates->current;
		if (cpu_pstate_index < 0)
			continue;
		cpu_freq = cpu->pstates->pstate[cpu_pstate_index].freq;
		if (cpu_freq < ret)
			ret = cpu_freq;
	}

	/* It is possible we don't know anything near the start of trace */
	if (ret == ~0)
		ret = 0;

	return ret;
}

int core_get_least_cstate(struct cpu_core *core)
{
	struct cpu_cpu *cpu;
	int cpu_cstate;
	int ret = MAXCSTATE;

	core_for_each_cpu(cpu, core) {
		cpu_cstate = cpu->cstates->current_cstate;
		if (cpu_cstate < ret)
			ret = cpu_cstate;
	}
	return ret;
}

int core_get_highest_freq(struct cpu_core *core)
{
	struct cpu_cpu *cpu;
	int cpu_pstate_index;
	unsigned int cpu_freq;
	unsigned int ret = ~0;

	core_for_each_cpu(cpu, core) {
		cpu_pstate_index = cpu->pstates->current;
		if (cpu_pstate_index < 0)
			continue;
		cpu_freq = cpu->pstates->pstate[cpu_pstate_index].freq;
		if (cpu_freq < ret)
			ret = cpu_freq;
	}

	/* It is possible we don't know anything near the start of trace */
	if (ret == ~0)
		ret = 0;

	return ret;
}

/**
 * create_states - Helper for allocating cpuidle_cstates and copying names
 *
 * This function allocates a struct cpuidle_cstates for recording state
 * statistics for a core or a cluster. The c-state information (e.g. names)
 * is copied from an existing array of struct cpuidle_cstate[MAXCSTATE]
 * pointed to by @s_state.
 *
 * In case of any error, this function will print an error message to
 * stderr before returning value from ptrerror().
 *
 * @s_state: Pointer to first element in an array of struct cpuidle_cstate
 * @return: Pointer to the created structure or ptrerror()
 */
static struct cpuidle_cstates *create_states(struct cpuidle_cstate *s_state)
{
	struct cpuidle_cstates *result;
	struct cpuidle_cstate *d_state;
	int i;

	result = calloc(1, sizeof(*result));
	if (!result)
		return ptrerror(__func__);

	result->cstate_max = -1;
	result->current_cstate = -1;

	/* Copy state information */
	d_state = result->cstate;
	for (i = 0; i <= MAXCSTATE; ++d_state, ++s_state, ++i) {
		if (s_state->name == NULL)
			continue;

		d_state->min_time = DBL_MAX;
		d_state->target_residency = s_state->target_residency;
		d_state->name = strdup(s_state->name);

		if (!d_state->name) {
			release_cstate_info(result, 1);
			return ptrerror(__func__);
		}
	}

	return result;
}


/**
 * create_core_states - Create c-state data structure for a core
 *
 * This function allocates a struct cpuidle_cstates for recording state
 * statistics for @s_core. The c-state information (e.g. names) is copied
 * from the first cpu within the core. The cpu topology mapping must have
 * been established before calling this function.
 *
 * The case where there are no cpus in the core is considered an internal
 * error.
 *
 * In case of any error, this function will print an error message to
 * stderr before returning value from ptrerror().
 *
 * @s_core: The core that the structure is allocated for
 * @return: Pointer to the created structure or ptrerror()
 */
static struct cpuidle_cstates *create_core_states(struct cpu_core *s_core)
{
	struct cpu_cpu *origin_cpu = NULL;
	struct cpuidle_cstate *first_s_state;

	assert(s_core != NULL);
	assert(!list_empty(&s_core->cpu_head));

	/* Copy state names from the first cpu */
	origin_cpu = list_first_entry(&s_core->cpu_head, struct cpu_cpu,
			list_cpu);
	first_s_state = origin_cpu->cstates->cstate;

	return create_states(first_s_state);
}

/**
 * create_cluster_states - Create c-state data structure for a cluster
 *
 * This function allocates a struct cpuidle_cstates for recording state
 * statistics for @s_phy. The c-state information (e.g. names) is copied
 * from the first core within the cluster. The core states must have
 * been established before calling this function.
 *
 * The case where there are no cores in the cluster is considered an internal
 * error.
 *
 * In case of any error, this function will print an error message to
 * stderr before returning value from ptrerror().
 *
 * @s_phy: The cluster that the structure is allocated for
 * @return: Pointer to the created structure or ptrerror()
 */
static struct cpuidle_cstates *create_cluster_states(struct cpu_physical *s_phy)
{
	struct cpu_core *origin_core = NULL;
	struct cpuidle_cstate *first_s_state;

	assert(s_phy != NULL);
	assert(!list_empty(&s_phy->core_head));

	/* Copy state names from the first cpu */
	origin_core = list_first_entry(&s_phy->core_head, struct cpu_core,
			list_core);
	first_s_state = origin_core->cstates->cstate;

	return create_states(first_s_state);
}

int setup_topo_states(struct cpuidle_datas *datas)
{
	struct cpu_physical *s_phy;
	struct cpu_core     *s_core;
	struct cpu_cpu      *s_cpu;
	int    i;
	struct cpu_topology *topo;

	assert(datas != NULL);
	topo = datas->topo;
	assert(topo != NULL);

	/* Map cpu state arrays into topology structures */
	for (i = 0; i < datas->nrcpus; i++) {
		s_cpu = find_cpu_point(topo, i);
		if (s_cpu) {
			s_cpu->cstates = &datas->cstates[i];
			s_cpu->pstates = &datas->pstates[i];
		} else {
			fprintf(stderr,
				"Warning: Cannot map cpu %d into topology\n",
				i);
		}
	}

	/* Create cluster-level and core-level state structures */
	topo_for_each_cluster(s_phy, topo) {
		cluster_for_each_core(s_core, s_phy) {
			s_core->cstates = create_core_states(s_core);
			if (is_err(s_core->cstates)) {
				s_core->cstates = NULL;
				return -1;
			}
		}
		s_phy->cstates = create_cluster_states(s_phy);
		if (is_err(s_phy->cstates)) {
			s_phy->cstates = NULL;
			return -1;
		}
	}

	return 0;
}

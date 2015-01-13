/*
 *  reports.c
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

#include "report_ops.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void list_report_formats_to_stderr(void)
{
	const struct report_ops **ops_it;

	/*
	 * The linker places pointers to all entries declared with
	 * EXPORT_REPORT_OPS into a special segment. This creates
	 * an array of pointers preceded by report_ops_head. Let
	 * the static analysis tool know that we know what we are
	 * doing.
	 */
	/* coverity[array_vs_singleton] */
	for (ops_it = (&report_ops_head)+1 ; *ops_it ; ++ops_it)
		fprintf(stderr, " %s", (*ops_it)->name);

	fprintf(stderr, "\n");
}

struct report_ops *get_report_ops(const char *name)
{
	struct report_ops **ops_it;

	for (ops_it = (struct report_ops **)(&report_ops_head)+1 ;
		*ops_it ; ++ops_it) {

		/* Compare name */
		assert((*ops_it)->name);
		if (strcmp((*ops_it)->name, name))
			continue;

		/* Prepare for use */
		if ((*ops_it)->prepare && -1 == (*ops_it)->prepare(*ops_it))
			return NULL;

		/* Check mandatory operations */
		assert((*ops_it)->check_output);
		assert((*ops_it)->open_report_file);
		assert((*ops_it)->close_report_file);
		assert((*ops_it)->cstate_table_header);
		assert((*ops_it)->cstate_table_footer);
		assert((*ops_it)->cstate_cpu_header);
		assert((*ops_it)->cstate_single_state);
		assert((*ops_it)->cstate_end_cpu);
		assert((*ops_it)->pstate_table_header);
		assert((*ops_it)->pstate_table_footer);
		assert((*ops_it)->pstate_cpu_header);
		assert((*ops_it)->pstate_single_freq);
		assert((*ops_it)->pstate_end_cpu);
		assert((*ops_it)->wakeup_table_header);
		assert((*ops_it)->wakeup_table_footer);
		assert((*ops_it)->wakeup_cpu_header);
		assert((*ops_it)->wakeup_single_irq);
		assert((*ops_it)->wakeup_end_cpu);

		return *ops_it;
	}

	fprintf(stderr, "Report style %s does not exist\n", name);
	return ptrerror(NULL);
}

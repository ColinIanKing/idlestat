/*
 *  trace_ops.h
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
#ifndef __TRACE_OPS_H
#define __TRACE_OPS_H

#include <stdio.h>

struct cpuidle_datas;

struct trace_ops {
	const char *name;
	int (*check_magic)(const char *filename);
	struct cpuidle_datas *(*load)(const char *filename);
};

extern int load_text_data_line(char *buffer, struct cpuidle_datas *datas, char *format, double *begin, double *end, size_t *start);
extern void load_text_data_lines(FILE *f, char *buffer, struct cpuidle_datas *datas);

#define EXPORT_TRACE_OPS(tracetype_name)			\
	static const struct trace_ops				\
	__attribute__ ((__used__))				\
	__attribute__ ((__section__ ("__trace_ops")))		\
	* tracetype_name ## _trace_ptr = &tracetype_name##_trace_ops

extern const struct trace_ops *trace_ops_head;

#endif

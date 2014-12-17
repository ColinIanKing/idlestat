#ifndef __TRACE_OPS_H
#define __TRACE_OPS_H

#include <stdio.h>

struct cpuidle_datas;

struct trace_ops {
	const char *name;
	int (*check_magic)(const char *filename);
	struct cpuidle_datas *(*load)(const char *filename);
};

extern int load_text_data_line(char *buffer, struct cpuidle_datas *datas, char *format, double *begin, double *end, size_t *count, size_t *start);
extern void load_text_data_lines(FILE *f, char *buffer, struct cpuidle_datas *datas);

#define EXPORT_TRACE_OPS(tracetype_name)			\
	static const struct trace_ops				\
	__attribute__ ((__used__))				\
	__attribute__ ((__section__ ("__trace_ops")))		\
	* tracetype_name ## _trace_ptr = &tracetype_name##_trace_ops

extern const struct trace_ops *trace_ops_head;

#endif

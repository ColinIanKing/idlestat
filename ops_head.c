#include "report_ops.h"
#include "trace_ops.h"
#include <stddef.h>

const struct trace_ops
	__attribute__((__used__)) __attribute__ ((__section__ ("__trace_ops")))
	*trace_ops_head = NULL;

const struct report_ops
	__attribute__((__used__)) __attribute__ ((__section__ ("__report_ops")))
	*report_ops_head = NULL;

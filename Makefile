#
# Makefile
#
# Copyright (C) 2014, Linaro Limited
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# Contributors:
#     Daniel Lezcano <daniel.lezcano@linaro.org>
#     Zoran Markovic <zoran.markovic@linaro.org>
#
CFLAGS?=-g -Wall -Wunused-parameter
CC=gcc

TRACE_OBJS =	tracefile_idlestat.o tracefile_ftrace.o \
		tracefile_tracecmd.o
REPORT_OBJS =	default_report.o csv_report.o comparison_report.o


OBJS =	idlestat.o topology.o trace.o utils.o energy_model.o reports.o \
	ops_head.o \
	$(REPORT_OBJS) \
	$(TRACE_OBJS) \
	ops_tail.o

default: idlestat

%.o: %.c
	$(CROSS_COMPILE)$(CC) -c -o $@ $< $(CFLAGS)

idlestat: $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(OBJS) -o $@

install: idlestat idlestat.1
	install -D -t /usr/local/bin idlestat
	install -D -t /usr/local/man/man1 idlestat.1

clean:
	rm -f $(OBJS) idlestat

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
CFLAGS?=-g -Wall
CC=gcc

OBJS = idlestat.o topology.o trace.o utils.o energy_model.o default_report.o csv_report.o

default: idlestat

%.o: %.c
	$(CROSS_COMPILE)$(CC) -c -o $@ $< $(CFLAGS)

idlestat: $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(OBJS) -o $@

clean:
	rm -f $(OBJS) idlestat

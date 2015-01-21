#
# Android.mk
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
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := idlestat
LOCAL_CFLAGS :=
LOCAL_LDFLAGS := -Wl,--no-gc-sections

TRACE_SRC_FILES = tracefile_idlestat.c tracefile_ftrace.c \
		tracefile_tracecmd.c

REPORT_SRC_FILES = default_report.c csv_report.c comparison_report.c

LOCAL_SRC_FILES += \
	idlestat.c \
	topology.c \
	trace.c    \
	utils.c   \
	energy_model.c   \
	reports.c   \
	ops_head.c   \
	$(TRACE_SRC_FILES) \
	$(REPORT_SRC_FILES) \
	ops_tail.c   \


include $(BUILD_EXECUTABLE)

/*
 *  utils.h
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
 *
 */
#ifndef __UTILS_H
#define __UTILS_H

#include <stdio.h>

extern void set_verbose_level(int level);
extern int verbose_printf(int min_level, const char *fmt, ...);
extern int verbose_fprintf(FILE *f, int min_level, const char *fmt, ...);

extern int write_int(const char *path, int val);
extern int read_int(const char *path, int *val);
extern int store_line(const char *line, void *data);
extern int file_read_value(const char *path, const char *name,
				const char *format, void *value);
extern int redirect_stdout_to_file(const char *path);
extern void display_factored_time(double time, int align);
extern void display_factored_freq(int freq, int align);
extern int check_window_size(void);

extern int error(const char *str);
extern void *ptrerror(const char *str);
extern int is_err(const void *ptr);

#endif

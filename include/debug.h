/*
 * This file is part of FILO.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */


#ifndef DEBUG_H
#define DEBUG_H

#include <lib.h>

/* Defining DEBUG_THIS before including this file enables debug() macro
 * for the file. DEBUG_ALL is for global control. */

#if DEBUG_THIS || DEBUG_ALL
#define DEBUG 1
#else
#undef DEBUG
#endif

#if DEBUG
# define debug(...) \
    printf(__VA_ARGS__)
# define debug_hexdump hexdump
#else
# define debug(...) /* nothing */
# define debug_hexdump(...) /* nothing */
#endif

#endif /* DEBUG_H */
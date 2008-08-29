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


#ifndef FS_H
#define FS_H

#include <libpayload.h>
#include <arch/byteorder.h>
typedef uint64_t sector_t;

#define DEV_SECTOR_BITS		9
#define DEV_SECTOR_SIZE		(1<<9)
#define DEV_SECTOR_MASK		(DEV_SECTOR_SIZE-1)

#ifdef CONFIG_IDE_DISK
int ide_probe(int drive);
int ide_read(int drive, sector_t sector, void *buffer);
#endif

#ifdef CONFIG_USB_DISK
int usb_probe(int drive);
int usb_read(int drive, sector_t sector, void *buffer);
#endif

#ifdef CONFIG_USB_NEW_DISK
int usb_new_probe(int drive);
int usb_new_read(int drive, sector_t sector, void *buffer);
#endif

#ifdef CONFIG_FLASH_DISK
int flash_probe(int drive);
int flash_read(int drive, sector_t sector, void *buffer);
int NAND_close(void);
#endif

#define DISK_IDE 1
#define DISK_MEM 2
#define DISK_USB 3
#define DISK_FLASH 4
#define DISK_NEW_USB 5

int devopen(const char *name, int *reopen);
void devclose(void);
int devread(unsigned long sector, unsigned long byte_offset,
	unsigned long byte_len, void *buf);
void dev_set_partition(unsigned long start, unsigned long size);
void dev_get_partition(unsigned long *start, unsigned long *size);

int file_open(const char *filename);
int file_read(void *buf, unsigned long len);
int file_seek(unsigned long offset);
unsigned long file_size(void);
void file_set_size(unsigned long size);
void file_close(void);

#define PARTITION_UNKNOWN	0xbad6a7

#ifdef CONFIG_ELTORITO
int open_eltorito_image(int part, unsigned long *start, unsigned long *length);
#else
# define open_eltorito_image(x,y,z) PARTITION_UNKNOWN
#endif

extern int using_devsize;

#endif /* FS_H */

/*
 *   OpenBIOS polled ide driver
 *   
 *   Copyright (C) 2004 Jens Axboe <axboe@suse.de>
 *   Copyright (C) 2005 Stefan Reinauer <stepan@openbios.org>
 *   Copyright (C) 2009 coresystems GmbH
 *
 *   Credit goes to Hale Landis for his excellent ata demo software
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#define GRUB

#include <libpayload.h>
#include <config.h>
#include <arch/byteorder.h>

#include <fs.h>
#include "ide_new.h"
#include "hdreg.h"

#ifdef CONFIG_SUPPORT_PCI
#include <pci.h>
#endif

#define DEBUG_THIS CONFIG_DEBUG_IDE
#include <debug.h>

/*
 * define to 2 for the standard 2 channels only
 */
#ifndef CONFIG_IDE_NUM_CHANNELS
#define IDE_NUM_CHANNELS 4
#else
#define IDE_NUM_CHANNELS CONFIG_IDE_NUM_CHANNELS
#endif
#define IDE_MAX_CHANNELS 4
#define IDE_MAX_DRIVES (IDE_MAX_CHANNELS * 2)

static struct ide_channel ob_ide_channels[IDE_MAX_CHANNELS];

/*
 * FIXME: probe, we just hardwire legacy ports for now
 */
static const int io_ports[IDE_MAX_CHANNELS] = { 0x1f0, 0x170, 0x1e8, 0x168 };
static const int ctl_ports[IDE_MAX_CHANNELS] = { 0x3f6, 0x376, 0x3ee, 0x36e };

/*
 * don't be pedantic
 */
#undef ATA_PEDANTIC

// debug function currently not used.
#if defined(CONFIG_DEBUG_IDE) && 0
static void dump_drive(struct ide_drive *drive)
{
	debug("IDE DRIVE @%lx:\n", (unsigned long)drive);
	debug("unit: %d\n",drive->unit);
	debug("present: %d\n",drive->present);
	debug("type: %d\n",drive->type);
	debug("media: %d\n",drive->media);
	debug("model: %s\n",drive->model);
	debug("nr: %d\n",drive->nr);
	debug("cyl: %d\n",drive->cyl);
	debug("head: %d\n",drive->head);
	debug("sect: %d\n",drive->sect);
	debug("bs: %d\n",drive->bs);
}
#endif

/*
 * old style io port operations
 */
static unsigned char
ob_ide_inb(unsigned long port)
{
	return inb(port);
}

static void
ob_ide_outb(unsigned char data, unsigned long port)
{
	outb(data, port);
}

static void
ob_ide_insw(unsigned long port, unsigned char *addr, unsigned int count)
{
	insw(port, addr, count);
}

static void
ob_ide_outsw(unsigned long port, unsigned char *addr, unsigned int count)
{
	outsw(port, addr, count);
}

static inline unsigned char
ob_ide_pio_readb(struct ide_drive *drive, unsigned int offset)
{
	struct ide_channel *chan = drive->channel;

	return chan->obide_inb(chan->io_regs[offset]);
}

static inline void
ob_ide_pio_writeb(struct ide_drive *drive, unsigned int offset,
		  unsigned char data)
{
	struct ide_channel *chan = drive->channel;

	chan->obide_outb(data, chan->io_regs[offset]);
}

static inline void
ob_ide_pio_insw(struct ide_drive *drive, unsigned int offset,
		unsigned char *addr, unsigned int len)
{
	struct ide_channel *chan = drive->channel;

	if (len & 1) {
		printf("%d: command not word aligned\n", drive->nr);
		return;
	}

	chan->obide_insw(chan->io_regs[offset], addr, len / 2);
}

static inline void
ob_ide_pio_outsw(struct ide_drive *drive, unsigned int offset,
		unsigned char *addr, unsigned int len)
{
	struct ide_channel *chan = drive->channel;

	if (len & 1) {
		printf("%d: command not word aligned\n", drive->nr);
		return;
	}

	chan->obide_outsw(chan->io_regs[offset], addr, len / 2);
}

static void
ob_ide_400ns_delay(struct ide_drive *drive)
{
	(void) ob_ide_pio_readb(drive, IDEREG_ASTATUS);
	(void) ob_ide_pio_readb(drive, IDEREG_ASTATUS);
	(void) ob_ide_pio_readb(drive, IDEREG_ASTATUS);
	(void) ob_ide_pio_readb(drive, IDEREG_ASTATUS);

	udelay(1);
}

static void
ob_ide_error(struct ide_drive *drive, unsigned char stat, char *msg)
{
	struct ide_channel *chan = drive->channel;
	unsigned char err;

	if (!stat)
		stat = ob_ide_pio_readb(drive, IDEREG_STATUS);

	printf("ob_ide_error drive<%d>: %s:\n", drive->nr, msg);
	printf("    cmd=%x, stat=%x", chan->ata_cmd.command, stat);

	if ((stat & (BUSY_STAT | ERR_STAT)) == ERR_STAT) {
		err = ob_ide_pio_readb(drive, IDEREG_ERROR);
		printf(", err=%x", err);
	}
	printf("\n");

	/*
	 * see if sense is valid and dump that
	 */
	if (chan->ata_cmd.command == WIN_PACKET) {
		struct atapi_command *cmd = &chan->atapi_cmd;
		unsigned char old_cdb = cmd->cdb[0];

		if (cmd->cdb[0] == ATAPI_REQ_SENSE) {
			old_cdb = cmd->old_cdb;

			printf("    atapi opcode=%02x", old_cdb);
		} else {
			int i;

			printf("    cdb: ");
			for (i = 0; i < sizeof(cmd->cdb); i++)
				printf("%02x ", cmd->cdb[i]);
		}
		if (cmd->sense_valid)
			printf(", sense: %02x/%02x/%02x", cmd->sense.sense_key, cmd->sense.asc, cmd->sense.ascq);
		else
			printf(", no sense");
		printf("\n");
	}
}

/*
 * wait for 'stat' to be set. returns 1 if failed, 0 if succesful
 */
static int
ob_ide_wait_stat(struct ide_drive *drive, unsigned char ok_stat,
		 unsigned char bad_stat, unsigned char *ret_stat)
{
	unsigned char stat;
	int i;

	ob_ide_400ns_delay(drive);

	for (i = 0; i < 5000; i++) {
		stat = ob_ide_pio_readb(drive, IDEREG_STATUS);
		if (!(stat & BUSY_STAT))
			break;

		udelay(1000);
	}

	if (ret_stat)
		*ret_stat = stat;

	if (stat & bad_stat)
		return 1;

	if ((stat & ok_stat) || !ok_stat)
		return 0;

	return 1;
}

static int
ob_ide_select_drive(struct ide_drive *drive)
{
	struct ide_channel *chan = drive->channel;
	unsigned char control = IDEHEAD_DEV0;

	if (ob_ide_wait_stat(drive, 0, BUSY_STAT, NULL)) {
		printf("select_drive: timed out\n");
		return 1;
	}

	/*
	 * don't select drive if already active. Note: we always
	 * wait for BUSY clear
	 */
	if (drive->unit == chan->selected)
		return 0;

	if (drive->unit)
		control = IDEHEAD_DEV1;

	ob_ide_pio_writeb(drive, IDEREG_CURRENT, control);
	ob_ide_400ns_delay(drive);

	if (ob_ide_wait_stat(drive, 0, BUSY_STAT, NULL)) {
		printf("select_drive: timed out\n");
		return 1;
	}

	chan->selected = drive->unit;
	return 0;
}

static void
ob_ide_write_tasklet(struct ide_drive *drive, struct ata_command *cmd)
{
	ob_ide_pio_writeb(drive, IDEREG_FEATURE, cmd->task[1]);
	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, cmd->task[3]);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, cmd->task[7]);
	ob_ide_pio_writeb(drive, IDEREG_LCYL, cmd->task[8]);
	ob_ide_pio_writeb(drive, IDEREG_HCYL, cmd->task[9]);

	ob_ide_pio_writeb(drive, IDEREG_FEATURE, cmd->task[0]);
	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, cmd->task[2]);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, cmd->task[4]);
	ob_ide_pio_writeb(drive, IDEREG_LCYL, cmd->task[5]);
	ob_ide_pio_writeb(drive, IDEREG_HCYL, cmd->task[6]);

	if (drive->unit)
		cmd->device_head |= IDEHEAD_DEV1;

	ob_ide_pio_writeb(drive, IDEREG_CURRENT, cmd->device_head);

	ob_ide_pio_writeb(drive, IDEREG_COMMAND, cmd->command);
	ob_ide_400ns_delay(drive);
}

static void
ob_ide_write_registers(struct ide_drive *drive, struct ata_command *cmd)
{
	/*
	 * we are _always_ polled
	 */
	ob_ide_pio_writeb(drive, IDEREG_CONTROL, cmd->control | IDECON_NIEN);

	ob_ide_pio_writeb(drive, IDEREG_FEATURE, cmd->feature);
	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, cmd->nsector);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, cmd->sector);
	ob_ide_pio_writeb(drive, IDEREG_LCYL, cmd->lcyl);
	ob_ide_pio_writeb(drive, IDEREG_HCYL, cmd->hcyl);

	if (drive->unit)
		cmd->device_head |= IDEHEAD_DEV1;

	ob_ide_pio_writeb(drive, IDEREG_CURRENT, cmd->device_head);

	ob_ide_pio_writeb(drive, IDEREG_COMMAND, cmd->command);
	ob_ide_400ns_delay(drive);
}

/*
 * execute given command with a pio data-in phase.
 */
static int
ob_ide_pio_data_in(struct ide_drive *drive, struct ata_command *cmd)
{
	unsigned char stat;
	unsigned int bytes, timeout;

	if (ob_ide_select_drive(drive))
		return 1;

	/*
	 * ATA must set ready and seek stat, ATAPI need only clear busy
	 */
	timeout = 0;
	do {
		stat = ob_ide_pio_readb(drive, IDEREG_STATUS);

		if (drive->type == ide_type_ata) {
			/*
			 * this is BIOS code, don't be too pedantic
			 */
#ifdef ATA_PEDANTIC
			if ((stat & (BUSY_STAT | READY_STAT | SEEK_STAT)) ==
			    (READY_STAT | SEEK_STAT))
				break;
#else
			if ((stat & (BUSY_STAT | READY_STAT)) == READY_STAT)
				break;
#endif
		} else {
			if (!(stat & BUSY_STAT))
				break;
		}
		ob_ide_400ns_delay(drive);
	} while (timeout++ < 1000);

	if (timeout >= 1000) {
		ob_ide_error(drive, stat, "drive timed out");
		cmd->stat = stat;
		return 1;
	}
				
	ob_ide_write_registers(drive, cmd);

	/*
	 * now read the data
	 */
	bytes = cmd->buflen;
	do {
		unsigned count = cmd->buflen;

		if (count > drive->bs)
			count = drive->bs;

		/* delay 100ms for ATAPI? */

		/*
		 * wait for BUSY clear
		 */
		if (ob_ide_wait_stat(drive, 0, BUSY_STAT | ERR_STAT, &stat)) {
			ob_ide_error(drive, stat, "timed out waiting for BUSY clear");
			cmd->stat = stat;
			break;
		}

		/*
		 * transfer the data
		 */
		if ((stat & (BUSY_STAT | DRQ_STAT)) == DRQ_STAT) {
			ob_ide_pio_insw(drive, IDEREG_DATA, cmd->buffer, count);
			cmd->bytes -= count;
			cmd->buffer += count;
			bytes -= count;

			ob_ide_400ns_delay(drive);
		}

		if (stat & (BUSY_STAT | WRERR_STAT | ERR_STAT)) {
			cmd->stat = stat;
			break;
		}

		if (!(stat & DRQ_STAT)) {
			cmd->stat = stat;
			break;
		}
	} while (bytes);

	if (bytes)
		printf("bytes=%d, stat=%x\n", bytes, stat);

	return bytes ? 1 : 0;
}

/*
 * execute ata command with pio packet protocol
 */
static int
ob_ide_pio_packet(struct ide_drive *drive, struct atapi_command *cmd)
{
	unsigned char stat, reason, lcyl, hcyl;
	struct ata_command *acmd = &drive->channel->ata_cmd;
	unsigned char *buffer;
	unsigned int bytes;

	if (ob_ide_select_drive(drive))
		return 1;

	if (cmd->buflen && cmd->data_direction == atapi_ddir_none)
		printf("non-zero buflen but no data direction\n");

	memset(acmd, 0, sizeof(*acmd));
	acmd->lcyl = cmd->buflen & 0xff;
	acmd->hcyl = (cmd->buflen >> 8) & 0xff;
	acmd->command = WIN_PACKET;
	ob_ide_write_registers(drive, acmd);

	/*
	 * BUSY must be set, _or_ DRQ | ERR
	 */
	stat = ob_ide_pio_readb(drive, IDEREG_ASTATUS);
	if ((stat & BUSY_STAT) == 0) {
		if (!(stat & (DRQ_STAT | ERR_STAT))) {
			ob_ide_error(drive, stat, "bad stat in atapi cmd");
			cmd->stat = stat;
			return 1;
		}
	}

	if (ob_ide_wait_stat(drive, 0, BUSY_STAT | ERR_STAT, &stat)) {
		ob_ide_error(drive, stat, "timeout, ATAPI BUSY clear");
		cmd->stat = stat;
		return 1;
	}

	if ((stat & (BUSY_STAT | DRQ_STAT | ERR_STAT)) != DRQ_STAT) {
		/*
		 * if command isn't request sense, then we have a problem. if
		 * we are doing a sense, ERR_STAT == CHECK_CONDITION
		 */
		if (cmd->cdb[0] != ATAPI_REQ_SENSE) {
			printf("odd, drive didn't want to transfer %x\n", stat);
			return 1;
		}
	}

	/*
	 * transfer cdb
	 */
	ob_ide_pio_outsw(drive, IDEREG_DATA, cmd->cdb,sizeof(cmd->cdb));
	ob_ide_400ns_delay(drive);

	/*
	 * ok, cdb was sent to drive, now do data transfer (if any)
	 */
	bytes = cmd->buflen;
	buffer = cmd->buffer;
	do {
		unsigned int bc;

		if (ob_ide_wait_stat(drive, 0, BUSY_STAT | ERR_STAT, &stat)) {
			ob_ide_error(drive, stat, "busy not clear after cdb");
			cmd->stat = stat;
			break;
		}

		/*
		 * transfer complete!
		 */
		if ((stat & (BUSY_STAT | DRQ_STAT)) == 0)
			break;

		if ((stat & (BUSY_STAT | DRQ_STAT)) != DRQ_STAT)
			break;

		reason = ob_ide_pio_readb(drive, IDEREG_NSECTOR);
		lcyl = ob_ide_pio_readb(drive, IDEREG_LCYL);
		hcyl = ob_ide_pio_readb(drive, IDEREG_HCYL);

		/*
		 * check if the drive wants to transfer data in the same
		 * direction as we do...
		 */
		if ((reason & IREASON_CD) && cmd->data_direction != atapi_ddir_read) {
			ob_ide_error(drive, stat, "atapi, bad transfer ddir");
			break;
		}

		bc = (hcyl << 8) | lcyl;
		if (!bc)
			break;

		if (bc > bytes)
			bc = bytes;

		if (cmd->data_direction == atapi_ddir_read)
			ob_ide_pio_insw(drive, IDEREG_DATA, buffer, bc);
		else
			ob_ide_pio_outsw(drive, IDEREG_DATA, buffer, bc);

		bytes -= bc;
		buffer += bc;

		ob_ide_400ns_delay(drive);
	} while (bytes);

	if (cmd->data_direction != atapi_ddir_none)
		(void) ob_ide_wait_stat(drive, 0, BUSY_STAT, &stat);

	if (bytes)
		printf("cdb failed, bytes=%d, stat=%x\n", bytes, stat);

	return (stat & ERR_STAT) || bytes;
}

/*
 * execute a packet command, with retries if appropriate
 */
static int
ob_ide_atapi_packet(struct ide_drive *drive, struct atapi_command *cmd)
{
	int retries = 5, ret;

	if (drive->type != ide_type_atapi)
		return 1;
	if (cmd->buflen > 0xffff)
		return 1;

	/*
	 * retry loop
	 */
	do {
		ret = ob_ide_pio_packet(drive, cmd);
		if (!ret)
			break;

		/*
		 * request sense failed, bummer
		 */
		if (cmd->cdb[0] == ATAPI_REQ_SENSE)
			break;

		if (ob_ide_atapi_request_sense(drive))
			break;

		/*
		 * we know sense is valid. retry if the drive isn't ready,
		 * otherwise don't bother.
		 */
		if (cmd->sense.sense_key != ATAPI_SENSE_NOT_READY)
			break;
		/*
		 * ... except 'medium not present'
		 */
		if (cmd->sense.asc == 0x3a)
			break;

		udelay(1000000);
	} while (retries--);

	if (ret)
		ob_ide_error(drive, 0, "atapi command");

	return ret;
}

static int
ob_ide_atapi_request_sense(struct ide_drive *drive)
{
	struct atapi_command *cmd = &drive->channel->atapi_cmd;
	unsigned char old_cdb;

	/*
	 * save old cdb for debug error
	 */
	old_cdb = cmd->cdb[0];

	memset(cmd, 0, sizeof(*cmd));
	cmd->cdb[0] = ATAPI_REQ_SENSE;
	cmd->cdb[4] = 18;
	cmd->buffer = (unsigned char *) &cmd->sense;
	cmd->buflen = 18;
	cmd->data_direction = atapi_ddir_read;
	cmd->old_cdb = old_cdb;

	if (ob_ide_atapi_packet(drive, cmd))
		return 1;

	cmd->sense_valid = 1;
	return 0;
}

/*
 * make sure drive is ready and media loaded
 */
static int
ob_ide_atapi_drive_ready(struct ide_drive *drive)
{
	struct atapi_command *cmd = &drive->channel->atapi_cmd;
	struct atapi_capacity cap;

	/*
	 * Test Unit Ready is like a ping
	 */
	memset(cmd, 0, sizeof(*cmd));
	cmd->cdb[0] = ATAPI_TUR;

	if (ob_ide_atapi_packet(drive, cmd)) {
		printf("%d: TUR failed\n", drive->nr);
		return 1;
	}

	/*
	 * don't force load of tray (bit 2 in byte 4 of cdb), it's
	 * annoying and we don't want to deal with errors from drives
	 * that cannot do it
	 */
	memset(cmd, 0, sizeof(*cmd));
	cmd->cdb[0] = ATAPI_START_STOP_UNIT;
	cmd->cdb[4] = 0x01;

	if (ob_ide_atapi_packet(drive, cmd)) {
		printf("%d: START_STOP unit failed\n", drive->nr);
		return 1;
	}

	/*
	 * finally, get capacity and block size
	 */
	memset(cmd, 0, sizeof(*cmd));
	memset(&cap, 0, sizeof(cap));

	cmd->cdb[0] = ATAPI_READ_CAPACITY;
	cmd->buffer = (unsigned char *) &cap;
	cmd->buflen = sizeof(cap);
	cmd->data_direction = atapi_ddir_read;

	if (ob_ide_atapi_packet(drive, cmd)) {
		drive->sectors = 0x1fffff;
		drive->bs = 2048;
		return 1;
	}

	drive->sectors = __be32_to_cpu(cap.lba) + 1;
	drive->bs = __be32_to_cpu(cap.block_size);
	return 0;
}

/*
 * read from an atapi device, using READ_10
 */
static int
ob_ide_read_atapi(struct ide_drive *drive, unsigned long long block, unsigned char *buf,
		  unsigned int sectors)
{
	struct atapi_command *cmd = &drive->channel->atapi_cmd;

	if (ob_ide_atapi_drive_ready(drive))
		return 1;

	if (drive->bs == 2048) {
		if (((block & 3) != 0) || ((sectors & 3) != 0)) {
			printf("ob_ide_read_atapi: unaligned atapi access: %x blocks, starting from %x\n", sectors, block);
		}
		block >>= 2;
		sectors >>= 2;
	}

	memset(cmd, 0, sizeof(*cmd));

	/*
	 * READ_10 should work on generally any atapi device
	 */
	cmd->cdb[0] = ATAPI_READ_10;
	cmd->cdb[2] = (block >> 24) & 0xff;
	cmd->cdb[3] = (block >> 16) & 0xff;
	cmd->cdb[4] = (block >>  8) & 0xff;
	cmd->cdb[5] = block & 0xff;
	cmd->cdb[7] = (sectors >> 8) & 0xff;
	cmd->cdb[8] = sectors & 0xff;

	cmd->buffer = buf;
	cmd->buflen = sectors * 2048;
	cmd->data_direction = atapi_ddir_read;

	return ob_ide_atapi_packet(drive, cmd);
}

static int
ob_ide_read_ata_chs(struct ide_drive *drive, unsigned long long block,
		    unsigned char *buf, unsigned int sectors)
{
	struct ata_command *cmd = &drive->channel->ata_cmd;
	unsigned int track = (block / drive->sect);
	unsigned int sect = (block % drive->sect) + 1;
	unsigned int head = (track % drive->head);
	unsigned int cyl = (track / drive->head);
	struct ata_sector ata_sector;

	/*
	 * fill in chs command to read from disk at given location
	 */
	cmd->buffer = buf;
	cmd->buflen = sectors * 512;

	ata_sector.all = sectors;
	cmd->nsector = ata_sector.low;
	cmd->sector = sect;
	cmd->lcyl = cyl;
	cmd->hcyl = cyl >> 8;
	cmd->device_head = head;

	cmd->command = WIN_READ;

	return ob_ide_pio_data_in(drive, cmd);
}

static int
ob_ide_read_ata_lba28(struct ide_drive *drive, unsigned long long block,
		      unsigned char *buf, unsigned int sectors)
{
	struct ata_command *cmd = &drive->channel->ata_cmd;

	memset(cmd, 0, sizeof(*cmd));

	/*
	 * fill in 28-bit lba command to read from disk at given location
	 */
	cmd->buffer = buf;
	cmd->buflen = sectors * 512;

	cmd->nsector = sectors;
	cmd->sector = block;
	cmd->lcyl = block >>= 8;
	cmd->hcyl = block >>= 8;
	cmd->device_head = ((block >> 8) & 0x0f);
	cmd->device_head |= (1 << 6);

	cmd->command = WIN_READ;

	return ob_ide_pio_data_in(drive, cmd);
}

static int
ob_ide_read_ata_lba48(struct ide_drive *drive, unsigned long long block,
		      unsigned char *buf, unsigned int sectors)
{
	struct ata_command *cmd = &drive->channel->ata_cmd;
	struct ata_sector ata_sector;

	memset(cmd, 0, sizeof(*cmd));

	cmd->buffer = buf;
	cmd->buflen = sectors * 512;
	ata_sector.all = sectors;

	/*
	 * we are using tasklet addressing here
	 */
	cmd->task[2] = ata_sector.low;
	cmd->task[3] = ata_sector.high;
	cmd->task[4] = block;
	cmd->task[5] = block >>  8;
	cmd->task[6] = block >> 16;
	cmd->task[7] = block >> 24;
	cmd->task[8] = (u64) block >> 32;
	cmd->task[9] = (u64) block >> 40;

	cmd->command = WIN_READ_EXT;

	ob_ide_write_tasklet(drive, cmd);

	return ob_ide_pio_data_in(drive, cmd);
}
/*
 * read 'sectors' sectors from ata device
 */
static int
ob_ide_read_ata(struct ide_drive *drive, unsigned long long block, 
		unsigned char *buf, unsigned int sectors)
{
	unsigned long long end_block = block + sectors;
	const int need_lba48 = (end_block > (1ULL << 28)) || (sectors > 255);

	if (end_block > drive->sectors)
		return 1;
	if (need_lba48 && drive->addressing != ide_lba48)
		return 1;

	/*
	 * use lba48 if we have to, otherwise use the faster lba28
	 */
	if (need_lba48)
		return ob_ide_read_ata_lba48(drive, block, buf, sectors);
	else if (drive->addressing != ide_chs)
		return ob_ide_read_ata_lba28(drive, block, buf, sectors);

	return ob_ide_read_ata_chs(drive, block, buf, sectors);
}

static int
ob_ide_read_sectors(struct ide_drive *drive, unsigned long long block,
		    unsigned char *buf, unsigned int sectors)
{
	if (!sectors)
		return 1;
	if (block + sectors > drive->sectors)
		return 1;

#ifdef CONFIG_DEBUG_IDE
		printf("ob_ide_read_sectors: block=%Ld sectors=%u\n", (unsigned long) block, sectors);
#endif

	if (drive->type == ide_type_ata)
		return ob_ide_read_ata(drive, block, buf, sectors);
	else
		return ob_ide_read_atapi(drive, block, buf, sectors);
}

/*
 * byte swap the string if necessay, and strip leading/trailing blanks
 */
static void
ob_ide_fixup_string(unsigned char *s, unsigned int len)
{
	unsigned char *p = s, *end = &s[len & ~1];

	/*
	 * if little endian arch, byte swap the string
	 */
#ifdef CONFIG_LITTLE_ENDIAN
	for (p = end ; p != s;) {
		unsigned short *pp = (unsigned short *) (p -= 2);
		*pp = __be16_to_cpu(*pp);
	}
#endif

	while (s != end && *s == ' ')
		++s;
	while (s != end && *s)
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	while (p != end)
		*p++ = '\0';
}

/*
 * it's big endian, we need to swap (if on little endian) the items we use
 */
static int
ob_ide_fixup_id(struct hd_driveid *id)
{
	ob_ide_fixup_string(id->model, 40);
	id->config = __le16_to_cpu(id->config);
	id->lba_capacity = __le32_to_cpu(id->lba_capacity);
	id->cyls = __le16_to_cpu(id->cyls);
	id->heads = __le16_to_cpu(id->heads);
	id->sectors = __le16_to_cpu(id->sectors);
	id->command_set_2 = __le16_to_cpu(id->command_set_2);
	id->cfs_enable_2 = __le16_to_cpu(id->cfs_enable_2);

	return 0;
}

static int
ob_ide_identify_drive(struct ide_drive *drive)
{
	struct ata_command *cmd = &drive->channel->ata_cmd;
	struct hd_driveid id;

	memset(cmd, 0, sizeof(*cmd));
	cmd->buffer = (unsigned char *) &id;
	cmd->buflen = 512;

	if (drive->type == ide_type_ata)
		cmd->command = WIN_IDENTIFY;
	else if (drive->type == ide_type_atapi)
		cmd->command = WIN_IDENTIFY_PACKET;
	else {
		printf("%s: called with bad device type %d\n", __FUNCTION__, drive->type);
		return 1;
	}

	if (ob_ide_pio_data_in(drive, cmd))
		return 1;

	ob_ide_fixup_id(&id);

	if (drive->type == ide_type_atapi) {
		drive->media = (id.config >> 8) & 0x1f;
		drive->sectors = 0x7fffffff;
		drive->bs = 2048;
		drive->max_sectors = 31;
	} else {
		drive->media = ide_media_disk;
		drive->sectors = id.lba_capacity;
		drive->bs = 512;
		drive->max_sectors = 255;

#ifdef CONFIG_IDE_LBA48
		if ((id.command_set_2 & 0x0400) && (id.cfs_enable_2 & 0x0400)) {
			drive->addressing = ide_lba48;
			drive->max_sectors = 65535;
		} else
#endif
		if (id.capability & 2)
			drive->addressing = ide_lba28;
		else {
			drive->addressing = ide_chs;
		}

		/* only set these in chs mode? */
		drive->cyl = id.cyls;
		drive->head = id.heads;
		drive->sect = id.sectors;
	}

	strcpy(drive->model, (char *)id.model);
	return 0;
}

/*
 * identify type of devices on channel. must have already been probed.
 */
static void
ob_ide_identify_drives(struct ide_channel *chan)
{
	struct ide_drive *drive;
	int i;

	for (i = 0; i < 2; i++) {
		drive = &chan->drives[i];

		if (!drive->present)
			continue;

		ob_ide_identify_drive(drive);
	}
}

/*
 * software reset (ATA-4, section 8.3)
 */
static void
ob_ide_software_reset(struct ide_drive *drive)
{
	struct ide_channel *chan = drive->channel;

	ob_ide_pio_writeb(drive, IDEREG_CONTROL, IDECON_NIEN | IDECON_SRST);
	ob_ide_400ns_delay(drive);
	ob_ide_pio_writeb(drive, IDEREG_CONTROL, IDECON_NIEN);
	ob_ide_400ns_delay(drive);

	/*
	 * if master is present, wait for BUSY clear
	 */
	if (chan->drives[0].present)
		ob_ide_wait_stat(drive, 0, BUSY_STAT, NULL);

	/*
	 * if slave is present, wait until it allows register access
	 */
	if (chan->drives[1].present) {
		unsigned char sectorn, sectorc;
		int timeout = 1000;

		do {
			/*
			 * select it
			 */
			ob_ide_pio_writeb(drive, IDEREG_CURRENT, IDEHEAD_DEV1);
			ob_ide_400ns_delay(drive);

			sectorn = ob_ide_pio_readb(drive, IDEREG_SECTOR);
			sectorc = ob_ide_pio_readb(drive, IDEREG_NSECTOR);

			if (sectorc == 0x01 && sectorn == 0x01)
				break;

		} while (--timeout);
	}

	/*
	 * reset done, reselect original device
	 */
	drive->channel->selected = -1;
	ob_ide_select_drive(drive);
}

/*
 * this serves as both a device check, and also to verify that the drives
 * we initially "found" are really there
 */
static void
ob_ide_device_type_check(struct ide_drive *drive)
{
	unsigned char sc, sn, cl, ch, st;

	if (ob_ide_select_drive(drive))
		return;

	sc = ob_ide_pio_readb(drive, IDEREG_NSECTOR);
	sn = ob_ide_pio_readb(drive, IDEREG_SECTOR);

	if (sc == 0x01 && sn == 0x01) {
		/*
		 * read device signature
		 */
		cl = ob_ide_pio_readb(drive, IDEREG_LCYL);
		ch = ob_ide_pio_readb(drive, IDEREG_HCYL);
		st = ob_ide_pio_readb(drive, IDEREG_STATUS);
		if (cl == 0x14 && ch == 0xeb)
			drive->type = ide_type_atapi;
		else if (cl == 0x00 && ch == 0x00 && st != 0x00)
			drive->type = ide_type_ata;
		else
			drive->present = 0;
	} else
		drive->present = 0;
}

/*
 * pure magic
 */
static void
ob_ide_device_check(struct ide_drive *drive)
{
	unsigned char sc, sn;

	/*
	 * non-existing io port should return 0xff, don't probe this
	 * channel at all then
	 */
	if (ob_ide_pio_readb(drive, IDEREG_STATUS) == 0xff) {
		drive->channel->present = 0;
		return;
	}

	if (ob_ide_select_drive(drive))
		return;

	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, 0x55);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, 0xaa);
	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, 0xaa);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, 0x55);
	ob_ide_pio_writeb(drive, IDEREG_NSECTOR, 0x55);
	ob_ide_pio_writeb(drive, IDEREG_SECTOR, 0xaa);

	sc = ob_ide_pio_readb(drive, IDEREG_NSECTOR);
	sn = ob_ide_pio_readb(drive, IDEREG_SECTOR);

	/*
	 * we _think_ the device is there, we will make sure later
	 */
	if (sc == 0x55 && sn == 0xaa) {
		drive->present = 1;
		drive->type = ide_type_unknown;
	}
}

/*
 * probe the legacy ide ports and find attached devices.
 */
static void
ob_ide_probe(struct ide_channel *chan)
{
	struct ide_drive *drive;
	int i;

	for (i = 0; i < 2; i++) {
		drive = &chan->drives[i];

		ob_ide_device_check(drive);

		/*
		 * no point in continuing
		 */
		if (!chan->present)
			break;

		if (!drive->present)
			continue;

		/*
		 * select and reset device
		 */
		if (ob_ide_select_drive(drive))
			continue;

		ob_ide_software_reset(drive);

		ob_ide_device_type_check(drive);
	}
}

int
ob_ide_read_blocks(struct ide_drive *drive, int n, u32 blk, unsigned char* dest)
{
	int cnt = n;
	while (n) {
		int len = n;
		if (len > drive->max_sectors)
			len = drive->max_sectors;
			
		debug("reading %d sectors from blk %d\n",len, blk);
		if (ob_ide_read_sectors(drive, blk, dest, len)) {
			return n-1;
		}
		debug("done\n");

		dest += len * drive->bs;
		n -= len;
		blk += len;
	}

	return (cnt);
}

#ifdef CONFIG_SUPPORT_PCI
static int pci_find_ata_device_on_bus(int bus, pcidev_t * dev, int *index, int sata, int pata)
{
	int slot, func;
	u32 val;
	unsigned char hdr;
	u32 class;

        for (slot = 0; slot < 0x20; slot++) {
		for (func = 0; func < 8; func++) {
			pcidev_t currdev = PCI_DEV(bus, slot, func);

			val = pci_read_config32(currdev, REG_VENDOR_ID);

			if (val == 0xffffffff || val == 0x00000000 ||
			    val == 0x0000ffff || val == 0xffff0000)
				continue;

			if (val == 0xac8f104c) {
				debug("Skipping TI bridge\n");
				continue;
			}

			class = pci_read_config16(currdev, 0x0a);
			debug("%02x:%02x.%02x [%04x:%04x] class %04x\n",
				bus, slot, func, val & 0xffff, val >> 16, class);

			if ((sata && class == 0x180) || (pata && class == 0x101)) {
				if (*index == 0) {
					*dev = currdev;
					return 1;
				}
				(*index)--;
			}

			hdr = pci_read_config8(currdev, REG_HEADER_TYPE) & 0x7f;

			if (hdr == HEADER_TYPE_BRIDGE || hdr == HEADER_TYPE_CARDBUS) {
				unsigned int new_bus;
				new_bus = (pci_read_config32(currdev, REG_PRIMARY_BUS) >> 8) & 0xff;
				if (new_bus == 0) {
					debug("Misconfigured bridge at %02x:%02x.%02x skipped.\n",
						bus, slot, func);
					continue;
				}
				if (pci_find_ata_device_on_bus(new_bus, dev, index, sata, pata))
					return 1;
			}
		}
	}

	return 0;
}

int pci_find_ata_device(pcidev_t *dev, int *index, int sata, int pata)
{
	debug(" Scanning for:%s%s\n", sata?" SATA":"", pata?" PATA":"");
	return pci_find_ata_device_on_bus(0, dev, index, sata, pata);
}
#endif


static void fixupregs(struct ide_channel *chan)
{
	int i;
	for (i = 1; i < 8; i++)
		chan->io_regs[i] = chan->io_regs[0] + i;
	chan->io_regs[9] = chan->io_regs[8] + 1;
}

static int find_ide_controller_compat(struct ide_channel *chan, int index)
{
#ifdef CONFIG_SUPPORT_PCI
	int skip, i, pci_index = index / 2;
	pcidev_t dev;
#else
	if (index >= IDE_MAX_CHANNELS)
		return -1;
#endif
#ifdef CONFIG_PCMCIA_CF
	if (index == 2) {
		chan->io_regs[0] = 0x1e0;
		chan->io_regs[8] = 0x1ec;
		fixupregs(chan);
		return 0;
	}
#endif
#ifdef CONFIG_SUPPORT_PCI
	/* skip any SATA and PATA PCI controllers in native mode */
	for (skip = i = 0; i < pci_index && index; i++) {
		int devidx = i;
		/* look for i:th ata (IDE/other storage really) device */
		if(!pci_find_ata_device(&dev, &devidx, 1, 1))
			break;
		/* only IDE can be in compat mode so skip all others */
		if (pci_read_config16(dev, 0xa) != 0x0101) {
			/* other storage (SATA) always has two channels */
			skip += 2;
			continue;
		}
		/* primary in native mode? then skip it. */
		if (1 == (pci_read_config8(dev, 0x09) & 1))
			skip++;
		/* secondary in native mode? then skip it. */
		if (index && 4 == (pci_read_config8(dev, 0x09)  & 4))
			skip++;
	}
	index = skip <= index ? index - skip : 0;
	debug("skipping %d native PCI controllers, new index=%d\n", skip, index);
	if (index >= IDE_MAX_CHANNELS)
		return -1;
#endif
	chan->io_regs[0] = io_ports[index];
	chan->io_regs[8] = ctl_ports[index];
	fixupregs(chan);
	return 0;
}

#ifdef CONFIG_SUPPORT_PCI
static int find_ide_controller(struct ide_channel *chan, int chan_index)
{
	int pci_index;
	pcidev_t dev;
	unsigned int mask;
	u8 prog_if;
	u16 vendor, device, devclass;

	/* A PCI IDE controller has two channels (pri, sec) */
	pci_index = chan_index >> 1;

	/* Find a IDE storage class device */

	if (!pci_find_ata_device(&dev, &pci_index, 1, 0)) { // S-ATA first
		pci_index = chan_index >> 1;
		if (!pci_find_ata_device(&dev, &pci_index, 0, 1)) { // P-ATA second
			debug("PCI IDE #%d not found\n", chan_index >> 1);
			return -1;
		}
	}
	
	vendor = pci_read_config16(dev, 0);
	device = pci_read_config16(dev, 2);
	prog_if = pci_read_config8(dev, 9);
	devclass = pci_read_config16(dev, 0x0a);

	debug("found PCI IDE controller %04x:%04x prog_if=%#x\n",
			vendor, device, prog_if);

	/* See how this controller is configured */
	mask = (chan_index & 1) ? 4 : 1;
	debug("%s channel: ", (chan_index & 1) ? "secondary" : "primary");
	if ((prog_if & mask) || (devclass != 0x0101)) {
		debug("native PCI mode\n");
		if ((chan_index & 1) == 0) {
			/* Primary channel */
			chan->io_regs[0] = pci_read_resource(dev, 0); // io ports
			chan->io_regs[8] = pci_read_resource(dev, 1); // ctrl ports
		} else {
			/* Secondary channel */
			chan->io_regs[0] = pci_read_resource(dev, 2); // io ports
			chan->io_regs[8] = pci_read_resource(dev, 3); // ctrl ports
		}
		chan->io_regs[0] &= ~3;
		chan->io_regs[8] &= ~3;
		fixupregs(chan);
	} else {
		debug("compatibility mode\n");
		if (find_ide_controller_compat(chan, chan_index) != 0)
			return -1;
	}
	return 0;
}
#else /* !CONFIG_SUPPORT_PCI */
# define find_ide_controller find_ide_controller_compat
#endif
//int ob_ide_init(int (*func)(struct ide_drive*))
int ob_ide_init(int drive)
{
	int j;

	struct ide_channel *chan;
	int chan_index;

	if (drive >= IDE_MAX_DRIVES) {
		printf("Unsupported drive number\n");
		return -1;
	}

	/* A controller has two drives (master, slave) */
	chan_index = drive >> 1;

	chan = &ob_ide_channels[chan_index];
	if (chan->present == 0) {
		if (find_ide_controller(chan, chan_index) != 0) {
			printf("IDE channel %d not found\n", chan_index);
			return -1;
		}

		chan->obide_inb = ob_ide_inb;
		chan->obide_insw = ob_ide_insw;
		chan->obide_outb = ob_ide_outb;
		chan->obide_outsw = ob_ide_outsw;

		chan->selected = -1;
		chan->mmio = 0;

		/*
		 * assume it's there, if not io port dead check will clear
		 */
		chan->present = 1;

		for (j = 0; j < 2; j++) {
			chan->drives[j].present = 0;
			chan->drives[j].unit = j;
			chan->drives[j].channel = chan;
			/* init with a decent value */
			chan->drives[j].bs = 512;

			chan->drives[j].nr = chan_index * 2 + j;
		}

		ob_ide_probe(chan);

		if (!chan->present)
			return -1;

		ob_ide_identify_drives(chan);

		printf("ata-%d: [io ports 0x%x-0x%x,0x%x]\n", chan_index,
				chan->io_regs[0], chan->io_regs[0] + 7,
				chan->io_regs[8]);

		for (j = 0; j < 2; j++) {
			struct ide_drive *drive = &chan->drives[j];
			char *media = "UNKNOWN";
			if (!drive->present)
				continue;
			printf("    drive%d [ATA%s ", j, drive->type == ide_type_atapi ? "PI" : "");
			switch (drive->media) {
			case ide_media_floppy:
				media = "floppy";
				break;
			case ide_media_cdrom:
				media = "cdrom";
				break;
			case ide_media_optical:
				media = "mo";
				break;
			case ide_media_disk:
				media = "disk";
				break;
			}
			printf("%s]: %s\n", media, drive->model);
		}

	}

	return 0;
}

int ide_probe(int drive)
{
	struct ide_drive *curr_drive = & ob_ide_channels[drive / 2].drives[drive % 2];
	ob_ide_init(drive);

	if (!curr_drive->present)
		return -1;

	return 0;
	
}

int ide_probe_verbose(int drive)
{
	struct ide_drive *curr_drive = & ob_ide_channels[drive / 2].drives[drive % 2];
	char *media = "UNKNOWN";

	ob_ide_init(drive);

	if (!curr_drive->present) {
		return -1;
	}
	printf("ata%d %s ", drive / 2, (drive % 2)?"slave ":"master");


	printf("[ATA%s ", curr_drive->type == ide_type_atapi ? "PI" : "");
	switch (curr_drive->media) {
	case ide_media_floppy:
		media = "Floppy";
		break;
	case ide_media_cdrom:
		media = "CDROM/DVD";
		break;
	case ide_media_optical:
		media = "MO";
		break;
	case ide_media_disk:
		media = "DISK";
		break;
	}
	printf("%s]: %s\n", media, curr_drive->model);

	return 0;
}

int ide_read(int drive, sector_t sector, void *buffer)
{
	int ret;
	ret = ob_ide_read_blocks(&ob_ide_channels[drive / 2].drives[drive % 2], 1, sector, buffer);
	if (ret!=1) { // right now only one block at a time.. bummer
		return -1;
	}
	return 0;
}

int ide_read_blocks(int drive, sector_t sector, int count, void *buffer)
{
	int ret;
	ret = ob_ide_read_blocks(&ob_ide_channels[drive / 2].drives[drive % 2],
			count, sector, buffer);
	if (ret!=count) { // right now only one block at a time.. bummer
		return -1;
	}
	return 0;
}


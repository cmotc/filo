/* char_io.c - basic console input and output */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2004  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libpayload.h>
#include <config.h>
#include <version.h>
#include <grub/shared.h>
#include <grub/term.h>
#include <grub/serial.h>

char *err_list[] =
{
  [ERR_NONE] = 0,
  [ERR_BAD_ARGUMENT] = "Invalid argument",
  [ERR_BAD_FILENAME] = "Filename must be either an absolute pathname or blocklist",
  [ERR_BAD_FILETYPE] = "Bad file or directory type",
  [ERR_BAD_GZIP_DATA] = "Bad or corrupt data while decompressing file",
  [ERR_BAD_GZIP_HEADER] = "Bad or incompatible header in compressed file",
  [ERR_BAD_PART_TABLE] = "Partition table invalid or corrupt",
  [ERR_BAD_VERSION] = "Mismatched or corrupt version of stage1/stage2",
  [ERR_BELOW_1MB] = "Loading below 1MB is not supported",
  [ERR_BOOT_COMMAND] = "Kernel must be loaded before booting",
  [ERR_BOOT_FAILURE] = "Unknown boot failure",
  [ERR_BOOT_FEATURES] = "Unsupported Multiboot features requested",
  [ERR_DEV_FORMAT] = "Unrecognized device string",
  [ERR_DEV_NEED_INIT] = "Device not initialized yet",
  [ERR_DEV_VALUES] = "Invalid device requested",
  [ERR_EXEC_FORMAT] = "Invalid or unsupported executable format",
  [ERR_FILELENGTH] = "Filesystem compatibility error, cannot read whole file",
  [ERR_FILE_NOT_FOUND] = "File not found",
  [ERR_FSYS_CORRUPT] = "Inconsistent filesystem structure",
  [ERR_FSYS_MOUNT] = "Cannot mount selected partition",
  [ERR_GEOM] = "Selected cylinder exceeds maximum supported by BIOS",
  [ERR_NEED_LX_KERNEL] = "Linux kernel must be loaded before initrd",
  [ERR_NEED_MB_KERNEL] = "Multiboot kernel must be loaded before modules",
  [ERR_NO_DISK] = "Selected disk does not exist",
  [ERR_NO_DISK_SPACE] = "No spare sectors on the disk",
  [ERR_NO_PART] = "No such partition",
  [ERR_NUMBER_OVERFLOW] = "Overflow while parsing number",
  [ERR_NUMBER_PARSING] = "Error while parsing number",
  [ERR_OUTSIDE_PART] = "Attempt to access block outside partition",
  [ERR_PRIVILEGED] = "Must be authenticated",
  [ERR_READ] = "Disk read error",
  [ERR_SYMLINK_LOOP] = "Too many symbolic links",
  [ERR_UNALIGNED] = "File is not sector aligned",
  [ERR_UNRECOGNIZED] = "Unrecognized command",
  [ERR_WONT_FIT] = "Selected item cannot fit into memory",
  [ERR_WRITE] = "Disk write error",
};



#ifndef STAGE1_5
struct term_entry term_table[] =
  {
#if CONFIG_VGA_CONSOLE == 1 && CONFIG_PC_KEYBOARD ==1
    {
      "console",
      0,
      console_putchar,
      console_checkkey,
      console_getkey,
      console_getxy,
      console_gotoxy,
      console_cls,
      console_setcolorstate,
      console_setcolor,
      console_setcursor
    },
#endif
    {
      "serial",
      /* A serial device must be initialized.  */
      TERM_NEED_INIT,
      grub_serial_putchar,
      serial_checkkey,
      serial_getkey,
      serial_getxy,
      serial_gotoxy,
      serial_cls,
      serial_setcolorstate,
      0,
      0
    },
    /* This must be the last entry.  */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  };

/* This must be console.  */
struct term_entry *current_term = term_table;

int max_lines = 24;
int count_lines = -1;
int use_pager = 1;
#endif

void
print_error (void)
{
  if (errnum > ERR_NONE && errnum < MAX_ERR_NUM)
#ifndef STAGE1_5
    /* printf("\7\n %s\n", err_list[errnum]); */
    printf ("\nError %u: %s\n", errnum, err_list[errnum]);
#else /* STAGE1_5 */
    printf ("Error %u\n", errnum);
#endif /* STAGE1_5 */
}

char *
convert_to_ascii (char *buf, int c,...)
{
  unsigned long num = *((&c) + 1), mult = 10;
  char *ptr = buf;

#ifndef STAGE1_5
  if (c == 'x' || c == 'X')
    mult = 16;

  if ((num & 0x80000000uL) && c == 'd')
    {
      num = (~num) + 1;
      *(ptr++) = '-';
      buf++;
    }
#endif

  do
    {
      int dig = num % mult;
      *(ptr++) = ((dig > 9) ? dig + 'a' - 10 : '0' + dig);
    }
  while (num /= mult);

  /* reorder to correct direction!! */
  {
    char *ptr1 = ptr - 1;
    char *ptr2 = buf;
    while (ptr1 > ptr2)
      {
	int tmp = *ptr1;
	*ptr1 = *ptr2;
	*ptr2 = tmp;
	ptr1--;
	ptr2++;
      }
  }

  return ptr;
}

void
grub_putstr (const char *str)
{
  while (*str)
    grub_putchar (*str++);
}

void
grub_printf (const char *format,...)
{
  int *dataptr = (int *) &format;
  char c, str[16];
  
  dataptr++;

  while ((c = *(format++)) != 0)
    {
      if (c != '%')
	grub_putchar (c);
      else
	switch (c = *(format++))
	  {
#ifndef STAGE1_5
	  case 'd':
	  case 'x':
	  case 'X':
#endif
	  case 'u':
	    *convert_to_ascii (str, c, *((unsigned long *) dataptr++)) = 0;
	    grub_putstr (str);
	    break;

#ifndef STAGE1_5
	  case 'c':
	    grub_putchar ((*(dataptr++)) & 0xff);
	    break;

	  case 's':
	    grub_putstr ((char *) *(dataptr++));
	    break;
#endif
	  }
    }
}

#ifndef STAGE1_5
void
init_page (void)
{
  cls ();
  grub_printf ("\n                                  %s %s\n\n", PROGRAM_NAME, PROGRAM_VERSION);
}

/* The number of the history entries.  */
static int num_history = 0;

/* Get the NOth history. If NO is less than zero or greater than or
   equal to NUM_HISTORY, return NULL. Otherwise return a valid string.  */
static char *
get_history (int no)
{
  if (no < 0 || no >= num_history)
    return 0;

  return (char *) HISTORY_BUF + MAX_CMDLINE * no;
}

/* Add CMDLINE to the history buffer.  */
static void
add_history (const char *cmdline, int no)
{
  grub_memmove ((char *) HISTORY_BUF + MAX_CMDLINE * (no + 1),
		(char *) HISTORY_BUF + MAX_CMDLINE * no,
		MAX_CMDLINE * (num_history - no));
  grub_strcpy ((char *) HISTORY_BUF + MAX_CMDLINE * no, cmdline);
  if (num_history < HISTORY_SIZE)
    num_history++;
}

static int
real_get_cmdline (char *prompt, char *cmdline, int maxlen,
		  int echo_char, int readline)
{
  /* This is a rather complicated function. So explain the concept.
     
     A command-line consists of ``section''s. A section is a part of the
     line which may be displayed on the screen, but a section is never
     displayed with another section simultaneously.

     Each section is basically 77 or less characters, but the exception
     is the first section, which is 78 or less characters, because the
     starting point is special. See below.

     The first section contains a prompt and a command-line (or the
     first part of a command-line when it is too long to be fit in the
     screen). So, in the first section, the number of command-line
     characters displayed is 78 minus the length of the prompt (or
     less). If the command-line has more characters, `>' is put at the
     position 78 (zero-origin), to inform the user of the hidden
     characters.

     Other sections always have `<' at the first position, since there
     is absolutely a section before each section. If there is a section
     after another section, this section consists of 77 characters and
     `>' at the last position. The last section has 77 or less
     characters and doesn't have `>'.

     Each section other than the last shares some characters with the
     previous section. This region is called ``margin''. If the cursor
     is put at the magin which is shared by the first section and the
     second, the first section is displayed. Otherwise, a displayed
     section is switched to another section, only if the cursor is put
     outside that section.  */

  /* XXX: These should be defined in shared.h, but I leave these here,
     until this code is freezed.  */
#define CMDLINE_WIDTH	78
#define CMDLINE_MARGIN	10
  
  int xpos, lpos, c, section;
  /* The length of PROMPT.  */
  int plen;
  /* The length of the command-line.  */
  int llen;
  /* The index for the history.  */
  int history = -1;
  /* The working buffer for the command-line.  */
  char *buf = (char *) CMDLINE_BUF;
  /* The kill buffer.  */
  char *kill_buf = (char *) KILL_BUF;
  
  /* Nested function definitions for code simplicity.  */

  /* The forward declarations of nested functions are prefixed
     with `auto'.  */
  auto void cl_refresh (int full, int len);
  auto void cl_backward (int count);
  auto void cl_forward (int count);
  auto void cl_insert (const char *str);
  auto void cl_delete (int count);
  auto void cl_init (void);
  
  /* Move the cursor backward.  */
  void cl_backward (int count)
    {
      lpos -= count;
      
      /* If the cursor is in the first section, display the first section
	 instead of the second.  */
      if (section == 1 && plen + lpos < CMDLINE_WIDTH)
	cl_refresh (1, 0);
      else if (xpos - count < 1)
	cl_refresh (1, 0);
      else
	{
	  xpos -= count;

	  if (current_term->flags & TERM_DUMB)
	    {
	      int i;
	      
	      for (i = 0; i < count; i++)
		grub_putchar ('\b');
	    }
	  else
	    gotoxy (xpos, getxy () & 0xFF);
	}
    }

  /* Move the cursor forward.  */
  void cl_forward (int count)
    {
      lpos += count;

      /* If the cursor goes outside, scroll the screen to the right.  */
      if (xpos + count >= CMDLINE_WIDTH)
	cl_refresh (1, 0);
      else
	{
	  xpos += count;

	  if (current_term->flags & TERM_DUMB)
	    {
	      int i;
	      
	      for (i = lpos - count; i < lpos; i++)
		{
		  if (! echo_char)
		    grub_putchar (buf[i]);
		  else
		    grub_putchar (echo_char);
		}
	    }
	  else
	    gotoxy (xpos, getxy () & 0xFF);
	}
    }

  /* Refresh the screen. If FULL is true, redraw the full line, otherwise,
     only LEN characters from LPOS.  */
  void cl_refresh (int full, int len)
    {
      int i;
      int start;
      int pos = xpos;
      
      if (full)
	{
	  /* Recompute the section number.  */
	  if (lpos + plen < CMDLINE_WIDTH)
	    section = 0;
	  else
	    section = ((lpos + plen - CMDLINE_WIDTH)
		       / (CMDLINE_WIDTH - 1 - CMDLINE_MARGIN) + 1);

	  /* From the start to the end.  */
	  len = CMDLINE_WIDTH;
	  pos = 0;
	  grub_putchar ('\r');

	  /* If SECTION is the first section, print the prompt, otherwise,
	     print `<'.  */
	  if (section == 0)
	    {
	      grub_printf ("%s", prompt);
	      len -= plen;
	      pos += plen;
	    }
	  else
	    {
	      grub_putchar ('<');
	      len--;
	      pos++;
	    }
	}

      /* Compute the index to start writing BUF and the resulting position
	 on the screen.  */
      if (section == 0)
	{
	  int offset = 0;
	  
	  if (! full)
	    offset = xpos - plen;
	  
	  start = 0;
	  xpos = lpos + plen;
	  start += offset;
	}
      else
	{
	  int offset = 0;
	  
	  if (! full)
	    offset = xpos - 1;
	  
	  start = ((section - 1) * (CMDLINE_WIDTH - 1 - CMDLINE_MARGIN)
		   + CMDLINE_WIDTH - plen - CMDLINE_MARGIN);
	  xpos = lpos + 1 - start;
	  start += offset;
	}

      /* Print BUF. If ECHO_CHAR is not zero, put it instead.  */
      for (i = start; i < start + len && i < llen; i++)
	{
	  if (! echo_char)
	    grub_putchar (buf[i]);
	  else
	    grub_putchar (echo_char);

	  pos++;
	}
      
      // FIXME
#if 1
      /* Fill up the rest of the line with spaces.  */
      for (; i < start + len; i++)
	{
	  grub_putchar (' ');
	  pos++;
	}
      /* If the cursor is at the last position, put `>' or a space,
	 depending on if there are more characters in BUF.  */
      if (pos == CMDLINE_WIDTH)
	{
	  if (start + len < llen)
	    grub_putchar ('>');
	  else
	    grub_putchar (' ');
	  
	  pos++;
	}
      
      /* Back to XPOS.  */
      if (current_term->flags & TERM_DUMB)
	{
	  for (i = 0; i < pos - xpos; i++)
	    grub_putchar ('\b');
	}
      else
	gotoxy (xpos, getxy () & 0xFF);

#endif
    }

  /* Initialize the command-line.  */
  void cl_init (void)
    {
#ifdef CONFIG_NEWLINE_BEFORE_EACH_PROMPT
      /* Distinguish us from other lines and error messages!  */
      grub_putchar ('\n');
#endif

      /* Print full line and set position here.  */
      cl_refresh (1, 0);
    }

  /* Insert STR to BUF.  */
  void cl_insert (const char *str)
    {
      int l = grub_strlen (str);

      if (llen + l < maxlen)
	{
	  if (lpos == llen)
	    grub_memmove (buf + lpos, str, l + 1);
	  else
	    {
	      grub_memmove (buf + lpos + l, buf + lpos, llen - lpos + 1);
	      grub_memmove (buf + lpos, str, l);
	    }
	  
	  llen += l;
	  lpos += l;
	  if (xpos + l >= CMDLINE_WIDTH)
	    cl_refresh (1, 0);
	  else if (xpos + l + llen - lpos > CMDLINE_WIDTH)
	    cl_refresh (0, CMDLINE_WIDTH - xpos);
	  else
	    cl_refresh (0, l + llen - lpos);
	}
    }

  /* Delete COUNT characters in BUF.  */
  void cl_delete (int count)
    {
      grub_memmove (buf + lpos, buf + lpos + count, llen - count + 1);
      llen -= count;
      
      if (xpos + llen + count - lpos > CMDLINE_WIDTH)
	cl_refresh (0, CMDLINE_WIDTH - xpos);
      else
	cl_refresh (0, llen + count - lpos);
    }

  plen = grub_strlen (prompt);
  llen = grub_strlen (cmdline);

  if (maxlen > MAX_CMDLINE)
    {
      maxlen = MAX_CMDLINE;
      if (llen >= MAX_CMDLINE)
	{
	  llen = MAX_CMDLINE - 1;
	  cmdline[MAX_CMDLINE] = 0;
	}
    }
  lpos = llen;
  grub_strcpy (buf, cmdline);

  cl_init ();

  while ((c = ASCII_CHAR (getkey ())) != '\n' && c != '\r')
    {
      /* If READLINE is non-zero, handle readline-like key bindings.  */
      if (readline)
	{
	  switch (c)
	    {
	    case 9:		/* TAB lists completions */
	      {
		int i;
		/* POS points to the first space after a command.  */
		int pos = 0;
		int ret;
		char *completion_buffer = (char *) COMPLETION_BUF;
		int equal_pos = -1;
		int is_filename;

		/* Find the first word.  */
		while (buf[pos] == ' ')
		  pos++;
		while (buf[pos] && buf[pos] != '=' && buf[pos] != ' ')
		  pos++;

		is_filename = (lpos > pos);

		/* Find the position of the equal character after a
		   command, and replace it with a space.  */
		for (i = pos; buf[i] && buf[i] != ' '; i++)
		  if (buf[i] == '=')
		    {
		      equal_pos = i;
		      buf[i] = ' ';
		      break;
		    }

		/* Find the position of the first character in this
		   word.  */
		for (i = lpos; i > 0 && buf[i - 1] != ' '; i--)
		  ;

		/* Invalidate the cache, because the user may exchange
		   removable disks.  */
		buf_drive = -1;

		/* Copy this word to COMPLETION_BUFFER and do the
		   completion.  */
		grub_memmove (completion_buffer, buf + i, lpos - i);
		completion_buffer[lpos - i] = 0;
		ret = print_completions (is_filename, 1);
		errnum = ERR_NONE;

		if (ret >= 0)
		  {
		    /* Found, so insert COMPLETION_BUFFER.  */
		    cl_insert (completion_buffer + lpos - i);

		    if (ret > 0)
		      {
			/* There are more than one candidates, so print
			   the list.  */
			grub_putchar ('\n');
			print_completions (is_filename, 0);
			errnum = ERR_NONE;
		      }
		  }

		/* Restore the command-line.  */
		if (equal_pos >= 0)
		  buf[equal_pos] = '=';

		if (ret)
		  cl_init ();
	      }
	      
	      break;
	    case 1:		/* C-a go to beginning of line */
	      cl_backward (lpos);
	      break;
	    case 5:		/* C-e go to end of line */
	      cl_forward (llen - lpos);
	      break;
	    case 6:		/* C-f forward one character */
	      if (lpos < llen)
		cl_forward (1);
	      break;
	    case 2:		/* C-b backward one character */
	      if (lpos > 0)
		cl_backward (1);
	      break;
	    case 21:		/* C-u kill to beginning of line */
	      if (lpos == 0)
		break;
	      /* Copy the string being deleted to KILL_BUF.  */
	      grub_memmove (kill_buf, buf, lpos);
	      kill_buf[lpos] = 0;
	      {
		/* XXX: Not very clever.  */
		
		int count = lpos;
		
		cl_backward (lpos);
		cl_delete (count);
	      }
	      break;
	    case 11:		/* C-k kill to end of line */
	      if (lpos == llen)
		break;
	      /* Copy the string being deleted to KILL_BUF.  */
	      grub_memmove (kill_buf, buf + lpos, llen - lpos + 1);
	      cl_delete (llen - lpos);
	      break;
	    case 25:		/* C-y yank the kill buffer */
	      cl_insert (kill_buf);
	      break;
	    case 16:		/* C-p fetch the previous command */
	      {
		char *p;

		if (history < 0)
		  /* Save the working buffer.  */
		  grub_strcpy (cmdline, buf);
		else if (grub_strcmp (get_history (history), buf) != 0)
		  /* If BUF is modified, add it into the history list.  */
		  add_history (buf, history);

		history++;
		p = get_history (history);
		if (! p)
		  {
		    history--;
		    break;
		  }

		grub_strcpy (buf, p);
		llen = grub_strlen (buf);
		lpos = llen;
		cl_refresh (1, 0);
	      }
	      break;
	    case 14:		/* C-n fetch the next command */
	      {
		char *p;

		if (history < 0)
		  {
		    break;
		  }
		else if (grub_strcmp (get_history (history), buf) != 0)
		  /* If BUF is modified, add it into the history list.  */
		  add_history (buf, history);

		history--;
		p = get_history (history);
		if (! p)
		  p = cmdline;

		grub_strcpy (buf, p);
		llen = grub_strlen (buf);
		lpos = llen;
		cl_refresh (1, 0);
	      }
	      break;
	    }
	}

      /* ESC, C-d and C-h are always handled. Actually C-d is not
	 functional if READLINE is zero, as the cursor cannot go
	 backward, but that's ok.  */
      switch (c)
	{
	case 27:	/* ESC immediately return 1 */
	  return 1;
	case 4:		/* C-d delete character under cursor */
	  if (lpos == llen)
	    break;
	  cl_delete (1);
	  break;
	case 8:		/* C-h backspace */
	case 127:	/* also backspace */
	  if (lpos > 0)
	    {
	      cl_backward (1);
	      cl_delete (1);
	    }
	  break;
	default:		/* insert printable character into line */
	  if (c >= ' ' && c <= '~')
	    {
	      char str[2];

	      str[0] = c;
	      str[1] = 0;
	      cl_insert (str);
	    }
	}
    }

  grub_putchar ('\n');

  /* If ECHO_CHAR is NUL, remove the leading spaces.  */
  lpos = 0;
  if (! echo_char)
    while (buf[lpos] == ' ')
      lpos++;

  /* Copy the working buffer to CMDLINE.  */
  grub_memmove (cmdline, buf + lpos, llen - lpos + 1);

  /* If the readline-like feature is turned on and CMDLINE is not
     empty, add it into the history list.  */
  if (readline && lpos < llen)
    add_history (cmdline, 0);

  return 0;
}

/* Don't use this with a MAXLEN greater than 1600 or so!  The problem
   is that GET_CMDLINE depends on the everything fitting on the screen
   at once.  So, the whole screen is about 2000 characters, minus the
   PROMPT, and space for error and status lines, etc.  MAXLEN must be
   at least 1, and PROMPT and CMDLINE must be valid strings (not NULL
   or zero-length).

   If ECHO_CHAR is nonzero, echo it instead of the typed character. */
int
get_cmdline (char *prompt, char *cmdline, int maxlen,
	     int echo_char, int readline)
{
  int old_cursor;
  int ret;

  old_cursor = setcursor (1);
  
  /* Because it is hard to deal with different conditions simultaneously,
     less functional cases are handled here. Assume that TERM_NO_ECHO
     implies TERM_NO_EDIT.  */
  if (current_term->flags & (TERM_NO_ECHO | TERM_NO_EDIT))
    {
      char *p = cmdline;
      int c;
      
      /* Make sure that MAXLEN is not too large.  */
      if (maxlen > MAX_CMDLINE)
	maxlen = MAX_CMDLINE;

      /* Print only the prompt. The contents of CMDLINE is simply discarded,
	 even if it is not empty.  */
      grub_printf ("%s", prompt);

      /* Gather characters until a newline is gotten.  */
      while ((c = ASCII_CHAR (getkey ())) != '\n' && c != '\r')
	{
	  /* Return immediately if ESC is pressed.  */
	  if (c == 27)
	    {
	      setcursor (old_cursor);
	      return 1;
	    }

	  /* Printable characters are added into CMDLINE.  */
	  if (c >= ' ' && c <= '~')
	    {
	      if (! (current_term->flags & TERM_NO_ECHO))
		grub_putchar (c);

	      /* Preceding space characters must be ignored.  */
	      if (c != ' ' || p != cmdline)
		*p++ = c;
	    }
	}

      *p = 0;

      if (! (current_term->flags & TERM_NO_ECHO))
	grub_putchar ('\n');

      setcursor (old_cursor);
      return 0;
    }

  /* Complicated features are left to real_get_cmdline.  */
  ret = real_get_cmdline (prompt, cmdline, maxlen, echo_char, readline);
  setcursor (old_cursor);
  return ret;
}

int
safe_parse_maxint (char **str_ptr, int *myint_ptr)
{
  char *ptr = *str_ptr;
  int myint = 0;
  int mult = 10, found = 0;

  /*
   *  Is this a hex number?
   */
  if (*ptr == '0' && tolower (*(ptr + 1)) == 'x')
    {
      ptr += 2;
      mult = 16;
    }

  while (1)
    {
      /* A bit tricky. This below makes use of the equivalence:
	 (A >= B && A <= C) <=> ((A - B) <= (C - B))
	 when C > B and A is unsigned.  */
      unsigned int digit;

      digit = tolower (*ptr) - '0';
      if (digit > 9)
	{
	  digit -= 'a' - '0';
	  if (mult == 10 || digit > 5)
	    break;
	  digit += 10;
	}

      found = 1;
      if (myint > ((MAXINT - digit) / mult))
	{
	  errnum = ERR_NUMBER_OVERFLOW;
	  return 0;
	}
      myint = (myint * mult) + digit;
      ptr++;
    }

  if (!found)
    {
      errnum = ERR_NUMBER_PARSING;
      return 0;
    }

  *str_ptr = ptr;
  *myint_ptr = myint;

  return 1;
}
#endif /* STAGE1_5 */

/* Wait for a keypress and return its code.  */
int
getkey (void)
{
  return current_term->getkey ();
}

/* Check if a key code is available.  */
int
checkkey (void)
{
  return current_term->checkkey ();
}

/* Display an ASCII character.  */
void
grub_putchar (int c)
{
  if (c == '\n')
    grub_putchar ('\r');
  else if (c == '\t' && current_term->getxy)
    {
      int n;
      
      n = 8 - ((current_term->getxy () >> 8) & 3);
      while (n--)
	grub_putchar (' ');
      
      return;
    }
  
  if (c == '\n')
    {
      /* Internal `more'-like feature.  */
      if (count_lines >= 0)
	{
	  count_lines++;
	  if (count_lines >= max_lines - 2)
	    {
	      int tmp;
	      
	      /* It's important to disable the feature temporarily, because
		 the following grub_printf call will print newlines.  */
	      count_lines = -1;

	      if (current_term->setcolorstate)
		current_term->setcolorstate (COLOR_STATE_HIGHLIGHT);
	      
	      grub_printf ("\n[Hit return to continue]");

	      if (current_term->setcolorstate)
		current_term->setcolorstate (COLOR_STATE_NORMAL);
	      
	      do
		{
		  tmp = ASCII_CHAR (getkey ());
		}
	      while (tmp != '\n' && tmp != '\r');
	      grub_printf ("\r                        \r");
	      
	      /* Restart to count lines.  */
	      count_lines = 0;
	      return;
	    }
	}
    }

  current_term->putchar (c);
  
}

void
gotoxy (int x, int y)
{
  current_term->gotoxy (x, y);
}

int
getxy (void)
{
  return current_term->getxy ();
}

void
cls (void)
{
  /* If the terminal is dumb, there is no way to clean the terminal.  */
  if (current_term->flags & TERM_DUMB)
    grub_putchar ('\n');
  else
    current_term->cls ();
}

int
setcursor (int on)
{
  if (current_term->setcursor)
    return current_term->setcursor (on);

  return 1;
}

int
substring (const char *s1, const char *s2)
{
  while (*s1 == *s2)
    {
      /* The strings match exactly. */
      if (! *(s1++))
        return 0;
      s2 ++;
    }

  /* S1 is a substring of S2. */
  if (*s1 == 0)
    return -1;

  /* S1 isn't a substring. */
  return 1;
}




/* Terminate the string STR with NUL.  */
int
nul_terminate (char *str)
{
  int ch;

  while (*str && ! grub_isspace (*str))
    str++;

  ch = *str;
  *str = 0;
  return ch;
}

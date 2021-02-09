/*
 * Error reporting routines for Simple Profiler.
 * Copyright (C) 2020  TAKAI Kousuke
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As additional permission under GNU GPL version 3 section 7,
 * you may dynamically link this program into independent programs,
 * regardless of the license terms of these independent programs,
 * provided that you also meet the terms and conditions of the license
 * of those programs.  An independent program is a program which is not
 * derived from or based on this program.  If you modify this program,
 * you may extend this exception to your version of the program, but
 * you are not obligated to do so.  If you do not wish to do so,
 * delete this exception statement from your version.
 */

/*
 * eprintf - Simple error reporter.
 *
 * Output to standard error with following format:
 *	<PROGRAM NAME> (simpleprof.so): <TAG>: <FMT>\n
 *
 * Minimal printf-like format is supported.  Currently supported conversions:
 *	%u, %d	with optional +, - or ' ' (space) flag and/or l/z/t modifier
 *	%x, %X	with optional # flag and/or l/z/t modifier
 *	%s	with optional # flag (*) and/or precision
 *	%%
 *
 * (*) Non-standard %#s conversion is to convert string to C literal format
 *     enclosed in double quotes, where control or 8-bit characters are
 *     encoded with escape sequences.
 *
 * This function outputs whole formatted output atomically with a single
 * system call (writev) so that messages on standard error will not
 * intermingled with messages from other processes.
 *
 * This implementation has a limit on the total number of %-specifiers
 * and strings of ordinary characters.  Currently this is 27 (= NIOV_MAX - 5).
 *
 * Idea is borrowed from glibc's _dl_debug_vdprintf.
 */

#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <alloca.h>
#include <stddef.h>
#include <sys/uio.h>

static size_t
my_strvis (char *dst, const char *src, _Bool binary, size_t maxlen)
{
  size_t n = 0;

  if (!src)
    {
      if (dst)
	strcpy(dst, "NULL");
      return 4;
    }
  if (dst)
    dst[n] = '"';
  n++;

  if (maxlen > 0)
    for (;;)
      {
	unsigned char c = *src;

	if (!c && !binary)
	  break;
	src++;

	const char *p;

#define CTRL1	"\"" "\\" "\a" "\b" "\f" "\n" "\r" "\t" "\v"
#define CTRL2	"\"" "\\" "a"  "b"  "f"  "n"  "r"  "t"  "v"

	if (c && (p = strchr(CTRL1 "\0" CTRL2, c)))
	  {
	    if (dst)
	      {
		dst[n    ] = '\\';
		dst[n + 1] = p[sizeof(CTRL1)];
	      }
	    n += 2;
	  }
	else if (c < ' ' ||  c >= 0177)
	  {
	    if (dst)
	      dst[n] = '\\';
	    n++;

#define ISOCTAL(c)	(((unsigned char) (c) - (unsigned char) '0') < 8)

	    if (c >= 0100 || (maxlen != 1 && ISOCTAL(*src)))
	      {
		if (dst)
		  dst[n] = '0' + (c >> 6);
		n++;
	      }
	    if (c >= 010 || (maxlen != 1 && ISOCTAL(*src)))
	      {
		if (dst)
		  dst[n] = '0' + ((c >> 3) & 7);
		n++;
	      }

#undef ISOCTAL

	    if (dst)
	      dst[n] = '0' + (c & 7);
	    n++;
	  }
	else
	  {
	    if (dst)
	      dst[n] = c;
	    n++;
	  }

#undef CTRL1
#undef CTRL2	

	if (!--maxlen)
	  break;
      }

  if (dst)
    dst[n] = '"';
  n++;
  if (!binary && !maxlen && *src)
    {
      dst[n    ] = '.';
      dst[n + 1] = '.';
      dst[n + 2] = '.';
      n += 3;
    }
  return n;
}

#define CANON_NAME	"simpleprof.so"

void
veprintf (const char *const tag, const char *fmt, va_list arg)
{
#define NIOV_MAX	32
  struct iovec iov[NIOV_MAX];
  unsigned int niov = 0;

  iov[niov].iov_base = (char *) program_invocation_name;
  iov[niov].iov_len = strlen(program_invocation_name);
  niov++;
  iov[niov].iov_base = " (" CANON_NAME "): ";
  iov[niov].iov_len = sizeof(" (" CANON_NAME "): ") - 1;
  niov++;
  if (tag)
    {
      iov[niov].iov_base = (char *) tag;
      iov[niov].iov_len = strlen(tag);
      niov++;
      iov[niov].iov_base = ": ";
      iov[niov].iov_len = 2;
      niov++;
    }

  while (*fmt)
    {
      assert(niov < NIOV_MAX);
      iov[niov].iov_base = (char *) fmt;
    fixed:
      while (*fmt && *fmt != '%')
	fmt++;
      if (fmt != iov[niov].iov_base)
	{
	  iov[niov].iov_len = fmt - (const char *) iov[niov].iov_base;
	  niov++;
	  assert(niov < NIOV_MAX);
	}
      if (!*fmt)
	break;

      unsigned int flags = 0;
#define ISLONG	(1U << 0)
#define ISPTR	(1U << 1)
#define ISSIZE	(1U << 2)
#define ISDIFF	(1U << 3)
#define ALT	(1U << 8)
#define PADSP	(1U << 9)
#define PLUS	(1U << 10)
#define LEFT	(1U << 11)
#define HEX	(1U << 15)
#define UPPER	(1U << 16)
#define NEGATIVE (1U << 17)

      for (;;)
	switch (*++fmt)
	  {
	  case ' ':
	    flags |= PADSP;
	    break;
#if 0				/* not implemented */
	  case '-':
	    flags |= LEFT;
	    break;
#endif
	  case '+':
	    flags |= PLUS;
	    break;
	  case '#':
	    flags |= ALT;
	    break;
	  default:
	    goto not_flags;
	  }
    not_flags:
      switch (*fmt)
	{
	case 'l':
	  flags |= ISLONG;
	  fmt++;
	  break;
	case 'z':
	  flags |= ISSIZE;
	  fmt++;
	  break;
	case 't':
	  flags |= ISDIFF;
	  fmt++;
	  break;
	}

      int prec = -1;

      if (*fmt == '.')
	{
	  if (fmt[1] == '*')
	    {
	      fmt += 2;
	      prec = va_arg(arg, int);
	    }
	  else
	    {
	      prec = 0;
	      unsigned int digit;
	      while ((digit = *++fmt - '0') < 10)
		prec = prec * 10 + digit;
	    }
	}

      unsigned long ulval;
      long lval;

      switch (*fmt)
	{
	case 'd':
	  if (flags & ISDIFF)
	    lval = va_arg(arg, ptrdiff_t);
	  else if (flags & ISSIZE)
	    lval = va_arg(arg, ssize_t);
	  else if (flags & ISLONG)
	    lval = va_arg(arg, long);
	  else
	    lval = va_arg(arg, int);
	  if (lval < 0)
	    {
	      /*
	       * "-lval" (simply negating lval) invokes undefined behavior
	       * due to signed integer overflow when lval is LONG_MIN.
	       * Avoid this situation by casting to unsigned integer type
	       * at first, because conversion to unsigned integer type and
	       * unary minus of unsigned integer type are both well-defined.
	       */
	      ulval = -(unsigned long)lval;
	      flags |= NEGATIVE;
	    }
	  else
	    ulval = lval;
	  break;
	case 'p':
	  flags |= ISPTR | HEX | ALT;
	  ulval = (unsigned long) va_arg(arg, void *);
	  break;
	case 'X':
	  flags |= UPPER;
	  /*FALLTHRU*/
	case 'x':
	  flags |= HEX;
	  /*FALLTHRU*/
	case 'u':
	  if (flags & ISSIZE)
	    ulval = va_arg(arg, size_t);
	  else if (flags & ISLONG)
	    ulval = va_arg(arg, unsigned long);
	  else
	    ulval = va_arg(arg, unsigned int);
	  break;
	case 's':
	  {
	    const char *str = va_arg(arg, const char *);

	    if (flags & ALT)
	      {
		iov[niov].iov_len = my_strvis(NULL, str, 0, prec >= 0 ? prec : SIZE_MAX);
		iov[niov].iov_base = alloca(iov[niov].iov_len);
		my_strvis(iov[niov].iov_base, str, 0, prec >= 0 ? prec : SIZE_MAX);
		niov++;
	      }
	    else if (str)
	      {
		iov[niov].iov_base = (char *) str;
		iov[niov].iov_len = strnlen(str, prec >= 0 ? prec : SIZE_MAX);
		niov++;
	      }
	    else if (prec < 0 || prec >= 5)
	      {
		iov[niov].iov_base = "(nil)";
		iov[niov].iov_len = 5;
		niov++;
	      }
	  }
	  fmt++;
	  continue;
	default:
	  iov[niov].iov_base = (char *) fmt;
	  fmt++;
	  goto fixed;
	}
      fmt++;

      size_t maxlen = 2 + (sizeof(unsigned long) * 8 + 2) / 3;
      char *buf = (char *) alloca(maxlen) + maxlen;
      char *p = buf;

      if (flags & HEX)
	{
	  if (!ulval)
	    flags &= ~ALT;
	  do
	    {
	      unsigned char digit = '0' + (ulval & 0xF);
	      if (digit > '9')
		{
		  digit += 'A' - '9' - 1;
		  if (!(flags & UPPER))
		    digit += 'a' - 'A';
		}
	      *--p = digit;
	    }
	  while ((ulval >>= 4) != 0);
	  if (flags & ALT)
	    {
	      *--p = (flags & UPPER) ? 'X' : 'x';
	      *--p = '0';
	    }
	}
      else
	{
	  do
	    *--p = '0' + (ulval % 10);
	  while ((ulval /= 10) != 0);
	  if (flags & NEGATIVE)
	    *--p = '-';
	  else if (flags & PLUS)
	    *--p = '+';
	  else if (flags & PADSP)
	    *--p = ' ';
	}
      
      iov[niov].iov_base = p;
      iov[niov].iov_len = buf - p;
      niov++;
    }
  assert(niov < NIOV_MAX);
  iov[niov].iov_base = "\n";
  iov[niov].iov_len = 1;
  niov++;
  writev(STDERR_FILENO, iov, niov);
}

void
eprintf (const char *const tag, const char *fmt, ...)
{
  va_list arg;
  va_start(arg, fmt);
  veprintf(tag, fmt, arg);
  va_end(arg);
}

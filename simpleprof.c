/*
 * Simple Profiler.
 * Copyright (C) 1999,2020  TAKAI Kousuke
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

#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <link.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/auxv.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <unistd.h>

#include <assert.h>

#include <stddef.h>
#include <stdalign.h>
#include <sys/gmon_out.h>

#define ENV_PREFIX	"SP_"

extern void eprintf (const char *, const char *, ...);
#define DPRINTF(Fmt, ...)	eprintf("debug", Fmt , ## __VA_ARGS__)
#define EPRINTF(Fmt, ...)	eprintf("error", Fmt , ## __VA_ARGS__)

/* Linux profil(3) man page does not tell where profil's interval
   came from... */
#ifdef HAVE___PROFILE_FREQUENCY
# if defined HAVE_DECL___PROFILE_FREQUENCY && !HAVE_DECL___PROFILE_FREQUENCY
extern int __profile_frequency (void);	/* Oops, libc internal function */
# endif
# define PROFILE_FREQUENCY()	__profile_frequency()
#elif defined HAVE_GETAUXVAL && defined AT_CLKTCK
# define PROFILE_FREQUENCY()	getauxval(AT_CLKTCK)
#else
# define PROFILE_FREQUENCY()	sysconf(_SC_CLK_TCK) /* XXX */
#endif

unsigned int
la_version (unsigned int version)
{
  return LAV_CURRENT;
}

static const char *
match_program_name (void)
{
  const char *const env = getenv(ENV_PREFIX "PROFILE");
  if (!env)
    return NULL;

  const char *const execfn = (const char *) getauxval(AT_EXECFN);
  const char *const execfn_base = execfn ? basename(execfn) : NULL;
  const char *const retval = execfn_base ? execfn_base : program_invocation_short_name;

  size_t envlen = strlen(env) + 1;
  char envcopy[envlen];
  memcpy(envcopy, env, envlen);
  char *saveptr;
  for (char *p = strtok_r(envcopy, ":", &saveptr);
       p;
       p = strtok_r(NULL, ":", &saveptr))
    if (strchr(p, '/'))
      {
	if (!fnmatch(p, execfn, FNM_PATHNAME) ||
	    !fnmatch(p, program_invocation_name, FNM_PATHNAME))
	  return retval;
      }
    else
      {
	if (!fnmatch(p, execfn_base, FNM_PATHNAME) ||
	    !fnmatch(p, program_invocation_short_name, FNM_PATHNAME))
	  return retval;
      }

  return NULL;
}

/*
 * In gmon.out format, "tag" is a single byte so that following members
 * may not align to natural boundary for the CPU.  We overcome this
 * by putting a dummy histogram record first to (at least) align
 * histogram bins of the main histogram record to natual boundaries.
 */

#define BASE_HEADER_SIZE	(sizeof(struct gmon_hdr) + 1 + sizeof(struct gmon_hist_hdr))

#define NEED_DUMMY_HIST_HDR	(BASE_HEADER_SIZE % alignof(unsigned short) != 0)
#define HEADER_SIZE	(NEED_DUMMY_HIST_HDR \
			 ? (BASE_HEADER_SIZE + sizeof(unsigned short) + 1 + sizeof(struct gmon_hist_hdr)) \
			 : BASE_HEADER_SIZE)

_Static_assert(HEADER_SIZE % alignof(unsigned short) == 0,
	       "gmon header is not properly aligned");

static void
make_gmon_header (void *const header, uintptr_t lowpc, size_t nsamples,
		  uint_fast32_t scale)
{
  static const struct my_gmon_hdr
    {
      char cookie[4];
      uint32_t version;
      char spare[3 * 4];
    } ghdr = { .cookie = GMON_MAGIC,
	       .version = GMON_VERSION };

  struct my_hist_hdr
    {
      uintptr_t low_pc;
      uintptr_t high_pc;
      uint32_t hist_size;
      uint32_t prof_rate;
      char dimen[15];
      char dimen_abbrev;
    } hist_hdr;

  _Static_assert(offsetof(struct my_gmon_hdr, version) == offsetof(struct gmon_hdr, version),
		 "my_gmon_hdr.version is not properly aligned");
  _Static_assert(sizeof(struct my_gmon_hdr) == sizeof(struct gmon_hdr),
		 "my_gmon_hdr has wrong size");

  _Static_assert(sizeof(struct my_hist_hdr) == sizeof(struct gmon_hist_hdr),
		 "my_hist_hdr has wrong size");

  hist_hdr.prof_rate = PROFILE_FREQUENCY();

  strncpy(hist_hdr.dimen, "seconds", sizeof(hist_hdr.dimen));
  hist_hdr.dimen_abbrev = 's';

  unsigned char *p = mempcpy(header, &ghdr, sizeof(struct my_gmon_hdr));
  if (NEED_DUMMY_HIST_HDR)
    {
      *p++ = GMON_TAG_TIME_HIST;
      
      hist_hdr.hist_size = 1;
      hist_hdr.low_pc = 0;	/* XXX */
      hist_hdr.high_pc = 0 + (65536 * 2 / scale);

      p = mempcpy(p, &hist_hdr, sizeof(struct my_hist_hdr));
      memset(p, '\0', sizeof(unsigned short));
      p += sizeof(unsigned short);
    }
  *p++ = GMON_TAG_TIME_HIST;
  hist_hdr.low_pc = lowpc;
  hist_hdr.hist_size = nsamples;
  hist_hdr.high_pc = lowpc + nsamples * (65536 * 2 / scale);
  p = mempcpy(p, &hist_hdr, sizeof(struct my_hist_hdr));
  assert(p == (unsigned char *) header + HEADER_SIZE);
}

void
la_preinit (uintptr_t *cookie)
{
  const char *const debug_env = getenv(ENV_PREFIX "DEBUG");
  _Bool debug = debug_env && (*debug_env != '\0');

  if (debug)
    DPRINTF("Entering %s", __func__);

  const char *const progname = match_program_name();
  if (!progname)
    return;

  unsigned long val;

  if ((val = getauxval(AT_PHENT)) != 0 && val != sizeof(ElfW(Phdr)))
    {
      EPRINTF("size of program header entry mismatch (%u, expected %u)",
	      (unsigned int) val, (unsigned int) sizeof(ElfW(Phdr)));
      return;
    }

  if (!(val = getauxval(AT_PHNUM)) || (unsigned int) val != val)
    return;
  const unsigned int phnum = (unsigned int) val;

  if (!(val = getauxval(AT_PHDR)))
    {
      EPRINTF("no program header");
      return;
    }

  const ElfW(Phdr) *const phdr = (const ElfW(Phdr) *) val;

  uintptr_t load_addr = 0;
  uintptr_t lowpc = 0;
  size_t memsz = 0;
  const ElfW(Phdr) *ph = phdr;
  for (unsigned int i = phnum; ; ph++)
    {
      if (ph->p_type == PT_PHDR)
	{
	  /* ELF spec says "If it is present, it must precede any loadable
	     segment entry."*/
	  load_addr = (ElfW(Addr)) phdr - ph->p_vaddr;
	}
      else if (ph->p_type == PT_LOAD &&
	       (ph->p_flags & PF_X) && ph->p_memsz != 0)
	{
	  lowpc = load_addr + ph->p_vaddr;
	  memsz = ph->p_memsz;
	  break;
	}
      if (__builtin_expect(!--i, 0))
	{
	  EPRINTF("no loadable and executable segment found");
	  return;
	}
    }

  const struct link_map *const map = (const struct link_map *) *cookie;
  if (map->l_addr != load_addr)
    {
      EPRINTF("load address mismatch (dynamic linker: %#" PRIxPTR
	      ", probed: %#" PRIxPTR ")",
	      (uintptr_t) map->l_addr, load_addr);
      return;
    }

  if (debug)
    DPRINTF("Range: %#" PRIxPTR " - %#" PRIxPTR " (%zu bytes), load offset: %#" PRIxPTR,
	    lowpc, (uintptr_t) (lowpc + (memsz - 1)), memsz, load_addr);

#define SCALE_1_TO_1	0x10000
#define DEFAULT_SCALE	4

  unsigned int s_scale = SCALE_1_TO_1 * sizeof(unsigned short) / DEFAULT_SCALE;
  const char *env = getenv(ENV_PREFIX "SCALE");
  if (env)
    {
      char dummy[1];
      unsigned int scale;

      if (sscanf(env, "%u %c", &scale, dummy) != 1 ||
	  scale == 0 ||
	  (s_scale = SCALE_1_TO_1 * sizeof(unsigned short) / scale) == 0)
	{
	  EPRINTF("invalid %s %#s", ENV_PREFIX "SCALE", env);
	  return;
	}
    }

  uintmax_t nsamples_tmp;
  size_t nsamples, bufsiz, mapsiz;
  if (__builtin_mul_overflow((memsz + 1) / 2, s_scale, &nsamples_tmp) ||
      (nsamples = nsamples_tmp / 65536) != nsamples_tmp / 65536 ||
      __builtin_mul_overflow(nsamples, sizeof(unsigned short), &bufsiz) ||
      __builtin_add_overflow(bufsiz, HEADER_SIZE, &mapsiz))
    {
      EPRINTF("profile buffer size overflow (segment size %zu, scale %u)",
	      memsz, s_scale);
      return;
    }

  if (debug)
    DPRINTF("scale = %u, %zu samples", s_scale, nsamples);

  const char *outputenv = getenv(ENV_PREFIX "PROFILE_OUTPUT");
  if (!outputenv)
    outputenv = "/var/tmp";
  char fnbuf[strlen(outputenv) + 1 + strlen(progname) + sizeof(".profile")];
  {
    char *p = stpcpy(fnbuf, outputenv);
    if (fnbuf != p && p[-1] != '/')
      *p++ = '/';
    stpcpy(stpcpy(p, progname), ".profile");
  }
  if (debug)
    DPRINTF("file = %#s", fnbuf);

  int fd = open(fnbuf, O_RDWR | O_CREAT, DEFFILEMODE);
  struct stat statbuf;
  if (fstat(fd, &statbuf))
    {
      EPRINTF("fstat: %s", strerror(errno));
      close(fd);
      return;
    }

  if (statbuf.st_size == 0)
    {
      int e = posix_fallocate(fd, 0, mapsiz);
      if (e)
	{
	  EPRINTF("cannot allocate %zu bytes for %#s: %s",
		  mapsiz, fnbuf, strerror(e));
	  close(fd);
	  return;
	}
    }
  else if (statbuf.st_size != mapsiz)
    {
      EPRINTF("profile file size mismatch (shall be %zu bytes)", mapsiz);
      close(fd);
      return;
    }

  void *const mapbase = mmap(NULL, mapsiz, PROT_READ | PROT_WRITE,
			     MAP_SHARED | MAP_FILE, fd, 0);
  if (mapbase == MAP_FAILED)
    {
      EPRINTF("mmap: %s", strerror(errno));
      close(fd);
      return;
    }
  close(fd);

  char header[HEADER_SIZE];
  make_gmon_header(header, lowpc - load_addr, nsamples, s_scale);
  
  if (statbuf.st_size == 0)
    memcpy(mapbase, header, HEADER_SIZE);
  else if (memcmp(mapbase, header, HEADER_SIZE))
    {
      EPRINTF("profile header mismatch");
      munmap(mapbase, mapsiz);
      return;
    }

  if (debug)
    DPRINTF("profil(%p, %zu, %#zx, %u)",
	    (char *) mapbase + HEADER_SIZE, bufsiz, lowpc, s_scale);
  if (profil((void *)((char *) mapbase + HEADER_SIZE), bufsiz, lowpc, s_scale))
    {
      EPRINTF("profil: %s", strerror(errno));
      munmap(mapbase, mapsiz);
      return;
    }
}

int
main (int argc, char *argv[])
{
  EPRINTF("This program is not intended to be invoked directly");

  return 1;
}

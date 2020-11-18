# simple-profiler

This is a very simple code profiler that enables you to profile your program
without recompiling.

## System Requirements

Glibc-based Linux system.

## Usage

1. Profile your code
   ```
   $ LD_AUDIT=path/to/simpleprof.so SP_PROFILE=\* ./your-program
   ```
   * `SP_PROFILE` environmental variable specifies colon-separated list
     of wildcard patterns of program names to be profiled.
     If each pattern contains "/" character, it will be matched against
     whole program name (argv[0]).  Otherwise it will be matched against
     basename (non-directory portion) of program name.
     If this variable is not defined or empty, `simpreprof.so` will not do
     anything.

   * Profile data is dumped into `/var/tmp/your-program.profile` by default.

2. Analyze profile data
   ```
   $ gprof ./your-program /var/tmp/your-program.profile
   ```
   * `simpleprof.so`'s output file is (intended to be) compatible with
     standard profiler's output file, `gmon.out`, and can be analyzed
     with `gprof` from GNU binutils.

## Copyright and License

Copyright 1999,2020 TAKAI Kousuke

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

As additional permission under GNU GPL version 3 section 7,
you may dynamically link this program into independent programs,
regardless of the license terms of these independent programs,
provided that you also meet the terms and conditions of the license
of those programs.  An independent program is a program which is not
derived from or based on this program.  If you modify this program,
you may extend this exception to your version of the program, but
you are not obligated to do so.  If you do not wish to do so,
delete this exception statement from your version.

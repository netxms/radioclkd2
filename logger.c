/*
 * Copyright (c) 2002 Jon Atkins http://www.jonatkins.com/
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"


#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <time.h>



static FILE*	logf_file;
static int	logf_file_level;
static int	logf_file_newline = 1;

static int	logf_syslog = 0;
static int	logf_syslog_level;

void
loggerSetFile ( FILE* file, int level )
{
	logf_file = file;
	logf_file_level = level;
}

void
loggerSyslog ( int flag, int level )
{
	logf_syslog = flag;
	logf_syslog_level = level;
}

int
loggerOpenFile ( const char *dirpath, const char *devname, int level )
{
	char	filename[512];
	char	filepath[512];
	const char	*p;
	char	*q;
	FILE	*fp;

	/* derive filename from device path */
	if ( strncmp ( devname, "/dev/", 5 ) == 0 )
	{
		/* /dev/ttyS0 -> ttyS0 */
		p = devname + 5;
		snprintf ( filename, sizeof(filename), "%s", p );
	}
	else if ( strncmp ( devname, "/sys/", 5 ) == 0 )
	{
		/* /sys/class/gpio/gpio0/value -> gpio0_value */
		const char *slash, *prev_slash;
		slash = devname + strlen(devname);
		/* find last component */
		while ( slash > devname && *(slash-1) != '/' )
			slash--;
		/* find second-to-last component */
		prev_slash = slash - 1;
		if ( prev_slash > devname )
		{
			prev_slash--;
			while ( prev_slash > devname && *(prev_slash-1) != '/' )
				prev_slash--;
		}
		snprintf ( filename, sizeof(filename), "%s", prev_slash );
	}
	else
	{
		/* fallback: use basename */
		p = strrchr ( devname, '/' );
		if ( p )
			p++;
		else
			p = devname;
		snprintf ( filename, sizeof(filename), "%s", p );
	}

	/* replace '/' with '_' in filename */
	for ( q = filename; *q; q++ )
	{
		if ( *q == '/' )
			*q = '_';
	}

	/* strip trailing '_' if any */
	q = filename + strlen(filename) - 1;
	while ( q >= filename && *q == '_' )
	{
		*q = 0;
		q--;
	}

	/* create directory if needed */
	mkdir ( dirpath, 0755 );

	/* build full path and open */
	snprintf ( filepath, sizeof(filepath), "%s/%s.log", dirpath, filename );

	fp = fopen ( filepath, "a" );
	if ( fp == NULL )
	{
		syslog ( LOG_ERR, "failed to open log file %s", filepath );
		return -1;
	}

	loggerSetFile ( fp, level );
	logf_file_newline = 1;

	syslog ( LOG_NOTICE, "logging to %s", filepath );
	return 0;
}

void
loggerf ( int level, char* format, ... )
{
	char	buf[512];
	va_list	ap;
	static char	syslogline[512];


	if ( format )
	{
		va_start ( ap, format );
		vsnprintf ( buf, sizeof(buf), format, ap );
		va_end ( ap );

		if ( logf_file && level <= logf_file_level )
		{
			if ( logf_file != stderr )
			{
				/* add timestamp prefix at start of each line */
				char	*s = buf;
				while ( *s )
				{
					if ( logf_file_newline )
					{
						time_t		now;
						struct tm	tm;
						char		tsbuf[32];
						now = time(NULL);
						localtime_r ( &now, &tm );
						strftime ( tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S ", &tm );
						fprintf ( logf_file, "%s", tsbuf );
						logf_file_newline = 0;
					}
					/* find next newline */
					char	*nl = strchr ( s, '\n' );
					if ( nl )
					{
						fwrite ( s, 1, nl - s + 1, logf_file );
						logf_file_newline = 1;
						s = nl + 1;
					}
					else
					{
						fprintf ( logf_file, "%s", s );
						break;
					}
				}
			}
			else
			{
				fprintf ( logf_file, "%s", buf );
			}
			fflush ( logf_file );
		}

		if ( logf_syslog && level <= logf_syslog_level )
		{
			//some (all?) syslog()s output strings without '\n' in them as a line anyway - so buffer it here...

			//if we have a lot, send it out anyway...
			if ( strlen(syslogline)+strlen(buf) >= sizeof(syslogline) )
			{
				syslog ( LOG_NOTICE, "%s", syslogline );
				syslogline[0] = 0;
			}

			strcat ( syslogline, buf );

			if ( syslogline[0] != 0 && syslogline[strlen(syslogline)-1] == '\n' )
			{
				syslog ( LOG_NOTICE, "%s", syslogline );
				syslogline[0] = 0;
			}

		}
	}
}

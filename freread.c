/*
 * File:	freread.c
 * Description:	This file contains functions to read FRE input lines.
 * Author:	Edward H. Chubin
 * Copyright:	1996 Edward Chubin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>

#include "sslfre.h"

extern FILE	*infile;
extern FILE	*logfile;

extern int	debug;
extern int	ref_count;
extern int	rewind_file;

extern char	line[MAXLINE];
extern char	line_save[];
extern int	line_number;

extern void	show_error(int,char*,int);

extern void	wait_for_input_on_fd(int);
extern int	parse_line(char*);

#ifdef WIN32
#include "windows.h"
#endif
int
do_next_line(int maxrequests)
{
    int	ret = 0;
    int	len;
    int infd = fileno(infile);
    char *ptr;

    if ( debug > 2 ) fprintf(logfile, "Getting next line...\n");

    while ( ref_count < maxrequests ) {
#ifndef WIN32
	if ( isatty(infd) ) {
	    wait_for_input_on_fd(infd);
	}
#endif
	ptr = fgets(line, sizeof(line), infile);
	if ( !ptr ) {
	    if ( !isatty(infd) && rewind_file ) {
		rewind(infile);
		continue;
	    }
	    ret = 0;
	    if ( logfile ) fprintf(logfile, "End of file...\n");
	    break;
	}

	++line_number;
	if ( line[0] == '#' || line[0] == '\n' ) {
	    continue;
	}
	strcpy(line_save, line);

	/* if the line is greater than 255, eat characters until EOL */
	len = strlen(line);
	if ( line[len - 1] != '\n' ) {
	    if ( logfile ) fprintf(logfile, "> %d, skip to NL...", MAXLINE);
	    while ( (ret = fgetc(infile)) != '\n' ) {
		if ( ret == EOF ) {
		    /* bad end of file */
		    if ( logfile ) fprintf(logfile,"End of file big line...\n");
		    ret = 0;
		    break;
		}
	    }

	    if ( logfile ) fprintf(logfile, "done\n");

	    if ( ret == EOF ) {
		/* don't care to print error if EOF */
		break;
	    }

	    show_error(line_number, line_save, FRE_ERROR_INVALID_LINE);
	    continue;
	}

	ret = parse_line(line);
	if ( ret < 0 ) {
	    show_error(line_number, line_save, ret);
	    return FALSE;
	}
    }

    if ( debug > 2 ) fprintf(logfile, "do_next_line: ret %d\n", !ret);
    if ( logfile ) fflush(logfile);
    return !ret;
}

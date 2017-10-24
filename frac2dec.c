/************************************************************************
 * File:	frac2dec.c
 * Product:	SSL Field Retrieval Engine (sslfre)
 * Author:	Edward H. Chubin
 * Company:	Vanguard Software Corp.
 * Copyright:	1996
 * Description:	This file contains routines which convert Reuters
 *		price strings to a decimal strings and manage the
 *		field formal file functionalty.
 *
 * Version:	1.x	03-11-96	Extracted from sslfre.c
 *		1.7	02-27-96	Fixed bug in convert_to_decimal()
 *					Didn't do negative right on 0 whole.
 *					Added precision specification to
 *					fidfile.
 *		1.8	03-11-96	Added precision and min_width for fid
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>

#include "triprice.h"

#ifndef lint
static char _str[] = {"price_to_string.c: version 1.8"};
#endif

extern FILE	*errfile;
extern FILE	*logfile;
extern int	debug;

static void	addFidFormat();
void		convert_to_decimal();

/*
 * may add different formats later
 */
int
read_fid_format_file(fidFormatFile)
char	*fidFormatFile;
{
	FILE	*fidFile;
	static char	line[80];
	char	*ptr, *tok;
	int	fid;
	int	precision;
	int	min_width;

	fidFile = fopen(fidFormatFile, "r");
	if ( !fidFile ) {
		return FALSE;
	}

	while ( fgets(line, sizeof(line), fidFile) != NULL ) {

		if ( line[0] == '#' || line[0] == '\n' ) {
			continue;
		}

		/* skip white space */
		for ( ptr = line; *ptr == ' ' || *ptr == '\t'; ptr++ ) {
			if ( ! *ptr ) {
				/* bad line - ignore (will print below) */
				break;
			}
		}

		if ( *ptr < '0' || *ptr > '9' ) {
			/* bad line - ignore */
			fprintf(errfile,"read_fid_format_file: Bad line\n");
			fprintf(errfile," \"%s\"\n", line);
			continue;
		}

		tok = strtok(ptr, " ,");
		if ( tok ) {
			fid = atoi(tok);
		}

		precision = 0;
		tok = strtok(NULL, " ,");
		if ( tok ) {
			precision = atoi(tok);
		}

		/* make sure there is a default value for precision */
		if ( precision == 0 ) {
			precision = 6;
		}

		/* make sure there is a default value for min_width */
		min_width = precision;
		tok = strtok(NULL, " ,");
		if ( tok ) {
			min_width = atoi(tok);
		}

		if ( logfile && debug > 2 ) {
			fprintf(logfile,
			  "Adding fidFormat %d precision %d min_width %d\n",
			  fid, precision, min_width);
			fflush(logfile);
		}

		addFidFormat(fid, precision, min_width);
	}
	return TRUE;
}

static FID_ARRAY	*format_base = NULL;
static FID_ARRAY	*format_last = NULL;

static void
addFidFormat(newFid, precision, min_width)
int	newFid;
int	precision;
int	min_width;
{
	FID_ARRAY	*current;

	current = (FID_ARRAY *) malloc(sizeof(FID_ARRAY));
	if ( !current ) {
	    fprintf(errfile, "addFidFormat: could not allocate memory (%d)\n",
		    sizeof(FID_ARRAY));
	    fflush(errfile);
	    return;
	}

	if ( !format_base ) {
		format_base = current;
	}
	current->next = NULL;
	current->fid = newFid;
	current->precision = precision;
	current->min_width = min_width;

	if ( format_last ) {
		format_last->next = current;
	}
	format_last = current;
}

/*
 * Return TRUE if fid format should be converted
 */
int
getFidFormat(fid, precision, min_width)
int	fid;
int	*precision;
int	*min_width;
{
  FID_ARRAY	*current;

  for ( current = format_base;
	current;
	current = current->next ) {

    if ( fid == current->fid ) {
      if ( logfile && debug > 1 ) {
	fprintf(logfile,"Found fidFormat %d\n", fid);
	fflush(logfile);
      }

      *precision = current->precision;
      *min_width = current->min_width;
      return TRUE;
    }
  }
  return FALSE;
}

/*
 * Does not have to be in any specific IDN format.
 * Returns string unchanged if can't convert.
 * Fixed bug when initial zero caused loss of sign. 
 */
void
convert_to_decimal(price_string, decimal_string, precision, min_width)
char	*price_string;
char	*decimal_string;
int	precision;
int	min_width;
{
	int	ret;
	int	integer;
	int	numerator;
	int	denominator;
	int	fraction;
	int	negative = FALSE;
	double  value;
	char	*original_string = price_string;

	if ( *price_string == '-' ) {
		++price_string;
		negative = TRUE;
	}

	/* first check whole num/den format */
	ret = sscanf(price_string, "%d %d/%d", &integer, &numerator, &denominator);
	if ( ret != 3 && sscanf(price_string,"%d/%d",&numerator,&denominator) == 2 ) {
		if ( logfile && debug > 2 ) {
			fprintf(logfile,"fidFormat (num/den)\n");
			fflush(logfile);
		}
		ret = 3;
		integer = 0;
	}
	if ( ret == 3 ) {
		if ( logfile && debug > 2 ) {
			fprintf(logfile,"fidFormat (%d %d/%d)\n",
				integer, numerator, denominator);
			fflush(logfile);
		}

		value = (double)integer+(double)numerator/(double)denominator;
		if ( negative && value >= 0 ) {
			value *= -1;
		}

		sprintf(decimal_string, "%*.*f", min_width, precision, value);
	}
	else if ( ret == 1 ) {
		/* check if its floating point */
		/* don't use fraction, cause it may loose zeros */
		if ( sscanf(original_string, "%d.%d", &integer, &fraction) == 2 ) {
			if ( logfile && debug > 2 ) {
				fprintf(logfile,"fidFormat (decimal)\n");
				fflush(logfile);
			}
			/*
			strcpy(decimal_string, price_string);
			*/
			value = atof(original_string);
			sprintf(decimal_string, "%*.*f",
				min_width, precision, value);
		}
		else {
			if ( logfile && debug > 2 ) {
				fprintf(logfile,"fidFormat (whole)\n");
				fflush(logfile);
			}
			/* it's a whole number */
			sprintf(decimal_string, "%*.*f",
				min_width, precision, (double)integer);
		}
	}
	else {
		/* Bad price format - just copy */
		strcpy(decimal_string, original_string);
	}
	return;
}

#ifdef UNIT_TEST
FILE *logfile = stdout;
int	debug = 4;
int
main(argc, argv)
int argc;
char *argv[];
{
	char	decimal_string[50];

	if ( argc < 2 ) {
	    printf("%s: need one arg\n", argv[0]);
	    exit(1);
	}

	convert_to_decimal(argv[1], decimal_string, 6, 10);

	printf("Decimal string is %s\n", decimal_string);
	return 0;
}
#endif

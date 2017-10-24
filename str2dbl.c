/*
 * File:	str2dbl.c
 * Description: Convert a IDN Price Field
 *		Will recover a large number of differences,
 *		but don't get too weird.
 * Returns:	A double result.
 *		Error indication (TRUE/FALSE)
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#if defined(__STDC__) && !defined(NeXT)
#if !defined(__cplusplus) || defined(WIN32)
extern double atof(const char *);
#endif
#endif

int
#if defined(_cplusplus) || defined(__STDC__)
string2double(char *string, double *result)
#else
string2double(string, result)		/* Price */
char		*string;
double		*result;
#endif
{
	static char buf[256];
	int minus_found = 0;
	double base, top, bottom;
	char *ptr;

	*result = 0.0;

	while ( *string == ' ' || *string == '\t' ) {
		if ( !*string ) return 0;
		string++;
	}
	if(*string == '+')
		string++;
	else if(*string == '-') {
		string++;
		minus_found++;
	}
	while ( *string == ' ' || *string == '\t' ) {
		if ( !*string ) return 0;
		string++;
	}
	strcpy(buf, string);
	ptr = strchr(buf, ' ');
	if (!ptr) {
		ptr = strchr(buf, '/');
		if ( !ptr ) {
			base = atof(buf);
		} else {
			ptr = strtok(buf, "/");
			if ( !ptr ) return 0;
			top = atof(ptr);

			ptr = strtok(NULL, "");
			if ( !ptr ) return 0;
			bottom = atof(ptr);
			if (bottom != 0) {
				base = top / bottom;
			} else {
				return 0;
			}
		}
	} else {
		ptr = strtok(buf, " ");
		if ( !ptr ) return 0;
		base = atof(ptr);

		ptr = strtok(NULL, "/");
		if ( ptr ) {
		    top = atof(ptr);

		    ptr = strtok(NULL, "");
		    if ( !ptr ) return 0;

		    bottom = atof(ptr);
		    if (bottom != 0) {
			    base = base + top / bottom;
		    } else {
			    return 0;
		    }
		}
	}
	if(minus_found && base)
		base = -base;
	*result = base;
	return 1;
}

#ifdef UNIT_TEST
main(argc, argv)
int	argc;
char	*argv[];
{
	double result;
	int	i;
	for ( i = 1; i < argc; i++ ) {
		if ( string2double(argv[i], &result) ) {
			printf("%s: %f\n", argv[i], result);
		} else {
			printf("%s: could not convert\n", argv[i]);
		}
	}
}
#endif

/*
 * File:	fidutils.c
 * Author:	Edward H. Chubin
 * Description:	Field storage and accessor utilities for the FRE
 *		The functions defined here manage a structure which
 *		contain the field numbers and other infomation about
 *		a particular record. They provide storage, allow access
 *		by iterating over all or just updated fields, and by
 *		specifying the field by number. Additional accessor
 *		functions additionally allow access by datatype.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>

#include "triprice.h"

#ifdef UNIT_TEST
FILE	*logfile = stderr;
FILE	*errfile = stderr;
int	debug = 0;
#else
extern FILE	*logfile;
extern FILE	*errfile;
extern int	debug;
#endif

RECORD_INFO *
create_record_info(void)
{
	RECORD_INFO *record_info = (RECORD_INFO *)malloc(sizeof(RECORD_INFO));
	if ( record_info ) {
	    memset((char *)record_info, 0, sizeof(RECORD_INFO));
	    record_info->fid_array = NULL;
	    record_info->last = NULL;
	    record_info->count = 0;
	    record_info->do_all_fids = 0;
	    record_info->original_item = NULL;
	    record_info->status = FRE_STATUS_NONE;
	}
	return record_info;
}

RECORD_INFO *
duplicate_record_info(RECORD_INFO *stuff_to_duplicate)
{
	RECORD_INFO *record_info = (RECORD_INFO *)malloc(sizeof(RECORD_INFO));
	if ( record_info ) {
	    FID_ARRAY	*current;

	    record_info->fid_array = NULL;
	    record_info->last = NULL;
	    record_info->count = 0;
	    record_info->do_all_fids = stuff_to_duplicate->do_all_fids;
#ifdef AUTOMATIC_REQUEST_OPTION
	    record_info->request_option = 0;
	    record_info->len_of_original_chain =
		stuff_to_duplicate->len_of_original_chain;
#endif
	    if ( stuff_to_duplicate->original_item ) {
		record_info->original_item =
		    malloc(strlen(stuff_to_duplicate->original_item)+1);
		if ( record_info->original_item ) {
		    strcpy(record_info->original_item,
			    stuff_to_duplicate->original_item);
		}
	    } else {
		record_info->original_item = NULL;
	    }

	    /* just keep a reference to this since it's static */
	    record_info->other_data = stuff_to_duplicate->other_data;

	    for ( current = stuff_to_duplicate->fid_array;
		  current;
		  current = current->next ) {

		FID_ARRAY *new = (FID_ARRAY *)malloc(sizeof(FID_ARRAY));
		if ( !record_info->fid_array ) {
		    record_info->fid_array = new;
		} else if ( record_info->last ) {
		    /* get to the end and add it */
		    (record_info->last)->next = new;
		}

		record_info->last = new;

		new->next = NULL;
		new->fid = current->fid;
		new->value = NULL;
		new->length = 0;
		new->updated = 0;

		if ( debug > 3 ) {
		    fprintf(logfile, "Duplicated fid %d\n", current->fid);
		    fflush(logfile);
		}
	    }
	}
	return record_info;
}

void
show_record_info(RECORD_INFO *record_info)
{
    FID_ARRAY	*current;

    if ( debug > 3 ) {
	fprintf(logfile, "show_record_info: %p", record_info);
	if ( record_info->do_all_fids )
	    fprintf(logfile, " DoAllFids");

#ifdef AUTOMATIC_REQUEST_OPTION
	if ( record_info->request_option )
	    fprintf(logfile, " ReqOpt");

	if ( record_info->len_of_original_chain )
	    fprintf(logfile, " OrigLen %d,",record_info->len_of_original_chain);
#endif

	if ( record_info->original_item )
	    fprintf(logfile, " OriginalItem %s,", record_info->original_item);

	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    fprintf(logfile, " %d", current->fid);
	}
	fprintf(logfile, "\n");
	fflush(logfile);
    }
}

void
free_record_info(RECORD_INFO *record_info)
{
    FID_ARRAY	*current;
    FID_ARRAY	*last = NULL;

    if ( !record_info ) {
	fprintf(errfile, "free_record_info: already free\n");
	return;
    }

    if ( record_info->count > 0 ) {
	/* EHC 6-27-97 clear the data */
	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    if ( current->value ) {
		free(current->value);
		current->value = NULL;
	    }
	    current->length = 0;
	}
	return;
    }

    current = record_info->fid_array;
    while ( current ) {
	/* EHC 3-26-96 */
	if ( current->value ) {
	    if ( debug > 2 ) {
		fprintf(logfile, "free_record_info: freeing fid value %d %p\n",
			current->fid, current->value);
		fflush(logfile);
	    }
	    free(current->value);
	    current->value = NULL;
	    current->length = 0;
	}

	last = current;
	current = current->next;
	free(last);
    }
    record_info->fid_array = NULL;

    if ( record_info->original_item ) {
	free(record_info->original_item);
	record_info->original_item = NULL;
    }

    if ( debug > 2 ) {
	fprintf(logfile, "free_record_info: freeing %p\n", record_info);
	fflush(logfile);
    }

    free((char *)record_info);
    record_info = NULL;
}

/*
 * If really want to store data also,
 * then need an update flag which would be cleared here
 * this is cleaner, but slower, and does not store data
 */
void
free_record_data(RECORD_INFO *record_info)
{
	FID_ARRAY	*current;

	if ( !record_info ) {
	    fprintf(errfile, "free_record_data: already free\n");
	    return;
	}

	/* EHC 6-27-97 clear the data */
	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    /* May just clear the update flag
	     * This would allow us to easily
	     * store the latest value.
	     * Should implement this after hashing
	     */
	    if ( current->value ) {
		free(current->value);
		current->value = NULL;
		current->length = 0;
	    }
	    current->updated = FALSE;
	}
	return;
}

/*
 * want to store data, so clear update flag when done
 */
void
free_record_update(RECORD_INFO *record_info)
{
	FID_ARRAY	*current;

	if ( !record_info ) {
	    fprintf(errfile, "free_record_data: pointer is NULL\n");
	    return;
	}

	/* EHC 6-27-97 clear the data */
	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    /* just clear the update flag
	     * Allow us to easily store the latest value.
	     */
	    if ( current->updated )
		 current->updated = FALSE;
	}
	return;
}

char *
mf_parse(char *str, char separator, char *end)
{
	while ( str < end && *str != separator && *str != FS ) {
		++str;
	}
	if ( str == end ) return NULL;
	return str;
}

void
set_all_fids(RECORD_INFO *record_info)
{
	record_info->do_all_fids = TRUE;
}

int
add_fid(RECORD_INFO *record_info, int fid)
{
	FID_ARRAY *current;

	/* check if fid already added */
	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    if ( current->fid == fid ) {
		if ( debug ) {
		    fprintf(logfile, "add_fid: fid %d already added\n",fid);
		}
		break;
	    }
	}

	if ( !current ) {
	    current = (FID_ARRAY *) malloc(sizeof(FID_ARRAY));
	    if ( !current ) {
		fprintf(errfile, "add_fid: could not allocate memory (%d)\n",
		    sizeof(FID_ARRAY));
		return FALSE;
	    }

	    if ( !record_info->fid_array ) {
		record_info->fid_array = current;
	    } else if ( record_info->last ) {
		/* get to the end and add it */
		record_info->last->next = current;
	    }

	    record_info->last = current;

	    current->next = NULL;
	    current->fid = fid;
	    current->value = NULL;
	    current->length = 0;
	    current->updated = 0;

	    if ( debug > 2 ) {
		fprintf(logfile, "adding fid %d\n", current->fid);
	    }
	}

	return TRUE;
}

int
set_fids(RECORD_INFO *record_info, char *str)
{
    char	*ptr;
    int		number_of_fids = 0;
    FID_ARRAY	*current = NULL;

    if ( !record_info ) {
	fprintf(errfile, "set_fids: record_info NULL\n");
	return FALSE;
    }

    if ( debug > 5 ) {
	fprintf(logfile, "set_fids: fids are %s\n", str);
	fflush(logfile);
    }

    /* clean up if memory is allocated -- Not implemented yet */

    /* this is the base */
    record_info->fid_array = NULL;
    record_info->last = NULL;

    for ( ptr = strtok(str, ","); 
	  ptr;
	  ptr = strtok(NULL, ",") ) {
	int fid = atoi(ptr);

	if ( debug > 5 ) {
	    fprintf(logfile, "set_fids: fid %d\n", fid);
	    fflush(logfile);
	}

	/* check if fid already added */
	for ( current = record_info->fid_array;
	      current;
	      current = current->next ) {
	    if ( current->fid == fid ) {
		if ( debug > 2 ) {
		    fprintf(logfile, "set_fids: fid %d already added\n",fid);
		    fflush(logfile);
		}
		break;
	    }
	}

	/* If current == NULL, then hasn't been added */
	if ( !current ) {
	    current = (FID_ARRAY *) malloc(sizeof(FID_ARRAY));
	    if ( !current ) {
		fprintf(errfile, "set_fids: could not allocate memory (%d)\n",
		    sizeof(FID_ARRAY));
		return FALSE;
	    }

	    if ( !record_info->fid_array ) {
		record_info->fid_array = current;
	    } else {
		record_info->last->next = current;
	    }

	    record_info->last = current;

	    current->next = NULL;
	    current->fid = fid;
	    current->value = NULL;
	    current->length = 0;
	    current->updated = 0;

	    if ( debug > 2 ) {
		fprintf(logfile, "adding fid %d\n", current->fid);
		fflush(logfile);
	    }

	    ++number_of_fids;
	}
    }

    if ( debug > 4 ) {
	fprintf(logfile, "Number of fids %d\n", number_of_fids);
	fflush(logfile);
    }

    return number_of_fids;
}


/*
 * Description: Add the data to the info structure
 * 		Search to see IF FID EXISTS.
 *		If it doesn't, and doing ALL fids, add it
 */

int
addToFidArray(RECORD_INFO *record_info, int fid, char *dataptr)
{
    FID_ARRAY	*current;
    int		len;

    if ( !record_info ) {
	return FALSE;
    }

    for ( current = record_info->fid_array;
	  current;
	  current = current->next ) {

	if ( fid == current->fid ) {
	    len = strlen(dataptr)+1;
	    /* reallocate memory if larger than allocated */
	    if ( len > current->length ) {
		if ( current->value )
		    free(current->value);
		current->value = malloc((unsigned)len);
		if ( !current->value ) {
		    fprintf(errfile, "addToFidArray: malloc failed\n");
		    return FALSE;
		}
	    } else if ( !current->value ) {
		/* must have freed it up */
		fprintf(errfile, "addToFidArray: value (fid %d, len %d, clen %d)\n",
		    fid, len, current->length);
		return FALSE;
	    }
	    memcpy(current->value, dataptr, len);
	    current->length = len;
	    current->updated = TRUE;
	    if ( debug > 5 ) {
		fprintf(logfile,"addToFidArray: fid %d:%s:%p\n",
			fid, dataptr, current->value);
		fflush(logfile);
	    }
	    return TRUE;
	}
    }

    /* Fid not set */
    if ( record_info->do_all_fids ) {
	if ( debug > 2 ) {
	    fprintf(logfile, "addToFidArray: adding to all fids\n");
	    fflush(logfile);
	}

	/* Find the tail of the tree and add this fid */
	current = (FID_ARRAY *) malloc(sizeof(FID_ARRAY));
	if ( !current ) {
	    fprintf(errfile, "addToFidArray: error allocating FID_ARRAY\n");
	    return FALSE;
	}
	if ( !record_info->fid_array ) {
	    record_info->fid_array = current;
	} else {
	    record_info->last->next = current;
	}

	record_info->last = current;
	current->next = NULL;
	current->fid = fid;
	current->length = strlen(dataptr)+1;
	current->value = malloc((unsigned)current->length);
	current->updated = TRUE;
	if ( !current->value ) {
	    fprintf(errfile, "addToFidArray: malloc [%d] failed\n",
		current->length);
	    return FALSE;
	}
	memcpy(current->value, dataptr, current->length);
	if ( debug > 3 ) {
	    fprintf(logfile, "addToFidArray: fid %d:%s:%p\n",
		    fid, dataptr, current->value);
	    fflush(logfile);
	}
	return TRUE;
    }
    return FALSE;
}

/* Iterators are dangerous and need to be used with care
 *  or designed with care.
 *  Designing with care is HARD! So be careful.
 *  This only allows one iteration at a time per structure.
 */
void
setBeginFidArray(RECORD_INFO *record_info)
{
    record_info->fidArrayNext = record_info->fid_array;
    if ( debug ) {
	if ( !record_info->fidArrayNext ) {
	    fprintf(logfile, "Fid array NULL\n");
	} else if ( debug > 2 ) {
	    fprintf(logfile, "setBeginFidArray: fid %d: %s\n",
		    (record_info->fidArrayNext)->fid,
		    (record_info->fidArrayNext)->value?
		    (record_info->fidArrayNext)->value:"[NULL]");
	}
	fflush(logfile);
    }
}

/*
 * Only get updated fields
 * pointer to string array with data is returned
 */
int
getNextFidArray(RECORD_INFO *record_info, int *fid, char **dataptr)
{
    FID_ARRAY	*current = record_info->fidArrayNext;
    while ( (current = record_info->fidArrayNext) != NULL ) {
	int was_null;

	record_info->fidArrayNext = current->next;
	*fid = current->fid;
	*dataptr = current->value;

	if ( !*dataptr ) {
	    *dataptr = "";
	    was_null = TRUE;
	} else {
	    was_null = FALSE;
	}

	if ( current->updated ) {
	    if ( debug > 2 ) {
		fprintf(logfile, "getNextFidArray: UPDATED %d: %s\n",
		    *fid, **dataptr ? *dataptr : "[NULL]");
		fflush(logfile);
	    }

	    /* don't clear the updated flag here because several
	       passes will be made because we don't hash it */

	    return TRUE;

	} else if ( !was_null && debug > 2 ) {
	    fprintf(logfile, "getNextFidArray: NOT UPDATED %d: %s\n",
		*fid, **dataptr ? *dataptr : "[NULL]");
	    fflush(logfile);
	}
    }
    return FALSE;
}

/*
 * Get specific field. Need to hash at some point
 * Doesn't care if it was updated
 * pointer to string array with data is returned
 */
int
GetField(RECORD_INFO *record_info, int fid, char **dataptr)
{
    FID_ARRAY *current;
    for ( current = record_info->fid_array;
	  current;
	  current = current->next ) {

	if ( fid == current->fid ) {
	    *dataptr = current->value;

	    if ( !*dataptr ) *dataptr = "";

	    if ( debug > 2 ) {
		fprintf(logfile, "getField: found fid %d: %s\n",
		    fid, **dataptr ? *dataptr : "[NULL]");
		fflush(logfile);
	    }
	    return TRUE;
	}
    }

    if ( debug ) {
	fprintf(logfile, "getField: didn't find fid %d\n", fid);
	fflush(logfile);
    }
    return FALSE;
}

int
GetIntField(RECORD_INFO	*record_info, int fid, int *ival)
{
	char	*ptr;
	if ( GetField(record_info, fid, &ptr) ) {
		*ival = atoi(ptr);
		return TRUE;
	}
	return FALSE;
}

#ifndef TWO
int
GetDoubleField(RECORD_INFO *record_info, int fid, double *dval)
{
	char	*ptr;
	if ( GetField(record_info, fid, &ptr) ) {
		if ( string2double(ptr, dval) )
			return TRUE;
		else
			return FALSE;
	}
	*dval = 0.0;
	return FALSE;
}
#endif

int
GetTimeField(RECORD_INFO *record_info, int fid, int *hour, int *minute, int *second)
{
    int		rc;
    char	*ptr;

    if ( GetField(record_info, fid, &ptr) ) {
	if ( !ptr || ptr[0] == '\0' ) {
	    if ( debug )
		fprintf(logfile, "GetTimeField: null time for fid %d\n", fid);
	    return FALSE;
	}

	rc = sscanf(ptr, "%d:%d:%d", hour, minute, second);
	if ( rc == 2 ) {
	    *second = 0;
	} else if ( rc != 3 ) {
	    fprintf(errfile, "GetTimeField: bad time [%s] fid %d\n", ptr, fid);
	    fflush(errfile);
	    return FALSE;
	}
	if ( debug > 2 ) {
	    fprintf(logfile, "GetTimeField: [%s] H:%d M:%d S:%d\n",
		ptr, *hour, *minute, *second);
	    fflush(logfile);
	}
	return TRUE;
    }
    return FALSE;
}

int
GetDateField(RECORD_INFO *record_info, int fid, int *day, int *month, int *year)
{
    int		rc;
    char	*ptr;
    static char monthbuf[25];
    static char *monthNames[12] =  { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
				     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

    if ( GetField(record_info, fid, &ptr) ) {
	if ( ptr[0] == '\0' ) {
	    if ( debug )
		fprintf(logfile, "GetDateField: null date for fid %d\n", fid);
	    return FALSE;
	}

	rc = sscanf(ptr, "%d %s %d", day, monthbuf, year);
	if(rc != 3) {
	    fprintf(errfile, "GetDateField: bad date [%s] fid %d\n", ptr, fid);
	    return FALSE;
	}
	for (*month = 1; *month <= 12; (*month)++)
	    if ( strcmp(monthNames[(*month)-1], monthbuf) == 0 )
		break;
	if (*month == 13) {
	    fprintf(errfile, "GetDateField: bad month '%s' fid %d\n",
		monthbuf, fid);
	    return FALSE;
	}
	if ( debug > 2 ) {
	    fprintf(logfile, "GetDateField: '%s' D:%d M:%d, Y:%d\n",
		    ptr, *day, *month, *year);
	    fflush(logfile);
	}
	return TRUE;
    }
    return FALSE;
}

/* If a speed improvement is needed, this is one of the main places.
   Could hash this easily with 2048 slots, one for each fid,
   - would not work for negative fids.
 */

int
do_this_fid(RECORD_INFO *record_info, int fid)
{
    FID_ARRAY	*current;

    if ( fid == 0 || !record_info ) {
	return FALSE;
    }

    for ( current = record_info->fid_array;
	  current;
	  current = current->next ) {
	if ( fid == current->fid ) {
	    if ( debug > 3 ) {
		fprintf(logfile, "found fid %d\n", fid);
		fflush(logfile);
	    }
	    return TRUE;
	}
    }
    if ( debug > 6 ) {
	fprintf(logfile, "didn't find fid %d\n", fid);
	fflush(logfile);
    }
    return FALSE;
}

#ifdef UNIT_TEST
main()
{
	int	fid;
	char	*dataptr;

	RECORD_INFO	*record_info = create_record_info();

	set_fids(record_info, "1,2,3,4");
	show_record_info(record_info);

	if ( !addToFidArray(record_info, 1, "This is one")) {
		printf("Couldn't addToFidArray 1\n");
	}
	if ( !addToFidArray(record_info, 2, "This is two")) {
		printf("Couldn't addToFidArray 2\n");
	}
	if ( !addToFidArray(record_info, 5, "This is five")) {
		printf("Couldn't addToFidArray 5\n");
	}

	setBeginFidArray(record_info);
	while ( getNextFidArray(record_info, &fid, &dataptr) ) {
		fprintf(logfile, "fid %d: %s:%p\n", fid, dataptr, dataptr);
	}

	free_record_info(record_info);

	return 0;
}
#endif

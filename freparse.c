/*
 * File:	freparse.c
 * Description:	This file contains functions to read and parse the
 *		FRE input lines. The record info structure is allocated,
 *		and populated with required field and other information.
 * Author:	Edward H. Chubin
 * Copyright:	1996 Edward Chubin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>

#include "sslfre.h"
#include "triprice.h"
#include "hash.h"

extern FILE		*infile;
extern FILE		*logfile;
extern FILE		*errfile;
extern int		display_all_fields;
extern int		processUpdates;
extern int		processOnlyUpdates;
extern int		ref_count;
extern int		debug;

extern HashTable	**hashTable;

extern int		request_item(char*,char*,int,void*);
extern int		set_fids(RECORD_INFO*,char*);
extern void		show_record_info(RECORD_INFO*);
extern void		free_record_info(RECORD_INFO*);

extern int		hash_add_entry(HashTable**,char*,char*,void**);
extern int		hash_find_entry(HashTable**,char*,char*,void**);

int
parse_line(char *line)
{
    static char		source[256];
    static char		item[256];
    char		*tmpstr;
    char		*identifier_name = NULL;
    RECORD_INFO		*record_info = NULL;
    IDENTIFIER_ENTRY	*identifier_entry = NULL,
			*identifier_new = NULL;


    tmpstr = strtok(line, " \t");
    if ( tmpstr == NULL ) {
	return FRE_ERROR_INVALID_LINE;
    }

    if ( strlen(tmpstr) > sizeof(source)-1 ) {
	return FRE_ERROR_SOURCE_NAME;
    }

    strcpy(source, tmpstr);

    tmpstr = strtok(NULL, " \t\n");
    if ( tmpstr == NULL ) {
	return FRE_ERROR_ITEM_NAME;
    }

    if ( strlen(tmpstr) > sizeof(item)-1 ) {
	return FRE_ERROR_ITEM_NAME;
    }

    strcpy(item, tmpstr);

    if ( processUpdates ) {

	/*
	 * Use an identifier
	 * should have done it that way anyway
	 */
	tmpstr = strtok(NULL, " \t\n");
	if ( tmpstr == NULL ) {
	    return FRE_ERROR_INVALID_LINE;
	}

	identifier_name = strdup(tmpstr);
	if ( !identifier_name ) {
	    fprintf(errfile, "parse_line: can not allocate memory for identifier\n");
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	}

    } else {

	identifier_name = strdup(item);
	if ( !identifier_name ) {
	    fprintf(errfile, "parse_line: can not allocate memory for identifier\n");
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	}

#ifdef AUTOMATIC_REQUEST_OPTIONS
	/* For the ugly automatic option processing which never
	 * was documented as a feature.
	 * sometimes demo it -- limited functionality not guarenteed to work
	 */
	len_of_original_chain = strlen(item);
	if ( len_of_original_chain < 0 ) {
	    len_of_original_chain = 0;
	}
	if ( !memcmp(item, "0#", 2) ) {
	    len_of_original_chain -= 3;
	}
#endif
    }

    if ( ! display_all_fields ) {
	tmpstr = strtok(NULL, "\n");
	if ( tmpstr ) {
	    if ( strcmp(tmpstr, "ALL") == 0 ) {
		record_info->do_all_fids  = TRUE;
#ifdef AUTOMATIC_REQUEST_OPTIONS
	    } else {
		if ( strncmp(tmpstr, "REQ_OPT", 7) == 0 ) {
		    tmpstr += 8;
		    request_option = TRUE;
		}
#endif
	    }
	} else {
	    /* no fids specified */
	    return FRE_ERROR_NO_FIDS;
	}
	if ( debug > 5 ) {
	    fprintf(logfile, "parse_line: fids are %s\n", tmpstr);
	    fflush(logfile);
	}
    }

    /* Does program know about this item? */
    if ( hash_find_entry(hashTable, source, item, (void**)&record_info) ) {

	/* Add the original item to the simple linked list.
	 * If not processing updates, at least we know how many.
	 */
	if ( debug > 2 ) {
	    fprintf(logfile, "parse_line: %s:%s duplicate handling\n",
		source, item);
	    fflush(logfile);
	}

	/*
	 * Don't need a new structure, use the old.
	 * If requested fields are different
	 * Then there may be a problem
	 * (A good argument for only one list of fields for file)
	 * Always add an identifier -- handles duplicates
	 */

	identifier_entry = (IDENTIFIER_ENTRY*) record_info->other_data;

	while ( identifier_entry->next ) {
	    identifier_entry = identifier_entry->next;
	}
	identifier_new = malloc(sizeof(IDENTIFIER_ENTRY));
	if ( !identifier_new ) {
	    fprintf(errfile, "parse_line: can not allocate memory for identifier\n");
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	}
	identifier_new->name = identifier_name;
	identifier_new->next = NULL;
	identifier_entry->next = identifier_new;

	/*
	 * Output additional data for images and/or updates.
	 * If expecting a sync/image or only processing updates
	 * Then do not request data again
	 */
	if ( record_info->status == FRE_STATUS_PENDING || processOnlyUpdates ) {
	    return 0;
	}

    } else {

	/* Build a record data structure including:
	 * 1. Do all fids
	 * 2. Fid array
	 * 3. Do option
	 * 4. Length of original ric
	 */
	record_info = create_record_info();
	if ( !record_info ) {
	    fprintf(errfile, "parse_line: can not allocate memory for record_info\n");
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	}

#ifdef AUTOMATIC_REQUEST_OPTIONS
	record_info->request_option = request_option;
#endif

	if ( !set_fids(record_info, tmpstr) ) {
	    return FRE_ERROR_NO_MEMORY;
	}

	if ( debug > 3 ) {
	    show_record_info(record_info);
	}

	identifier_new = (IDENTIFIER_ENTRY *)malloc(sizeof(IDENTIFIER_ENTRY));
	if ( !identifier_new ) {
	    fprintf(errfile, "parse_line: can not allocate memory for identifier\n");
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	}
	identifier_new->name = identifier_name;
	identifier_new->next = NULL;

	record_info->other_data = (void *)identifier_new;

	hash_add_entry(hashTable, source, item, (void**)&record_info);
    }

    if ( !request_item(source, item, processUpdates, record_info) ) {
	fprintf(errfile, "request_item(): failed\n");
	fflush(errfile);
	free_record_info(record_info);
	return FRE_ERROR_NO_MEMORY;
    } else {
	++ref_count;
	record_info->status = FRE_STATUS_PENDING;
    }

    if ( logfile && debug ) {
	fprintf(logfile, "parse_line(): %s %s (ref_count %d)\n",
		source, item, ref_count);
	fflush(logfile);
    }

    return 0;
}


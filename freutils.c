/*
 * File:	freutils.c
 * Author:	Edward H. Chubin
 * Description:	This file contains callbacks which are required for using the
 *		sslsubs layer on top of the SSL 3/4 API. It's the glue to
 *		the sslsub layer from the program. This is where most of the
 *		functionality of the program lives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>
#include <time.h>

#include "sslfre.h"
#include "triprice.h"
#include "hash.h"

extern FILE		*logfile;
extern FILE		*outfile;
extern FILE		*errfile;
extern FILE		*badricfile;
extern int		display_all_fields;
extern int		ref_count;
extern char		delimiter_start;
extern char		delimiter_end;
extern int		useFidFormats;
extern int		Precision;
extern int		fill_null_data;
extern int		processUpdates;
extern int		processOnlyUpdates;
extern int		display_timestamp;
extern int		debug;
extern HashTable	**hashTable;

extern int	request_item(char*,char*,int,void*);
extern void	free_record_info(RECORD_INFO*);
extern void	free_record_data(RECORD_INFO*);
extern void	show_record_info(RECORD_INFO*);
RECORD_INFO	*duplicate_record_info(RECORD_INFO*);

extern int	hash_find_entry(HashTable**,char*,char*,void**);
extern int	hash_add_entry(HashTable**,char*,char*,void**);
extern int	hash_delete_entry(HashTable**,char*,char*);

extern void	convert_to_decimal(char*,char*,int,int);	/* price_to_float.c */
extern int	getFidFormat(int,int*,int*);		/* ? */
extern int      MFis_price(int,char*);

extern int	do_this_fid(RECORD_INFO*,int);
extern int	addToFidArray(RECORD_INFO*,int,char*);
extern void	setBeginFidArray(RECORD_INFO*);
extern int	getNextFidArray(RECORD_INFO*,int*,char**);

extern char	*mf_parse(char*,char,char*);

static int	process_fid(int,char*,char*,char*,RECORD_INFO*);
static char	*getTimestamp(void);

#define RS  0x1e
#define US  0x1f

/*
 * SSL4 provides individual item status
 * want to provide additional status if updating
 */
void
do_global_status(int code, char	*text, void *ClientData)
{
#if lint
    ClientData = ClientData;
#endif

    if ( processUpdates ) {
	fprintf(outfile, "GlobalStatus: %s\n", text);
	fflush(outfile);
    }

    fprintf(errfile, "GlobalStatus: %s: code = %d\n", text, code);
    fflush(errfile);
}

void
do_record_status(int code, char *source_name, char *item_name, char *text,
		void *client_data, void *ClientData)
{
    IDENTIFIER_ENTRY	*identifier_entry,
			*last = NULL;
    int			ret;
    RECORD_INFO		*record_info = (RECORD_INFO *)client_data;
#if lint
    ClientData = ClientData;
#endif

/* Remmember, ssl4 will continue to request sometimes */

    if ( !record_info ) {
	fprintf(errfile, "Bad record_info for %s %s\n", source_name, item_name);
	fflush(errfile);
	return;
    }

    if ( record_info->status == FRE_STATUS_PENDING && ref_count > 0 ) {
	--ref_count;
    }

    ret = hash_delete_entry(hashTable, source_name, item_name);
    if ( !ret ) {
	fprintf(errfile, "Can't find %s %s\n", source_name, item_name);
	fflush(errfile);
	return;
    }

    if ( logfile ) {
	fprintf(logfile, "source: %s item: %s text: %s (ref_count %d)\n",
		    source_name, item_name, text, ref_count);
	fflush(logfile);
    }

    /*
     * For each identifier entry
     */
    for ( identifier_entry = record_info->other_data;
	  identifier_entry;
	  identifier_entry = identifier_entry->next ) {

	char *identifier_name = identifier_entry->name;

	fprintf(badricfile, "%s %s", source_name, item_name);

	if ( processUpdates ) {
	    fprintf(badricfile, " %s", identifier_name);
	} else {
	    /* If not the same item as requested,
	     * Then show the original item using delimiters
	     */
	    if ( strcmp(identifier_name, item_name) ) {
		fprintf(badricfile, "%c%s%c",
		    delimiter_start,
		    identifier_name,
		    delimiter_end);
	    }
	}

	/* write status and code to the error file */
	fprintf(badricfile, " %d %s\n", code, text);
	fflush(badricfile);

	/* Free the identifier list */
	free(identifier_entry->name);
	identifier_entry->name = NULL;
	if ( last ) free(last);
	last = identifier_entry;
    }

    /* Free the last identifier list */
    if ( last ) free(last);

    free_record_info(record_info);
}

#define MF_RESPONSE     340
#define MF_CLOSE        312
#define MF_UPDATE       316
#define MF_CORRECTION   317
#define MF_VERIFY       318

void
do_record_data(char *source_name, char *rec_name, char *data, int length,
		void *client_data, void *ClientData)
{
    register RECORD_INFO *record_info = (RECORD_INFO *)client_data;
    register char	*ptr1 = NULL,
			*ptr2 = data + 1;
    char		*dataptr,
			*end = &data[length];
    static char		newstr[255];
    int			fid;
    int			num_fields = 0;
    int			ret;
    int			precision;
    int			min_width;
    int 		isALink = FALSE;
    int			mf_type = atoi(&data[1]);
    static char		out_buffer[4096]; /* safety in numbers */
    int			out_len = 0;

#if lint
    ClientData = ClientData;
#endif

    if ( !record_info ) {
	fprintf(errfile, "Bad record_info for %s %s\n", source_name, rec_name);
	fflush(errfile);
	return;
    }

    /* Only want updates really, but need some info */
    switch ( mf_type )
    {
    case MF_CORRECTION: /* probably want corrections */
    case MF_UPDATE:
        break;
    case MF_RESPONSE:
    case MF_VERIFY:
        ptr1 = strchr(data + 5, US) + 1;
        ptr2 = strchr(ptr1, US);
        record_info->template = atoi(ptr1);
        /* Need to know that this is the real response - not in recovery! */
        if ( record_info->status == FRE_STATUS_PENDING && ref_count > 0 ) {
            --ref_count;
        }
        break;
    case MF_CLOSE:
        /* ignore closing run */
        if ( debug ) {
            fprintf(logfile, "MF_CLOSE ignored\n");
        }
        return;
    }

    /* if template = 80 or 85, then its a chain --> don't output data */
    if ( record_info->template == 80 ||
	 record_info->template == 85 ||
	 (rec_name[1] == '#' && isdigit(rec_name[0])) ) {
	isALink = TRUE;
    }

    if ( logfile ) {
	fprintf(logfile, "source: %s item: %s (ref %d, template %d)\n",
		source_name, rec_name, ref_count, record_info->template);
    }

    if ( debug > 2 ) show_record_info(record_info);

    /* skip to first field, get location of terminator */
    ptr1 = mf_parse(ptr2, RS, end);
    if ( !ptr1 ) {
	fprintf(errfile,"Ptr1 0\n");
	fflush(errfile);
	return;
    }

    while ( ptr1 ) {
	ptr2 = mf_parse(ptr1 + 1, US, end);
	if ( !ptr2 ) {
	    if ( debug > 6 ) fprintf(logfile,"Break1\n");
	    break;
	}
	*ptr2 = '\0';

	fid = atoi(ptr1 + 1);
	dataptr = ptr2 + 1;

	ptr1 = mf_parse(ptr2 + 1, RS, end);
	if ( !ptr1 ) {
	    if ( debug > 6 ) fprintf(logfile, "Break2\n");
	    break;
	}
	*ptr1 = '\0';

	if ( debug > 5 ) fprintf(logfile, "%d: %s\n", fid, dataptr);

	if ( fid == NEXT_LR || fid == LONGNEXTLR ) {
	    /* For Playback */
	    isALink = TRUE;
	}

	if ( isALink ) {
	    /* chain stuff here */
	    ret = process_fid(fid, dataptr, source_name, rec_name, record_info);
	    if ( ret < 0 ) {
		return;
	    }
	    ++num_fields;
	    continue;
	}

	/*
	 * If if either all fids flags are set,
	 * Then process it now.
	 *
	 * Normally place the data in the fid array storage
	 *  and then cycle through fid array later
	 *
	 * The fids in fid array are stored in the original order
	 */
	if ( record_info->do_all_fids ||
	     display_all_fields ||
	     do_this_fid(record_info, fid) ) {

	    /* Does not even store a non-ascii value */
	    if ( isascii(*dataptr) ) {
		/*
		 * If the Precision or useFidFormats is set
		 * Then do best to interpret the price
		 * FidFormats allows a width spec that simplifies
		 * fixed format displays.
		 */
		if ( Precision && MFis_price(fid, dataptr) ) {
		    double  dval;
		    string2double(dataptr, &dval);
		    sprintf(newstr, "%.*f", Precision, dval);
		    dataptr = newstr;
		} else if ( useFidFormats &&
		     getFidFormat(fid,&precision,&min_width)) {
		    if ( precision > 0 ) {
			convert_to_decimal(dataptr,
					   newstr,
					   precision,
					   min_width);
		    } else {
			memcpy(newstr, dataptr, min_width);
			newstr[min_width] = '\0';
		    }
		    dataptr = newstr;
		}

		if ( record_info->do_all_fids || display_all_fields ) {
		    /*
		     * Output to the buffer
		     */
		    sprintf(&out_buffer[out_len], "%d%c%s%c",
				    fid,
				    delimiter_start,
				    dataptr,
				    delimiter_end);
		    out_len += strlen(&out_buffer[out_len]);
		} else {
		    /* ignore return
		     * Bad if malloc failed
		     * Also means field not set
		     */
		    (void)addToFidArray(record_info,fid,dataptr);
		}
		++num_fields;
	    } else if ( debug > 1 ) {
		fprintf(logfile, "fid %d not ascii\n", fid);
		fflush(logfile);
	    }
	}
    }

    /* really don't need this test because have not stored any fields */
    if ( !isALink && !(record_info->do_all_fids || display_all_fields) ) {
	/*
	 * Use simple iteration
	 */
	setBeginFidArray(record_info);
	while ( getNextFidArray(record_info, &fid, &dataptr) ) {
	    if ( !dataptr || !*dataptr ) {
		if ( !fill_null_data ) {
		    continue;
		}
		if ( Precision && MFis_price(fid, dataptr) ) {
		    sprintf(newstr, "%.*f", Precision, 0.0);
		    dataptr = newstr;
		} else if ( useFidFormats &&
			    getFidFormat(fid, &precision, &min_width) ) {
		    if ( precision > 0 ) {
			convert_to_decimal("0", newstr, precision, min_width);
		    } else {
			memset(newstr, ' ', min_width);
			newstr[min_width] = '\0';
		    }
		    dataptr = newstr;
		} else {
		    dataptr = "";
		}
	    }

	    sprintf(&out_buffer[out_len], "%d%c%s%c",
		    fid,
		    delimiter_start,
		    dataptr,
		    delimiter_end);
	    out_len += strlen(&out_buffer[out_len]);

	    if ( debug > 5 ) {
		fprintf(logfile,"fid %d:%s\n",fid,dataptr);
		fflush(logfile);
	    }
	}
    }

    /* Make sure there is something to print */
    if ( !isALink && num_fields ) {

	IDENTIFIER_ENTRY *identifier_entry;

	/*
	 * For each identifier entry
	 */
	for ( identifier_entry = record_info->other_data;
	      identifier_entry;
	      identifier_entry = identifier_entry->next ) {

	    char *identifier = identifier_entry->name;

	    if ( display_timestamp ) {
		fprintf(outfile, "%s ", getTimestamp());
	    }

	    fprintf(outfile, "%s %s", source_name, rec_name);
	    if ( processUpdates ) {
		/* Always output the identifier */
		fprintf(outfile, " %s", identifier);
	    } else {
		/* If not the same item as requested,
		 * Then show the original item using delimiters
		 */
		if ( *identifier && strcmp(identifier, rec_name) ) {
		    fprintf(outfile, "%c%s%c",
			delimiter_start,
			identifier,
			delimiter_end);
		}
	    }
	    fprintf(outfile, " %s\n", out_buffer);
	    fflush(outfile);
	}
    }

    if ( processUpdates ) {
	free_record_data(record_info);
    } else {
	IDENTIFIER_ENTRY *identifier_entry, *last = NULL;
	/*
	 * For each identifier entry
	 */
	for ( identifier_entry = record_info->other_data;
	      identifier_entry;
	      identifier_entry = identifier_entry->next ) {

	    free(identifier_entry->name);

	    if ( last ) free(last);
	    last = identifier_entry;
	}
	if ( last ) free(last);

	ret = hash_delete_entry(hashTable, source_name, rec_name);
	if ( !ret ) {
	    fprintf(errfile, "Can't find %s %s\n", source_name, rec_name);
	    fflush(errfile);
	}
	free_record_info(record_info);
    }

    if ( logfile ) fflush(logfile);
}

static char *
getTimestamp(void)
{
    static char datestr[16];
    static long last_time = 0;

    void                *pNothing = NULL;
    struct timeval      tv_now;

    int ret = gettimeofday(&tv_now, pNothing);
    if ( ret == -1 ) {
	perror("gettimeofday");
    }

    if ( debug > 3 ) {
	fprintf(logfile, "timeval: %d.%06d\n",
	tv_now.tv_sec, tv_now.tv_usec);
    }

    if ( tv_now.tv_sec != last_time ) {
	// cftime(datestr, "%Y%m%d %H%M%S", &(tv_now.tv_sec));
	struct tm *t = localtime(&(tv_now.tv_sec));
#if linux
	strftime(datestr, sizeof datestr, "%Y%m%d %H%M%S", t);
#else
	strftime(datestr, "%Y%m%d %H%M%S", t);
#endif
	last_time = tv_now.tv_sec;
    }
    return datestr;
}
    
static int
isValidRic(char *string)
{
    for ( ; *string; string++ ) {
	if ( isspace(*string) ) {
	    return FALSE;
	}
    }
    return TRUE;
}

/*
 * process_fid:
 * If it is an update or resync
 * Then different processing is required
 */
static int
process_fid(int fid, char *value, char *source_name, char *rec_name, RECORD_INFO *record_info)
{
    char	*item_name;
    int		LongLink = fid >= LONGLINK_1 && fid <= LONGLINK_14;
    int		Link = fid >= LINK_1 && fid <= LINK_14;
    RECORD_INFO *n_record_info;

#if lint
    rec_name = rec_name;
#endif
    if ( strlen(value) == 0 || !isValidRic(value) ) {
	if ( logfile && debug > 1 ) {
	    if ( fid == NEXT_LR || fid == LONGNEXTLR ) {
		fprintf(logfile, "last_link (ref_count %d)!\n", ref_count);
	    } else if ( Link || LongLink ) {
		fprintf(logfile, "No link defined for FID %d\n", fid);
	    }
	    fflush(logfile);
	}
	return 0;
    }

    if ( LongLink || Link ) {
	item_name = value;

#ifdef AUTOMATIC_REQUEST_OPTION
	/*
	 * ignore the LINK call if (all three)
	 * 1. The record is first link (i.e. 0# prefix), and
	 * 2. The length of record is greater than
	 *     the prefix (0# = 2), the suffix (+,: = 1), and
	 *     the length of the original chain root (1,2,3...)
	 * 3. The record name root matches the RIC.
	 * For Example:
	 *  the chain 0#ED:
	 *  the root is ED, length is 2
	 *  will call the first link because number 2 above
	 *   is not satisfied, because 2+2+1 !< 5
	 * But if were calling an option from 0#ED:
	 *  like, options on EDU3, chain is 0#EDU3+
	 *  the original chain root is ED, length 2
	 *  will not call the first line because number
	 *  2 above is satisfied, because 2+2+1 < 7
	 * Because, it calls the original link, and
	 * the system will oscilate.
	 */
	if ( (fid == LONGLINK_1 || fid == LINK_1)
	     && memcmp(rec_name, "0#", 2) == 0
	     && (ret = strlen(rec_name))
	     > record_info->len_of_original_chain + 3
	     && memcmp(rec_name+2, value, ret-3) == 0 ) {

	    /* ignore it, we've called it */
	    /* could also check the watchlist */
	    if ( logfile ) {
		fprintf(logfile, "%s: not opening item for LINK_%d\n",
			item_name, Link ? fid-LINK_1+1 : fid-LONGLINK_1+1);
		fflush(logfile);
	    }
	    return 0;
	}
#endif

	if ( logfile ) {
	    fprintf(logfile, "%s: opening item for LINK_%d\n",
		    item_name, Link ? fid-LINK_1+1 : fid-LONGLINK_1+1);
	    fflush(logfile);
	}

	/****************************************************
	 ****************************************************
	 *** Rework the handling for multiple items here  ***
	 ****************************************************
	 ****************************************************/
	if ( hash_find_entry(hashTable, source_name, item_name, (void**)&n_record_info) ) {

	    /*
	     * Already requested this link entry
	     * Add another identifier entry to this info from chain
	     * The chain *should* only have one identifier
	     * Otherwise, there are multiple levels of chains
	     */

	    IDENTIFIER_ENTRY *identifier_entry, *old_identifier_entry;

	    for ( old_identifier_entry = record_info->other_data;
		  old_identifier_entry;
		  old_identifier_entry = identifier_entry->next ) {

		char	*identifier = NULL;
		int	found_identifier = FALSE;

		if ( old_identifier_entry->name ) {
		    identifier = strdup(old_identifier_entry->name);
		}

		for ( identifier_entry = n_record_info->other_data;
		      identifier_entry;
		      identifier_entry = identifier_entry->next ) {

		    if ( !strcmp(identifier_entry->name, identifier) ) {
			found_identifier = TRUE;
			break;
		    }
		    if ( !identifier_entry->next ) {
			break;
		    }
		}

		if ( !found_identifier ) {
		    IDENTIFIER_ENTRY *identifier_new =
			(IDENTIFIER_ENTRY *) malloc(sizeof(IDENTIFIER_ENTRY));
		    if ( identifier_new ) {
			identifier_new->name = identifier;
			identifier_new->next = NULL;
			identifier_entry->next = identifier_new;
		    }
		} else if ( identifier ) {
		    free(identifier);
		}
	    }

	    /* Already processing this item
	     *
	     * If snapshots or syncs are needed, and this is an image
	     * Then need to request
	     * If update...do the same ..request
	     */

	    if ( n_record_info->status == FRE_STATUS_PENDING || processOnlyUpdates ) {
		return 0;
	    }

	} else {

	    /* Deep copy record_info structure */
	    char *identifier = strdup(((IDENTIFIER_ENTRY*)(record_info->other_data))->name);

	    n_record_info = duplicate_record_info(record_info);
	    if ( !n_record_info ) {
		fprintf(errfile, "process_fid: duplicate_record_info() failed\n");
		fflush(errfile);
	    }

	    if ( hash_add_entry(hashTable, source_name, item_name, (void**)&n_record_info) ) {
		/* set the IDENTIFIERS */
		IDENTIFIER_ENTRY *identifier_new =
			(IDENTIFIER_ENTRY *) malloc(sizeof(IDENTIFIER_ENTRY));
		if ( identifier_new && identifier ) {
		    identifier_new->name = identifier;
		    identifier_new->next = NULL;
		}
		n_record_info->other_data = identifier_new;
		n_record_info->status = FRE_STATUS_NONE;
	    } else {
		fprintf(errfile, "hash_add_entry [%s] failed\n", item_name);
		fflush(errfile);
		free_record_info(n_record_info);
		return FRE_ERROR_NO_MEMORY;
	    }
	}


	if ( !request_item(source_name, item_name, processUpdates, n_record_info) ) {
	    fprintf(errfile, "process_fid: request_item(%s,%s) failed\n",
		source_name, item_name);
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	} else {
	    n_record_info->status = FRE_STATUS_PENDING;
	    ++ref_count;
	}

#ifdef AUTOMATIC_REQUEST_OPTION
	/*
	 * --> check if it already is an option
	 * check if the length is the
	 * 1. length of the original root 
	 * 2. plus the length of month and year code
	 */
	if ( record_info->request_option &&
	     (int)strlen(value) <= record_info->len_of_original_chain + 3 ){

	    sprintf(item_name, "0#%s+", value);

	    if ( !add_item(source_name, item_name, (void **)&n_record_info)) {
		if ( logfile ) {
		    fprintf(logfile,"%s: outstanding; ignored\n",item_name);
		    fflush(logfile);
		}
	    } else if ( !request_item(source_name, item_name, processUpdates, n_record_info) ) {
		fprintf(errfile, "process_fid: request_item(%s,%s) failed\n",
		    source_name, item_name);
		fflush(errfile);
		return FRE_ERROR_NO_MEMORY;
	    } else {
		n_record_info->status = FRE_STATUS_PENDING;
		++ref_count;
	    }
	}
#endif
    } else if ( fid == NEXT_LR || fid == LONGNEXTLR ) {
	item_name = value;
	if ( logfile ) {
	    fprintf(logfile, "%s: opening next link\n", item_name);
	    fflush(logfile);
	}

	if ( hash_find_entry(hashTable, source_name, item_name, (void**)&n_record_info) ) {
	    /*
	     * *Very* unlikely
	     * Add another identifier entry to this info from chain
	     * The chain *should* only have one identifier
	     */
	    IDENTIFIER_ENTRY *identifier_entry;
	    char *identifier = strdup(((IDENTIFIER_ENTRY*)(record_info->other_data))->name);
	    int	found_identifier = FALSE;

	    for ( identifier_entry = n_record_info->other_data;
		  identifier_entry;
		  identifier_entry = identifier_entry->next ) {
		if ( !strcmp(identifier_entry->name, identifier) ) {
		    found_identifier = TRUE;
		    break;
		}
		if ( !identifier_entry->next ) {
		    break;
		}
	    }
	    if ( !found_identifier ) {
		IDENTIFIER_ENTRY *identifier_new =
			(IDENTIFIER_ENTRY *) malloc(sizeof(IDENTIFIER_ENTRY));
		if ( identifier_new ) {
		    identifier_new->name = identifier;
		    identifier_new->next = NULL;
		    identifier_entry->next = identifier_new;
		}
	    }

	    /* If not outstanding, and not only doing updates, send request */
	    if ( n_record_info->status == FRE_STATUS_PENDING || processOnlyUpdates ) {
		return 0;
	    }
	} else {
	    /*
	     * Deep copy record_info structure
	     */
	    char *identifier = strdup(((IDENTIFIER_ENTRY*)(record_info->other_data))->name);

	    n_record_info = duplicate_record_info(record_info);
	    if ( !n_record_info ) {
		fprintf(errfile, "process_fid: duplicate_record_info() failed\n");
		fflush(errfile);
	    }

	    if ( hash_add_entry(hashTable, source_name, item_name, (void**)&n_record_info) ) {
		/* set the IDENTIFIERS */
		IDENTIFIER_ENTRY *identifier_new =
			(IDENTIFIER_ENTRY *) malloc(sizeof(IDENTIFIER_ENTRY));
		if ( identifier_new && identifier ) {
		    identifier_new->name = identifier;
		    identifier_new->next = NULL;
		}
		n_record_info->other_data = identifier_new;
		n_record_info->status = FRE_STATUS_NONE;
	    } else {
		fprintf(errfile, "hash_add_entry [%s] exists\n", item_name);
		fflush(errfile);
		free_record_info(n_record_info);
		return FRE_ERROR_NO_MEMORY;
	    }
	}

	if ( !request_item(source_name, item_name, processUpdates, n_record_info) ) {
	    fprintf(errfile, "process_fid: request_item(%s,%s) failed\n",
		source_name, item_name);
	    fflush(errfile);
	    return FRE_ERROR_NO_MEMORY;
	} else {
	    n_record_info->status = FRE_STATUS_PENDING;
	    ++ref_count;
	}
    }
    return 0;
}

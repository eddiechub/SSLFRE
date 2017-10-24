/*
 * File:	page.c
 * Author:	Edward Chubin
 * Description:	This file implements the programs page, record, ts, and n2k.
 *		The program checks its name to determine the output format.
 *		The page program will output either a small or large IDN page.
 *		The record program will display any or all data fields.
 *		The ts program outputs time and sales prices.
 *		The n2k program outputs news 2000 stories.
 *		All programs require the exact specification of the ric.
 *		Uses the triprice layer on top of the sslsub layer.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>

#include "triprice.h"

extern RECORD_INFO	*create_record_info();
extern void	set_all_fids();
extern int	set_fids();
extern void	show_record_info();
extern void	free_record_info();
static int	GetPage1FromInfo();
static int	GetPage2FromInfo();
static int	IdnGetNextField();
extern void	setBeginFidArray();
extern int	getNextFidArray();

#ifdef __STDC__
static int	doit(char [],char [],char []);
static int	buffer_is_spaces(char *, int);
extern int	MFis_price(int,char*);
extern void	read_field_info(void);
extern char	*MFacronym(int);
#else
static int	doit();
static int	buffer_is_spaces();
extern int	MFis_price();
extern void	read_field_info();
extern char	*MFacronym();
#endif

#ifdef WIN32
static char	*optarg;
static int	optind;
#else
extern char	*optarg;
extern int	optind;
#endif
extern int	debug;
extern FILE	*logfile;
static FILE	*outfile = stdout;

static char	*prog_name;
static int	first = TRUE;
static int	Precision = 0;
static int	specialPERM = FALSE;
static int	expand_fid_acronym = FALSE;

typedef enum { F_FID_PER_ROW, F_FRE_FORMAT, F_COLUMN_FORMAT } FORMAT;
static FORMAT	format = F_FID_PER_ROW;

static void
usage()
{
fprintf(stderr,
"usage: %s -vCFR [-f fidlist] [-o outfile] [-s source] [-S sink]\n\
\t\t[-c precision] [-l last_page] symbols ...\n\
where:\n\
\t-v Verbose\n\
\t-a Expand Acronyms for record display in column format only\n\
\t-o Specify an output file. Default is stdout.\n\
\t-c Convert all prices to decimal precision (record only).\n\
\t-C Display in column format.\n\
\t-F Display in FRE format fid(value).\n\
\t-R Display Row by Row. This is the default.\n\
\t-l Reuters permission page handling. Specify the last page\n\
\t-f Specify fids. The default is all fids.\n\
\t   Can use a Comma delimited list or multiple specify -f.\n\
\t-s Specify Service name. The default is IDN_SELECTFEED\n\
\t   or the value in the environment variable SSL_SOURCE_NAME.\n\
\t-S Specify Sink name. The default is triarch_sink.\n", prog_name);
}

#define DOIT_NOT_OK	0
#define DOIT_OK		1
#define DOIT_AGAIN	2

int
#ifdef __STDC__
main(int argc, char *argv[])
#else
main(argc, argv)
int	argc;
char	*argv[];
#endif
{
    int	ret, last_page;
    char	ric[40];
    char	buf[1024];
    char	*source_name;

    prog_name = (char *)strrchr(argv[0], '/');
    if ( !prog_name ) {
	prog_name = argv[0];
    } else {
	++prog_name;
    }

    if ( !(source_name = getenv("SSL_SOURCE_NAME")) ) {
	source_name = "IDN_SELECTFEED";
    }

    strcpy(buf, "");
#ifdef sun
    while ((ret = getopt(argc, argv, "ac:f:s:S:o:CFRvl:")) != -1) {
#elif defined(WIN32)
    for ( optind = 1; optind < argc && argv[optind][0] == '-'; optind++ ) {
	ret = argv[optind][1];
	if ( ret == 'c' || ret == 'f' || ret == 's' || ret == 'o' || ret == 'l' ) {
	    optarg = argv[++optind];
	}
#else
    while ((ret = getopt(argc, argv, "c:f:s:o:CFRvl:")) != -1) {
#endif
	switch (ret) {
	case 'o': /* outfile */
	    outfile = fopen(optarg, "w+");
	    if ( !outfile ) {
		fprintf(stderr, "Cannot write outfile\n");
		perror(optarg);
		return ret;
	    }
	    break;
	case 'f': /* fid */
	    if ( strlen(buf) > 0 ) {
		    strcat(buf, ",");
	    }
	    strcat(buf, optarg);
	    break;
	case 's': /* source name */
	    source_name = optarg;
	    if ( debug ) {
		printf("source_name is now: \"%s\"\n", source_name);
	    }
	    break;
	case 'S': /* sink_name */
#ifdef sun
	    {
		char	buf2[256];
		sprintf(buf2,"SSL_SINK_NAME=%s",optarg);
		putenv(buf2);
	    }
	    if ( logfile ) {
		printf("sink_name is now: \"%s\"\n", optarg);
	    }
#else
	    printf("sorry, can't set sink name\n");
#endif
	    break;
	case 'c': /* convert prices */
	    Precision = atoi(optarg);
	    if ( Precision <= 0 ) {
		Precision = 6;
	    }
	    break;
	case 'a': /* column format */
	    expand_fid_acronym = TRUE;
	    break;
	case 'C': /* column format */
	    format = F_COLUMN_FORMAT;
	    break;
	case 'F': /* fre format */
	    format = F_FRE_FORMAT;
	    break;
	case 'R': /* fid per row format (DFLT) */
	    format = F_FID_PER_ROW;
	    break;
	case 'l':
	    specialPERM = TRUE;
	    last_page = atoi(optarg) - 1;
	    if ( last_page <= 0 ) {
		last_page = 999;
	    }
	    break;
	case 'v':
	    ++debug;
	    logfile = stderr;
	    break;
	default:
	    printf("ret %c\n", ret);
	    usage();
	    return ret;
	}
    }

    if ( expand_fid_acronym || Precision ) {
	read_field_info();
    }

    if ( logfile && strlen(buf) > 0 ) {
	fprintf(logfile, "buf is now: %s\n", buf);
    }

    if ( optind == argc ) {
	printf("Enter RIC: ");
	if ( gets(ric) ) {
	    do {
		ret = doit(ric, source_name, buf);
	    } while ( ret == DOIT_AGAIN );
	}
    } else {
	if ( specialPERM ) {
	    int page = 0;
	    do {
		sprintf(ric, "%s%03d", argv[optind], page);
		ret = doit(ric, source_name, buf);
	    } while ( ret == DOIT_AGAIN && ++page <= last_page );
	    fputc('\n', outfile);
	} else {
	    for ( ; optind < argc; optind++ ) {
		first = TRUE;
		strcpy(ric, argv[optind]);
		do {
		    ret = doit(ric, source_name, buf);
		} while ( ret == DOIT_AGAIN );
		fputc('\n', outfile);
	    }
	}
    }

    doneWithTriarch();
    return !ret;
}

#define PAGE_HEIGHT	14
#define PAGE_WIDTH	64
#define TOTAL_PAGE_SIZE	(PAGE_HEIGHT * PAGE_WIDTH)

#define PAGE_HEIGHT_2	25
#define PAGE_WIDTH_2	80
#define TOTAL_PAGE_SIZE_2 (PAGE_HEIGHT_2 * PAGE_WIDTH_2)

static int
buffer_is_spaces(buffer, size)
char	*buffer;
int	size;
{
	int i;
	for ( i = 0; i < size; i++ ) {
		if ( buffer[i] != ' ' ) {
			return FALSE;
		}
	}
	return TRUE;
}

static int
doit(ric, source, fidbuf)
char	ric[];
char	source[];
char	fidbuf[];
{
    int		fid;
    char	*data;
    int		pos;
    int		ret = DOIT_OK;
    RECORD_INFO	*record_info = create_record_info();
    char	buf[1024];

    if ( !record_info ) {
	fprintf(stderr, "Could not create record info structure\n");
	perror("malloc");
	return DOIT_NOT_OK;
    }

    if ( strncmp(prog_name, "page", 4) == 0 ) {
	char	buf2[10];
	buf[0] = '\0';
	for ( pos = ROW64_1; pos <= ROW64_14; pos++ ) {
	    sprintf(buf2,"%d,",pos);
	    strcat(buf, buf2);
	}
	for ( pos = ROW80_1; pos <= ROW80_25; pos++ ) {
	    sprintf(buf2,"%d,",pos);
	    strcat(buf, buf2);
	}
	/* get rid of last ',' */
	buf[strlen(buf)-1] = '\0';
	if ( !set_fids(record_info, buf) ) {
	    fprintf(stderr, "Could not set_fids\n");
	    return DOIT_NOT_OK;
	}
    } else if ( strncmp(prog_name, "record", 6) == 0 ) {
	if ( strlen(fidbuf) > 0 ) {
	    strcpy(buf, fidbuf);
	    if ( !set_fids(record_info, buf) ) {
		fprintf(stderr, "Could not set_fids\n");
		return DOIT_NOT_OK;
	    }
	} else {
	    set_all_fids(record_info);
	}
    } else if ( ( strncmp(prog_name, "n2k", 3) == 0 ) ||
		( strncmp(prog_name, "ts", 2) == 0 ) ) {
	strcpy(buf, "258,238");
	if ( !set_fids(record_info, buf) ) {
	    fprintf(stderr, "Could not set_fids\n");
	    return DOIT_NOT_OK;
	}
    } else {
	fprintf(stderr, "Unknown program\n");
	return DOIT_NOT_OK;
    }

    show_record_info(record_info);

    ret = GetTriarchData(record_info, ric, source);
    if ( !ret ) {
	fprintf(stderr, "Triarch Problem: can't get ric %s\n", ric);
	free_record_info(record_info);
	return DOIT_NOT_OK;
    }

    if ( !strncmp(prog_name, "page", 4) ) {
	char	page_buffer[TOTAL_PAGE_SIZE+1];
	ret = GetPage1FromInfo(record_info, page_buffer);
	if ( !ret ) {
	    char	page2_buffer[TOTAL_PAGE_SIZE_2+1];
	    ret = GetPage2FromInfo(record_info, page2_buffer);
	    if ( !ret ) {
		fprintf(stderr, "Can't get page data; %s is not a page\n", ric);
		free_record_info(record_info);
		return FALSE;
	    }
	    for ( pos = specialPERM ? PAGE_WIDTH_2 : 0;
		  pos < TOTAL_PAGE_SIZE_2;
		  pos += PAGE_WIDTH_2 ) {

		if ( !buffer_is_spaces(&page2_buffer[pos], PAGE_WIDTH_2) ) {
		    if ( specialPERM && !isspace(page2_buffer[pos]) ) {
			fputc('\n', outfile);
		    }
		    fwrite(&page2_buffer[pos], PAGE_WIDTH_2, 1, outfile);
		    if ( !specialPERM ) {
			fputc('\n', outfile);
		    } else if ( page2_buffer[pos] == '#' ) {
			fputc('\n', outfile);
			ret = DOIT_OK;
			break;
		    }
		}
		if ( specialPERM ) {
		    ret = DOIT_AGAIN;
		}
	    }
	    if ( first ) first = FALSE;
	} else {
	    for ( pos = first ? 0 : PAGE_WIDTH;
		  pos < TOTAL_PAGE_SIZE - 3 * PAGE_WIDTH;
		  pos += PAGE_WIDTH ) {

		if ( !buffer_is_spaces(&page_buffer[pos], PAGE_WIDTH) ) {
		    fwrite(&page_buffer[pos], PAGE_WIDTH, 1, outfile);
		    fputc('\n', outfile);
		}
	    }

	    if ( first ) first = FALSE;

	    /* could skip the first line on pages after first
	       could also skip last two lines on every bottom */
	    data = (char *)&page_buffer[892];
	    if ( isalnum(*data) && isupper(*data) &&
		 isalnum(*(data+1)) && isupper(*(data+1)) &&
		 isalnum(*(data+2)) && isupper(*(data+2)) &&
		 isalnum(*(data+3)) && isupper(*(data+3)) ) {
		data[4] = '\0';
		if ( logfile ) {
		    fprintf(logfile, "Next page is %s\n", data);
		}
		strcpy(ric, data);
		ret = DOIT_AGAIN;
	    }
	}
    } else if ( strncmp(prog_name, "record", 6) == 0 ) {
	fid = 0;
	while ( IdnGetNextField(record_info, &fid, &data) ) {
	    char *acronym = NULL;
	    if ( Precision && MFis_price(fid, data) ) {
		double	dval;
		string2double(data, &dval);
		sprintf(buf, "%.*f", Precision, dval);
		data = buf;
	    }
	    switch( format ) {
	    case F_COLUMN_FORMAT:
		fprintf(outfile, "%s ", data?data:"-NA-");
		break;
	    case F_FRE_FORMAT:
		fprintf(outfile, "%d(%s)",fid, data?data:"");
		break;
	    case F_FID_PER_ROW:
	    default:
		if ( expand_fid_acronym ) {
		    acronym = MFacronym(fid);
		    if ( !acronym && debug ) {
			fprintf(stderr, "Could not expand fid %d\n", fid);
		    }
		}
		if ( !acronym ) {
		    static char ibuf[10];
		    sprintf(ibuf,"%d",fid);
		    acronym = ibuf;
		}
		fprintf(outfile, "%s: %s\n",acronym,data?data:"NO SUCH FIELD");
		break;
	    }
	}
	ret = DOIT_OK;
    } else if ( ( strncmp(prog_name, "n2k", 3) == 0 ) ||
		( strncmp(prog_name, "ts", 2) == 0 ) ) {
	static char	next_link[40];
	next_link[0] = '\0';
	fid = 0;
	while ( IdnGetNextField(record_info, &fid, &data) ) {
	    if ( !data || !*data ) {
		continue;
	    }

	    if ( fid == 238 ) {
		strcpy(next_link, data);
	    } else if ( fid == 258 ) {
		if ( strncmp(prog_name, "ts", 2) == 0 ) {
		    char *ptr = strtok(data, "\n");
		    while ( ptr ) {
			if ( !buffer_is_spaces(ptr, strlen(ptr)) ) {
			    fprintf(outfile, ptr);
			    fprintf(outfile, "\n");
			    fflush(outfile);
			}
			ptr = strtok(NULL, "\n");
		    }
		} else {
		    fprintf(outfile, data);
		    fflush(outfile);
		}
	    }
	}
	if ( strlen(next_link) > 0 ) {
	    if ( logfile ) {
		fprintf(logfile, "\nNext link is \"%s\"\n", data);
	    }
	    strcpy(ric, next_link);
	    ret = DOIT_AGAIN;
	} else {
	    if ( logfile ) {
		fprintf(logfile, "\nNo next link\n");
	    }
	    ret = DOIT_OK;
	}
    } else {
	ret = DOIT_NOT_OK;
    }

    free_record_info(record_info);

    return ret;
}

static int
IdnGetNextField(record_info, fid, data)
RECORD_INFO *record_info;
int	*fid;
char	**data;
{
	if ( *fid == 0 ) {
	    setBeginFidArray(record_info);
	}

	if ( getNextFidArray(record_info, fid, data) ) {
	    return TRUE;
	}

	return FALSE;
}

static int
GetPage1FromInfo(record_info, page_buffer)
RECORD_INFO *record_info;
char	*page_buffer;
{
    int	ret = FALSE;
    int	fid;
    char *data;

    memset(page_buffer, 0, TOTAL_PAGE_SIZE+1);

    setBeginFidArray(record_info);
    while ( getNextFidArray(record_info, &fid, &data) ) {
	if ( data && *data && (fid >= ROW64_1) && (fid <= ROW64_14) ) {
	    ret = TRUE;
	    memcpy(&page_buffer[(fid-ROW64_1)*PAGE_WIDTH], data, PAGE_WIDTH);
	}
    }
    return ret;
}

static int
GetPage2FromInfo(record_info, page_buffer)
RECORD_INFO *record_info;
char	*page_buffer;
{
    int	ret = FALSE;
    int	fid;
    char *data;

    memset(page_buffer, 0, TOTAL_PAGE_SIZE_2+1);

    setBeginFidArray(record_info);
    while ( getNextFidArray(record_info, &fid, &data) ) {
	if ( data && *data && (fid >= ROW80_1) && (fid <= ROW80_25) ) {
	    ret = TRUE;
	    memcpy(&page_buffer[(fid-ROW80_1)*PAGE_WIDTH_2], data, PAGE_WIDTH_2);
	}
    }
    return ret;
}

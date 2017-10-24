/************************************************************************
 *
 * Program:	SSL Field Retrieval Engine (sslfre)
 *
 * Author:	Edward H. Chubin
 *
 * Company:	Reuters Information Management Systems (IMS) (-1994)
 * Company:	Vanguard Software (1994-1995)
 * Company:	None (1995-1996)
 *
 * Description:	This program reads the specified input file or the
 *		standard input for a TRIARCH 2000 record service name,
 *		a record name from that service, and a list of fields.
 *		The SSL API is then used to retrieve the required information.
 *
 * Version:	0.1	05-21-93	First controlled version
 * 		1.0	05-21-93	First Shipped version
 *		1.1	06-01-94	Corrected some errors in reading lines.
 *		1.2	08-03-94	Changed at SocGen for bid prob
 *		1.3	08-15-94	Adding new feature which allows an
 *					optionally specified file containing
 *					FID's which should be converted to
 *					decimal.
 *		1.4	04-10-95	Corrected Marketfeed parsing.
 *		1.5	04-24-95	Added function to read line from file
 *					and queue the request.
 *					Need to create structures to fill fields
 *					because the implementation will not do
 *					the fid stuff correct now.
 *		1.6	05-17-95	Finished Structures. Purified.
 *					Tested. Works very fast and accurate.
 *		1.7	02-27-96	Fixed bug in convert_to_decimal()
 *					Didn't do negative right on 0 whole.
 *					Added precision specification to
 *					fidfile.
 *		1.8	03-26-96	Restructured, tested heavily.
 *					Does ordered fid list.
 *		1.9	12-16-96	Latest 4.0 SSL. Reworked functionality
 *		4.0	06-05-97	Fixes/Updates
 *		5.0	06-12-98	Completed wildcard src functionality
 *		6.0	10-25-98	Begin work on update functionality
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <ctype.h>

#include "sslfre.h"
#include "hash.h"

#ifndef lint
static char Version[] =
	{"@(#)SSL Field Retrieval Engine Release 5.0 (11/23/1998)"};
static char File[] = "FILE: sslfre.c 10-28-1998";
#endif

FILE		*logfile;
FILE		*outfile;
FILE		*errfile;
FILE		*badricfile;
FILE		*infile;
int		display_all_fields = FALSE;
int		ref_count = 0;
char		delimiter_start = DFL_DELIMITER_START;
char		delimiter_end = DFL_DELIMITER_END;
int		useFidFormats = FALSE;
int		fill_null_data = FALSE;
int		display_timestamp = FALSE;
char		line[MAXLINE];
char		line_save[MAXLINE];
int		line_number = 0;
int		debug = 0;
int		rewind_file = FALSE;
int		Precision = 0;

#define DFL_HASH_TABLE_SIZE     10000
HashTable	**hashTable = NULL;
static unsigned	hashTableSize = DFL_HASH_TABLE_SIZE;

static char	*freSinkName = NULL;
static char	freUserName[255];
static char	*prog_name;
static int	makeFastRequests = FALSE;
int		processUpdates = FALSE;
int		processOnlyUpdates = FALSE;

extern int	InitSSL(char*,char*);
extern void	CloseSSL(void);
extern void	GetSSLData(int);
extern void     read_field_info(void);
extern int	read_fid_format_file(char*);
extern int      MFis_price(int,char*);
extern int	do_next_line(int);
extern HashTable **initHashTable(unsigned int);
extern void	destroyHashTable(HashTable**);

#ifdef _WIN32
#include <io.h>
#include <conio.h>
#include <winsock.h>
char getopt(int argc, char** argv, char *optstring);
char *optarg;
int optind=1;
#define __STDC__

HANDLE hStdOut;
CONSOLE_SCREEN_BUFFER_INFO csbiConsole;
#endif

static void
usage(void)
{
    fprintf(stderr, 
"usage: %s [-azFN] [-i inputfile] [-o outfile] [-b errfile]\n\
\t[-e badricfile] [-l logfile] [-c precision] [-d <d1>[<d2>]]\n\
\t[-S sink_name] [-f fid_format_file] <infile> <outfile>\n\
\t-a:\tOutput all fids each symbol. Overrides input line specification.\n\
\t-c:\tConvert prices to fixed point with specified precision.\n\
\t-b:\tError output filename. Defaults to stderr.\n\
\t-d:\tDelimiters between fields. Defaults to ()\n\
\t-e:\tBad symbol filename. Prints status. Defaults to stderr.\n\
\t-f:\tFid Format filename. Specifies fixed point conversion\n\
\t\tspecifications. Also specifies field widths to fill null data\n\
\t-i:\tInput filename. Lines should be in the format\n\
\t\tSOURCE_NAME ITEM_NAME fid1,fid2,...\n\
\t-l:\tLog filename. Very verbose. Used primarily for debugging.\n\
\t-o:\tOutput filename.\n\
\t-z:\tPrint this help message.\n\
\t-F:\tMake requests as fast as possible. The requests are processed\n\
\t\tasyncronously so the output order may be different.\n\
\t-R:\tRewind (use for debugging)\n\
\t-S:\tSpecified the SSL Sink Server Name. This name is mapped\n\
\t\tin the ipcroute file. Defaults to \"triarch_sink\".\n\
\t-T:\tDisplay timestamps before each output line\n\
\t-N:\tFill null data. Use widths in the fid_format_file.\n", prog_name);
    fflush(stderr);
}

/*
 * Function:	main()
 * Description:	read program arguments and manage requests while they exist
 *		until there is no more data to be read.
 */
int
main(int argc, char **argv)
{
    int		ret;
    char	*tmpstr;
    static char	fidFormatFile[255];
#ifndef WIN32
    extern char	*optarg;
    extern int	optind;
#else
    int		optind;
    char	*optarg;
#endif

    prog_name = argv[0];
    logfile = NULL;
    outfile = stdout;
    errfile = stderr;
    badricfile = stderr;
    infile = stdin;

#ifndef WIN32
    while ( (ret = getopt(argc, argv, "azc:i:o:e:b:l:d:f:FNS:uvRT")) != -1 ) {
#else
    for ( optind = 1; optind < argc && argv[optind][0] == '-'; optind++ ) {
	ret = argv[optind][1];
	switch (ret) {
	case 'c': case 'd': case 'i': case 'o': case 'b':
	case 'e': case 'l': case 'f': case 'S':
	    optarg = argv[++optind];
	    break;
	}
#endif
	switch (ret) {
	case 'a':
		display_all_fields = TRUE;
		break;
	case 'c': /* convert prices */
		Precision = atoi(optarg);
		if ( Precision <= 0 ) {
		    Precision = 6;
		}
		read_field_info();
		break;
	case 'd':
		if ( logfile ) {
			fprintf(logfile, "delimiters: [%s]\n", optarg);
			fflush(logfile);
		}
		if ( strlen(optarg) > 2 ) {
			fprintf(stderr,
			    "%s: specify one or two delimiters\n", prog_name);
			fflush(stderr);
			exit(2);
		}
		if ( strlen(optarg) == 2 ) {
			delimiter_start = optarg[0];
			delimiter_end = optarg[1];
		}
		else if ( strlen(optarg) == 1 ) {
			delimiter_start = optarg[0];
			delimiter_end = optarg[0];
		}
		break;
	case 'i':
		infile = fopen(optarg, "r");
		if ( infile == NULL ) {
			fprintf(stderr,
				"%s: can not open (%s) for reading\n",
				argv[0], optarg);
			fflush(stderr);
			exit(2);
		}
		break;
	case 'o':
		outfile = fopen(optarg, "w");
		if ( outfile == NULL ) {
			fprintf(stderr,
				"%s: can not open (%s) for writing\n",
				argv[0], optarg);
			fflush(stderr);
			exit(3);
		}
		break;
	case 'b':
		errfile = fopen(optarg, "w");
		if ( errfile == NULL ) {
			fprintf(stderr,
				"%s: can not open (%s) for writing\n",
				argv[0], optarg);
			fflush(stderr);
			exit(4);
		}
		break;
	case 'e':
		badricfile = fopen(optarg, "w");
		if ( badricfile == NULL ) {
			fprintf(stderr,
				"%s: can not open (%s) for writing\n",
				argv[0], optarg);
			fflush(stderr);
			exit(5);
		}
		break;
	case 'l':
		logfile = fopen(optarg, "w");
		if ( !logfile ) {
			fprintf(stderr,
				"%s: can not open (%s) for writing\n",
				argv[0], optarg);
			fflush(stderr);
			exit(6);
		}
		break;
	case 'N':
		fill_null_data = TRUE;
		break;
	case 'F':
		makeFastRequests = TRUE;
		break;
	case 'f':
		useFidFormats = TRUE;
		strcpy(fidFormatFile, optarg);
		break;
	case 'S': /* ssl_sink_server */
		freSinkName = optarg;
		break;
	case 'u': /* process updates: different input file format */
		processUpdates = TRUE;
		/* has to do fast requests, would otherwise block */
		makeFastRequests = TRUE;
		break;
	case 'U': /* process only updates: different input file format */
		processOnlyUpdates = TRUE;
		processUpdates = TRUE;
		/* has to do fast requests, would otherwise block */
		makeFastRequests = TRUE;
		break;
	case 'v':
		++debug;
		if ( !logfile ) logfile = stderr;
		break;
        case 'H': /* HashTable Size */
		hashTableSize = (unsigned int)atoi(optarg);
		if ( !hashTableSize ) {
		    hashTableSize = DFL_HASH_TABLE_SIZE;
		}
		break;
	case 'R':
		rewind_file = TRUE;
		break;
	case 'T':
		display_timestamp = TRUE;
		break;
	default:
	case '?':
	case 'z':
		usage();
		exit(0);
	}
    }

    if ( infile == stdin && optind < argc ) {
	    infile = fopen(argv[optind], "r");
	    if ( infile == NULL ) {
		    fprintf(stderr,
			    "%s: can not open (%s) for reading\n",
			    argv[0], argv[optind]);
		    fflush(stderr);
		    exit(2);
	    }
    }

    ++optind;

    if ( outfile == stdout && optind < argc ) {
	    outfile = fopen(argv[optind], "w");
	    if ( outfile == NULL ) {
		    fprintf(stderr,
			    "%s: can not open (%s) for writing\n",
			    argv[0], argv[optind]);
		    fflush(stderr);
		    exit(2);
	    }
    }

/*
 * read the fid format file if selected
 */
    if ( useFidFormats && !read_fid_format_file(fidFormatFile) ) {
	    fprintf(stderr,
		    "%s: can not open (%s) for reading\n",
		    argv[0], fidFormatFile);
	    fflush(stderr);
	    exit(2);

    }

    if ( infile == stdin && makeFastRequests && !processUpdates ) {
	    if ( logfile ) {
		    fprintf(logfile, "makeFastRequests set to FALSE (stdin)\n");
		    fflush(logfile);
	    }
	    makeFastRequests = FALSE;
    }

    hashTable = initHashTable(hashTableSize);
    if ( ! hashTable ) {
        perror("malloc");
        fprintf(stderr, "%s: error allocating hashtable\n", argv[0]);
        fflush(stderr);
        return 2;
    }

    if ( logfile && debug > 1 ) {
	    fprintf(logfile, "Initializing SSL\n");
	    fflush(logfile);
    }

    if ( tmpstr = getenv("TRIARCH_USER_NAME") ) {
	    strcpy(freUserName, tmpstr);
    } else {
	    strcpy(freUserName, "");
	    strcat(freUserName, "+260");	/* application id (pick one)*/
    }

    if ( !InitSSL(freUserName, freSinkName) ) {
	    fprintf(errfile, "SSL Init: Failed\n");
	    fflush(errfile);
	    return 9;
    }

    if ( logfile && debug > 1 ) {
	    if ( infile == stdin ) {
		    fprintf(logfile, "Awaiting Request...\n");
	    }
	    else {
		    fprintf(logfile, "Making Requests...\n");
	    }
	    fflush(logfile);
    }

    while ( processUpdates || ref_count > 0 ||
		    (!feof(infile) && do_next_line(1)) ) {

	    if ( (processUpdates || makeFastRequests) && !feof(infile) ) {
		    (void)do_next_line(100);
	    }
	    if ( ref_count || processUpdates ) {
		GetSSLData(!makeFastRequests ||
		    ( feof(infile) && (ref_count || processUpdates) ) );
	    }
    }

    CloseSSL();
    destroyHashTable(hashTable);
    if ( logfile ) {
	fprintf(logfile, "byebye..\n");
	fflush(logfile);
	fclose(logfile);
    }
    fclose(infile);
    fclose(outfile);
    fclose(errfile);
    fclose(badricfile);
    return 0;
}

void
show_error(int line_number, char *line_save, int error_value)
{
	if ( line_save[strlen(line_save)-1]=='\n' ) {
		line_save[strlen(line_save)-1]=0;
	}

	fprintf(badricfile, "%s %d line_number %d\n",
		line_save, error_value, line_number);
	fflush(badricfile);
	return;
}

#ifdef _WIN32
/* This functions returns options one by one from the list of options */
char getopt( int argc, char** argv, char *optstring)
{
   unsigned int i;
   char option;

   if ((optind>=argc) || (*argv[optind]!='-') || (*argv[optind]!='/'))
   /* All options have been processed */
     return -1;

   option = *(argv[optind]+1);
   for (i=0; i<strlen(optstring); i++)
   {
        if (option==optstring[i]) 
          break;
   }

   if (i==strlen(optstring))
   {
        option='?';
        optind++;
        return option;
   }
                
   if (*(optstring+i+1)!=':')
   /* The option doesn't require argument */
   {
      optind ++;
      return option;
   } 
   else  
   {
        optind ++;
        optarg=*(argv+optind);
        optind ++;
        return option;
   }

}
#endif


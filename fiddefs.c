/* File:	fiddefs.c
 * Description:	This file is used as an accessor of field information
 *		It normally reads the reuters appendix_a or field.def files.
 *		The most notable function is  MFis_price(int fid, char *value)
 *		which attempts to determine if field is a price field.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define TRUE	1
#define FALSE	0

#define MAX_FILE_NAME 255

typedef enum {
    FIDTYPE_ALPHANUMERIC,
    FIDTYPE_INTEGER,
    FIDTYPE_ENUMERATED,
    FIDTYPE_DATE,
    FIDTYPE_TIME,
    FIDTYPE_PRICE,
    FIDTYPE_BINARY,
    FIDTYPE_TIME_SECONDS,
    FIDTYPE_OTHER
} FidType;

typedef struct _fidEntry {
    int 	fid;
    int 	rip_field;
    int 	max_length;
    FidType	type;
    char	acronym[25];
    char	rip_acronym[25];
    char	dde_acronym[30];
    struct _fidEntry *next;
} FidEntry;

static FidEntry *fidListHead = NULL;
static FidEntry *fidListTail = NULL;

static char	db_dir[MAX_FILE_NAME];

#ifdef __STDC__
static int parseLine(char *, FidEntry *);
static int parseLineWithDDEAcronym(char *, FidEntry *);
static int parseLineWithoutDDEAcronym(char *, FidEntry *);
int	MFgetfid(char *);
#else
static int parseLine();
static int parseLineWithDDEAcronym();
static int parseLineWithoutDDEAcronym();
int	MFgetfid();
#endif

extern FILE	*logfile;
extern FILE	*errfile;
extern int	debug;

/*
 * If added hashing (i.e. fast lookups) to the mix
 * we could probably use this in wonderful ways!
 * For now the performace is acceptable.
 * Not making big use of it.
 */

void
#ifdef __STDC__
read_field_info(void)
#else
read_field_info()
#endif
{
    FILE	*fp;
    static char	string[256];
    FidEntry	*fidEntry;
    static char	path[MAX_FILE_NAME];
    char	*ptr;

    if ( ptr = getenv("XQUOTE_DB_DIR") ) {
	strcpy(db_dir, ptr);
    } else if ( ptr = getenv("FIELD_DB_DIR") ) {
	strcpy(db_dir, ptr);
    } else {
	strcpy(db_dir, ".");
    }
    strcpy(path, db_dir);
    strcat(path, "/field.defs");
    fp = fopen(path, "r");
    if ( !fp ) {
	/* try to open appendix_a */
	if ( debug ) {
	    fprintf(logfile, "read_field_info: cannot open %s\n", path);
	    fflush(logfile);
	}
	strcpy(path, db_dir);
	strcat(path, "/appendix_a");
	fp = fopen(path, "r");
	if ( !fp ) {
	    fprintf(errfile,"read_field_info: cannot open %s\n", path);
	    fflush(errfile);
	    return;
	}
    }
    if ( debug > 1 ) {
	fprintf(logfile, "read_field_info: loading from %s\n", path);
	fflush(logfile);
    }

    /* X_RIC_NAME */
    fidEntry = (FidEntry*)malloc(sizeof(FidEntry));
    if ( !fidEntry ) {
	fprintf(errfile, "Couldn't malloc FidEntry0\n");
	fflush(errfile);
	fclose(fp);
	return;
    }
    fidEntry->fid = -1;
    fidEntry->rip_field = 0;
    fidEntry->max_length = 17;
    fidEntry->type = FIDTYPE_ALPHANUMERIC;
    fidEntry->next = NULL;
    strcpy(fidEntry->acronym,"X_RIC_NAME");
    strcpy(fidEntry->rip_acronym,"NULL");
    fidListHead = fidEntry;
    fidListTail = fidEntry;

    while ( fgets(string, sizeof(string)-1, fp) ) {
	if ( string[0] == '#' || string[0] == '!' || string[0] == '\0' ) {
	    continue;
	}

	fidEntry = (FidEntry*)malloc(sizeof(FidEntry));
	if ( !fidEntry ) {
	    fprintf(errfile, "Couldn't malloc FidEntry\n");
	    fflush(errfile);
	    fclose(fp);
	    return;
	}

	if ( !parseLine(string, fidEntry) ) {
	    free(fidEntry);
	    fclose(fp);
	    return;
	}

	fidEntry->next = NULL;

	if ( !fidListHead ) {
	    fidListHead = fidEntry;
	} else {
	    fidListTail->next = fidEntry;
	}
	fidListTail = fidEntry;
    }

    /* resolve the rips */
    for ( fidEntry = fidListHead; fidEntry; fidEntry = fidEntry->next ) {
	if ( strcmp(fidEntry->rip_acronym, "NULL") ) {
	    fidEntry->rip_field = MFgetfid(fidEntry->rip_acronym);
	} else {
	    fidEntry->rip_field = 0;
	}
    }

    fclose(fp);
    return;
}

static int
#ifdef __STDC__
parseLine(char *str, FidEntry *fidEntry)
#else
parseLine(str, fidEntry)
char *str;
FidEntry *fidEntry;
#endif
{
    if ( strchr(str, '"') ) {
	return parseLineWithDDEAcronym(str, fidEntry);
    } else {
	fidEntry->dde_acronym[0] = '\0';
	return parseLineWithoutDDEAcronym(str, fidEntry);
    }
}

static int
#ifdef __STDC__
parseLineWithDDEAcronym(char *str, FidEntry *fidEntry)
#else
parseLineWithDDEAcronym(str, fidEntry)
char *str;
FidEntry *fidEntry;
#endif
{
    char	*ptr;
    int	pos;
    int	negative = FALSE;
    static char	stype[20];

    /* ACRONYM DDE_ACRONYM FID RIPPLES_TO FIELD_TYPE LENGTH */
    ptr = str;

    /* ACRONYM */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: acronym\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    while ( *ptr != ' ' && *ptr != '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: acronym2\n");
		return FALSE;
	    }
	    fidEntry->acronym[pos++] = *ptr;
	    ++ptr;
    }
    fidEntry->acronym[pos] = '\0';

    /* DDE ACRONYM */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: dde acronym\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    ++ptr;
    while ( *ptr != '"' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: dde acronym2\n");
		return FALSE;
	    }
	    fidEntry->dde_acronym[pos++] = *ptr;
	    ++ptr;
    }
    fidEntry->dde_acronym[pos] = '\0';
    ++ptr;

    /* FID */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: fid\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    fidEntry->fid = 0;
    if ( *ptr == '-' ) {
	negative = TRUE;
	++ptr;
    }
    while ( *ptr != ' ' && *ptr != '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: fid2\n");
		return FALSE;
	    }
	    fidEntry->fid = *ptr - '0' + fidEntry->fid * 10;
	    ++ptr;
    }
    if ( negative )
	fidEntry->fid = -fidEntry->fid;

    /* RIP ACRONYM */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: rip\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    while ( *ptr != ' ' && *ptr != '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: rip2\n");
		return FALSE;
	    }
	    fidEntry->rip_acronym[pos++] = *ptr;
	    ++ptr;
    }
    fidEntry->rip_acronym[pos] = '\0';

    /* STYPE */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: stype\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    while ( *ptr != ' ' && *ptr != '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: stype2\n");
		return FALSE;
	    }
	    stype[pos++] = *ptr;
	    ++ptr;
    }
    stype[pos] = '\0';

    /* LEN */
    while ( *ptr == ' ' || *ptr == '\t' ) {
	    if ( *ptr == '\0' ) {
		fprintf(errfile, "parseLineWithDDEAcronym: len\n");
		return FALSE;
	    }
	    ++ptr;
    }
    pos=0;
    fidEntry->max_length = 0;
    while ( *ptr != ' ' && *ptr != '\t' && *ptr != '\0' ) {
	if ( *ptr >= '0' && *ptr <= '9' )
	    fidEntry->max_length = *ptr - '0' + fidEntry->max_length * 10;
	++ptr;
    }

    if ( debug > 5 ) {
	fprintf(logfile, "%d %s \"%s\" %s %s %d\n",
	    fidEntry->fid,
	    fidEntry->acronym,
	    fidEntry->dde_acronym,
	    fidEntry->rip_acronym,
	    stype,
	    fidEntry->max_length);
	fflush(logfile);
    }

    /* fill in others later */
    if ( !strcmp(stype,"ENUMERATED") ) {
	    fidEntry->type = FIDTYPE_ENUMERATED;
    } else if ( !strcmp(stype,"DATE") ) {
	    fidEntry->type = FIDTYPE_DATE;
    } else if ( !strcmp(stype,"TIME") ) {
	    fidEntry->type = FIDTYPE_TIME;
    } else if ( !strcmp(stype,"PRICE") ) {
	    fidEntry->type = FIDTYPE_PRICE;
    } else if ( !strcmp(stype,"INTEGER") ) {
	    fidEntry->type = FIDTYPE_INTEGER;
    } else if ( !strcmp(stype,"ALPHANUMERIC") ) {
	    fidEntry->type = FIDTYPE_ALPHANUMERIC;
    } else if ( !strcmp(stype,"BINARY") ) {
	    fidEntry->type = FIDTYPE_BINARY;
    } else if ( !strcmp(stype,"TIME_SECONDS") ) {
	    fidEntry->type = FIDTYPE_TIME_SECONDS;
    } else if ( !strcmp(stype,"(null)") ) {
	    fidEntry->type = FIDTYPE_ALPHANUMERIC;
    } else {
	fprintf(errfile, "parseLineWithDDEAcronym: Unknown type %s\n", stype);
	fidEntry->type = 0;
	return FALSE;
    }
    return TRUE;
}

static int
#ifdef __STDC__
parseLineWithoutDDEAcronym(char *str, FidEntry *fidEntry)
#else
parseLineWithoutDDEAcronym(str, fidEntry)
char *str;
FidEntry *fidEntry;
#endif
{
	char	dif[10];
	char	tef[10];
	char	*ptr;

	ptr = strtok(str, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: acronym\n");
	    return FALSE;
	}
	strcpy(fidEntry->acronym, ptr);

	ptr = strtok(NULL, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: fid\n");
	    return FALSE;
	}
	fidEntry->fid = atoi(ptr);

	ptr = strtok(NULL, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: rip_field\n");
	    return FALSE;
	}
	strcpy(fidEntry->rip_acronym, ptr);

	ptr = strtok(NULL, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: tef\n");
	    return FALSE;
	}
	strcpy(tef, ptr);

	ptr = strtok(NULL, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: dif\n");
	    return FALSE;
	}
	strcpy(dif, ptr);

	/* fill in others later */
	if ( !strcmp(dif,"ET") ) {
		fidEntry->type = FIDTYPE_ENUMERATED;
	} else if ( !strcmp(dif,"DATE") ) {
		fidEntry->type = FIDTYPE_DATE;
	} else if ( !strcmp(dif,"NUMERAL") ) {
		fidEntry->type = FIDTYPE_PRICE;
	}

	ptr = strtok(NULL, "\t ");
	if ( !ptr ) {
	    fprintf(errfile, "parseLineWithoutDDEAcronym: len\n");
	    return FALSE;
	}
	fidEntry->max_length = atoi(ptr);
	return TRUE;
}

char *
#ifdef __STDC__
MFacronym(int fid)
#else
MFacronym(fid)
int	fid;
#endif
{
	FidEntry	*fidEntry = fidListHead;

	for ( ; fidEntry; fidEntry = fidEntry->next ) {
		if ( fidEntry->fid == fid ) {
			return fidEntry->acronym;
		}
	}
	return NULL;
}

FidType
#ifdef __STDC__
MFget_field_type(int fid)
#else
MFget_field_type(fid)
int	fid;
#endif
{
	FidEntry	*fidEntry = fidListHead;

	for ( ; fidEntry; fidEntry = fidEntry->next ) {
		if ( fidEntry->fid == fid ) {
			return fidEntry->type;
		}
	}
	return FIDTYPE_OTHER;
}

int
#ifdef __STDC__
MFget_maxlen(int fid)
#else
MFget_maxlen(fid)
int	fid;
#endif
{
	FidEntry	*fidEntry = fidListHead;

	for ( ; fidEntry; fidEntry = fidEntry->next ) {
		if ( fidEntry->fid == fid ) {
			return fidEntry->max_length;
		}
	}
	return 0;
}

int
#ifdef __STDC__
MFget_rip(int fid)
#else
MFget_rip(fid)
int	fid;
#endif
{
	FidEntry	*fidEntry = fidListHead;

	for ( ; fidEntry; fidEntry = fidEntry->next ) {
		if ( fidEntry->fid == fid ) {
			return fidEntry->rip_field;
		}
	}
	return 0;
}

/*
 * This is why a hash should be used
 */
int
#ifdef __STDC__
MFgetfid(char *acronym)
#else
MFgetfid(acronym)
char	*acronym;
#endif
{
	FidEntry	*fidEntry = fidListHead;

	if ( !strcmp(acronym,"X_RIC_NAME") ) {
		return -1;
	}

	if ( !strcmp(acronym,"X_ERRORMSG") ) {
		return -2;
	}

	for ( ; fidEntry; fidEntry = fidEntry->next ) {
		if ( !strcmp(acronym, fidEntry->acronym) ) {
			return fidEntry->fid;
		}
	}
	return 0;
}

int
#ifdef __STDC__
MFis_date(int fid)
#else
MFis_date(fid)
int	fid;
#endif
{
    if ( MFget_field_type(fid) == FIDTYPE_DATE ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

int
#ifdef __STDC__
MFis_enumerated(int fid)
#else
MFis_enumerated(fid)
int	fid;
#endif
{
    if ( MFget_field_type(fid) == FIDTYPE_ENUMERATED ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

int
#ifdef __STDC__
MFis_alphanum(int fid, char *value)
#else
MFis_alphanum(fid, value)
int	fid;
char	*value;
#endif
{
    /* Check if we properly initialized the appendix_a file data */
    /* if not use some other representation */
    if ( !fidListHead ) {
	/* if there is an embedded space - treat as alphanumeric */
	/* Not the Reuters definition */
	/* use this to detect when to use quotes */
	for ( ; *value; ++value ) {
	    if ( isspace(*value) ) {
		return TRUE;
	    }
	}
	return FALSE;
    } else if ( MFget_field_type(fid) == FIDTYPE_ALPHANUMERIC ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

int
#ifdef __STDC__
MFis_int(int fid, char *value)
#else
MFis_int(fid, value)
int	fid;
char	*value;
#endif
{
    if ( !fidListHead ) {
	char *ptr;
	int int_indicator_found = FALSE;
	for ( ptr = value; *ptr; ++ptr ) {
	    if ( *ptr == '+' || *ptr == '-' || isdigit(*ptr) ) {
		int_indicator_found = TRUE;
	    } else {
		return FALSE;
	    }
	}
	return int_indicator_found;
    } else if ( MFget_field_type(fid) == FIDTYPE_INTEGER ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

multiple_space_found(char *value)
{
    char *first_space = strchr(value, ' ');
    if ( first_space && strchr(first_space+1, ' ') ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

int
is_price_character(char c)
{
    return isdigit(c) || c=='-' || c=='+' || c==' ' || c=='/' || c=='.';
}

int
#ifdef __STDC__
MFis_price(int fid, char *value)
#else
MFis_price(fid, value)
int	fid;
char	*value;	/* in case we didn't read file */
#endif
{
    if ( !fidListHead ) {
	char *ptr;
	int price_indicator_found = 0;
	if ( !value || !*value ) return FALSE;

	/* reject if multiple spaces */
	if ( multiple_space_found(value) ) {
	    return FALSE;
	}

	for ( ptr = value; *ptr; ++ptr ) {
	    if ( (*ptr == '/' || *ptr == '.') && ptr != value ) {
		price_indicator_found = 1;
	    } else if ( !is_price_character(*ptr) ) {
#ifdef UNIT_TEST
		printf("[%s] is not price because of %c (%d) in position %d\n",
			value, *ptr, *ptr, ptr-value+1);
#endif
		return FALSE;
	    }
	}
	return price_indicator_found;
    }
    if ( MFget_field_type(fid) == FIDTYPE_PRICE ) {
	return TRUE;
    } else {
	return FALSE;
    }
}

#ifdef UNIT_TEST
FILE *logfile=stdout, *outfile=stdout, *errfile=stdout;
int debug=2;

struct {
    int fid;
    char *value;
} test_seq[] = {
    {6,"12345"},
    {6,"abcde"},
    {9,"+123.450"},
    {9,"123 1/2"},
    {9,"+123 1/2"},
    {9,"-123 1/2"},
    {9,"-123z1/2"},
    {3,"hello+"},
    {4,"+ello"},
    {6,"+ello"},
    {4,"*ello"},
    {4,"-ello"},
    {6,"-ello"},
    {6,"-1"},
    {6,"-1/2"},
    {6,"-01/2"},
    {6,"-01/02"},
    {6,"-1/02"},
    {6,"-1/32"},
    {6,"-1/128"},
    {6,"-1/256"},
    {6,"1"},
    {6,"1/2"},
    {6,"01/2"},
    {6,"01/02"},
    {6,"1/02"},
    {6,"1/32"},
    {6,"1/128"},
    {6,"1/256"},
    {4,"/3"},
    {6,"/3"},
    {6,"1.0003"},
    {6,"999.0003"},
    {6,"-1.0003"},
    {6,"-999.0003"},
    {6,"+1.0003"},
    {6,"+999.0003"},
    {0, NULL}
};

test_price()
{
    int i;
    for ( i = 0; test_seq[i].fid; i++ ) {
	if ( MFis_price(test_seq[i].fid, test_seq[i].value) ) {
	    printf("%d: %s is price\n", test_seq[i].fid, test_seq[i].value);
	} else {
	    printf("%d: %s is not price\n", test_seq[i].fid, test_seq[i].value);
	}
    }
}

main()
{
    test_price();

    printf("reading field info...");
    read_field_info();
    printf("done\n");

    test_price();

}
#endif

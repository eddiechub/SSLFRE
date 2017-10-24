/*
 * File:	triprice.c
 * Description:	higher level interface to the sslsubs layer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "triprice.h"

#ifdef __cplusplus
}
#endif

#ifdef __STDC__
extern char	*mf_parse(char*,char,char*);
extern int	InitSSL(char*,char*);
extern int	request_item(char*,char*,int,RECORD_INFO*);
extern void	GetSSLData(int);
extern void	CloseSSL(void);
extern int	addToFidArray(RECORD_INFO*,int,char*);
static void	parse_ssl_data(RECORD_INFO*,unsigned char*,int);
#else
extern char	*mf_parse();
extern int	InitSSL();
extern int	request_item();
extern void	GetSSLData();
extern void	CloseSSL();
extern int	addToFidArray();
static void	parse_ssl_data();
#endif

FILE		*errfile = stderr;
FILE		*logfile = NULL;
FILE		*badricfile = stderr;
int		debug = 0;

static int	got_it = 0;
static char	UserName[255] = "";

int
#ifdef __STDC__
GetTriarchData(RECORD_INFO *record_info, char item[], char source[])
#else
GetTriarchData(record_info, item, source)
RECORD_INFO	*record_info;
char		item[];
char		source[];
#endif
{
    int	ret;
    static int first = TRUE;

    if ( first ) {
	char	*ptr;
	char	*SinkName = NULL;

	if ( ptr = getenv("SSL_SINK_NAME") ) {
	    SinkName = ptr;
	}
	if ( ptr = getenv("SSL_USER_NAME") ) {
	    if ( strlen(ptr) < sizeof(UserName) )
		strcpy(UserName, ptr);
	}
	if ( ptr = getenv("LOG_GET_TRIARCH_DATA") ) {
	    logfile = stderr;
	}
	if ( logfile && debug > 1 ) {
	    fprintf(logfile,"Connecting to SSL...\n");
	    fflush(logfile);
	}
	ret = InitSSL(UserName, SinkName);
	if ( !ret ) {
	    fprintf(errfile, "InitSSL(\"%s\",\"%s\") failed\n",
		UserName?UserName:"",
		SinkName?SinkName:"");
	    return ret;
	}
	first = FALSE;
	if ( logfile && debug > 1 ) {
	    fprintf(logfile,"Connected to SSL...\n");
	    fflush(logfile);
	}
    }

    if ( logfile && debug ) {
	fprintf(logfile, "Requesting item %s source %s\n",
		item, source);
	fflush(logfile);
    }

    ret = request_item(source, item, FALSE, record_info);
    if ( !ret ) {
#ifdef DEBUG
	fprintf(errfile,"Request Error\n");
	fflush(errfile);
#endif
	return ret;
    }

    if ( logfile && debug ) {
	fprintf(logfile,"Requested %s... waiting for reply...\n", item);
	fflush(logfile);
    }

    for ( got_it = 0; !got_it; ) {
	if ( logfile && debug ) {
	    fprintf(logfile,"Trying to get it...\n");
	    fflush(logfile);
	}
	GetSSLData(TRUE);
    }
    if ( got_it > 0 )
	return TRUE;
    else
	return FALSE;
}

void
doneWithTriarch()
{
    CloseSSL();
}

void
SetTriarchUserName(user_name)
char	*user_name;
{
	if ( strlen(user_name) < sizeof(UserName) ) {
		strcpy(UserName, user_name);
	}
}

void
do_record_data(ServiceName, ItemName, Data, DataLength, CbData, ClientData)
char	*ServiceName;
char	*ItemName;
char	*Data;
int	DataLength;
void	*CbData;
void	*ClientData;
{
    got_it = 1;
    if ( logfile && debug ) {
	fprintf(logfile,"do_record_data: src %s item %s CbData %p\n",
		ServiceName, ItemName, CbData);
	fflush(logfile);
    }
    parse_ssl_data((RECORD_INFO *)CbData,(unsigned char *)Data,DataLength);
}

void
do_record_status(code, source_name, item_name, text, CbData, ClientData)
int	code;
char	*source_name;
char	*item_name;
char	*text;
void	*CbData;
void	*ClientData;
{
#if lint
    ClientData = ClientData;
    CbData = CbData;
#endif
    got_it = -1;
    fprintf(errfile, "%s:%s: %s (code: %d) \n",
	source_name, item_name, text, code);
    fflush(errfile);
}

void
do_global_status(code, text, ClientData)
int	code;
char	*text;
void	*ClientData;
{
#if lint
    ClientData = ClientData;
#endif
    fprintf(errfile, "%s (code = %d)\n", text, code);
    fflush(errfile);
}

static void
#ifdef __STDC__
parse_ssl_data(RECORD_INFO *record_info, unsigned char data[], int len)
#else
parse_ssl_data(record_info, data, len)
RECORD_INFO	*record_info;
unsigned char	*data;
int		len;
#endif
{
    int	fid;
    unsigned char *end = &data[len];
    unsigned char *dataptr, *ptr1, *ptr2;

    if ( logfile && debug > 4 ) {
	fprintf(logfile, "parse_ssl_data: RECORD_INFO * %p fid %d\n",
	    record_info, (record_info->fid_array)->fid);
    }

    /* skip to first field, get location of terminator */
    ptr1 = (unsigned char *)mf_parse((char *)data+1, RS, (char *)end);
    if ( !ptr1 && logfile && debug > 1 ) {
	fprintf(logfile,"Ptr1 0\n");
	fflush(logfile);
    }
    while ( ptr1 ) {
	ptr2 = (unsigned char *)mf_parse((char *)ptr1 + 1, US, (char *)end);
	if ( !ptr2 ) {
	    if ( logfile && debug > 3 ) {
		fprintf(logfile,"Break1\n");
		fflush(logfile);
	    }
	    break;
	}

	*ptr2 = '\0';

	fid = atoi((char *)(ptr1 + 1));
	dataptr = ptr2 + 1;

	ptr1 = (unsigned char *)mf_parse((char *)ptr2 + 1, RS, (char *)end);
	if ( !ptr1 ) {
	    if ( logfile && debug > 3 ) {
		fprintf(logfile, "Break2\n");
		fflush(logfile);
	    }
	    break;
	}

	*ptr1 = '\0';

	if ( logfile && debug > 3 ) {
	    fprintf(logfile, "%d: %s\n", fid, dataptr);
	    fflush(logfile);
	}

	/* don't care whether it succeeded because if it didn't
	   succeed, we didn't want it */
	(void)addToFidArray(record_info, fid, (char *)dataptr);
    }
}

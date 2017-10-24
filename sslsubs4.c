/*
 * File:	sslsubs4.c
 * Author:	Edward Chubin
 * Description:	Generic layer above the SSL 4 API.
 *		Provides a request interface to Triarch. Need to implement
 *		the functions
 *		do_record_data(), do_record_status(), and do_global_status()
 *		which return either data or status to the reuqest program.
 *		Queing is implemented to pace the requests more efficiently.
 *		Keep track of number of requests outstanding overall and
 *		for each source service.
 * Date:	12-96	Created
 *		06-98	Wildcard Services added
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#else
#include <winsock2.h>
#endif

#ifdef WILDCARDS
#include <libgen.h>
#include <regex.h>
#endif

#include "ssl.h"

#ifdef __STDC__
#if sun
extern void	bzero();
#endif
extern int	select();
#endif

#define CHANNEL_TIMEOUT 0xffff

#undef TRUE
#undef FALSE
#define FALSE 0
#define TRUE 1

#if defined(__STDC__) || defined(WIN32)
static SSL_EVENT_RETCODE Data(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE ItemState(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE ServInfo(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE Default(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE Ignore(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE Disconnect(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
static SSL_EVENT_RETCODE Reconnect(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
#ifdef INSERTS
static SSL_EVENT_RETCODE InsertResponse(int,SSL_EVENT_TYPE,SSL_EVENT_INFO*,void*);
#endif
extern void	do_record_data(char*,char*,char*,int,void*,void*);
extern void	do_record_status(int,char*,char*,char*,void*,void*);
extern void	do_global_status(SSL_EVENT_TYPE,char*,void*);
#ifdef CONNECTION_STATUS
extern void	do_connected_status(int,char*,void*);
#endif

extern void	GetSSLData(int);
#else
static SSL_EVENT_RETCODE Data();
static SSL_EVENT_RETCODE ItemState();
static SSL_EVENT_RETCODE ServInfo();
static SSL_EVENT_RETCODE Default();
static SSL_EVENT_RETCODE Ignore();
static SSL_EVENT_RETCODE Disconnect();
static SSL_EVENT_RETCODE Reconnect();
#ifdef INSERTS
static SSL_EVENT_RETCODE InsertResponse();
#endif
extern void	do_record_data();
extern void	do_record_status();
extern void	do_global_status();
extern void	GetSSLData();
#endif

extern int	get_queue();	/* queue.c */
extern int	add_queue();	/* queue.c */

static int	channel = -1;
static int	ssl_channel = -1;

static int      max_outstanding = 64;
static int	num_outstanding = 0;

extern FILE	*logfile;
extern FILE	*errfile;
extern int	debug;

void		*ClientData = NULL;

#if defined(__cplusplus) || defined(__STDC__)
int getSSLChannelFd(void)
#else
int getSSLChannelFd()
#endif
{
    return channel;
}

int
#if defined(__cplusplus) || defined(__STDC__)
InitSSL(char *UserName, char *SinkName)
#else
InitSSL(UserName, SinkName)
char	*UserName;
char	*SinkName;
#endif
{
    int ival;
    int ntries;
    /* int ssl_off = SSL_OFF; */
    int	ssl_on = SSL_ON;
    static int first = TRUE;

#ifdef WIN32
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2,0);
    int err = WSAStartup(wVersionRequested, &wsaData);
    if ( err && logfile ) {
	fprintf(logfile, "WSAStartup returned %d\n", err);
	return FALSE;
    }
#endif

    if ( ssl_channel >= 0 ) {
	return TRUE;
    }

    if ( first ) {
	char *ssl_logfile_name;
	first = FALSE;
	if ( sslInit(SSL_VERSION_NO) < 0) {
	    fprintf(errfile, "sslInit() failed\n%s\n",
			 sslGetErrorText(SSL_ALL_CHANNEL));
	    return FALSE;
	}
	ssl_logfile_name = getenv("SSLAPI_ERRFILE");
	if ( !ssl_logfile_name ) {
	    ssl_logfile_name = "./sslapi.log";
	}
	if ( sslErrorLog(ssl_logfile_name, 100000) < 0 &&
	     sslErrorLog("/tmp/ssl.log", 100000) < 0 ) {
	    fprintf(errfile, "errlog failed.Error logging not enabled.\n");
	} else {
	    /* Turn on error logging of Function Errors. */
	    if (sslSetProperty(SSL_ALL_CHANNEL, SSL_OPT_LOG_FUNCTION_ERROR,
				(void *) &ssl_on) < 0) {
		fprintf(errfile, "Log Function Error failed\n%s\n",
			 sslGetErrorText(SSL_ALL_CHANNEL));
		return FALSE;
	    }
	}
    }

    for ( ntries = 0; ssl_channel < 0 && ntries < 10; ntries++ ) {
	unsigned int timeout = CHANNEL_TIMEOUT;
	if ( SinkName && *SinkName ) {
	    ssl_channel = sslSnkMount(UserName,
		    SSL_SNK_MO_SERVER, (void *)SinkName,
		    SSL_SNK_MO_SET_TIMEOUT, (void *)&timeout,
		    NULL);
	} else {
	    ssl_channel = sslSnkMount(UserName,
		    SSL_SNK_MO_SET_TIMEOUT, (void *)&timeout,
		    NULL);
	}
	if ( ssl_channel == -4 ) break;
    }
    if ( ssl_channel < 0 ) {
        fprintf(errfile, "sslSnkMount(\"%s\",\"%s\") failed[%d]\n%s\n",
	    UserName?UserName:"",
	    SinkName?SinkName:"",
	    ssl_channel,
	    sslGetErrorText(SSL_ALL_CHANNEL));
	return FALSE;
    }
    if ( sslGetProperty(ssl_channel, SSL_OPT_CHANNEL_FD, &channel) < 0 ) {
        fprintf(errfile, "sslGetProperty(SSL_OPT_CHANNEL_FD) failed\n%s\n",
	    sslGetErrorText(ssl_channel));
	return FALSE;
    }
 
    /* EHC try to set it above the threshold (- --> +)*/
    ival = max_outstanding + 1;
    
    if ( sslSetProperty(ssl_channel, SSL_SNK_RESPONSE_THROTTLE, (void *)&ival)
		< 0 ) {
        fprintf(stderr,"sslSetProperty(SSL_SNK_RESPONSE_THROTTLE) failed\n%s\n",
           sslGetErrorText(ssl_channel));
    }

    if ( sslSetProperty(ssl_channel, SSL_SNK_IMAGE_ONLY, (void *)&ssl_on) < 0 ){
        fprintf(stderr,"sslSetProperty(SSL_SNK_IMAGE_ONLY) failed\n%s\n",
           sslGetErrorText(ssl_channel));
    }

    sslRegisterCallBack(ssl_channel,
	SSL_ET_ITEM_IMAGE, Data, ClientData);
    sslRegisterCallBack(ssl_channel,
	SSL_ET_ITEM_UPDATE, Data, ClientData);
    sslRegisterClassCallBack(ssl_channel,	
	SSL_EC_ITEM_STATUS, ItemState, ClientData);
    sslRegisterCallBack(ssl_channel,
	SSL_ET_SERVICE_INFO, ServInfo, ClientData);
    sslRegisterCallBack(ssl_channel,
	SSL_ET_SESSION_DISCONNECTED, Disconnect, ClientData);
    sslRegisterCallBack(ssl_channel,	
	SSL_ET_SESSION_RECONNECTED, Reconnect, ClientData);
    sslRegisterCallBack(ssl_channel,
	SSL_ET_BROADCAST, Ignore, ClientData);
    sslRegisterClassCallBack(ssl_channel,
	SSL_EC_DEFAULT_HANDLER, Default, ClientData);
#ifdef INSERTS
    sslRegisterClassCallBack(ssl_channel,
	SSL_EC_INSERT_RESPONSE, InsertResponse, ClientData);
#endif

    GetSSLData(1);

    if ( debug > 2 )
	fprintf(logfile, "SSL channel %d fd %d\n", ssl_channel, channel);
    return TRUE;
}

typedef struct _serviceStruct {
    char	*name;
    int		up;
    int		num_outstanding;
    struct _serviceStruct *next;
} SERVICE_STRUCT;

static SERVICE_STRUCT *services = NULL;
static SERVICE_STRUCT *services_last = NULL;

typedef struct _srcList {
    SERVICE_STRUCT	*ss;
    struct _srcList	*next;
} SrcList;

typedef struct {
	void			*client_data;
	SSL_REQUEST_TYPE	request_type;
	char			*org_source;
	SERVICE_STRUCT		*service_struct;
#ifdef WILDCARDS
	/* WHAT: list of failed services,
	 * WHY: avoid calling same source
	 * Used for recovery with wildcards
	 */
	SrcList			*failed_src_list;
#endif
} CallbackData;

static void
#if defined(__cplusplus) || defined(__STDC__)
markServicesDown(void)
#else
markServicesDown()
#endif
{
    SERVICE_STRUCT *service_ptr = services;
    while ( service_ptr ) {
	service_ptr->up = FALSE;
	service_ptr = service_ptr->next;
    }
}

/*
 * Set the named service up
 */
static void
#if defined(__cplusplus) || defined(__STDC__)
serviceUp(char *source)
#else
serviceUp(source)
char	* source;
#endif
{
    SERVICE_STRUCT *service_ptr = services;

    if ( debug > 3 )
	fprintf(logfile, "serviceUp: Service %s\n", source);

    while ( service_ptr ) {
	if ( !strcmp(service_ptr->name, source) ) {
	    if ( !service_ptr->up ) {
		service_ptr->up = TRUE;
		if ( debug > 2 )
		    fprintf(logfile, "serviceUp: Service %s now UP\n", source);
	    }
	    return;
	}
	service_ptr = service_ptr->next;
    }
    service_ptr = (SERVICE_STRUCT *)malloc(sizeof(SERVICE_STRUCT));
    if ( !service_ptr ) {
	/* bummer, hope we didn't need it */
	fprintf(errfile, "serviceUp: malloc failed for service %s\n", source);
	fflush(errfile);
	return;
    }

    if ( debug > 2 )
	fprintf(logfile, "serviceUp: Added New Service %s\n", source);

    if ( !services ) {
	 services = service_ptr;
    } else if ( services_last ) {
	 services_last->next = service_ptr;
    }

    services_last = service_ptr;
    service_ptr->next = NULL;
    service_ptr->num_outstanding = 0;
    service_ptr->up = TRUE;
    service_ptr->name = strdup(source);
    if ( !service_ptr->name ) {
	fprintf(errfile, "serviceUp: strdup failed for service %s\n", source);
	fflush(errfile);
    }
}

/*
 * Set the named service down
 * But don't add a down source now
 * May need to handle it if managing down services
 */
static void
#if defined(__cplusplus) || defined(__STDC__)
serviceDown(char *source)
#else
serviceDown(source)
char	*source;
#endif
{
    SERVICE_STRUCT *service_ptr = services;

    if ( debug > 3 )
	fprintf(logfile, "serviceDown: Service %s\n", source);

    while ( service_ptr ) {
	if ( !strcmp(service_ptr->name, source) ) {
	    if ( service_ptr->up ) {
		 service_ptr->up = FALSE;
		if ( debug > 2 )
		    fprintf(logfile,
			"serviceDown: Service %s now DOWN\n",source);
	    }
	    return;
	}
	service_ptr = service_ptr->next;
    }
    /* if doing wildcards, items will switch in status */
}

#define MAX_SERVICES_ARRAY	256

#if defined(__cplusplus) || defined(__STDC__)
char **getUpServices(void)
#else
char **getUpServices()
#endif
{
    int	count = 0;
    SERVICE_STRUCT *service_ptr;
    static char	*array[MAX_SERVICES_ARRAY+1];
    int printed_log_message = FALSE;

    /* wait for the services to be set */
    while ( (service_ptr = services) == NULL ) {
	sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);
	if ( !services ) {
	    if ( debug > 2 && !printed_log_message ) {
		printed_log_message = TRUE;
		fprintf(logfile, "getUpServices: waiting for services\n");
		/* might be here a while, so fflush */
		fflush(logfile);
	    }
#ifndef WIN32
	    sleep(1);
#endif
	}
    }

    while ( service_ptr && count < MAX_SERVICES_ARRAY ) {
	if ( debug > 2 )
	    fprintf(logfile, "getUpServices: Service %s %s\n",
		service_ptr->name, service_ptr->up?"UP":"DOWN");

	if ( service_ptr->up ) {
	    array[count++] = service_ptr->name;
	}
	service_ptr = service_ptr->next;
    }

    array[count] = NULL;

    return array;
}

static SERVICE_STRUCT *
#if defined(__cplusplus) || defined(__STDC__)
getServiceStruct(char *source, SrcList *failed_src_list)
#else
getServiceStruct(source, failed_src_list)
char	*source;
SrcList *failed_src_list;
#endif
{
    int printed_log_message = FALSE;
    SERVICE_STRUCT *service_ptr;
    SERVICE_STRUCT *best_svc = NULL;
#ifdef WILDCARDS
    int cont;
    SrcList *list;
    char *reg_expr;
    int	r_len = strlen(source);
/*
    char *r_buf = malloc(r_len+3);
*/
    static char r_buf[50];

    r_buf[0] = '^';
    r_buf[r_len+1] = '$';
    r_buf[r_len+2] = '\0';
    memcpy(&r_buf[1], source, r_len);

    reg_expr = regcmp(r_buf, NULL);
    if ( !reg_expr ) {
	fprintf(errfile, "Regular Expression failed for service [%s]\n",source);
	fflush(errfile);
    }
#endif

    /* wait for at least one service to be set */
    /* give up after awile ???? */
    while ( (service_ptr = services) == NULL ) {
	sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);
	if ( !services ) {
	    if ( debug > 2 && !printed_log_message ) {
		printed_log_message = TRUE;
		fprintf(logfile,"getServiceStruct: wait for services\n");
		/* might be here a while, so fflush */
		fflush(logfile);
	    }
#ifndef WIN32
	    sleep(1);
#endif
	}
    }

    for ( ; service_ptr; service_ptr = service_ptr->next ) {
	/* Look for 'source' service with lowest load */
	if ( debug > 4 ) {
	    fprintf(logfile,
		"getServiceStruct: looking at service %s status %s (want:%s)\n",
		service_ptr->name, service_ptr->up?"UP":"DOWN", source);
	}
#ifdef WILDCARDS
	if ( !regex(reg_expr, service_ptr->name) ) {
	    continue;
	}
#else
	if ( strcmp(source, service_ptr->name) ) {
	    continue;
	}
#endif
	if ( debug > 4 ) {
	    fprintf(logfile,
		"getServiceStruct: matched service %s is %s\n",
		service_ptr->name, service_ptr->up?"UP":"DOWN");
	}

	if ( !service_ptr->up ) {
	    continue;
	}

#ifdef WILDCARDS
	/* check if all sources have been tried? */
	cont = FALSE;
	for ( list = failed_src_list;
	      list;
	      list = list->next ) {

	    if ( debug > 2 ) {
		fprintf(logfile,
		    "getServiceStruct: check src %s vs failed src %s (%p)\n",
		    service_ptr->name, (list->ss)->name, list);
	    }

	    /* has this source been tried? */
	    if ( !strcmp((list->ss)->name, service_ptr->name) ) {
		/* try a different one */
		cont = TRUE;
		break;
	    } else if ( debug ) {
		fprintf(logfile, "getServiceStruct: no match failed src %s\n",
		    (list->ss)->name);
	    }
	}
	if ( cont ) {
	    if ( debug ) {
		fprintf(logfile, "getServiceStruct: src %s already tried\n",
		    (list->ss)->name);
	    }
	    continue;
	}
#endif

	if ( debug > 2 ) {
	    fprintf(logfile, "getServiceStruct: srvc %s load %d\n",
		service_ptr->name, service_ptr->num_outstanding);
	}

	/* The best one is the first one if not set
	 * or the current one if it has a lower
	 * number of items outstanding
	 */
	if ( (!best_svc ||
	      (best_svc->num_outstanding > service_ptr->num_outstanding)) ) {
	    best_svc = service_ptr;
	    if ( debug > 1 ) {
		fprintf(logfile, "getServiceStruct: trying %s\n",
		    service_ptr->name);
	    }
	}
    }
    if ( debug > 1 ) {
	fprintf(logfile,"getServiceStruct: returning %s\n",
	    best_svc?best_svc->name:"NULL");
    }
#ifdef WILDCARDS
    /*free(r_buf);*/
    free(reg_expr);
#endif
    return best_svc;
}

static int
#if defined(__cplusplus) || defined(__STDC__)
_request_item(
char	*source,
char	*item,
void	*client_data)
#else
_request_item(source, item, client_data)
char	*source;
char	*item;
void	*client_data;
#endif
{
    int	ret, ntries = 0;
    CallbackData *cb = (CallbackData *)client_data;
    SERVICE_STRUCT *ss;

    /*sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);*/

    if ( debug > 1 ) {
	fprintf(logfile,
	    "_request_item:  cb->client_data %p cb->request_type %s\n",
	    cb->client_data,
	    cb->request_type==SSL_RT_NORMAL?"NORMAL":"SNAP");
    }

    cb->org_source = strdup(source);
#ifdef WILDCARDS
    cb->failed_src_list = NULL;
#endif

    /* get best service -- may manipulate source, so already dup in cb */
    ss = getServiceStruct(source, NULL);
    if ( !ss ) {
	do_record_status(SSL_INFO_NONE, source, item,
		"Service is not available",
		cb->client_data, ClientData);
	free(cb->org_source);
	return FALSE;
    }
    cb->service_struct = ss;

    do {
	/*sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);*/
	ret = sslSnkOpen(ssl_channel, ss->name, item, client_data,
		SSL_SOO_REQUEST_TYPE, &cb->request_type, NULL);
    } while ( ret < 0 && ntries++ < 3 );

    if ( ret < 0 ) {
	fprintf(errfile, "sslSnkOpen() failed: %s\n",
	    sslGetErrorText(ssl_channel));
	return FALSE;
    }
    ++ss->num_outstanding;
    ++num_outstanding;
    return TRUE;
}

int
#if defined(__cplusplus) || defined(__STDC__)
request_item(
char	*source,
char	*item,
int	want_updates,
void	*client_data)
#else
request_item(source, item, want_updates, client_data)
char	*source;
char	*item;
int	want_updates;
void	*client_data;
#endif
{
    CallbackData *cb = (CallbackData *)malloc(sizeof(CallbackData));
    if ( !cb ) {
	fprintf(errfile, "request_item: malloc failed: %d source %s item %s\n",
		errno, source, item);
	return FALSE;
    }

    if ( want_updates )
	cb->request_type = SSL_RT_NORMAL;
    else
	cb->request_type = SSL_RT_SNAPSHOT;

    cb->client_data = client_data;
    if ( debug > 2 ) {
	fprintf(logfile,
	    "request_item:  cb %p cb->client_data %p cb->request_type %s\n",
	    cb, cb->client_data,
	    cb->request_type==SSL_RT_NORMAL?"NORMAL":"SNAP");
    }
#ifdef IMPLEMENT_QUEUE
    return add_queue(source, item, &cb);
#else
    return _request_item(source, item, (void *)cb);
#endif
}

/*
 * Need to free up the callback pointer
 * This is not trivial since we have no pointer to it
 * Set up a hash list based on source/item.
 * This could also prevent multiple requests while one is in
 * progress, thereby losing the first callback info.
 * This has NOT been implemented because most uses are for snapshots.
 */
int
#if defined(__cplusplus) || defined(__STDC__)
close_item(char *source, char *item)
#else
close_item(source, item)
char	*source;
char	*item;
#endif
{
    /* not snapshot -- should get rid of allocated memory if any */
    if ( sslSnkClose(ssl_channel, source, item) < 0 ) {
	return FALSE;
    }
    return TRUE;
}

void
#if defined(__cplusplus) || defined(__STDC__)
CloseSSL(void)
#else
CloseSSL()
#endif
{
    int ret;
    if ( ssl_channel < 0 )
	return;
    ret = sslDismount(ssl_channel);
    if ( ret < 0 ) {
	fprintf(errfile, "snkDismount() failed: %s\n",
	    sslGetErrorText(SSL_ALL_CHANNEL));
	fflush(errfile);
    }
}

#ifdef IMPLEMENT_QUEUE
static void
#if defined(__cplusplus) || defined(__STDC__)
check_request_queue(void)
#else
check_request_queue()
#endif
{
    void                *client_data;
    static char source_name[SSL_MAX_SOURCE_NAME+1];
    static char item_name[SSL_MAX_ITEM_NAME+1];

    while ( num_outstanding < max_outstanding &&
                    get_queue(source_name, item_name, &client_data) ) {
        if ( debug ) {
            fprintf(logfile, "GetSSLData(): requesting %s %s\n",
                    source_name, item_name);
            fflush(logfile);
        }
        /* Already log the error */
        (void) _request_item(source_name, item_name, client_data);
    }
}
#endif

#ifndef WIN32
void
#if defined(__cplusplus) || defined(__STDC__)
wait_for_input_on_fd(int fd)
#else
wait_for_input_on_fd(fd)
int	fd;
#endif
{
    fd_set	fdset;

    do {
	int	ret;
	struct timeval	tmout;
	tmout.tv_usec = 0;
	tmout.tv_sec = 2;

#ifdef IMPLEMENT_QUEUE
	check_request_queue();
#endif
	FD_ZERO(&fdset);
	if ( channel != -1 ) {
	    FD_SET((unsigned)channel, &fdset);
	} 
	if ( fd != -1 ) {
	    FD_SET((unsigned)fd, &fdset);
	}

	ret = select(FD_SETSIZE, &fdset, NULL, NULL, &tmout);
	if ( ret == -1 ) {
#ifdef WIN32
	    errno = WSAGetLastError();
#endif
	    if ( errno == EBADF ) {
		fprintf(errfile, "Sink Distributor has gone down\n");
		FD_CLR((unsigned)fd, &fdset);
		channel = -1;
	    } else if ( errno != EINTR ) {
		fprintf(errfile, "select: errno %d\n", errno);
		FD_CLR((unsigned)fd, &fdset);
	    }
	    continue;
	}
	/* poll the ssl periodically anyway -- automatic recovery */
	sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);
    } while ( !FD_ISSET(fd, &fdset) );
}
#endif

void
#if defined(__cplusplus) || defined(__STDC__)
GetSSLData(int block)
#else
GetSSLData(block)
int	block;
#endif
{
#ifdef IMPLEMENT_QUEUE
    check_request_queue();
#endif
    if ( block ) {
	int	ret;
	int	fdswid = 0;
	fd_set	fdset;
	struct timeval	tmout;
	tmout.tv_usec = 0;
	tmout.tv_sec = 2;

	FD_ZERO(&fdset);
	if ( channel != -1 ) {
	    fdswid = channel + 1;
	    FD_SET((unsigned)channel, &fdset);	/* ssl channel */
	} 

	ret = select(fdswid, &fdset, NULL, NULL, &tmout);
	if ( ret == -1 ) {
#ifdef WIN32
	    errno = WSAGetLastError();
#endif
	    if ( errno == EBADF ) {
		fprintf(errfile, "Sink Distributor has gone down\n");
		channel = -1;
	    } else if ( errno != EINTR ) {
		fprintf(errfile, "select: errno %d\n", errno);
	    }
	}
    }

    /* poll the ssl periodically anyway -- automatic recovery */
    sslDispatchEvent(SSL_ALL_CHANNEL, SSL_EXHAUST_READ);
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE Data(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE Data(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
    CallbackData *cb = (CallbackData *)EventInfo->ItemImage.ClientItemTag;
#if lint
    Channel = Channel;
    ClientData = ClientData; 
#endif

    if ( !cb ) {
	fprintf(errfile, "Data() Callback Data Error\n");
	fflush(errfile);
        return SSL_ER_EVENT_HANDLE_PUNT;
    }

    if ( Event == SSL_ET_ITEM_IMAGE ) {
	SERVICE_STRUCT *ss = cb->service_struct;
	if ( ss->num_outstanding > 0 ) {
	    --(ss->num_outstanding);
	}
	if ( num_outstanding > 0 ) {
	    --num_outstanding;
	}
    }

    /* Make sure that we've got a Record type. */
    if (EventInfo->ItemImage.ItemType.DataFormat != SSL_DF_MARKETFEED_RECORD) {
        return SSL_ER_EVENT_HANDLE_PUNT;
    }
    if ( debug > 4 ) {
	fprintf(logfile, "Data: cb %p cb->client_data %p cb->request_type %s\n",
	    cb, cb->client_data,
	    cb->request_type==SSL_RT_NORMAL?"NORMAL":"SNAP");
    }
    do_record_data(
	cb->org_source,
	EventInfo->ItemImage.ItemName,
	EventInfo->ItemImage.Data,
	EventInfo->ItemImage.DataLength,
	cb->client_data,
	ClientData);

    if ( cb->request_type == SSL_RT_SNAPSHOT ) {
	SrcList *curr, *next;
#ifdef WILDCARDS
	for ( curr = cb->failed_src_list; curr; curr = next) {
	    next = curr->next;
	    free(curr);
	}
#endif
	free(cb->org_source);
	free((char*)cb);
    }
    return SSL_ER_EVENT_HANDLE_OK;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE ItemState(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE ItemState(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
    CallbackData *cb = (CallbackData *)EventInfo->ItemStatus.ClientItemTag;
    SERVICE_STRUCT *ss = cb->service_struct;

    if ( !cb ) {
	fprintf(errfile, "ItemState() Callback Data Error\n");
	fflush(errfile);
        return SSL_ER_EVENT_HANDLE_PUNT;
    }

    if ( debug > 1 ) {
	char *state = "UNKNOWN";
	switch (Event) {
	case SSL_ET_ITEM_STATUS_CLOSED: state = "CLOSED"; break;
	case SSL_ET_ITEM_STATUS_INFO: state = "INFO"; break;
	case SSL_ET_ITEM_STATUS_OK: state = "OK"; break;
	case SSL_ET_ITEM_STATUS_STALE: state = "STALE"; break;
	default: break;
	}
	fprintf(logfile, "STATUS:%s:%s: code: %d text: %s State: %s\n",
	    EventInfo->ItemStatus.ServiceName,
	    EventInfo->ItemStatus.ItemName,
	    EventInfo->ItemStatus.StateInfoCode,
	    EventInfo->ItemStatus.Text,
	    state);
    }

    /* If the original source is wildcard,
     * then see if still requestable....
     */
    if ( Event == SSL_ET_ITEM_STATUS_CLOSED ||
	(Event == SSL_ET_ITEM_STATUS_STALE &&
	  cb->request_type==SSL_RT_SNAPSHOT) ) {
	SERVICE_STRUCT *nss = NULL;
	if ( debug > 4 ) {
	    fprintf(logfile, "Data: cb %p client_data %p request_type %d\n",
		cb, cb->client_data, cb->request_type);
	}

	/* WHAT: If the item is stale, and just want a snapshot,
	 *       Then send the close,
	 * WHY:  The SSL4 API will keep retrying
	 *       Done with item, unless doing the wildcard thing
	 */
	if ( Event == SSL_ET_ITEM_STATUS_STALE && cb->request_type == SSL_RT_SNAPSHOT ) {
	    /* Small hack to clear misleading error message */
	    char *ptr = strstr(EventInfo->ItemStatus.Text, "Retrying");
	    if ( ptr ) {
		/* Clear out the retrying */
		*ptr = '\0';
	    }
	    sslSnkClose(Channel,
			EventInfo->ItemStatus.ServiceName,
			EventInfo->ItemStatus.ItemName);
	}

#ifdef WILDCARDS
	/* If sources are different (it is a wildcard),
	 * Then attempt an alternate source
	 */
	if ( strcmp(cb->org_source, EventInfo->ItemStatus.ServiceName) ) {
	    SrcList *new, *curr, *next;

	    /* create new failed source list item */
	    new = (SrcList *) malloc(sizeof(SrcList));
	    if ( !new ) {
		fprintf(errfile, "ItemState() malloc failed for failed_src\n");
		fflush(errfile);
		return SSL_ER_EVENT_HANDLE_PUNT;
	    }
	    new->next = NULL;
	    new->ss = ss;

	    /* Add to failed list */
	    if ( cb->failed_src_list ) {
		SrcList *curr = cb->failed_src_list;
		while ( curr->next ) {
		    curr = curr->next;
		}
		curr->next = new;
		if ( debug ) {
		    fprintf(logfile, "added failed source list with %s\n",
			ss->name);
		}
	    } else {
		cb->failed_src_list = new;
		if ( debug ) {
		    fprintf(logfile, "initialized failed source list with %s\n",
			ss->name);
		}
	    }

	    nss = getServiceStruct(cb->org_source, cb->failed_src_list);
	    if ( nss ) {
		int ret, ntries = 0;
		if ( debug ) {
		    fprintf(logfile,
			"retrying wildcard source %s for %s using source %s\n",
			cb->org_source,
			EventInfo->ItemStatus.ItemName,
			nss->name);
		}
		cb->service_struct = nss;
		do {
		    ret = sslSnkOpen(Channel,
			    nss->name, EventInfo->ItemStatus.ItemName,
			    cb, SSL_SOO_REQUEST_TYPE, &cb->request_type, NULL);
		} while ( ret < 0 && ntries++ < 3 );
		if ( ret < 0 ) {
		    fprintf(errfile, "sslSnkOpen() failed: %s\n",
			sslGetErrorText(ssl_channel));
		}
		return SSL_ER_EVENT_HANDLE_OK;
	    }

	    for ( curr = cb->failed_src_list; curr; curr = next) {
		next = curr->next;
		free(curr);
	    }

	    /* May not have a good description in text */
	} else {
	    /* Not looking for a wildcard */
	    SrcList *curr, *next;
	    for ( curr = cb->failed_src_list; curr; curr = next) {
		next = curr->next;
		free(curr);
	    }
	}
#endif
	do_record_status(
	    EventInfo->ItemStatus.StateInfoCode,
	    cb->org_source,
	    EventInfo->ItemStatus.ItemName,
	    EventInfo->ItemStatus.Text,
	    cb->client_data,
	    ClientData);

	if ( ss && ss->num_outstanding > 0 ) {
	    --(ss->num_outstanding);
	}

	if ( num_outstanding > 0 ) {
	    --num_outstanding;
	}

	free(cb->org_source);
	free((char*)cb);
    }
    return SSL_ER_EVENT_HANDLE_OK;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE Default(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE Default(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    ClientData = ClientData; 
    EventInfo = EventInfo;
#endif
    if ( debug ) {
	fprintf(logfile, "DefaultHandler: Type %d\n", (int)Event);
	fflush(logfile);
    }
    sslRegisterCallBack(Channel, Event, Ignore, NULL);
    return SSL_ER_EVENT_HANDLE_LOG;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE Ignore(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE Ignore(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    Channel = Channel;
    Event = Event;
    EventInfo = EventInfo;
    ClientData = ClientData; 
#endif
    return SSL_ER_EVENT_HANDLE_OK;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE Disconnect(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE Disconnect(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    Channel = Channel;
    ClientData = ClientData; 
    Event = Event;
#endif
    /* It will recover if we call dispatch once and a while */
    if ( debug > 2 ) {
	fprintf(logfile, "Disconnect: %s\n", EventInfo->SessionDisconnected.Text);
	fflush(logfile);
    }
    /* Recover silently if possible */
#ifdef CONNECTION_STATUS
    do_connected_status(FALSE, EventInfo->SessionDisconnected.Text, ClientData);
#else
    do_global_status(Event, EventInfo->SessionDisconnected.Text, ClientData);
#endif
    channel = -1;

    num_outstanding = 0;
    markServicesDown();
    return SSL_ER_EVENT_HANDLE_OK;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE Reconnect(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE Reconnect(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    ClientData = ClientData; 
    Event = Event;
#endif
    if ( debug > 2 ) {
	fprintf(logfile,"SSL Reconnected: %s\n",
		    EventInfo->SessionReconnected.ServerName);
	fflush(logfile);
    }
    /* Recover silently if possible */
#ifdef CONNECTION_STATUS
    do_connected_status(TRUE, EventInfo->SessionReconnected.ServerName, ClientData);
#endif
    if ( sslGetProperty(Channel, SSL_OPT_CHANNEL_FD, &channel) < 0 ) {
        fprintf(errfile, "sslGetProperty(SSL_OPT_CHANNEL_FD) failed\n%s\n",
			sslGetErrorText(ssl_channel));
    }
    num_outstanding = 0;
    return SSL_ER_EVENT_HANDLE_OK;
}

#if defined(__cplusplus) || defined(__STDC__)
static SSL_EVENT_RETCODE ServInfo(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
static SSL_EVENT_RETCODE ServInfo(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    Channel = Channel;
    Event = Event;
    ClientData = ClientData; 
#endif
    if ( debug > 2 ) {
	fprintf(logfile, "Server Info: %s is %s\n",
		EventInfo->ServiceInfo.ServiceName,
		EventInfo->ServiceInfo.ServiceStatus == SSL_SS_SERVER_UP?
		"UP":"DOWN");
	fflush(logfile);
    }
    if ( EventInfo->ServiceInfo.ServiceStatus == SSL_SS_SERVER_UP ) {
	serviceUp(EventInfo->ServiceInfo.ServiceName);
    } else {
	serviceDown(EventInfo->ServiceInfo.ServiceName);
    }
    return SSL_ER_EVENT_HANDLE_OK;
}

#ifdef INSERTS
int
#if defined(__cplusplus) || defined(__STDC__)
DoTheInsert(
char *srcname,
char *ricname,
char *output,
int len,
int tag)
#else
DoTheInsert(srcname, ricname, output, len, tag)
char *srcname;
char *ricname;
char *output;
int len;
int tag;
#endif
{
    int	ret;
    SSL_INSERT_TYPE	EventInfo;

    EventInfo.ServiceName = srcname;
    EventInfo.InsertName = ricname;
    EventInfo.InsertTag = (void *)tag;
    EventInfo.Data = output;
    EventInfo.DataLength = len;

    ret = sslPostEvent(ssl_channel,SSL_ET_INSERT,(SSL_EVENT_INFO *)&EventInfo);
    if ( ret < 0 )
	ret = FALSE;
    else
	ret = TRUE;
    return ret;
}

static
#if defined(__cplusplus) || defined(__STDC__)
SSL_EVENT_RETCODE InsertResponse(
int		Channel,
SSL_EVENT_TYPE	Event,
SSL_EVENT_INFO	*EventInfo,
void		*ClientData)
#else
SSL_EVENT_RETCODE InsertResponse(Channel, Event, EventInfo, ClientData)
int		Channel;
SSL_EVENT_TYPE	Event;
SSL_EVENT_INFO	*EventInfo;
void		*ClientData;
#endif
{
#if lint
    Channel = Channel;
    Event = Event;
    ClientData = ClientData; 
#endif
    if ( debug > 2 ) {
	fprintf(logfile, "InsertResponse: code %d tag %d\n",
		(int)EventInfo->InsertResponse.InsertNakCode,
		(int)EventInfo->InsertResponse.InsertTag);
	fflush(logfile);
    }
    insert_ack(EventInfo->InsertResponse.InsertTag,
		EventInfo->InsertResponse.Data,
		EventInfo->InsertResponse.DataLength);
    return SSL_ER_EVENT_HANDLE_OK;
}
#endif

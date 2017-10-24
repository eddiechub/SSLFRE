/*
 * File:	sslsubs.c
 * Author:	Edward Chubin
 * Description:	generic layer above the SSL3 API
 *		Wildcard functionality not implemented.
 * Date:	12-90
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>

#include "ssl.h"

typedef struct {
	void	*client_data;
	int	request_type;
} CallbackData;

#undef TRUE
#undef FALSE
#define TRUE 1
#define FALSE 0

#if defined(sun) || !defined(__STDC__)
extern void	bzero();
extern int	select();
#endif

extern int	add_queue();	/* queue.c */
extern int	get_queue();	/* queue.c */

extern void	do_record_data();
extern void	do_record_status();
extern void	do_global_status();

extern FILE	*logfile;
extern FILE	*errfile;
extern FILE	*badricfile;
extern int	debug;

static int	ssl_channel = -1;
static int	max_outstanding = 50;
static int	num_outstanding = 0;

static void	process_ssl();

void		*ClientData = NULL;

int
InitSSL(UserName, SinkName)
char	*UserName;
char	*SinkName;
{
    static int first = TRUE;
    if ( first ) {
	unsigned long errlog_flags = SSL_PF_FUNC_LOG_OFF | SSL_PF_NAK_LOG_OFF;
	int ret = ssl_init((unsigned long) 0);
	if ( ret == SSL_FAILURE ) {
	    fprintf(errfile, "ssl_init() failed\n");
	    return FALSE;
	}

	ret = ssl_errlog(errlog_flags);
	if ( ret == SSL_FAILURE ) {
	    fprintf(errfile, "ssl_errlog() failed\n");
	    return FALSE;
	}
	first = FALSE;
    }

    if ( logfile && debug > 1 ) {
	fprintf(logfile, "Mount SSL Sink; snk_mount(\"%s\", \"%s\")\n",
	    UserName, SinkName?SinkName:"");
	fflush(logfile);
    }

    if ( SinkName ) {
	ssl_channel = snk_mount(UserName, SSL_SNK_SERVER, SinkName);
    } else {
	ssl_channel = snk_mount(UserName, 0);
    }
    if ( ssl_channel == SSL_FAILURE ) {
	if ( logfile ) {
	    fprintf(logfile, "snk_mount(\"%s\",\"%s\") failed ssl_errno %d\n",
		UserName, SinkName?SinkName:"", ssl_errno);
	    fflush(logfile);
	}

	switch (ssl_errno)
	{
	case SSL_E_PERMISSION:
	    fprintf(errfile, "TRIARCH: Access Denied\n");
	    break;
	case SSL_E_INVALID_NAME:
	    fprintf(errfile, "TRIARCH: Invalid User Name %s\n", UserName);
	    break;
	case SSL_E_ATTRIBUTES:
	    fprintf(errfile, "TRIARCH: Attributes\n");
	    break;
	case SSL_E_BAD_LENGTH:
	    fprintf(errfile, "TRIARCH: Bad Length\n");
	    break;
	case SSL_E_NET_DOWN:
	    fprintf(errfile, "TRIARCH: Net is Down\n");
	    break;
	case SSL_E_TOO_MANY_CHANNELS:
	case SSL_E_NO_RESOURCE:
	    fprintf(errfile, "TRIARCH: No resources or memory\n");
	    break;
	case SSL_E_ILLEGAL_OP:
	    fprintf(errfile, "TRIARCH: server version is incorrect\n");
	    break;
	case SSL_E_INCOMPATIBLE: /* trying to mount 223 from 301 ssl */
	    fprintf(errfile, "TRIARCH: server version is incorrect\n");
	    break;
	case SSL_E_SYSTEM:
	default:
	    fprintf(errfile, "TRIARCH: unknown error\n");
	    break;
	}
	return FALSE;
    }
    if ( logfile && debug ) {
	fprintf(logfile, "Mount SSL Sink Succeeded; Channel %d\n", ssl_channel);
	fflush(logfile);
    }
    return TRUE;
}

static int
_request_item(source, item, client_data)
char	*source;
char	*item;
void	*client_data;
{
    int	ret;

    CallbackData *cb = (CallbackData *)client_data;

    ret = snk_open(ssl_channel, source, item,
	SSL_PF_REQUEST_TYPE | SSL_PF_ITEM_IDENTIFIER,
	cb->request_type, client_data);
    if ( ret == SSL_FAILURE ) {
	if ( logfile ) {
	    fprintf(logfile, "snk_open(\"%s\",\"%s\"): failed ssl_errno %d\n",
		source, item, ssl_errno);
	    fflush(logfile);
	}
	switch (ssl_errno)
	{
	case SSL_E_SRC_DOWN:
	    fprintf(badricfile, "%s %s Source is Down\n", source, item);
	    break;
	case SSL_E_SRC_UNKNOWN:
	    fprintf(badricfile, "%s %s Source is Unknown\n", source, item);
	    break;
	case SSL_E_BAD_CHANNEL:
	case SSL_E_NO_SESSION:
	case SSL_E_NET_DOWN:
	    fprintf(badricfile, "%s %s SSL channel is not UP\n", source, item);
	    break;
	default:
	    fprintf(badricfile, "%s %s Unknown Problem (errno %d)\n",
		source, item, ssl_errno);
	    break;
	}
	return FALSE;
    }
    ++num_outstanding;
    return TRUE;
}

int
request_item(source, item, want_updates, client_data)
char	*source;
char	*item;
int	want_updates;
void	*client_data;
{
    CallbackData *cb = (CallbackData *)malloc(sizeof(CallbackData));
    if ( !cb ) {
	fprintf(errfile, "request_item: malloc failed: %d source %s item %s\n",
		errno, source, item);
	return FALSE;
    }

    if ( want_updates ) {
	cb->request_type = SSL_REQ_NORMAL;
    } else {
	cb->request_type = SSL_REQ_SNAPSHOT;
    }

    cb->client_data = client_data;
    return add_queue(source, item, &cb);
}

void
CloseSSL()
{
    (void)snk_dismount(ssl_channel);
    ssl_channel = SSL_FAILURE;
}

void
wait_for_input_on_fd(fd)
int     fd;
{
    int ret;
    int fdswid = 0;
    fd_set      fdset;
    fd_set      err_fds;
    struct timeval      tmout;

    do {
        FD_ZERO(&fdset);
        if ( ssl_channel != -1 ) {
            fdswid = ssl_channel + 1;
            FD_SET(ssl_channel, &fdset);    /* ssl channel */
        }
        if ( fd != -1 ) {
            if ( fd >= fdswid ) {
                fdswid = fd + 1;
            }
            FD_SET(fd, &fdset);
        }

        err_fds = fdset;
        ret = select(fdswid, &fdset, NULL, &err_fds, NULL);
        if ( ret == -1 ) {
            if (errno == EBADF) {
		fprintf(errfile, "Sink Distributor has gone down\n");
		/* don't recover */
		exit(0);
            } else {
                fprintf(errfile, "wait_for_input_on_fd: select errno %d\n",
			errno);
                perror("wait_for_input_on_fd:select");
            }
	} else if ( ret > 0 ) {
	    if ( FD_ISSET(ssl_channel, &fdset) ) {
		process_ssl();
	    }
        }
    } while ( !FD_ISSET(fd, &fdset) );
}

void
GetSSLData(block)
int	block;
{
    int     ret;
    int     fdswid = 0;
    fd_set  fdset;
    fd_set  err_fds;
    struct timeval  tmout;

    void		*client_data;
    static char	source_name[SSL_MAX_SOURCE_NAME+1];
    static char	item_name[SSL_MAX_ITEM_NAME+1];

    while ( num_outstanding < max_outstanding &&
		    get_queue(source_name, item_name, &client_data) ) {
	if ( logfile && debug ) {
	    fprintf(logfile, "GetSSLData(): requesting %s %s\n",
		    source_name, item_name);
	    fflush(logfile);
	}
	/* Already log the error */
	(void) _request_item(source_name, item_name, client_data);
    }

    if ( !num_outstanding ) {
	return;
    }

    FD_ZERO(&fdset);
    if ( ssl_channel != -1 ) {
	fdswid = ssl_channel + 1;
	FD_SET(ssl_channel, &fdset);	/* ssl channel */
    }

    tmout.tv_usec = 0;
    if ( block ) {
	tmout.tv_sec = 60;
    } else {
	tmout.tv_sec = 0;
    }

    err_fds = fdset;
    ret = select(fdswid, &fdset, NULL, &err_fds, &tmout);
    if ( ret == -1 ) {
	if (errno == EBADF) {
	    fprintf(errfile, "Sink Distributor has gone down\n");
	    /* don't recover */
	    exit(0);
	} else if ( errno != EINTR ) {
	    fprintf(errfile, "GetSSLData: select errno %d\n", errno);
	    perror("GetSSLData: select");
	}
    } else if ( ret > 0 ) {
	if ( FD_ISSET(ssl_channel, &fdset) ) {
	    process_ssl();
	}
    }
}

static void
process_ssl()
{
    SSL_MESSAGE	ssl_message;
    int	datalen;
    int	textlen;
    int	ret;
    CallbackData *cb;

    static char	source_name[SSL_MAX_SOURCE_NAME + 1];
    static char	item_name[SSL_MAX_ITEM_NAME + 1];
    static int	max_data = SSL_MAX_DATA + 1;
    static int	max_text = SSL_MAX_STATUS_TEXT + 1;
    static char *data = NULL, *text = NULL;

    ssl_message.source_name.max_length = sizeof(source_name) - 1;
    ssl_message.source_name.buffer = source_name;
    ssl_message.item_name.max_length = sizeof(item_name) - 1;
    ssl_message.item_name.buffer = item_name;

    if ( !data ) {
	data = malloc(max_data-1);
    }
    if ( !text ) {
	text = malloc(max_text-1);
    }

    for ( ;; ) {
	ssl_message.data.max_length =  max_data;
	ssl_message.data.buffer = data;
	ssl_message.text.max_length = max_text;
	ssl_message.text.buffer = text;

	ret = ssl_read(&ssl_message, SSL_NO_WAIT, (unsigned long)0);
	if ( ret == SSL_FAILURE ) {
	    if ( logfile && ssl_errno != SSL_E_NO_DATA ) {
		fprintf(logfile, "ssl_read(): failed err %d\n", ssl_errno);
		fflush(logfile);
	    }
	    switch (ssl_errno) {
	    case SSL_E_NO_DATA:
		return;
	    case SSL_E_NO_SESSION:
	    case SSL_E_NET_DOWN:
		/* Should really exit, don't know what we missed yet */
		fprintf(errfile, "TRIARCH: network is unavailable\n");
		exit(1);
	    default:
		fprintf(errfile, "TRIARCH: unknown ssl_errno %d\n", ssl_errno);
		break;
	    }
	    return;
	}

	if ( ssl_message.item_name.length > 0 )
	    ssl_message.item_name.buffer[ssl_message.item_name.length]='\0';
	if ( ssl_message.source_name.length > 0 )
	    ssl_message.source_name.buffer[ssl_message.source_name.length]='\0';

	datalen = ssl_message.data.length;
	textlen = ssl_message.text.length;

	while ( ret & (SSL_MORE_DATA | SSL_MORE_TEXT) ) {
	    if ( logfile ) {
		fprintf(logfile, "ssl_read(): more data/text\n");
		fflush(logfile);
	    }

	    if ( ret & SSL_MORE_DATA ) {
		max_data += datalen;
		data = realloc(data, max_data - 1);
	    }
	    if ( ret & SSL_MORE_TEXT ) {
		max_text += textlen;
		text = realloc(text, max_text - 1);
	    }

	    ssl_message.data.buffer = data + datalen;
	    ssl_message.text.buffer = text + textlen;

	    /* Drain this message out */
	    ret = ssl_read(&ssl_message, SSL_NO_WAIT, (unsigned long)0);

	    datalen += ssl_message.data.length;
	    textlen += ssl_message.text.length;
	    continue;
	}

	if ( textlen > 0 ) text[textlen] = '\0';
	if ( datalen > 0 ) data[datalen] = '\0';

	switch ( ssl_message.message_type ) {
	case SSL_MT_OPEN_NAK:
	    if ( logfile ) {
		fprintf(logfile, "NAK: %s %s %s %d\n",
		    source_name, item_name, text, ssl_message.status.code);
		fflush(logfile);
	    }

	    if ( num_outstanding > 0 ) {
		--num_outstanding;
	    }

	    if ( ssl_message.status.code == SSL_E_TOO_MANY_ITEMS ) {
		if ( logfile && debug ) {
		    fprintf(logfile, "Rerequesting %s %s\n",
			source_name, item_name);
		    fflush(logfile);
		}
		ret = add_queue(source_name, item_name,
		    (void **)&ssl_message.item_identifier);
		if ( !ret ) {
		    fprintf(errfile, "%s %s FATAL: Add To Queue\n",
			source_name, item_name);
		    exit(1);
		}
	    } else {
		cb = (CallbackData *)ssl_message.item_identifier;
		do_record_status((int)ssl_message.status.code,
		    source_name, item_name, text, cb->client_data, ClientData);
	    }
	    break;
	case SSL_MT_RECORD_STATUS:
	    if ( logfile && debug ) {
		fprintf(logfile, "RSTATUS: %s %s %s C%d F%d\n",
			source_name, item_name, text,
			ssl_message.status.code,
			ssl_message.status.flags);
		fflush(logfile);
	    }

	    if ( num_outstanding > 0 ) {
		--num_outstanding;
	    }

	    /* rerequest if not fatal */
	    if ( ssl_message.status.flags & SSL_SF_FATAL ||
		 ((ssl_message.status.flags & SSL_SF_INACTIVE) &&
		   ssl_message.status.code == SSL_SC_DISCONNECT) ) {
		/* pretty fatal */
		cb = (CallbackData *)ssl_message.item_identifier;
		do_record_status((int)ssl_message.status.code,
		    source_name, item_name, text, cb->client_data, ClientData);
	    } else if ( ssl_message.status.flags & SSL_SF_INACTIVE ) {
		if ( logfile && debug ) {
		    fprintf(logfile, "Rerequest\n");
		    fflush(logfile);
		}

		if ( max_outstanding > 16 ) {
		    /* slow down (forever) could be more elegant
			and gradually increase speed later */
		    --max_outstanding;
		}

		ret = add_queue(source_name, item_name,
			(void**)&ssl_message.item_identifier);
		if ( !ret ) {
		    fprintf(errfile, "%s %s FATAL: Add To Queue\n",
			source_name, item_name);
		    exit(1);
		}
	    }
	    break;
	case SSL_MT_GLOBAL_STATUS:
	    if ( logfile && debug ) {
		fprintf(logfile, "GSTATUS: %s %s C%d F%d\n", source_name, text,
		    ssl_message.status.code, ssl_message.status.flags);
		fflush(logfile);
	    }

	    /* Not sure what to do here
	     * - not really keeping track of which sources are used
	     * SSL 4.0 will do a status for each, so don't worry
	    if ( ssl_message.status.flags & SSL_SF_FATAL ) {
	    }
	     */
	    do_global_status((int)ssl_message.status.code, text, ClientData);
	    break;
	case SSL_MT_RECORD_IMAGE:
	    cb = (CallbackData *)ssl_message.item_identifier;
	    do_record_data(source_name, item_name, data, datalen,
		cb->client_data, ClientData);
	    if ( num_outstanding > 0 ) {
		--num_outstanding;
	    }
	    break;
	case SSL_MT_CLOSE_NAK:
	    break;
	case SSL_MT_RENAME:
	case SSL_MT_PAGE_STATUS:
	case SSL_MT_PAGE_IMAGE:
	case SSL_MT_PAGE_UPDATE:
	    fprintf(badricfile,"%s %s not record service\n",
		    source_name, item_name);
	    fflush(badricfile);
	    break;
	default:
	    break;
	}
    }
}

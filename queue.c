/*
 * File:	queue.c
 * Author:	Edward Chubin
 * Description:	simple FIFO queue. Implemented with a linked list.
 */
#include <stdio.h>
#include <string.h>
#include <memory.h>
#if sun
#include <malloc.h>
#endif
#ifdef WIN32
extern void *malloc(int);
extern void free(void *);
#endif

typedef struct qlist
{
	struct qlist	*next;
	char		*source;
	char		*item;
	void		*data;
} QLIST;

static QLIST	*qlast = NULL;
static QLIST	*q_get = NULL;

#define TRUE	1
#define FALSE	0

int
add_queue(source_name, item_name, data_ptr)
char	*source_name;
char	*item_name;
void	**data_ptr;
{
	QLIST	*qcurr;
	int	source_len;
	int	item_len;

	qcurr = (QLIST *) malloc(sizeof(QLIST));
	if ( ! qcurr ) {
		return FALSE;
	}

	if ( ! q_get ) {
		q_get = qcurr;
	}

	if ( qlast ) {
		qlast->next = qcurr;
	}

	qlast = qcurr;

	source_len = strlen(source_name);
	item_len = strlen(item_name);

	qcurr->next = NULL;
	qcurr->source = (char *) malloc(source_len + 1);
	if ( ! qcurr->source ) {
		return FALSE;
	}
	qcurr->item = (char *) malloc(item_len + 1);
	if ( ! qcurr->item ) {
		return FALSE;
	}

	memcpy(qcurr->source, source_name, source_len+1);
	memcpy(qcurr->item, item_name, item_len+1);
	qcurr->data = *data_ptr;

	return TRUE;
}

int
get_queue(source_name, item_name, data_ptr)
char	*source_name;
char	*item_name;
void	**data_ptr;
{
	QLIST	*qcurr;

	if ( ! q_get ) {
		/* haven't got any yet, or at top */
		return FALSE;
	}

	if ( ! q_get->source ) {
		return FALSE;
	}
	if ( ! q_get->item ) {
		return FALSE;
	}

	strcpy(source_name, q_get->source);
	strcpy(item_name, q_get->item);
	*data_ptr = q_get->data;

	qcurr = q_get;

	/* next get */
	q_get = q_get->next;

	free(qcurr->source);
	free(qcurr->item);

	if ( qcurr == qlast )
		qlast = NULL;
	if ( qcurr == q_get )
		q_get = NULL;

	free((char *)qcurr);

	return TRUE;
}

#ifdef UNIT_TEST

#include <signal.h>

void intr();

main()
{
	int	i,j;

	int	ret;

	char	source[20];
	char	item[20];

	char	*p_source;
	char	*p_item;
	char	*p_data;

	/* check for leaks */
	malloc_debug(2);

	printf("\n"); fflush(stdout);

	mallocmap();

	for ( j=1; j<=100; j++ ) {
		printf("+"); fflush(stdout);
		for ( i = 1; i <= 500; i++ ) {
			sprintf(source, "MySource");
			sprintf(item, "%d", i);

			p_data = malloc(30);
			sprintf(p_data, "%s.%s.%d.%d", source, item, i, j);

			ret = add_queue(source, item, &p_data);
			if ( !ret ) {
			    printf("add_queue() %s %s %s\n",source,item,p_data);
			    break;
			}
#ifdef QUICK_TEST
			get_queue(source, item, &p_data);
			free(p_data);
#endif
		}
#ifndef QUICK_TEST
		while ( get_queue(source, item, &p_data) ) {
			printf("get_queue() %s %s [%s]\n",source,item,p_data);
			fflush(stdout);
			free(p_data);
		}
#endif
		printf("-"); fflush(stdout);
	}

	printf("\n"); fflush(stdout);

	mallocmap();

	if ( malloc_verify() == 0 ) {
		printf("Malloc Verfy has err\n");
	}
}
void
intr()
{
	mallocmap();
	exit(0);
}
#endif

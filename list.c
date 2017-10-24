/*
 * File:	list.c
 * Author:	Edward Chubin
 * Description:	Maintain a list of records with data
 *		This file is meant to be used only in a small setting, where
 *		a relatively small number of items (<100) exist. Otherwise,
 *		a hash lookup must be used. Not a major performance penalty
 *		for the generic FRE. Implemented using a simple linked list.
 *		Note that find_item() destroys the entry for the item.
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

typedef struct wlist {
	struct wlist	*next;
	struct wlist	*prev;
	char		*source;
	char		*item;
	int		ref_count;
	void		*data;
} WLIST;

static WLIST	*wfirst = NULL;
static WLIST	*wlast = NULL;

#define TRUE	1
#define FALSE	0

static WLIST *
item_exists(source_name, item_name)
char	*source_name;
char	*item_name;
{
	WLIST	*wcurr;
	for ( wcurr = wfirst; wcurr; wcurr = wcurr->next ) {
		if ( wcurr->source
		  && !strcmp(wcurr->source, source_name)
		  && wcurr->item
		  && !strcmp(wcurr->item, item_name) ) {
			return wcurr;
		}
	}
	return NULL;
}

int
add_item(source_name, item_name, data)
char	*source_name;
char	*item_name;
void	**data;		/* pointer to the data */
{
	WLIST	*wcurr;
	int	source_len;
	int	item_len;

	if ( wcurr = item_exists(source_name, item_name) ) {
		/* don't do anything with this yet */
		++(wcurr->ref_count);
		*data = wcurr->data;
		return FALSE;
	}

	wcurr = (WLIST *) malloc(sizeof(WLIST));
	if ( ! wcurr ) {
		return FALSE;
	}

	wcurr->ref_count = 1;
	if ( ! wfirst ) {
		wfirst = wcurr;
		wfirst->prev = NULL;
	}

	if ( wlast ) {
		wlast->next = wcurr;
		wcurr->prev = wlast;
	}

	wlast = wcurr;

	source_len = strlen(source_name);
	item_len = strlen(item_name);

	wcurr->next = NULL;
	wcurr->source = (char *) malloc(source_len + 1);
	if ( ! wcurr->source ) {
		return FALSE;
	}
	wcurr->item = (char *) malloc(item_len + 1);
	if ( ! wcurr->item ) {
		return FALSE;
	}

	memcpy(wcurr->source, source_name, source_len+1);
	memcpy(wcurr->item, item_name, item_len+1);

	wcurr->data = *data;

	return TRUE;
}

/*
 * Return the fields associated with item.
 * Release the data (Free it).
 * This routine should be called whenever a request is processed,
 * Especially when rejected like illegal name, etc.
 */

int
find_item(source_name, item_name, data)
char	*source_name;
char	*item_name;
void	**data;
{
	int count;
	WLIST	*wcurr;

	/* it's not the fastest,
	   but for a relatively small number of outstanding requests
	   it's not so bad.
	   It can be seamlessly improved with hashing
	*/
	for ( wcurr = wfirst; wcurr; wcurr = wcurr->next ) {
		if ( wcurr->item && !strcmp(wcurr->item, item_name)
		  && wcurr->source && !strcmp(wcurr->source, source_name) ) {
			*data = wcurr->data;
			count = wcurr->ref_count;

			/* relink the list */
			if ( wcurr->prev )
				(wcurr->prev)->next = wcurr->next;

			if ( wcurr->next )
				(wcurr->next)->prev = wcurr->prev;

			/* Free the current one */
			free(wcurr->source);
			free(wcurr->item);

			if ( wcurr == wfirst ) {
				wfirst = wcurr->next;
			}
			if ( wcurr == wlast ) {
				wlast = wcurr->prev;
			}

			free(wcurr);
			return count;
		}
	}
	return FALSE;
}


#ifdef UNIT_TEST
#include <signal.h>
void intr();

main()
{
	int	i,j;
	int	*data;

	int	ret;

	char	source[20];
	char	item[20];

	/* check for leaks */
	malloc_debug(2);

	printf("mallocmap\n"); fflush(stdout);
	mallocmap();
	printf("done\n"); fflush(stdout);

	sprintf(source, "MySource");

	for ( j=0; j<10; j++ ) {
		printf("+"); fflush(stdout);

		for ( i = 0; i <= 500; i++ ) {

			sprintf(item, "%d", i);

			data = (int *)malloc(sizeof(int));
			*data = i;

			ret = add_item(source, item, &data);
			if ( ! ret ) {
				printf("add_item() %s %s failed\n",
					source, item);
			}
		}

		/* worst case */
		for ( i = 500; i >= 0; i-- ) {

			sprintf(item, "%d", i);

			ret = find_item(source, item, &data);
			if ( !ret ) {
				printf("find_item() %s %s failed\n",
					source, item);
				fflush(stdout);
			}
			else {
				if ( *data != i ) {
					printf("find_item() %s %s data %d %x\n",
						source, item, *data, *data);
					fflush(stdout);
				}
				free(data);
			}
		}

		printf("-"); fflush(stdout);
	}

	printf("\n"); fflush(stdout);

	printf("mallocmap\n"); fflush(stdout);
	mallocmap();
	printf("done\n"); fflush(stdout);

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

/* Written by Edward Chubin
 * On or about the night/morning of 4/22/98
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "hash.h"

/* should hash the list of the hashtables */
static unsigned int	bit_mask = 1;
static unsigned int	hashTableSize = 0;

#ifdef UNIT_TEST
int		debug = 2;
FILE		*logfile = stdout;
#else
extern int	debug;
extern FILE	*logfile;
#endif

/* take the hashtable as an arg so we know the bit mask */

static unsigned int
hash(register char *str)
{
    /* probably should seed it */
    unsigned int hash_val = 0;
    unsigned int inverse_bit_mask = bit_mask ^ 0xffffffff; /* one's compliment */
    for ( ; *str; str++ ) {
	/* spread it out */
	hash_val <<= 3;
	hash_val ^= (unsigned int)*str;
	/* large number -- shift over */
	if ( hash_val & inverse_bit_mask ) {
	    /* nice randomnization */
	    hash_val >>=  0x7 & *str;
#ifdef UNIT_TEST2
	    printf("Shifted %d\n", 0x7 & *str);
#endif
	}
    }
    return hash_val & bit_mask;
}

HashTable **
initHashTable(unsigned int minTableSize)
{
    HashTable **hashTable;

    if (minTableSize > MAXINT) {
	minTableSize = MAXINT;
    }

    /* Find mask to accomodate those entries */
    while ( bit_mask < minTableSize ) {
	  bit_mask <<= 1;
	  ++bit_mask;
    }

    hashTableSize = bit_mask + 1;

    /* allocate the hash table -- need slots for all possible bit masks */
    hashTable = (HashTable **)malloc(sizeof(HashTable) * hashTableSize);
    if ( hashTable ) {
	memset(hashTable, '\0', sizeof(HashTable) * hashTableSize);
/* This is where an entry would be updated */
#ifdef UNIT_TEST
printf("Allocated HashTable %p Entries %d bit_mask X%x\n", hashTable, hashTableSize, bit_mask);
#endif
    }
    return hashTable;
}

/* Walk and destroy entries? */
void
destroyHashTable(HashTable **ht)
{
    if ( ht ) {
	free(ht);
	ht = NULL;
    }
}

HashTable *
hash_add_entry(HashTable **ht, char *source, char *item, void **value)
{
    int res = 0;
    int hash_val;
    HashTable *entry;
    HashTable *new_entry = NULL;

    static char	hash_name[512];

    if ( !ht ) {
	return NULL;
    }

    /* can make this optional when dealing with single source */
    strcpy(hash_name, item);
    strcat(hash_name, source);

    hash_val = hash(hash_name);

    entry = ht[hash_val];
    while ( entry ) {
	res = strcmp(entry->name, hash_name);
	if ( res == 0 ) {
	    /* already exists */
	    return NULL;
	} else if ( entry->f_val ) {
	    entry = entry->f_val;
	} else {
	    break;
	}
    }

    /* install it here */
    new_entry = (HashTable *)malloc(sizeof(HashTable));
    if ( new_entry ) {
	memset(new_entry, 0, sizeof(HashTable));

	new_entry->name = strdup(hash_name);
	new_entry->value = (void *)*value;
	new_entry->f_val = NULL;

	if ( entry ) {
	    entry->f_val = new_entry;
	    new_entry->b_val = entry;
	} else {
	    ht[hash_val] = new_entry;
	    new_entry->b_val = NULL;
	}
    }

    if ( debug > 6 ) {
	fprintf(logfile, "AddItem: Returning entry %p %s\n",
		new_entry, new_entry?new_entry->name:"");
    }
    return new_entry;
}

HashTable *
hash_find_entry(HashTable **ht, char *source, char *item, void **value)
{
    int res = 0;
    int hash_val;
    HashTable *entry = NULL;

    static char	hash_name[512];

    if ( !ht ) {
	return NULL;
    }

    strcpy(hash_name, item);
    strcat(hash_name, source);

    hash_val = hash(hash_name);

    entry = ht[hash_val];
    while ( entry ) {
	res = strcmp(entry->name, hash_name);
	if ( res == 0 ) {
	    /* exists */
	    *value = (void **)entry->value;
	    break;
	} else {
	    entry = entry->f_val;
	}
    }

    if ( debug > 6 ) {
	fprintf(logfile, "FindItem: Returning entry %p %s\n",
	    entry, entry?entry->name:"");
    }
    return entry;
}

int
hash_delete_entry(HashTable **ht, char *source, char *item)
{
    int res = 0;
    int hash_val;
    HashTable *entry = NULL;

    static char	hash_name[512];

    if ( !ht ) {
	return 0;
    }

    strcpy(hash_name, item);
    strcat(hash_name, source);

    hash_val = hash(hash_name);

    entry = ht[hash_val];
    while ( entry ) {
	res = strcmp(entry->name, hash_name);
	if ( res == 0 ) {
	    if ( entry->b_val ) {
		entry->b_val->f_val = entry->f_val;
	    } else {
		ht[hash_val] = entry->f_val;
	    }
	    if ( entry->f_val ) {
		entry->f_val->b_val = entry->b_val;
	    }

	    free(entry->name);
	    free(entry);

	    res = 1;
	    break;
	} else {
	    entry = entry->f_val;
	}
    }
    return res;
}

#ifdef UNIT_TEST

static unsigned int depth = 0;
static unsigned int max_depth = 0;
static unsigned int num_base_entries = 0;
static unsigned int total_entries = 0;
static unsigned int num_forward_entries = 0;

static void follow_f_entries(HashTable *entry)
{
    if ( entry ) {
	++total_entries;
	++num_forward_entries;
	++depth;
	follow_f_entries(entry->f_val);
    }
}

/* To understand recursion, you must understand recursion ... */
void compute_hash_table_stats(HashTable **ht)
{
    if ( ht ) {
	unsigned int i;
	for ( i = 0; i < hashTableSize; i++ ) {
	    HashTable *entry = ht[i];
	    depth = 0;
	    if ( entry ) {
		++total_entries;
		++num_base_entries;
		follow_f_entries(entry->f_val);
	    }
	    if (depth > max_depth) max_depth = depth;
	}
    }
    printf("HashTable %p Size %u Entries %u Base %u Forward %u Depth %u\n",
	ht, hashTableSize, total_entries, num_base_entries, num_forward_entries, max_depth);
}

int
main(int argc, char **argv)
{
    char	string[512];

    HashTable **ht = initHashTable(50000);
    if ( !ht ) {
	printf("Couldn't doit: out of memory\n");
	exit(0);
    }

    while ( gets(string) ) {
	int ret;
	char *src, *name;

	src = strtok(string, " \t,:");
	name = strtok(NULL, " \t\n,:");

	if ( src && name ) {
	    char *val2;
	    char *val1 = strdup(name);
	    printf("src \"%s\" name \"%s\" val %p...\n", src, name, val1);

	    ret = (int)hash_find_entry(ht, src, name, (void**)&val2);
	    printf("FIND: %s\n", ret?"TRUE":"FALSE");

	    ret = (int)hash_add_entry(ht, src, name, (void**)&val1);
	    printf("ADD: %s\n", ret?"TRUE":"FALSE");

	    if ( ret ) {
		ret = (int)hash_find_entry(ht, src, name, (void**)&val2);
		printf("FIND: %s\n", ret?"TRUE":"FALSE");
		printf("val..%p\n", val2);

		if ( argc > 1 ) {
		    hash_delete_entry(ht, src, name);
		}
	    }

	    free(val1);
	} else {
	    printf("Bad input - need 2 values\n");
	}
    }
    compute_hash_table_stats(ht);
    destroyHashTable(ht);
    return 0;
}
#endif

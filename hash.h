/* Written by Edward Chubin
 * On or about the night/morning of 4/22/98
 */
#define MAXINT	0xffffffff

typedef struct _ht {
    char 	*name;
    void 	*value;
    struct _ht  *b_val;	/* back pointer */
    struct _ht  *f_val;	/* forward pointer */
} HashTable;


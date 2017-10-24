/*
 * SSLFRE.H
 */

/*
 * General Defs
 */
#ifndef TRUE
# define TRUE	1
# define FALSE	0
#endif

#define MAXLINE		1024

/*
 * Very Interesting fid's
 */
#define REF_COUNT	239
#define NEXT_LR		238
#define LINK_1		240
#define LINK_14		LINK_1+13
#define LONGLINK_1	800
#define LONGLINK_14	LONGLINK_1+13
#define LONGNEXTLR	815

/*
 * Fre status codes
 */
#define FRE_OK				0
#define FRE_ERROR_ITEM_NAME		(-1)
#define FRE_ERROR_SOURCE_NAME		(-2)
#define FRE_ERROR_ACCESS_DENIED		(-3)
#define FRE_ERROR_NO_FIDS		(-4)
#define FRE_ERROR_COMM_PROBLEM		(-5)
#define FRE_ERROR_INVALID_LINE		(-6)
#define FRE_ERROR_INVALID_FID		(-7)
#define FRE_ERROR_NET_DOWN		(-8)
#define FRE_ERROR_UNSPECIFIED		(-9)
#define FRE_ERROR_NO_MEMORY		(-10)
#define FRE_ERROR_NOT_RECORD_SERVICE	(-11)
#define FRE_ERROR_DUPLICATE_RECORD	(-12)

/*
 * Default delimiter
 */
#define DFL_DELIMITER_START	'('
#define DFL_DELIMITER_END	')'


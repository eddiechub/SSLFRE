/*
 * Copyright:	1996 Edward Chubin
 */

/*
 * General defs
 */
#ifndef TRUE
# define TRUE	1
# define FALSE	0
#endif

/*
 * Marketfeed ascii defines
 */
#define FS  0x1c
#define GS  0x1d
#define RS  0x1e
#define US  0x1f

/*
 * Page Interesting fids
 */
#define ROW64_1 	215
#define ROW64_14	(ROW64_1 + 13)
#define ROW80_1		315
#define ROW80_25	(ROW80_1 + 24)

typedef struct IDENTIFIER_ENTRY {
	char		*name;
	struct IDENTIFIER_ENTRY *next;
} IDENTIFIER_ENTRY;

typedef struct FID_ARRAY {
	int		fid;
	int		precision;
	int		min_width;
	int		length;
	int		updated;
	char		*value;
	struct FID_ARRAY *next;
} FID_ARRAY;

#define FRE_STATUS_NONE		0
#define FRE_STATUS_OK		1
#define FRE_STATUS_PENDING	2
#define FRE_STATUS_DEAD		2

typedef struct {
	int		count;
	int		status;
	int		template;
	int		do_all_fids;
#ifdef AUTOMATIC_REQUEST_OPTION
	int		request_option;
	int		len_of_original_chain;
#endif
	char		*original_item;
	void		*other_data;
	FID_ARRAY	*fid_array;
	FID_ARRAY	*last;
	FID_ARRAY	*fidArrayNext;	/* Simple iterator */
} RECORD_INFO;

extern int		GetDoubleField(RECORD_INFO*, int, double*);
extern int		GetIntField(RECORD_INFO*, int, int*);
extern int		GetDateField(RECORD_INFO*, int, int*, int*, int*);
extern int		GetTimeField(RECORD_INFO*, int, int*, int*, int*);
extern void		SetTriarchUserName(char*);
extern int		GetTriarchData(RECORD_INFO*,char*,char*);
extern void		doneWithTriarch(void);
extern RECORD_INFO	*create_record_info(void);
extern void		free_record_info(RECORD_INFO*);
extern void		show_record_info(RECORD_INFO*);
extern int		set_fids(RECORD_INFO*,char*);
extern void		set_all_fids(RECORD_INFO*);
extern int		add_fid(RECORD_INFO*,int);
extern int		do_this_fid(RECORD_INFO*,int);
extern int		addToFidArray(RECORD_INFO*,int,char*);
extern int		string2double(char*,double*);

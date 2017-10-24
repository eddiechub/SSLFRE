#include <stdio.h>
#include <sys/types.h>

extern int fflush();
extern int fclose();

void
fhexdump (title, area, len)
char 		*title;
char 		*area;
int 		len;
{  
	int 		i, j;
	char 		buff[30];
	char 		*ptr;
	FILE		*fp;

	fp = fopen ("dump", "a");

	fprintf(fp,"\n*** hexdump '%s' at 0x%x length %d\n\n",
		title,area,len);
	ptr = buff;
	j = 1;
	for (i = 0; i < len; ++i) {
		fprintf(fp, "%02x ", *area);
		if(*area > ' ' && *area < '}') {
			*ptr++ = *area;
		}
		else {
			*ptr++ = '.';
		}
		area++;
		if (j == 8) {
			fprintf(fp, "| ");
			*ptr++ = ' ';
		}
		if (j == 16) {
			*ptr = 0;
			fprintf(fp, "   %s\n", buff);
			ptr = buff;
			j = 0;
		}
		j++;
	}
	/* print last line */
	for (;j <= 16; ++j) {
		fprintf(fp, "-- ");
		*ptr++ = '.';
		if (j == 8) {
			fprintf(fp, "| ");
			*ptr++ = ' ';
		}
	}
	*ptr = 0;
	fprintf(fp, "   %s\n", buff);
	fprintf(fp,"\n*** end of dump\n");
	fflush(fp);
	fclose (fp);
}

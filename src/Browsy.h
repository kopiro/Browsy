#ifndef _BROWSY_H
#define _BROWSY_H

//const short defaultALRT = 129;

#define BROWSY_VERSION "0.2.0"

Boolean HasColorQD;
Boolean Sys7;

/* Packed into a single Handle: struct + address + title strings inline */
typedef struct HistoryItem {
	char *title;     /* points into same Handle, or NULL */
	char *address;   /* points into same Handle, or NULL */
	struct HistoryItem *prev;
	struct HistoryItem *next;
} HistoryItem;

void Terminate();

#endif

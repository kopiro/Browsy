#ifndef _PARSER_H
#define _PARSER_H

#include <TextEdit.h>
#include <MacWindows.h>

/* Parser states */
typedef enum {
	PS_TEXT,
	PS_TAG,
	PS_COMMENT,
	PS_ENTITY
} HtmlParserState;

/* Text style record pushed to TextEdit */
typedef struct {
	short font;
	short size;
	Style face;
} HtmlTextStyle;

typedef struct HtmlParser {
	TEHandle outputTE;
	WindowPtr window;

	HtmlParserState state;

	/* Tag accumulation (survives chunk boundaries) */
	char tagBuf[32];
	short tagBufLen;
	Boolean isClosingTag;

	/* Text batch buffer */
	char textBuf[512];
	short textBufLen;
	long totalInserted;

	/* Comment detection: counts '-' after '<!' */
	short commentDashes;

	/* Entity accumulation */
	char entityBuf[10];
	short entityBufLen;

	/* Style tracking */
	Boolean isBold;
	Boolean isItalic;
	Boolean isUnderline;
	Boolean isPre;
	Boolean inScript;
	Boolean inStyle;
	Boolean inTitle;
	short headingLevel;
	short listDepth;

	/* Title capture */
	char titleBuf[128];
	short titleBufLen;

	/* Whitespace collapsing */
	Boolean lastWasSpace;
	Boolean needsNewline;
} HtmlParser;

void HtmlParserInit(HtmlParser *p, TEHandle te, WindowPtr win);
void HtmlParserFeed(HtmlParser *p, const char *data, short len);
void HtmlParserEnd(HtmlParser *p);

#endif

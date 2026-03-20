#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <Memory.h>
#include <Fonts.h>
#include <TextEdit.h>
#include "parser.h"
#include "utils.h"

#define MAX_INSERT 30000
#define HR_WIDTH 40
#define BULLET_CHAR '\245'

typedef struct {
	short scrpNStyles;
	ScrpSTElement scrpStyleTab[1];
} HtmlStyleScrap;

/* Forward declarations */
static void FlushText(HtmlParser *p);
static void EmitChar(HtmlParser *p, char c);
static void EmitString(HtmlParser *p, const char *s, short len);
static void EmitNewline(HtmlParser *p);
static void GetCurrentStyle(HtmlParser *p, TextStyle *ts);
static StScrpHandle NewStyleScrap(WindowPtr win, const TextStyle *ts);
static void HandleOpenTag(HtmlParser *p, const char *tag, short len);
static void HandleCloseTag(HtmlParser *p, const char *tag, short len);
static void HandleEntity(HtmlParser *p);
static Boolean TagEquals(const char *tagBuf, short tagLen, const char *name);
static short TagNameLength(const char *tag, short len);
static long CurrentOutputOffset(const HtmlParser *p);
static char *ExtractAttributeValue(const char *tag, short len, const char *name);
static Boolean AppendLinkRange(HtmlParser *p, long startOffset, long endOffset,
	char *href);
static short CurrentHeadingLevel(const HtmlParser *p);
static void PushHeading(HtmlParser *p, short level);
static void PopHeading(HtmlParser *p, short level);
static void IncrementDepth(short *depth);
static void DecrementDepth(short *depth);

/* Case-insensitive tag comparison */
static Boolean TagEquals(const char *tagBuf, short tagLen, const char *name)
{
	short i;
	short nameLen = strlen(name);
	if (tagLen != nameLen) return false;
	for (i = 0; i < tagLen; i++) {
		char a = tagBuf[i];
		char b = name[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return false;
	}
	return true;
}

static short TagNameLength(const char *tag, short len)
{
	short i;

	for (i = 0; i < len; i++) {
		char c = tag[i];
		if (isspace(c) || c == '/') return i;
	}

	return len;
}

static long CurrentOutputOffset(const HtmlParser *p)
{
	return p->totalInserted + p->textBufLen;
}

static char *ExtractAttributeValue(const char *tag, short len, const char *name)
{
	short nameLen = strlen(name);
	short i = TagNameLength(tag, len);

	while (i < len) {
		short attrStart, valueStart, valueLen;
		char quote;

		while (i < len && isspace(tag[i])) i++;
		if (i >= len || tag[i] == '/') break;

		attrStart = i;
		while (i < len && !isspace(tag[i]) && tag[i] != '=' && tag[i] != '/') i++;
		if (i - attrStart == nameLen
			&& strncasecmp(tag + attrStart, name, nameLen) == 0) {
			while (i < len && isspace(tag[i])) i++;
			if (i >= len || tag[i] != '=') return NULL;
			i++;
			while (i < len && isspace(tag[i])) i++;
			if (i >= len) return NULL;

			quote = tag[i];
			if (quote == '"' || quote == '\'') {
				valueStart = ++i;
				while (i < len && tag[i] != quote) i++;
				valueLen = i - valueStart;
			} else {
				valueStart = i;
				while (i < len && !isspace(tag[i]) && tag[i] != '>') i++;
				valueLen = i - valueStart;
			}

			if (valueLen <= 0) return NULL;
			{
				char *value = malloc(valueLen + 1);
				if (!value) return NULL;
				memcpy(value, tag + valueStart, valueLen);
				value[valueLen] = '\0';
				return value;
			}
		}

		while (i < len && isspace(tag[i])) i++;
		if (i < len && tag[i] == '=') {
			i++;
			while (i < len && isspace(tag[i])) i++;
			if (i < len && (tag[i] == '"' || tag[i] == '\'')) {
				quote = tag[i++];
				while (i < len && tag[i] != quote) i++;
				if (i < len) i++;
			} else {
				while (i < len && !isspace(tag[i]) && tag[i] != '>') i++;
			}
		}
	}

	return NULL;
}

static Boolean AppendLinkRange(HtmlParser *p, long startOffset, long endOffset,
	char *href)
{
	HtmlLinkRange *newLinks;
	short newCapacity;

	if (!href || endOffset <= startOffset) return false;

	if (p->linkCount >= p->linkCapacity) {
		newCapacity = p->linkCapacity ? p->linkCapacity * 2 : 8;
		newLinks = realloc(p->links, sizeof(HtmlLinkRange) * newCapacity);
		if (!newLinks) return false;
		p->links = newLinks;
		p->linkCapacity = newCapacity;
	}

	p->links[p->linkCount].startOffset = startOffset;
	p->links[p->linkCount].endOffset = endOffset;
	p->links[p->linkCount].href = href;
	p->linkCount++;
	return true;
}

static short CurrentHeadingLevel(const HtmlParser *p)
{
	if (p->headingDepth <= 0) return 0;
	return p->headingStack[p->headingDepth - 1];
}

static void PushHeading(HtmlParser *p, short level)
{
	short maxDepth = (short)(sizeof(p->headingStack) / sizeof(p->headingStack[0]));

	if (p->headingDepth < maxDepth) {
		p->headingStack[p->headingDepth++] = level;
	}
}

static void PopHeading(HtmlParser *p, short level)
{
	short i;

	for (i = p->headingDepth - 1; i >= 0; i--) {
		if (p->headingStack[i] == level) {
			for (; i < p->headingDepth - 1; i++) {
				p->headingStack[i] = p->headingStack[i + 1];
			}
			p->headingDepth--;
			return;
		}
	}
}

static void IncrementDepth(short *depth)
{
	if (*depth < 32767) (*depth)++;
}

static void DecrementDepth(short *depth)
{
	if (*depth > 0) (*depth)--;
}

/* Flush the text buffer into TextEdit with current style */
static void FlushText(HtmlParser *p)
{
	TextStyle ts;
	StScrpHandle styleScrap;

	if (p->textBufLen == 0) return;
	if (p->totalInserted >= MAX_INSERT) return;

	/* Truncate if we'd exceed the limit */
	if (p->totalInserted + p->textBufLen > MAX_INSERT) {
		p->textBufLen = MAX_INSERT - p->totalInserted;
	}

	GetCurrentStyle(p, &ts);
	styleScrap = NewStyleScrap(p->window, &ts);
	TESetSelect(32767, 32767, p->outputTE);
	if (styleScrap != NULL) {
		TEStyleInsert(p->textBuf, p->textBufLen, styleScrap, p->outputTE);
		DisposeHandle((Handle)styleScrap);
	} else {
		TEInsert(p->textBuf, p->textBufLen, p->outputTE);
	}
	p->totalInserted += p->textBufLen;
	p->textBufLen = 0;

	if (p->totalInserted >= MAX_INSERT) {
		static char trunc[] = "\r[Page truncated]";
		GetCurrentStyle(p, &ts);
		styleScrap = NewStyleScrap(p->window, &ts);
		TESetSelect(32767, 32767, p->outputTE);
		if (styleScrap != NULL) {
			TEStyleInsert(trunc, sizeof(trunc) - 1, styleScrap, p->outputTE);
			DisposeHandle((Handle)styleScrap);
		} else {
			TEInsert(trunc, sizeof(trunc) - 1, p->outputTE);
		}
	}
}

/* Compute the current styled TextEdit text style */
static void GetCurrentStyle(HtmlParser *p, TextStyle *ts)
{
	short headingLevel = CurrentHeadingLevel(p);
	Boolean isPre = p->preDepth > 0;
	Boolean isMonospace = isPre || p->monospaceDepth > 0;

	memset(ts, 0, sizeof(TextStyle));

	if (isMonospace) {
		ts->tsFont = kFontIDMonaco;
	} else {
		ts->tsFont = kFontIDGeneva;
	}

	if (isPre) {
		ts->tsSize = 9;
	} else if (headingLevel > 0) {
		switch (headingLevel) {
			case 1: ts->tsSize = 18; break;
			case 2: ts->tsSize = 14; break;
			case 3: ts->tsSize = 12; break;
			case 4: ts->tsSize = 11; break;
			case 5: ts->tsSize = 10; break;
			default: ts->tsSize = 9; break;
		}
	} else {
		ts->tsSize = 9;
	}

	ts->tsFace = 0;
	if (p->boldDepth > 0 || headingLevel > 0) ts->tsFace |= bold;
	if (p->italicDepth > 0) ts->tsFace |= italic;
	if (p->underlineDepth > 0) ts->tsFace |= underline;
}

static StScrpHandle NewStyleScrap(WindowPtr win, const TextStyle *ts)
{
	Handle h;
	HtmlStyleScrap *scrap;
	FontInfo fi;
	GrafPtr oldPort;

	h = NewHandleClear(sizeof(HtmlStyleScrap));
	if (h == NULL) return NULL;

	HLock(h);
	scrap = (HtmlStyleScrap *)*h;
	scrap->scrpNStyles = 1;
	scrap->scrpStyleTab[0].scrpStartChar = 0;
	scrap->scrpStyleTab[0].scrpFont = ts->tsFont;
	scrap->scrpStyleTab[0].scrpFace = ts->tsFace;
	scrap->scrpStyleTab[0].scrpSize = ts->tsSize;
	scrap->scrpStyleTab[0].scrpColor = ts->tsColor;

	if (win != NULL) {
		GetPort(&oldPort);
		SetPort(win);
		TextFont(ts->tsFont);
		TextFace(ts->tsFace);
		TextSize(ts->tsSize);
		GetFontInfo(&fi);
		SetPort(oldPort);
		scrap->scrpStyleTab[0].scrpHeight = fi.ascent + fi.descent + fi.leading;
		scrap->scrpStyleTab[0].scrpAscent = fi.ascent;
	} else {
		scrap->scrpStyleTab[0].scrpHeight = ts->tsSize + 3;
		scrap->scrpStyleTab[0].scrpAscent = ts->tsSize;
	}

	HUnlock(h);
	return (StScrpHandle)h;
}

/* Emit a single character, respecting whitespace collapsing */
static void EmitChar(HtmlParser *p, char c)
{
	if (p->totalInserted >= MAX_INSERT) return;
	if (p->inScript || p->inStyle) return;

	/* Title capture */
	if (p->inTitle) {
		if (p->titleBufLen < (short)(sizeof(p->titleBuf) - 1)) {
			p->titleBuf[p->titleBufLen++] = c;
		}
		return;
	}

	if (p->preDepth <= 0) {
		/* Whitespace collapsing */
		if (c == '\n' || c == '\t' || c == '\r') c = ' ';
		if (c == ' ') {
			if (p->lastWasSpace) return;
			p->lastWasSpace = true;
		} else {
			p->lastWasSpace = false;
		}
	}

	/* Emit pending newline before non-whitespace content */
	if (p->needsNewline && c != ' ') {
		p->needsNewline = false;
		if (p->textBufLen > 0 || p->totalInserted > 0) {
			if (p->textBufLen >= (short)(sizeof(p->textBuf) - 1)) {
				FlushText(p);
			}
			p->textBuf[p->textBufLen++] = '\r';
			p->lastWasSpace = true;
		}
	}

	if (p->textBufLen >= (short)sizeof(p->textBuf)) {
		FlushText(p);
	}
	if (p->activeLinkHref && !p->activeLinkHasStart) {
		p->activeLinkStart = CurrentOutputOffset(p);
		p->activeLinkHasStart = true;
	}
	p->textBuf[p->textBufLen++] = c;
}

static void EmitString(HtmlParser *p, const char *s, short len)
{
	short i;
	for (i = 0; i < len; i++) {
		EmitChar(p, s[i]);
	}
}

static void EmitNewline(HtmlParser *p)
{
	if (p->inScript || p->inStyle || p->inTitle) return;
	/* Mark that a newline is needed before the next visible char */
	p->needsNewline = true;
	p->lastWasSpace = true;
}

/* Resolve an HTML entity from entityBuf */
static void HandleEntity(HtmlParser *p)
{
	char resolved = '?';
	p->entityBuf[p->entityBufLen] = '\0';

	if (p->entityBuf[0] == '#') {
		/* Numeric: just emit '?' for non-ASCII */
		long val = 0;
		short i;
		if (p->entityBuf[1] == 'x' || p->entityBuf[1] == 'X') {
			for (i = 2; i < p->entityBufLen; i++) {
				char c = p->entityBuf[i];
				if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
				else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
			}
		} else {
			for (i = 1; i < p->entityBufLen; i++) {
				char c = p->entityBuf[i];
				if (c >= '0' && c <= '9') val = val * 10 + (c - '0');
			}
		}
		if (val > 0 && val < 128) resolved = (char)val;
	} else if (strcmp(p->entityBuf, "amp") == 0) {
		resolved = '&';
	} else if (strcmp(p->entityBuf, "lt") == 0) {
		resolved = '<';
	} else if (strcmp(p->entityBuf, "gt") == 0) {
		resolved = '>';
	} else if (strcmp(p->entityBuf, "quot") == 0) {
		resolved = '"';
	} else if (strcmp(p->entityBuf, "apos") == 0) {
		resolved = '\'';
	} else if (strcmp(p->entityBuf, "nbsp") == 0) {
		resolved = ' ';
	}

	EmitChar(p, resolved);
}

/* Process an opening tag */
static void HandleOpenTag(HtmlParser *p, const char *tag, short len)
{
	short tagNameLen = TagNameLength(tag, len);
	char *href;

	/* Strip trailing '/' for self-closing tags */
	if (len > 0 && tag[len - 1] == '/') len--;

	/* Block elements emit newlines */
	if (TagEquals(tag, tagNameLen, "p") ||
		TagEquals(tag, tagNameLen, "div") ||
		TagEquals(tag, tagNameLen, "br") ||
		TagEquals(tag, tagNameLen, "tr")) {
		FlushText(p);
		EmitNewline(p);
	} else if (TagEquals(tag, tagNameLen, "h1")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 1);
	} else if (TagEquals(tag, tagNameLen, "h2")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 2);
	} else if (TagEquals(tag, tagNameLen, "h3")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 3);
	} else if (TagEquals(tag, tagNameLen, "h4")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 4);
	} else if (TagEquals(tag, tagNameLen, "h5")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 5);
	} else if (TagEquals(tag, tagNameLen, "h6")) {
		FlushText(p);
		EmitNewline(p);
		PushHeading(p, 6);
	} else if (TagEquals(tag, tagNameLen, "hr")) {
		short i;
		FlushText(p);
		EmitNewline(p);
		for (i = 0; i < HR_WIDTH; i++) EmitChar(p, '-');
		EmitNewline(p);
	} else if (TagEquals(tag, tagNameLen, "li")) {
		FlushText(p);
		EmitNewline(p);
		if (p->listDepth > 1) {
			EmitChar(p, ' ');
			EmitChar(p, ' ');
		}
		EmitChar(p, BULLET_CHAR);
		EmitChar(p, ' ');
	} else if (TagEquals(tag, tagNameLen, "ul") || TagEquals(tag, tagNameLen, "ol")) {
		FlushText(p);
		p->listDepth++;
		EmitNewline(p);
	} else if (TagEquals(tag, tagNameLen, "b") || TagEquals(tag, tagNameLen, "strong")) {
		FlushText(p);
		IncrementDepth(&p->boldDepth);
	} else if (TagEquals(tag, tagNameLen, "i") || TagEquals(tag, tagNameLen, "em") ||
		TagEquals(tag, tagNameLen, "cite")) {
		FlushText(p);
		IncrementDepth(&p->italicDepth);
	} else if (TagEquals(tag, tagNameLen, "u")) {
		FlushText(p);
		IncrementDepth(&p->underlineDepth);
	} else if (TagEquals(tag, tagNameLen, "a")) {
		FlushText(p);
		IncrementDepth(&p->underlineDepth);
		href = ExtractAttributeValue(tag, len, "href");
		if (p->activeLinkHref) free(p->activeLinkHref);
		p->activeLinkHref = href;
		p->activeLinkHasStart = false;
		p->activeLinkStart = CurrentOutputOffset(p);
	} else if (TagEquals(tag, tagNameLen, "pre")) {
		FlushText(p);
		IncrementDepth(&p->preDepth);
		EmitNewline(p);
	} else if (TagEquals(tag, tagNameLen, "code") || TagEquals(tag, tagNameLen, "tt") ||
		TagEquals(tag, tagNameLen, "kbd") || TagEquals(tag, tagNameLen, "samp")) {
		FlushText(p);
		IncrementDepth(&p->monospaceDepth);
	} else if (TagEquals(tag, tagNameLen, "script")) {
		FlushText(p);
		p->inScript = true;
	} else if (TagEquals(tag, tagNameLen, "style")) {
		FlushText(p);
		p->inStyle = true;
	} else if (TagEquals(tag, tagNameLen, "title")) {
		FlushText(p);
		p->inTitle = true;
		p->titleBufLen = 0;
	} else if (TagEquals(tag, tagNameLen, "img")) {
		EmitChar(p, '[');
		EmitChar(p, 'i');
		EmitChar(p, 'm');
		EmitChar(p, 'g');
		EmitChar(p, ']');
	} else if (TagEquals(tag, tagNameLen, "td") || TagEquals(tag, tagNameLen, "th")) {
		EmitChar(p, '\t');
	}
}

/* Process a closing tag */
static void HandleCloseTag(HtmlParser *p, const char *tag, short len)
{
	if (TagEquals(tag, len, "h1")) {
		FlushText(p);
		PopHeading(p, 1);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h2")) {
		FlushText(p);
		PopHeading(p, 2);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h3")) {
		FlushText(p);
		PopHeading(p, 3);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h4")) {
		FlushText(p);
		PopHeading(p, 4);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h5")) {
		FlushText(p);
		PopHeading(p, 5);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h6")) {
		FlushText(p);
		PopHeading(p, 6);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "p") || TagEquals(tag, len, "div")) {
		FlushText(p);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "b") || TagEquals(tag, len, "strong")) {
		FlushText(p);
		DecrementDepth(&p->boldDepth);
	} else if (TagEquals(tag, len, "i") || TagEquals(tag, len, "em") ||
		TagEquals(tag, len, "cite")) {
		FlushText(p);
		DecrementDepth(&p->italicDepth);
	} else if (TagEquals(tag, len, "u")) {
		FlushText(p);
		DecrementDepth(&p->underlineDepth);
	} else if (TagEquals(tag, len, "a")) {
		long linkEnd;

		FlushText(p);
		DecrementDepth(&p->underlineDepth);
		linkEnd = CurrentOutputOffset(p);
		if (p->activeLinkHref) {
			if (!p->activeLinkHasStart) {
				p->activeLinkStart = linkEnd;
			}
			if (!AppendLinkRange(p, p->activeLinkStart, linkEnd,
					p->activeLinkHref)) {
				free(p->activeLinkHref);
			}
			p->activeLinkHref = NULL;
			p->activeLinkHasStart = false;
		}
	} else if (TagEquals(tag, len, "pre")) {
		FlushText(p);
		DecrementDepth(&p->preDepth);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "code") || TagEquals(tag, len, "tt") ||
		TagEquals(tag, len, "kbd") || TagEquals(tag, len, "samp")) {
		FlushText(p);
		DecrementDepth(&p->monospaceDepth);
	} else if (TagEquals(tag, len, "ul") || TagEquals(tag, len, "ol")) {
		FlushText(p);
		if (p->listDepth > 0) p->listDepth--;
		EmitNewline(p);
	} else if (TagEquals(tag, len, "script")) {
		p->inScript = false;
	} else if (TagEquals(tag, len, "style")) {
		p->inStyle = false;
	} else if (TagEquals(tag, len, "title")) {
		p->inTitle = false;
		if (p->titleBufLen > 0 && p->window) {
			Str255 pTitle;
			short tLen = p->titleBufLen;
			if (tLen > 255) tLen = 255;
			pTitle[0] = tLen;
			memcpy(pTitle + 1, p->titleBuf, tLen);
			SetWTitle(p->window, pTitle);
		}
	} else if (TagEquals(tag, len, "tr")) {
		FlushText(p);
		EmitNewline(p);
	}
}

void HtmlParserInit(HtmlParser *p, TEHandle te, WindowPtr win)
{
	memset(p, 0, sizeof(HtmlParser));
	p->outputTE = te;
	p->window = win;
	p->state = PS_TEXT;
	p->lastWasSpace = true;  /* suppress leading space */
}

void HtmlParserFeed(HtmlParser *p, const char *data, short len)
{
	short i;
	for (i = 0; i < len; i++) {
		char c = data[i];

		switch (p->state) {
		case PS_TEXT:
			if (p->inScript || p->inStyle) {
				if (c == '<') {
					p->state = PS_TAG;
					p->tagBufLen = 0;
					p->isClosingTag = false;
					p->commentDashes = 0;
				}
			} else if (c == '<') {
				p->state = PS_TAG;
				p->tagBufLen = 0;
				p->isClosingTag = false;
				p->commentDashes = 0;
			} else if (c == '&') {
				p->state = PS_ENTITY;
				p->entityBufLen = 0;
			} else {
				EmitChar(p, c);
			}
			break;

		case PS_TAG:
			if ((p->inScript || p->inStyle) && c != '>') {
				if (p->tagBufLen == 0 && c == '/') {
					p->isClosingTag = true;
				} else if (p->tagBufLen < (short)(sizeof(p->tagBuf) - 1)) {
					p->tagBuf[p->tagBufLen++] = c;
				}
			} else if ((p->inScript || p->inStyle) && c == '>') {
				short tagNameLen = TagNameLength(p->tagBuf, p->tagBufLen);

				if (p->isClosingTag) {
					if (p->inScript && TagEquals(p->tagBuf, tagNameLen, "script")) {
						p->inScript = false;
					} else if (p->inStyle && TagEquals(p->tagBuf, tagNameLen, "style")) {
						p->inStyle = false;
					}
				}
				p->state = PS_TEXT;
			} else if (p->tagBufLen == 0 && c == '/') {
				p->isClosingTag = true;
			} else if (p->tagBufLen == 0 && c == '!') {
				/* Could be comment: <!-- */
				p->commentDashes = 0;
				p->tagBuf[p->tagBufLen++] = '!';
			} else if (p->tagBuf[0] == '!' && p->tagBufLen <= 3) {
				/* Detecting <!-- comment --> */
				if (c == '-') {
					p->commentDashes++;
					if (p->commentDashes >= 2) {
						p->state = PS_COMMENT;
						p->commentDashes = 0;
						break;
					}
				}
				/* DOCTYPE or other <! declaration -- skip to > */
				if (p->tagBufLen < (short)(sizeof(p->tagBuf) - 1)) {
					p->tagBuf[p->tagBufLen++] = c;
				}
				if (c == '>') {
					p->state = PS_TEXT;
				}
			} else if (c == '>') {
				/* Tag complete */
				if (p->isClosingTag) {
					HandleCloseTag(p, p->tagBuf, p->tagBufLen);
				} else {
					HandleOpenTag(p, p->tagBuf, p->tagBufLen);
				}
				p->state = PS_TEXT;
			} else {
				if (p->tagBufLen < (short)(sizeof(p->tagBuf) - 1)) {
					p->tagBuf[p->tagBufLen++] = c;
				}
			}
			break;

		case PS_COMMENT:
			/* Look for --> */
			if (c == '-') {
				p->commentDashes++;
			} else if (c == '>' && p->commentDashes >= 2) {
				p->state = PS_TEXT;
				p->commentDashes = 0;
			} else {
				p->commentDashes = 0;
			}
			break;

		case PS_ENTITY:
			if (c == ';') {
				HandleEntity(p);
				p->state = PS_TEXT;
			} else if (p->entityBufLen < (short)(sizeof(p->entityBuf) - 1)) {
				p->entityBuf[p->entityBufLen++] = c;
			} else {
				/* Entity too long, emit as literal */
				EmitChar(p, '&');
				EmitString(p, p->entityBuf, p->entityBufLen);
				EmitChar(p, c);
				p->state = PS_TEXT;
			}
			break;
		}
	}
}

void HtmlParserEnd(HtmlParser *p)
{
	/* Flush any remaining text */
	FlushText(p);
	if (p->activeLinkHref) {
		long linkEnd = CurrentOutputOffset(p);
		char *href = p->activeLinkHref;

		p->activeLinkHref = NULL;
		if (!p->activeLinkHasStart) {
			p->activeLinkStart = linkEnd;
		}
		if (!AppendLinkRange(p, p->activeLinkStart, linkEnd, href)) {
			free(href);
		}
		p->activeLinkHasStart = false;
	}
}

void HtmlParserDispose(HtmlParser *p)
{
	short i;

	if (!p) return;

	for (i = 0; i < p->linkCount; i++) {
		free(p->links[i].href);
	}
	free(p->links);
	p->links = NULL;
	p->linkCount = 0;
	p->linkCapacity = 0;

	if (p->activeLinkHref) {
		free(p->activeLinkHref);
		p->activeLinkHref = NULL;
	}
}

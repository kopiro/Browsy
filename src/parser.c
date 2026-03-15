#include <string.h>
#include <ctype.h>
#include <Memory.h>
#include <Fonts.h>
#include <TextEdit.h>
#include "parser.h"
#include "utils.h"

#define MAX_INSERT 30000
#define HR_WIDTH 40
#define BULLET_CHAR '\245'

/* Forward declarations */
static void FlushText(HtmlParser *p);
static void EmitChar(HtmlParser *p, char c);
static void EmitString(HtmlParser *p, const char *s, short len);
static void EmitNewline(HtmlParser *p);
static void ApplyStyle(HtmlParser *p);
static void HandleOpenTag(HtmlParser *p, const char *tag, short len);
static void HandleCloseTag(HtmlParser *p, const char *tag, short len);
static void HandleEntity(HtmlParser *p);
static Boolean TagEquals(const char *tagBuf, short tagLen, const char *name);

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

/* Flush the text buffer into TextEdit with current style */
static void FlushText(HtmlParser *p)
{
	if (p->textBufLen == 0) return;
	if (p->totalInserted >= MAX_INSERT) return;

	/* Truncate if we'd exceed the limit */
	if (p->totalInserted + p->textBufLen > MAX_INSERT) {
		p->textBufLen = MAX_INSERT - p->totalInserted;
	}

	ApplyStyle(p);
	TESetSelect(32767, 32767, p->outputTE);
	TEInsert(p->textBuf, p->textBufLen, p->outputTE);
	p->totalInserted += p->textBufLen;
	p->textBufLen = 0;

	if (p->totalInserted >= MAX_INSERT) {
		static char trunc[] = "\r[Page truncated]";
		TESetSelect(32767, 32767, p->outputTE);
		TEInsert(trunc, sizeof(trunc) - 1, p->outputTE);
	}
}

/* Apply current style state to TextEdit */
static void ApplyStyle(HtmlParser *p)
{
	TextStyle ts;
	short mode = doFont | doFace | doSize;

	memset(&ts, 0, sizeof(ts));

	if (p->isPre) {
		ts.tsFont = kFontIDMonaco;
		ts.tsSize = 9;
	} else if (p->headingLevel > 0) {
		ts.tsFont = kFontIDGeneva;
		switch (p->headingLevel) {
			case 1: ts.tsSize = 18; break;
			case 2: ts.tsSize = 14; break;
			case 3: ts.tsSize = 12; break;
			case 4: ts.tsSize = 11; break;
			case 5: ts.tsSize = 10; break;
			default: ts.tsSize = 9; break;
		}
	} else {
		ts.tsFont = kFontIDGeneva;
		ts.tsSize = 9;
	}

	ts.tsFace = 0;
	if (p->isBold || p->headingLevel > 0) ts.tsFace |= bold;
	if (p->isItalic) ts.tsFace |= italic;
	if (p->isUnderline) ts.tsFace |= underline;

	TESetSelect(32767, 32767, p->outputTE);
	TESetStyle(mode, &ts, false, p->outputTE);
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

	if (!p->isPre) {
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
	/* Strip trailing '/' for self-closing tags */
	if (len > 0 && tag[len - 1] == '/') len--;

	/* Block elements emit newlines */
	if (TagEquals(tag, len, "p") ||
		TagEquals(tag, len, "div") ||
		TagEquals(tag, len, "br") ||
		TagEquals(tag, len, "tr")) {
		FlushText(p);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "h1")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 1;
	} else if (TagEquals(tag, len, "h2")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 2;
	} else if (TagEquals(tag, len, "h3")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 3;
	} else if (TagEquals(tag, len, "h4")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 4;
	} else if (TagEquals(tag, len, "h5")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 5;
	} else if (TagEquals(tag, len, "h6")) {
		FlushText(p);
		EmitNewline(p);
		p->headingLevel = 6;
	} else if (TagEquals(tag, len, "hr")) {
		short i;
		FlushText(p);
		EmitNewline(p);
		for (i = 0; i < HR_WIDTH; i++) EmitChar(p, '-');
		EmitNewline(p);
	} else if (TagEquals(tag, len, "li")) {
		FlushText(p);
		EmitNewline(p);
		if (p->listDepth > 1) {
			EmitChar(p, ' ');
			EmitChar(p, ' ');
		}
		EmitChar(p, BULLET_CHAR);
		EmitChar(p, ' ');
	} else if (TagEquals(tag, len, "ul") || TagEquals(tag, len, "ol")) {
		FlushText(p);
		p->listDepth++;
		EmitNewline(p);
	} else if (TagEquals(tag, len, "b") || TagEquals(tag, len, "strong")) {
		FlushText(p);
		p->isBold = true;
	} else if (TagEquals(tag, len, "i") || TagEquals(tag, len, "em")) {
		FlushText(p);
		p->isItalic = true;
	} else if (TagEquals(tag, len, "u") || TagEquals(tag, len, "a")) {
		FlushText(p);
		p->isUnderline = true;
	} else if (TagEquals(tag, len, "pre") || TagEquals(tag, len, "code")) {
		FlushText(p);
		p->isPre = true;
		if (TagEquals(tag, len, "pre")) EmitNewline(p);
	} else if (TagEquals(tag, len, "script")) {
		FlushText(p);
		p->inScript = true;
	} else if (TagEquals(tag, len, "style")) {
		FlushText(p);
		p->inStyle = true;
	} else if (TagEquals(tag, len, "title")) {
		FlushText(p);
		p->inTitle = true;
		p->titleBufLen = 0;
	} else if (TagEquals(tag, len, "img")) {
		EmitChar(p, '[');
		EmitChar(p, 'i');
		EmitChar(p, 'm');
		EmitChar(p, 'g');
		EmitChar(p, ']');
	} else if (TagEquals(tag, len, "td") || TagEquals(tag, len, "th")) {
		EmitChar(p, '\t');
	}
}

/* Process a closing tag */
static void HandleCloseTag(HtmlParser *p, const char *tag, short len)
{
	if (TagEquals(tag, len, "h1") || TagEquals(tag, len, "h2") ||
		TagEquals(tag, len, "h3") || TagEquals(tag, len, "h4") ||
		TagEquals(tag, len, "h5") || TagEquals(tag, len, "h6")) {
		FlushText(p);
		p->headingLevel = 0;
		EmitNewline(p);
	} else if (TagEquals(tag, len, "p") || TagEquals(tag, len, "div")) {
		FlushText(p);
		EmitNewline(p);
	} else if (TagEquals(tag, len, "b") || TagEquals(tag, len, "strong")) {
		FlushText(p);
		p->isBold = false;
	} else if (TagEquals(tag, len, "i") || TagEquals(tag, len, "em")) {
		FlushText(p);
		p->isItalic = false;
	} else if (TagEquals(tag, len, "u") || TagEquals(tag, len, "a")) {
		FlushText(p);
		p->isUnderline = false;
	} else if (TagEquals(tag, len, "pre") || TagEquals(tag, len, "code")) {
		FlushText(p);
		p->isPre = false;
		if (TagEquals(tag, len, "pre")) EmitNewline(p);
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
			if (c == '<') {
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
			if (p->tagBufLen == 0 && c == '/') {
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
			} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				/* Space after tag name = start of attributes. Skip until > */
				/* But only if we have a tag name already */
				if (p->tagBufLen > 0) {
					/* Process the tag name we have, then skip to '>' */
					/* We stay in PS_TAG but stop accumulating */
				}
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
}

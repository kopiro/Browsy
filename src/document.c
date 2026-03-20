#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <Memory.h>
#include "document.h"
#include "window.h"
#include "utils.h"
#include "tokenizer.h"

Node *NewNode() {
	return (Node *)calloc(1, sizeof(Node));
}

void DisposeNode(Node *node) {
	free(node);
}

void DisposeNodes(Node *node) {
	Node *curr;
	for (curr = node->firstChild; curr; curr = curr->nextSibling) {
		DisposeNodes(curr);
	}
	DisposeNode(node);
}

DOMDocument *NewDOMDocument() {
	Handle h = NewHandle(sizeof(DOMDocument));
	DOMDocument *doc;
	if (!h) return NULL;
	MoveHHi(h);
	HLock(h);
	doc = (DOMDocument *) *h;
	doc->data = NULL;
	doc->rootNode = NewNode();
	doc->tokenizer = NewTokenizer();
	doc->parserInited = false;
	return doc;
}

void DisposeDOMDocument(DOMDocument *doc) {
	Handle h;
	DisposeNodes(doc->rootNode);
	DisposeTokenizer(doc->tokenizer);
	HtmlParserDispose(&doc->parser);
	h = RecoverHandle((Ptr)doc);
	if (h) {
		HUnlock(h);
		DisposeHandle(h);
	}
}

void DOMDocumentInitParser(DOMDocument *doc, TEHandle te, WindowPtr win) {
	HtmlParserInit(&doc->parser, te, win);
	doc->parserInited = true;
}

void DOMDocumentParseAppend(DOMDocument *doc, Ptr data, long bytes) {
	if (doc->parserInited) {
		HtmlParserFeed(&doc->parser, data, (short)bytes);
	}
}

void DOMDocumentFinishParse(DOMDocument *doc) {
	if (doc->parserInited) {
		HtmlParserEnd(&doc->parser);
	}
}

const char *DOMDocumentGetLinkAtOffset(DOMDocument *doc, long offset)
{
	short i;

	if (!doc) return NULL;

	for (i = 0; i < doc->parser.linkCount; i++) {
		HtmlLinkRange *link = &doc->parser.links[i];
		if (offset >= link->startOffset && offset < link->endOffset) {
			return link->href;
		}
	}

	return NULL;
}

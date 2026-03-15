#ifndef _DOCUMENT_H
#define _DOCUMENT_H

#include <TextEdit.h>
#include <MacWindows.h>
#include "tokenizer.h"
#include "parser.h"

typedef struct Node {
	struct Node *parentNode;
	struct Node *firstChild;
	struct Node *nextSibling;
} Node;

typedef struct {
	void *data;
	Node *rootNode;
	Tokenizer *tokenizer;
	HtmlParser parser;
	Boolean parserInited;
} DOMDocument;

Node *NewNode();
void DisposeNode(Node *node);
void DisposeNodes(Node *node);

DOMDocument *NewDOMDocument();
void DisposeDOMDocument(DOMDocument *doc);
void DOMDocumentParseAppend(DOMDocument *doc, Ptr data, long bytes);
void DOMDocumentInitParser(DOMDocument *doc, TEHandle te, WindowPtr win);
void DOMDocumentFinishParse(DOMDocument *doc);

#endif

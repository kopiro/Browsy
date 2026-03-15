#include <MacTypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "Browsy.h"
#include "stream.h"
#include "tcpstream.h"
#include "utils.h"
#include "uri.h"
#include "http_parser.h"
#include "uri/http.h"

#define HTTP_UA "Browsy/" BROWSY_VERSION " (Macintosh; N; 68K)"
// "Lynx/2.8 (compatible; Browsy/" VERSION " (Macintosh; N; 68K)"

#define MAX_REDIRECTS 5

struct HTTPURIData {
	URI *uri;
	Stream *tcpStream;
	http_parser parser;
	short err;
	short port;
	Boolean messageComplete;
	Boolean shouldRedirect;
	Boolean isLocationHeader;
	short redirectCount;
	char *host;
	char *path;
	char *redirectLocation;
};

void *HTTPProviderInit(URI *uri, char *uriStr);
void HTTPProviderClose(URI *uri, void *providerData);
void HTTPProviderRequest(URI *uri, void *providerData, HTTPMethod *method,
		Stream *postData);

struct URIProvider *httpURIProvider = &(URIProvider) {
	.init = HTTPProviderInit,
	.request = HTTPProviderRequest,
	.close = HTTPProviderClose
};

int HTTPOnMessageBegin(http_parser *parser);
int HTTPOnStatus(http_parser *parser, const char *at, size_t len);
int HTTPOnHeaderField(http_parser *parser, const char *at, size_t len);
int HTTPOnHeaderValue(http_parser *parser, const char *at, size_t len);
int HTTPOnHeadersComplete(http_parser *parser);
int HTTPOnBody(http_parser *parser, const char *at, size_t len);
int HTTPOnMessageComplete(http_parser *parser);

http_parser_settings parserSettings = {
	.on_message_begin		= HTTPOnMessageBegin,
	.on_status				= HTTPOnStatus,
	.on_header_field		= HTTPOnHeaderField,
	.on_header_value		= HTTPOnHeaderValue,
	.on_headers_complete	= HTTPOnHeadersComplete,
	.on_body				= HTTPOnBody,
	.on_message_complete	= HTTPOnMessageComplete
};

void TCPOnOpen(void *consumerData);
void TCPOnData(void *consumerData, char *data, short len);
void TCPOnError(void *consumerData, short err);
void TCPOnClose(void *consumerData);
void TCPOnEnd(void *consumerData);

StreamConsumer tcpConsumer = {
	.on_open = TCPOnOpen,
	.on_data = TCPOnData,
	.on_error = TCPOnError,
	.on_close = TCPOnClose,
	.on_end = TCPOnEnd,
};

// Case-insensitive header field name comparison.
// `name` must be lowercase.
static Boolean HeaderFieldIs(const char *at, size_t len, const char *name)
{
	size_t i;
	if (len != strlen(name)) return false;
	for (i = 0; i < len; i++) {
		if (tolower((unsigned char)at[i]) != name[i]) return false;
	}
	return true;
}

// Follow an HTTP redirect by closing the current TCP stream
// and opening a new connection to the redirect target.
static void HTTPFollowRedirect(struct HTTPURIData *data)
{
	char *location = data->redirectLocation;
	struct http_parser_url urlParser;
	size_t hostLen, pathLen, queryLen;
	char *newPath = NULL;
	Stream *newStream;

	data->redirectLocation = NULL;
	data->shouldRedirect = false;

	// Close current connection while messageComplete is still true,
	// so TCPOnClose won't fire URIClosed
	StreamClose(data->tcpStream);

	// Now safe to reset state for new connection
	data->messageComplete = false;
	data->redirectCount++;
	data->isLocationHeader = false;
	data->err = 0;

	if (strncmp(location, "http://", 7) == 0) {
		// Absolute URL
		char *newHost;
		short newPort;

		if (http_parser_parse_url(location, strlen(location), false,
				&urlParser)) {
			goto fail;
		}
		if (!(urlParser.field_set & (1 << UF_HOST))) {
			goto fail;
		}

		hostLen = urlParser.field_data[UF_HOST].len;
		newHost = malloc(hostLen + 1);
		if (!newHost) goto fail;
		memcpy(newHost, location + urlParser.field_data[UF_HOST].off, hostLen);
		newHost[hostLen] = '\0';

		if (urlParser.field_set & (1 << UF_PATH)) {
			pathLen = urlParser.field_data[UF_PATH].len;
		} else {
			pathLen = 0;
		}

		queryLen = 0;
		if (urlParser.field_set & (1 << UF_QUERY)) {
			queryLen = urlParser.field_data[UF_QUERY].len;
		}

		newPath = malloc((pathLen > 0 ? pathLen : 1)
				+ (queryLen > 0 ? queryLen + 1 : 0) + 1);
		if (!newPath) { free(newHost); goto fail; }

		if (pathLen == 0) {
			newPath[0] = '/';
			pathLen = 1;
		} else {
			memcpy(newPath,
					location + urlParser.field_data[UF_PATH].off, pathLen);
		}

		if (queryLen > 0) {
			newPath[pathLen] = '?';
			memcpy(newPath + pathLen + 1,
					location + urlParser.field_data[UF_QUERY].off, queryLen);
			newPath[pathLen + 1 + queryLen] = '\0';
		} else {
			newPath[pathLen] = '\0';
		}

		newPort = urlParser.port ? urlParser.port : 80;

		free(data->host);
		data->host = newHost;
		free(data->path);
		data->path = newPath;
		data->port = newPort;

	} else if (location[0] == '/') {
		// Absolute path - keep current host and port
		size_t locLen = strlen(location);
		newPath = malloc(locLen + 1);
		if (!newPath) goto fail;
		memcpy(newPath, location, locLen + 1);
		free(data->path);
		data->path = newPath;

	} else {
		goto fail;
	}

	free(location);

	newStream = NewStream();
	if (!newStream) {
		URIClosed(data->uri, -1);
		return;
	}

	data->tcpStream = newStream;
	http_parser_init(&data->parser, HTTP_RESPONSE);
	data->parser.data = data;

	StreamConsume(newStream, &tcpConsumer, data);
	ProvideTCPActiveStream(newStream, data->host, data->port);
	StreamOpen(newStream);
	return;

fail:
	free(location);
	URIClosed(data->uri, -1);
}

// create and return the provider data
void *HTTPProviderInit(URI *uri, char *uriStr)
{
	struct HTTPURIData *data;
	Stream *tcpStream;
	struct http_parser_url urlParser;
	size_t hostLen, pathLen;

	if (http_parser_parse_url(uriStr, strlen(uriStr), false, &urlParser)) {
		alertf("Error parsing URL %s", uriStr);
		return NULL;
	}

	if (!(urlParser.field_set & (1 << UF_HOST))) {
		alertf("URL missing host");
		return NULL;
	}
	hostLen = urlParser.field_data[UF_HOST].len;

	if (!(urlParser.field_set & (1 << UF_PATH))) {
		pathLen = 0;
	} else {
		pathLen = urlParser.field_data[UF_PATH].len;
	}

	data = malloc(sizeof(struct HTTPURIData));
	if (!data) {
		return NULL;
	}

	data->host = malloc(hostLen + 1);
	/* +2 to allow for "/" default path */
	data->path = malloc((pathLen > 0 ? pathLen : 1) + 1);
	if (!data->host || !data->path) {
		if (data->host) free(data->host);
		if (data->path) free(data->path);
		free(data);
		return NULL;
	}

	data->port = urlParser.port ? urlParser.port : 80;
	memcpy(data->host, uriStr + urlParser.field_data[UF_HOST].off, hostLen);
	data->host[hostLen] = '\0';

	if (pathLen == 0) {
		data->path[0] = '/';
		data->path[1] = '\0';
	} else {
		memcpy(data->path, uriStr + urlParser.field_data[UF_PATH].off, pathLen);
		data->path[pathLen] = '\0';
	}

	tcpStream = NewStream();
	if (!tcpStream) {
		free(data->host);
		free(data->path);
		free(data);
		return NULL;
	}

	data->uri = uri;
	data->tcpStream = tcpStream;
	data->err = 0;
	data->messageComplete = false;
	data->shouldRedirect = false;
	data->isLocationHeader = false;
	data->redirectCount = 0;
	data->redirectLocation = NULL;
	http_parser_init(&data->parser, HTTP_RESPONSE);
	data->parser.data = data;

	StreamConsume(tcpStream, &tcpConsumer, data);
	ProvideTCPActiveStream(tcpStream, data->host, data->port);
	return data;
}

void HTTPProviderClose(URI *uri, void *providerData)
{
	struct HTTPURIData *data = (struct HTTPURIData *)providerData;
	StreamClose(data->tcpStream);
	free(data->host);
	free(data->path);
	if (data->redirectLocation) free(data->redirectLocation);
	free(data);
}

void HTTPProviderRequest(URI *uri, void *providerData, HTTPMethod *method,
		Stream *postData)
{
	struct HTTPURIData *data = (struct HTTPURIData *)providerData;

	// ignore POST data
	(void)postData;

	if (method->type != httpGET) {
		// only GET is supported
		URIClosed(uri, uriBadMethodErr);
		return;
	}

	StreamOpen(data->tcpStream);
}

// TCP connection opened
void TCPOnOpen(void *consumerData)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)consumerData;
	char reqMsg[256];
	short reqLen;

	// Build the HTTP request
	reqLen = snprintf(reqMsg, sizeof reqMsg,
			"GET %s HTTP/1.1\r\n"
			"User-Agent: " HTTP_UA "\r\n"
			"Host: %s\r\n"
			"Connection: Close\r\n"
			"\r\n",
			hData->path, hData->host);
	if (reqLen >= sizeof reqMsg) {
		// request was truncated
		alertf("request truncated");
		StreamClose(hData->tcpStream);
		URIClosed(hData->uri, -2);
		return;
	}

	//alertf("sending http request (%hu): %s", reqLen, reqMsg);

	// Send the request
	StreamWrite(hData->tcpStream, reqMsg, reqLen);
}

void TCPOnData(void *consumerData, char *data, short len)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)consumerData;
	size_t nparsed;

	nparsed = http_parser_execute(&hData->parser, &parserSettings, data, len);

	// After a redirect, the parser was re-initialized for the new connection.
	// Don't check errors against the old parse state.
	if (hData->messageComplete) return;

	if (nparsed != len && hData->parser.http_errno != HPE_OK) {
		StreamClose(hData->tcpStream);
		URIClosed(hData->uri, -1);
	}
}

void TCPOnError(void *consumerData, short err)
{
	struct HTTPURIData *data = (struct HTTPURIData *)consumerData;
	data->err = err;
	if (err == tcpMissingDriverErr) {
		alertf("Missing MacTCP driver");
		return;
	}
	alertf("tcp stream error: %ld", err);
}

void TCPOnClose(void *consumerData)
{
	struct HTTPURIData *data = (struct HTTPURIData *)consumerData;
	http_parser_execute(&data->parser, &parserSettings, "", 0);
	if (!data->messageComplete) {
		URIClosed(data->uri, data->err);
	}
}

void TCPOnEnd(void *consumerData)
{
}

// HTTP parser callbacks
//
// Consumer notifications (URIMessageBegin, URIGotStatus, URIHeadersComplete)
// are deferred until HTTPOnHeadersComplete so that redirect responses
// are transparent to the consumer.

int HTTPOnMessageBegin(http_parser *parser)
{
	return 0;
}

int HTTPOnStatus(http_parser *parser, const char *at, size_t len)
{
	return 0;
}

int HTTPOnHeaderField(http_parser *parser, const char *at, size_t len)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)parser->data;
	hData->isLocationHeader = HeaderFieldIs(at, len, "location");
	return 0;
}

int HTTPOnHeaderValue(http_parser *parser, const char *at, size_t len)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)parser->data;
	if (hData->isLocationHeader) {
		if (hData->redirectLocation) free(hData->redirectLocation);
		hData->redirectLocation = malloc(len + 1);
		if (hData->redirectLocation) {
			memcpy(hData->redirectLocation, at, len);
			hData->redirectLocation[len] = '\0';
		}
	}
	return 0;
}

int HTTPOnHeadersComplete(http_parser *parser)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)parser->data;
	short status = parser->status_code;

	// Check for redirect
	if ((status == 301 || status == 302 || status == 303 ||
	     status == 307 || status == 308) &&
	    hData->redirectLocation &&
	    hData->redirectCount < MAX_REDIRECTS) {
		hData->shouldRedirect = true;
		return 0;
	}

	// Not a redirect - notify consumer
	URIMessageBegin(hData->uri);
	URIGotStatus(hData->uri, status);
	URIHeadersComplete(hData->uri);
	return 0;
}

int HTTPOnBody(http_parser *parser, const char *at, size_t len)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)parser->data;
	if (hData->shouldRedirect) return 0;
	URIGotData(hData->uri, (char *)at, len);
	return 0;
}

int HTTPOnMessageComplete(http_parser *parser)
{
	struct HTTPURIData *hData = (struct HTTPURIData *)parser->data;
	hData->messageComplete = true;

	if (hData->shouldRedirect) {
		HTTPFollowRedirect(hData);
		return 0;
	}

	URIClosed(hData->uri, hData->err);
	return 0;
}

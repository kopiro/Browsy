#include <MacTypes.h>
#include <Devices.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <OSUtils.h>
#include "Browsy.h"
#include "AddressXlation.h"
#include "MacTCP.h"
#include "stream.h"
#include "utils.h"
#include "uri.h"
#include "http_parser.h"
#include "http_response.h"
#include "uri/http.h"

#define HTTP_UA "Browsy/" BROWSY_VERSION " (Macintosh; N; 68K)"
#define HTTP_CREATE_BUF_SIZE 4096
#define HTTP_RECV_BUF_SIZE 1024
#define HTTP_HEADER_BUF_SIZE 8192

struct HTTPURIData {
	URI *uri;
	short port;
	char *host;
	char *path;
};

static short gTCPRefNum;
static Boolean gResolverOpen;

struct HTTPResolveState {
	Boolean done;
	struct hostInfo *hostInfo;
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

static Boolean ParseDottedQuad(const char *host, ip_addr *addr)
{
	unsigned long octets[4];
	unsigned long value = 0;
	short octetIndex = 0;
	const unsigned char *p = (const unsigned char *)host;

	while (*p) {
		if (*p < '0' || *p > '9') {
			if (*p != '.' || octetIndex >= 3) return false;
			octets[octetIndex++] = value;
			value = 0;
			p++;
			continue;
		}
		value = (value * 10) + (*p - '0');
		if (value > 255) return false;
		p++;
	}

	if (octetIndex != 3) return false;
	octets[octetIndex] = value;
	*addr = ((octets[0] << 24) | (octets[1] << 16)
			| (octets[2] << 8) | octets[3]);
	return true;
}

static short EnsureTCPDriver(void)
{
	if (gTCPRefNum) return noErr;
	return OpenDriver("\p.IPP", &gTCPRefNum);
}

static pascal void HTTPResolveProc(struct hostInfo *hostInfo, char *userData)
{
	struct HTTPResolveState *state = (struct HTTPResolveState *)userData;
	(void)hostInfo;
	state->done = true;
}

static short ResolveHost(const char *host, ip_addr *addr)
{
	struct hostInfo hostInfo;
	struct HTTPResolveState state;
	unsigned long deadline;
	OSErr err;

	if (ParseDottedQuad(host, addr)) {
		return noErr;
	}

	if (!gResolverOpen) {
		err = OpenResolver(NULL);
		if (err != noErr) return err;
		gResolverOpen = true;
	}

	memset(&hostInfo, 0, sizeof(hostInfo));
	memset(&state, 0, sizeof(state));
	state.hostInfo = &hostInfo;

	err = StrToAddr((char *)host, &hostInfo, HTTPResolveProc, (Ptr)&state);
	if (err != noErr && err != cacheFault) {
		return err;
	}

	if (err == cacheFault && !state.done) {
		deadline = TickCount() + 60 * 15;
		while (!state.done && TickCount() < deadline) {
			SystemTask();
		}
	}

	if (!state.done) {
		return commandTimeout;
	}
	if (hostInfo.rtnCode != noErr && hostInfo.rtnCode != cacheFault) {
		return (short)hostInfo.rtnCode;
	}
	if (hostInfo.addr[0] == 0) {
		return noNameServer;
	}

	*addr = hostInfo.addr[0];
	return noErr;
}

static void ReleaseTCPStream(StreamPtr tcpStream)
{
	TCPiopb pb;

	if (!tcpStream) return;

	memset(&pb, 0, sizeof(pb));
	pb.csCode = TCPRelease;
	pb.tcpStream = tcpStream;
	pb.ioCRefNum = gTCPRefNum;
	PBControlSync((ParmBlkPtr)&pb);
}

static short HTTPBlockingFetch(struct HTTPURIData *data)
{
	TCPiopb pb;
	StreamPtr tcpStream = 0;
	ip_addr remoteAddr;
	char createBuf[HTTP_CREATE_BUF_SIZE];
	char recvBuf[HTTP_RECV_BUF_SIZE];
	char reqMsg[512];
	wdsEntry wds[2];
	short reqLen;
	char *headerBuf = NULL;
	long headerLen = 0;
	long contentLength = -1;
	long bodyBytesRead = 0;
	Boolean headersComplete = false;
	short statusCode = 0;
	short err;

	err = ResolveHost(data->host, &remoteAddr);
	if (err != noErr) {
		alertf("DNS failed %s: %hd", data->host, err);
		return err;
	}

	err = EnsureTCPDriver();
	if (err != noErr) {
		alertf("OpenDriver .IPP failed: %hd", err);
		return err;
	}

	reqLen = snprintf(reqMsg, sizeof reqMsg,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"User-Agent: " HTTP_UA "\r\n"
			"\r\n",
			data->path, data->host);
	if (reqLen <= 0 || reqLen >= sizeof reqMsg) {
		alertf("request truncated");
		return -1;
	}

	headerBuf = malloc(HTTP_HEADER_BUF_SIZE);
	if (!headerBuf) {
		return memFullErr;
	}

	memset(&pb, 0, sizeof(pb));
	pb.csCode = TCPCreate;
	pb.ioCRefNum = gTCPRefNum;
	pb.csParam.create.rcvBuff = createBuf;
	pb.csParam.create.rcvBuffLen = sizeof(createBuf);
	pb.csParam.create.notifyProc = NULL;
	pb.csParam.create.userDataPtr = NULL;
	PBControlSync((ParmBlkPtr)&pb);
	if (pb.ioResult != noErr) {
		err = pb.ioResult;
		goto done;
	}
	tcpStream = pb.tcpStream;

	memset(&pb, 0, sizeof(pb));
	pb.csCode = TCPActiveOpen;
	pb.ioCRefNum = gTCPRefNum;
	pb.tcpStream = tcpStream;
	pb.csParam.open.commandTimeoutValue = 30;
	pb.csParam.open.remoteHost = remoteAddr;
	pb.csParam.open.remotePort = data->port;
	PBControlSync((ParmBlkPtr)&pb);
	if (pb.ioResult != noErr) {
		err = pb.ioResult;
		goto done;
	}

	memset(&pb, 0, sizeof(pb));
	pb.csCode = TCPSend;
	pb.ioCRefNum = gTCPRefNum;
	pb.tcpStream = tcpStream;
	pb.csParam.send.pushFlag = true;
	wds[0].length = reqLen;
	wds[0].ptr = reqMsg;
	wds[1].length = 0;
	wds[1].ptr = NULL;
	pb.csParam.send.wdsPtr = (Ptr)wds;
	pb.csParam.send.sendLength = reqLen;
	PBControlSync((ParmBlkPtr)&pb);
	if (pb.ioResult != noErr) {
		err = pb.ioResult;
		goto done;
	}

	for (;;) {
		short ioResult;
		unsigned short bytesRead;
		long headerEnd;
		Boolean consumeData = true;

		memset(&pb, 0, sizeof(pb));
		pb.csCode = TCPRcv;
		pb.ioCRefNum = gTCPRefNum;
		pb.tcpStream = tcpStream;
		pb.csParam.receive.commandTimeoutValue = 30;
		pb.csParam.receive.rcvBuff = recvBuf;
		pb.csParam.receive.rcvBuffLen = sizeof(recvBuf);
		PBControlSync((ParmBlkPtr)&pb);

		ioResult = pb.ioResult;
		bytesRead = pb.csParam.receive.rcvBuffLen;
		bytesRead = HTTPClipBodyBytes(contentLength, bodyBytesRead, bytesRead);
		if (headersComplete &&
				(ioResult == connectionClosing ||
				 ioResult == connectionTerminated) &&
				bytesRead > 0 &&
				(HTTPLooksLikeResponse(recvBuf, bytesRead) ||
				 HTTPFindHeaderEnd(recvBuf, bytesRead) >= 0)) {
			consumeData = false;
		}

		if (consumeData && bytesRead > 0) {
			if (!headersComplete) {
				if (headerLen + bytesRead > HTTP_HEADER_BUF_SIZE) {
					err = memFullErr;
					goto done;
				}
				memcpy(headerBuf + headerLen, recvBuf, bytesRead);
				headerLen += bytesRead;
				headerEnd = HTTPFindHeaderEnd(headerBuf, headerLen);
				if (headerEnd >= 0) {
					headersComplete = true;
					contentLength = HTTPParseContentLength(headerBuf, headerEnd);
					statusCode = HTTPParseStatusCode(headerBuf, headerEnd);
					if (statusCode == 0) statusCode = 200;
					URIMessageBegin(data->uri);
					URIGotStatus(data->uri, statusCode);
					URIHeadersComplete(data->uri);
					if (headerLen > headerEnd) {
						short initialBodyLen = (short)(headerLen - headerEnd);
						if (contentLength >= 0 &&
								initialBodyLen > contentLength - bodyBytesRead) {
							initialBodyLen = (short)(contentLength - bodyBytesRead);
						}
						if (initialBodyLen > 0) {
							URIGotData(data->uri, headerBuf + headerEnd,
									initialBodyLen);
							bodyBytesRead += initialBodyLen;
						}
					}
				}
			} else {
				URIGotData(data->uri, recvBuf, bytesRead);
				bodyBytesRead += bytesRead;
			}
		}

		if (headersComplete && contentLength >= 0 &&
				bodyBytesRead >= contentLength) {
			err = noErr;
			break;
		}

		if (ioResult == noErr) {
			continue;
		}
		if (ioResult == connectionClosing || ioResult == connectionTerminated) {
			err = noErr;
			break;
		}
		err = ioResult;
		break;
	}

done:
	if (tcpStream) {
		ReleaseTCPStream(tcpStream);
	}
	if (headerBuf) free(headerBuf);
	return err;
}

void *HTTPProviderInit(URI *uri, char *uriStr)
{
	struct HTTPURIData *data;
	struct http_parser_url urlParser;
	size_t hostLen, pathLen, queryLen;

	if (http_parser_parse_url(uriStr, strlen(uriStr), false, &urlParser)) {
		alertf("Error parsing URL %s", uriStr);
		return NULL;
	}
	if (!(urlParser.field_set & (1 << UF_HOST))) {
		alertf("URL missing host");
		return NULL;
	}

	hostLen = urlParser.field_data[UF_HOST].len;
	pathLen = (urlParser.field_set & (1 << UF_PATH))
			? urlParser.field_data[UF_PATH].len : 0;
	queryLen = (urlParser.field_set & (1 << UF_QUERY))
			? urlParser.field_data[UF_QUERY].len : 0;

	data = calloc(1, sizeof(*data));
	if (!data) return NULL;

	data->host = malloc(hostLen + 1);
	data->path = malloc((pathLen > 0 ? pathLen : 1)
			+ (queryLen > 0 ? queryLen + 1 : 0) + 1);
	if (!data->host || !data->path) {
		if (data->host) free(data->host);
		if (data->path) free(data->path);
		free(data);
		return NULL;
	}

	memcpy(data->host, uriStr + urlParser.field_data[UF_HOST].off, hostLen);
	data->host[hostLen] = '\0';

	if (pathLen == 0) {
		data->path[0] = '/';
		pathLen = 1;
	} else {
		memcpy(data->path, uriStr + urlParser.field_data[UF_PATH].off, pathLen);
	}
	if (queryLen > 0) {
		data->path[pathLen] = '?';
		memcpy(data->path + pathLen + 1,
				uriStr + urlParser.field_data[UF_QUERY].off, queryLen);
		data->path[pathLen + 1 + queryLen] = '\0';
	} else {
		data->path[pathLen] = '\0';
	}

	data->uri = uri;
	data->port = urlParser.port ? urlParser.port : 80;
	return data;
}

void HTTPProviderClose(URI *uri, void *providerData)
{
	struct HTTPURIData *data = (struct HTTPURIData *)providerData;
	(void)uri;
	free(data->host);
	free(data->path);
	free(data);
}

void HTTPProviderRequest(URI *uri, void *providerData, HTTPMethod *method,
		Stream *postData)
{
	struct HTTPURIData *data = (struct HTTPURIData *)providerData;
	short err;

	(void)postData;

	if (method->type != httpGET) {
		URIClosed(uri, uriBadMethodErr);
		return;
	}

	err = HTTPBlockingFetch(data);
	URIClosed(uri, err);
}

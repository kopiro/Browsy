#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stdbool.h>

long HTTPFindHeaderEnd(const char *buf, long len);
short HTTPParseStatusCode(const char *headerBuf, long headerLen);
long HTTPParseContentLength(const char *headerBuf, long headerLen);
bool HTTPLooksLikeResponse(const char *buf, long len);
unsigned short HTTPClipBodyBytes(long contentLength, long bodyBytesRead,
		unsigned short bytesRead);

#endif

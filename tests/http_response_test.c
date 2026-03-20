#include <stdio.h>
#include <string.h>
#include "../src/http_response.h"

static const char kHelloResponse[] =
	"HTTP/1.1 200 OK\r\n"
	"Accept-Ranges: bytes\r\n"
	"Content-Length: 287\r\n"
	"Content-Type: text/html\r\n"
	"Date: Fri, 20 Mar 2026 17:05:09 GMT\r\n"
	"Etag: \"69b6cead-11f\"\r\n"
	"Last-Modified: Sun, 15 Mar 2026 15:22:21 GMT\r\n"
	"Server: nginx\r\n"
	"\r\n"
	"<!DOCTYPE html>\n"
	"<html lang=\"en\">\n"
	"<head>\n"
	"  <meta charset=\"utf-8\">\n"
	"  <title>Hello Kopiro</title>\n"
	"  <style>\n"
	"    body {\n"
	"      margin: 24px;\n"
	"      background: #fff;\n"
	"      color: #000;\n"
	"      font-family: serif;\n"
	"    }\n"
	"  </style>\n"
	"</head>\n"
	"<body>\n"
	"  <p>HELLO FROM <b>KOPIRO</b></p>\n"
	"</body>\n"
	"</html>\n";

static int Check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}
	return 0;
}

int main(void)
{
	long responseLen = (long)strlen(kHelloResponse);
	long headerEnd = HTTPFindHeaderEnd(kHelloResponse, responseLen);
	long contentLength;
	const char *body;
	long bodyLen;

	if (Check(HTTPLooksLikeResponse(kHelloResponse, responseLen),
			"response signature should be detected")) return 1;
	if (Check(headerEnd > 0, "header terminator should be found")) return 1;
	if (Check(HTTPParseStatusCode(kHelloResponse, headerEnd) == 200,
			"status code should parse as 200")) return 1;

	contentLength = HTTPParseContentLength(kHelloResponse, headerEnd);
	if (Check(contentLength == 287, "content length should parse as 287")) {
		return 1;
	}

	body = kHelloResponse + headerEnd;
	bodyLen = responseLen - headerEnd;
	if (Check(bodyLen == contentLength,
			"body length should match parsed content length")) return 1;
	if (Check(strncmp(body, "<!DOCTYPE html>", 15) == 0,
			"body should begin at the HTML payload")) return 1;
	if (Check(HTTPClipBodyBytes(contentLength, 0, 1024) == 287,
			"initial body clip should cap to content length")) return 1;
	if (Check(HTTPClipBodyBytes(contentLength, contentLength, 1024) == 0,
			"no bytes should remain after the full body is read")) return 1;

	puts("http_response_test: ok");
	return 0;
}

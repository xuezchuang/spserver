/*
 * Copyright 2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <assert.h>

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "spporting.hpp"

#include "spwin32iocp.hpp"
#include "spiocpserver.hpp"

#include "sphttp.hpp"
#include "sphttpmsg.hpp"
#include "spserver.hpp"

#pragma comment(lib,"ws2_32")
#pragma comment(lib,"mswsock")

//#include <xstring>
#include <fstream>
#include <sstream>

class SP_HttpEchoHandler : public SP_HttpHandler
{
public:
	SP_HttpEchoHandler() {}
	virtual ~SP_HttpEchoHandler() {}

	virtual void handle(SP_HttpRequest* request, SP_HttpResponse* response);
private:
	bool endsWith(const std::string& fullString, const std::string& ending);
	void static_handle(SP_HttpRequest* request, SP_HttpResponse* response);

};

class SP_HttpEchoHandlerFactory : public SP_HttpHandlerFactory
{
public:
	SP_HttpEchoHandlerFactory() {}
	virtual ~SP_HttpEchoHandlerFactory() {}

	virtual SP_HttpHandler* create() const
	{
		return new SP_HttpEchoHandler();
	}
};

int main(int argc, char* argv[])
{
	int port = 8080, maxThreads = 10;
	const char* serverType = "hahs";

	if(0 != sp_initsock()) assert(0);

	SP_IocpServer server("", port, new SP_HttpHandlerAdapterFactory(new SP_HttpEchoHandlerFactory()));

	server.setTimeout(60);
	server.setMaxThreads(maxThreads);
	server.setReqQueueSize(100, "HTTP/1.1 500 Sorry, server is busy now!\r\n");

	server.runForever();

	sp_closelog();

	return 0;
}

void SP_HttpEchoHandler::handle(SP_HttpRequest* request, SP_HttpResponse* response)
{
	if(1)
	{
		static_handle(request, response);
	}
	else
	{
		response->setStatusCode(200);
		response->appendContent("<html><head>"
								"<title>Welcome to simple http</title>"
								"</head><body>");

		char buffer[512] = {0};
		snprintf(buffer, sizeof(buffer), "<p>The requested URI is : %s.</p>", request->getURI());
		response->appendContent(buffer);

		snprintf(buffer, sizeof(buffer), "<p>Client IP is : %s.</p>", request->getClientIP());
		response->appendContent(buffer);

		int i = 0;

		for(i = 0; i < request->getParamCount(); i++)
		{
			snprintf(buffer, sizeof(buffer), "<p>Param - %s = %s<p>", request->getParamName(i), request->getParamValue(i));
			response->appendContent(buffer);
		}

		for(i = 0; i < request->getHeaderCount(); i++)
		{
			snprintf(buffer, sizeof(buffer), "<p>Header - %s: %s<p>", request->getHeaderName(i), request->getHeaderValue(i));
			response->appendContent(buffer);
		}

		if(NULL != request->getContent())
		{
			response->appendContent("<p>");
			response->appendContent(request->getContent(), request->getContentLength());
			response->appendContent("</p>");
		}

		response->appendContent("</body></html>\n");
	}
}

static void* p_open(const char* path)
{
	const char* mode = "rb";
	wchar_t b1[128], b2[10];
	MultiByteToWideChar(CP_UTF8, 0, path, -1, b1, sizeof(b1) / sizeof(b1[0]));
	MultiByteToWideChar(CP_UTF8, 0, mode, -1, b2, sizeof(b2) / sizeof(b2[0]));
	return (void*)_wfopen(b1, b2);
}

static int p_stat(const char* path, size_t* size, time_t* mtime)
{
	struct _stati64 st;
	wchar_t tmp[128];
	MultiByteToWideChar(CP_UTF8, 0, path, -1, tmp, sizeof(tmp) / sizeof(tmp[0]));
	if(_wstati64(tmp, &st) != 0) return 0;
	// If path is a symlink, windows reports 0 in st.st_size.
	// Get a real file size by opening it and jumping to the end
	if(st.st_size == 0 && (st.st_mode & _S_IFREG))
	{
		FILE* fp = _wfopen(tmp, L"rb");
		if(fp != NULL)
		{
			fseek(fp, 0, SEEK_END);
			if(ftell(fp) > 0) st.st_size = ftell(fp);  // Use _ftelli64 on win10+
			fclose(fp);
		}
	}
	if(size) *size = (size_t)st.st_size;
	if(mtime) *mtime = st.st_mtime;
	//return MG_FS_READ | MG_FS_WRITE | (S_ISDIR(st.st_mode) ? MG_FS_DIR : 0);
	return 1;
}

static size_t p_read(void* fp, void* buf, size_t len)
{
	return fread(buf, 1, len, (FILE*)fp);
}

struct mg_str
{
	const char* ptr;  // Pointer to string data
	size_t len;       // String len
};
#define MG_C_STR(a) { (a), sizeof(a) - 1 }
static struct mg_str s_known_types[] =
{
	MG_C_STR("html"), MG_C_STR("text/html; charset=utf-8"),
	MG_C_STR("htm"), MG_C_STR("text/html; charset=utf-8"),
	MG_C_STR("css"), MG_C_STR("text/css; charset=utf-8"),
	MG_C_STR("js"), MG_C_STR("text/javascript; charset=utf-8"),
	MG_C_STR("gif"), MG_C_STR("image/gif"),
	MG_C_STR("png"), MG_C_STR("image/png"),
	MG_C_STR("jpg"), MG_C_STR("image/jpeg"),
	MG_C_STR("jpeg"), MG_C_STR("image/jpeg"),
	MG_C_STR("woff"), MG_C_STR("font/woff"),
	MG_C_STR("ttf"), MG_C_STR("font/ttf"),
	MG_C_STR("svg"), MG_C_STR("image/svg+xml"),
	MG_C_STR("txt"), MG_C_STR("text/plain; charset=utf-8"),
	MG_C_STR("avi"), MG_C_STR("video/x-msvideo"),
	MG_C_STR("csv"), MG_C_STR("text/csv"),
	MG_C_STR("doc"), MG_C_STR("application/msword"),
	MG_C_STR("exe"), MG_C_STR("application/octet-stream"),
	MG_C_STR("gz"), MG_C_STR("application/gzip"),
	MG_C_STR("ico"), MG_C_STR("image/x-icon"),
	MG_C_STR("json"), MG_C_STR("application/json"),
	MG_C_STR("mov"), MG_C_STR("video/quicktime"),
	MG_C_STR("mp3"), MG_C_STR("audio/mpeg"),
	MG_C_STR("mp4"), MG_C_STR("video/mp4"),
	MG_C_STR("mpeg"), MG_C_STR("video/mpeg"),
	MG_C_STR("pdf"), MG_C_STR("application/pdf"),
	MG_C_STR("shtml"), MG_C_STR("text/html; charset=utf-8"),
	MG_C_STR("tgz"), MG_C_STR("application/tar-gz"),
	MG_C_STR("wav"), MG_C_STR("audio/wav"),
	MG_C_STR("webp"), MG_C_STR("image/webp"),
	MG_C_STR("zip"), MG_C_STR("application/zip"),
	MG_C_STR("3gp"), MG_C_STR("video/3gpp"),
	{0, 0},
};

//static const char* mg_http_status_code_str(int status_code)
//{
//	switch(status_code)
//	{
//	case 100: return "Continue";
//	case 101: return "Switching Protocols";
//	case 102: return "Processing";
//	case 200: return "OK";
//	case 201: return "Created";
//	case 202: return "Accepted";
//	case 203: return "Non-authoritative Information";
//	case 204: return "No Content";
//	case 205: return "Reset Content";
//	case 206: return "Partial Content";
//	case 207: return "Multi-Status";
//	case 208: return "Already Reported";
//	case 226: return "IM Used";
//	case 300: return "Multiple Choices";
//	case 301: return "Moved Permanently";
//	case 302: return "Found";
//	case 303: return "See Other";
//	case 304: return "Not Modified";
//	case 305: return "Use Proxy";
//	case 307: return "Temporary Redirect";
//	case 308: return "Permanent Redirect";
//	case 400: return "Bad Request";
//	case 401: return "Unauthorized";
//	case 402: return "Payment Required";
//	case 403: return "Forbidden";
//	case 404: return "Not Found";
//	case 405: return "Method Not Allowed";
//	case 406: return "Not Acceptable";
//	case 407: return "Proxy Authentication Required";
//	case 408: return "Request Timeout";
//	case 409: return "Conflict";
//	case 410: return "Gone";
//	case 411: return "Length Required";
//	case 412: return "Precondition Failed";
//	case 413: return "Payload Too Large";
//	case 414: return "Request-URI Too Long";
//	case 415: return "Unsupported Media Type";
//	case 416: return "Requested Range Not Satisfiable";
//	case 417: return "Expectation Failed";
//	case 418: return "I'm a teapot";
//	case 421: return "Misdirected Request";
//	case 422: return "Unprocessable Entity";
//	case 423: return "Locked";
//	case 424: return "Failed Dependency";
//	case 426: return "Upgrade Required";
//	case 428: return "Precondition Required";
//	case 429: return "Too Many Requests";
//	case 431: return "Request Header Fields Too Large";
//	case 444: return "Connection Closed Without Response";
//	case 451: return "Unavailable For Legal Reasons";
//	case 499: return "Client Closed Request";
//	case 500: return "Internal Server Error";
//	case 501: return "Not Implemented";
//	case 502: return "Bad Gateway";
//	case 503: return "Service Unavailable";
//	case 504: return "Gateway Timeout";
//	case 505: return "HTTP Version Not Supported";
//	case 506: return "Variant Also Negotiates";
//	case 507: return "Insufficient Storage";
//	case 508: return "Loop Detected";
//	case 510: return "Not Extended";
//	case 511: return "Network Authentication Required";
//	case 599: return "Network Connect Timeout Error";
//	default: return "";
//	}
//}

int mg_strcmp(const struct mg_str str1, const struct mg_str str2)
{
	size_t i = 0;
	while(i < str1.len && i < str2.len)
	{
		int c1 = str1.ptr[i];
		int c2 = str2.ptr[i];
		if(c1 < c2) return -1;
		if(c1 > c2) return 1;
		i++;
	}
	if(i < str1.len) return 1;
	if(i < str2.len) return -1;
	return 0;
}

static struct mg_str guess_content_type(const char* s)
{
	struct mg_str path { s, strlen(s) };
	size_t i = 0;

	// Shrink path to its extension only
	while(i < path.len && path.ptr[path.len - i - 1] != '.') i++;
	path.ptr += path.len - i;
	path.len = i;

	// Process built-in mime types
	for(i = 0; s_known_types[i].ptr != NULL; i += 2)
	{
		if(mg_strcmp(path, s_known_types[i]) == 0) return s_known_types[i + 1];
	}

	return mg_str("text/plain; charset=utf-8");
}

void SP_HttpEchoHandler::static_handle(SP_HttpRequest* request, SP_HttpResponse* response)
{
	std::string url = request->getURL();
	//std::string rootDir = "layuimini";
	std::string path = "web_root";
	if(url == "/")
		path += "/index.html";
	else
		path += url;

	void* fp = p_open(path.c_str());
	if(fp == 0)
		return;

	struct mg_str mime = guess_content_type(path.c_str());

	response->setStatusCode(200);
	response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, mime.ptr);
	

	char buffer[512] = {0};
	int len = 0;
	while((len = p_read(fp, buffer, 512)) != 0)
	{
		response->appendContent(buffer, len);
	}
}


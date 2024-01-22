#include "workHttp.h"
#include "sphttpmsg.hpp"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <dirent.h>
#include "spbuffer.hpp"

/****************************************************************************/
/*					mongoose:主要解析char									*/
/****************************************************************************/

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

// Parameter for mg_http_next_multipart
struct mg_http_part
{
	struct mg_str name;      // Form field name
	struct mg_str filename;  // Filename for file uploads
	struct mg_str body;      // Part contents
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

static const char* mg_http_status_code_str(int status_code)
{
	switch(status_code)
	{
	case 100: return "Continue";
	case 101: return "Switching Protocols";
	case 102: return "Processing";
	case 200: return "OK";
	case 201: return "Created";
	case 202: return "Accepted";
	case 203: return "Non-authoritative Information";
	case 204: return "No Content";
	case 205: return "Reset Content";
	case 206: return "Partial Content";
	case 207: return "Multi-Status";
	case 208: return "Already Reported";
	case 226: return "IM Used";
	case 300: return "Multiple Choices";
	case 301: return "Moved Permanently";
	case 302: return "Found";
	case 303: return "See Other";
	case 304: return "Not Modified";
	case 305: return "Use Proxy";
	case 307: return "Temporary Redirect";
	case 308: return "Permanent Redirect";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 402: return "Payment Required";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 406: return "Not Acceptable";
	case 407: return "Proxy Authentication Required";
	case 408: return "Request Timeout";
	case 409: return "Conflict";
	case 410: return "Gone";
	case 411: return "Length Required";
	case 412: return "Precondition Failed";
	case 413: return "Payload Too Large";
	case 414: return "Request-URI Too Long";
	case 415: return "Unsupported Media Type";
	case 416: return "Requested Range Not Satisfiable";
	case 417: return "Expectation Failed";
	case 418: return "I'm a teapot";
	case 421: return "Misdirected Request";
	case 422: return "Unprocessable Entity";
	case 423: return "Locked";
	case 424: return "Failed Dependency";
	case 426: return "Upgrade Required";
	case 428: return "Precondition Required";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 444: return "Connection Closed Without Response";
	case 451: return "Unavailable For Legal Reasons";
	case 499: return "Client Closed Request";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable";
	case 504: return "Gateway Timeout";
	case 505: return "HTTP Version Not Supported";
	case 506: return "Variant Also Negotiates";
	case 507: return "Insufficient Storage";
	case 508: return "Loop Detected";
	case 510: return "Not Extended";
	case 511: return "Network Authentication Required";
	case 599: return "Network Connect Timeout Error";
	default: return "";
	}
}

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

static int mg_base64_encode_single(int c)
{
	if(c < 26)
	{
		return c + 'A';
	}
	else if(c < 52)
	{
		return c - 26 + 'a';
	}
	else if(c < 62)
	{
		return c - 52 + '0';
	}
	else
	{
		return c == 62 ? '+' : '/';
	}
}

static int mg_base64_decode_single(int c)
{
	if(c >= 'A' && c <= 'Z')
	{
		return c - 'A';
	}
	else if(c >= 'a' && c <= 'z')
	{
		return c + 26 - 'a';
	}
	else if(c >= '0' && c <= '9')
	{
		return c + 52 - '0';
	}
	else if(c == '+')
	{
		return 62;
	}
	else if(c == '/')
	{
		return 63;
	}
	else if(c == '=')
	{
		return 64;
	}
	else
	{
		return -1;
	}
}

size_t mg_base64_update(unsigned char ch, char* to, size_t n)
{
	unsigned long rem = (n & 3) % 3;
	if(rem == 0)
	{
		to[n] = (char)mg_base64_encode_single(ch >> 2);
		to[++n] = (char)((ch & 3) << 4);
	}
	else if(rem == 1)
	{
		to[n] = (char)mg_base64_encode_single(to[n] | (ch >> 4));
		to[++n] = (char)((ch & 15) << 2);
	}
	else
	{
		to[n] = (char)mg_base64_encode_single(to[n] | (ch >> 6));
		to[++n] = (char)mg_base64_encode_single(ch & 63);
		n++;
	}
	return n;
}

size_t mg_base64_final(char* to, size_t n)
{
	size_t saved = n;
	// printf("---[%.*s]\n", n, to);
	if(n & 3) n = mg_base64_update(0, to, n);
	if((saved & 3) == 2) n--;
	// printf("    %d[%.*s]\n", n, n, to);
	while(n & 3) to[n++] = '=';
	to[n] = '\0';
	return n;
}

size_t mg_base64_encode(const unsigned char* p, size_t n, char* to, size_t dl)
{
	size_t i, len = 0;
	if(dl > 0) to[0] = '\0';
	if(dl < ((n / 3) + (n % 3 ? 1 : 0)) * 4 + 1) return 0;
	for(i = 0; i < n; i++) len = mg_base64_update(p[i], to, len);
	len = mg_base64_final(to, len);
	return len;
}

size_t mg_base64_decode(const char* src, size_t n, char* dst, size_t dl)
{
	const char* end = src == NULL ? NULL : src + n;  // Cannot add to NULL
	size_t len = 0;
	if(dl > 0) dst[0] = '\0';
	if(dl < n / 4 * 3 + 1) return 0;
	while(src != NULL && src + 3 < end)
	{
		int a = mg_base64_decode_single(src[0]),
			b = mg_base64_decode_single(src[1]),
			c = mg_base64_decode_single(src[2]),
			d = mg_base64_decode_single(src[3]);
		if(a == 64 || a < 0 || b == 64 || b < 0 || c < 0 || d < 0) return 0;
		dst[len++] = (char)((a << 2) | (b >> 4));
		if(src[2] != '=')
		{
			dst[len++] = (char)((b << 4) | (c >> 2));
			if(src[3] != '=') dst[len++] = (char)((c << 6) | d);
		}
		src += 4;
	}
	dst[len] = '\0';
	return len;
}

int mg_lower(const char* s)
{
	int c = *s;
	if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return c;
}

int mg_ncasecmp(const char* s1, const char* s2, size_t len)
{
	int diff = 0;
	if(len > 0) do
	{
		diff = mg_lower(s1++) - mg_lower(s2++);
	} while(diff == 0 && s1[-1] != '\0' && --len > 0);
	return diff;
}

int mg_vcasecmp(const struct mg_str* str1, const char* str2)
{
	size_t n2 = strlen(str2), n1 = str1->len;
	int r = mg_ncasecmp(str1->ptr, str2, (n1 < n2) ? n1 : n2);
	if(r == 0) return (int)(n1 - n2);
	return r;
}

static char* sp_strsep(char** s, const char* del)
{
	char* d, * tok;

	if(!s || !*s) return NULL;
	tok = *s;
	d = strstr(tok, del);

	if(d)
	{
		*s = d + strlen(del);
		*d = '\0';
	}
	else
	{
		*s = NULL;
	}

	return tok;
}
void mg_http_creds(mg_str* v, char* user, size_t userlen, char* pass, size_t passlen)
{
	user[0] = pass[0] = '\0';
	if(v != NULL && v->len > 6 && memcmp(v->ptr, "Basic ", 6) == 0)
	{
		char buf[256];
		size_t n = mg_base64_decode(v->ptr + 6, v->len - 6, buf, sizeof(buf));
		char* ori = buf;
		char* name = sp_strsep(&ori, ":");
		strcpy(user, name);
		strcpy(pass, ori);
	}
	else if(v != NULL && v->len > 7 && memcmp(v->ptr, "Bearer ", 7) == 0)
	{
		//mg_snprintf(pass, passlen, "%.*s", (int)v->len - 7, v->ptr + 7);
	}
	//else if((v = mg_http_get_header(hm, "Cookie")) != NULL)
	//{
	//	struct mg_str t = mg_http_get_header_var(*v, mg_str_n("access_token", 12));
	//	if(t.len > 0) mg_snprintf(pass, passlen, "%.*s", (int)t.len, t.ptr);
	//}
	//else
	//{
	//	mg_http_get_var(&hm->query, "access_token", pass, passlen);
	//}
}

bool mg_match(const char* s, const char* p)
{
	size_t s_len = strlen(s);
	size_t p_len = strlen(p);
	size_t i = 0, j = 0, ni = 0, nj = 0;
	while(i < p_len || j < s_len)
	{
		if(i < p_len && j < s_len && (p[i] == '?' || s[j] == p[i]))
		{
			i++, j++;
		}
		else if(i < p_len && (p[i] == '*' || p[i] == '#'))
		{
			ni = i++, nj = j + 1;
		}
		else if(nj > 0 && nj <= s_len && (p[ni] == '#' || s[j] != '/'))
		{
			i = ni, j = nj;
		}
		else
		{
			return false;
		}
	}
	return true;
}

struct mg_str mg_str_n(const char* s, size_t n)
{
	struct mg_str str = {s, n};
	return str;
}

static struct mg_str stripquotes(struct mg_str s)
{
	return s.len > 1 && s.ptr[0] == '"' && s.ptr[s.len - 1] == '"' ? mg_str_n(s.ptr + 1, s.len - 2) : s;
}

struct mg_str mg_http_get_header_var(struct mg_str s, struct mg_str v)
{
	size_t i;
	for(i = 0; v.len > 0 && i + v.len + 2 < s.len; i++)
	{
		if(s.ptr[i + v.len] == '=' && memcmp(&s.ptr[i], v.ptr, v.len) == 0)
		{
			const char* p = &s.ptr[i + v.len + 1], * b = p, * x = &s.ptr[s.len];
			int q = p < x && *p == '"' ? 1 : 0;
			while(p < x && (q ? p == b || *p != '"' : *p != ';' && *p != ' ' && *p != ','))
				p++;
			// MG_INFO(("[%.*s] [%.*s] [%.*s]", (int) s.len, s.ptr, (int) v.len,
			// v.ptr, (int) (p - b), b));
			return stripquotes(mg_str_n(b, (size_t)(p - b + q)));
		}
	}
	return mg_str_n(NULL, 0);
}

// Multipart POST example:
// --xyz
// Content-Disposition: form-data; name="val"
//
// abcdef
// --xyz
// Content-Disposition: form-data; name="foo"; filename="a.txt"
// Content-Type: text/plain
//
// hello world
//
// --xyz--
size_t mg_http_next_multipart(struct mg_str body, size_t ofs,struct mg_http_part* part)
{
	struct mg_str cd = mg_str_n("Content-Disposition", 19);
	const char* s = body.ptr;
	size_t b = ofs, h1, h2, b1, b2, max = body.len;

	// Init part params
	if(part != NULL) part->name = part->filename = part->body = mg_str_n(0, 0);

	// Skip boundary
	while(b + 2 < max && s[b] != '\r' && s[b + 1] != '\n') b++;
	if(b <= ofs || b + 2 >= max) return 0;
	// MG_INFO(("B: %zu %zu [%.*s]", ofs, b - ofs, (int) (b - ofs), s));

	// Skip headers
	h1 = h2 = b + 2;
	for(;;)
	{
		while(h2 + 2 < max && s[h2] != '\r' && s[h2 + 1] != '\n') h2++;
		if(h2 == h1) break;
		if(h2 + 2 >= max) return 0;
		// MG_INFO(("Header: [%.*s]", (int) (h2 - h1), &s[h1]));
		if(part != NULL && h1 + cd.len + 2 < h2 && s[h1 + cd.len] == ':' &&
		   mg_ncasecmp(&s[h1], cd.ptr, cd.len) == 0)
		{
			struct mg_str v = mg_str_n(&s[h1 + cd.len + 2], h2 - (h1 + cd.len + 2));
			part->name = mg_http_get_header_var(v, mg_str_n("name", 4));
			part->filename = mg_http_get_header_var(v, mg_str_n("filename", 8));
		}
		h1 = h2 = h2 + 2;
	}
	b1 = b2 = h2 + 2;
	while(b2 + 2 + (b - ofs) + 2 < max && !(s[b2] == '\r' && s[b2 + 1] == '\n' && memcmp(&s[b2 + 2], s, b - ofs) == 0))
		b2++;

	if(b2 + 2 >= max) return 0;
	if(part != NULL) part->body = mg_str_n(&s[b1], b2 - b1);
	// MG_INFO(("Body: [%.*s]", (int) (b2 - b1), &s[b1]));
	return b2 + 2;
}

struct user
{
	const char* name, * pass, * token;
};

static struct user users[] =
{
	{"admin", "pass0", "admin_token"},
	{"user1", "pass1", "user1_token"},
	{"user2", "pass2", "user2_token"},
	{NULL, NULL, NULL},
};



/****************************************************************************/
/*					SP_Http_iocp_Handler									*/
/****************************************************************************/


// 定义结构体
struct GlobalServerInfo
{
	SP_Buffer sp_buffer;
	//SP_Buffer sp_temp_buffer;
	char sp_path[256] = {0};
};

// 声明一个全局变量
struct GlobalServerInfo g_ServerInfo;


void InitGlobalInfo()
{
	g_ServerInfo.sp_buffer.reserve(10 * 1024);
	if(GetModuleFileName(NULL, g_ServerInfo.sp_path, MAX_PATH) == 0)
	{
		printf("Error getting path\n");
	}
	char* lastBackslash = strrchr(g_ServerInfo.sp_path, '\\');
	if(lastBackslash != NULL)
	{
		*(lastBackslash + 1) = '\0';
	}
}

void SP_Http_iocp_Handler::handle(SP_HttpRequest* request, SP_HttpResponse* response)
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

void SP_Http_iocp_Handler::static_handle(SP_HttpRequest* request, SP_HttpResponse* response)
{
	std::string url = request->getURL();
	std::string path = g_ServerInfo.sp_path;
	path += "layuimini";
	//path += "web_root";
	if(url == "/")
		path += "/index.html";
	else if(mg_match(url.c_str(), "/api/#") && !mg_match(url.c_str(), "/api/init.json"))
	{
		return api_process(request, response);
	}
	else if(mg_match(url.c_str(), "/page/api/#"))
	{
		return api_process(request, response);
	}
	else
	{
		for(int i = 0; i < url.length(); i++)
		{
			if(url[i] == '?')
			{
				break;
			}
			path += url[i];
		}
	}
	void* fp = p_open(path.c_str());
	if(fp == 0)
		return;

	struct mg_str mime = guess_content_type(path.c_str());
	time_t mtime = 0;
	response->setStatusCode(200);
	response->setReasonPhrase(mg_http_status_code_str(200));
	response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, mime.ptr);
	size_t size;
	p_stat(path.c_str(), &size, &mtime);
	char etag[64];
	sprintf(etag, "\"%lld.%lld\"", (int64_t)mtime, (int64_t)size);
	const char* inm = request->getHeaderValue("If-None-Match");
	if(inm)
	{
		mg_str str = {inm,strlen(inm)};
		if(mg_vcasecmp(&str, etag) == 0)
		{
			//update resource file
			response->setStatusCode(304);
			response->setReasonPhrase(mg_http_status_code_str(304));
			fclose((FILE*)fp);
			return;
		}
	}

	response->addHeader("Etag", etag);
	g_ServerInfo.sp_buffer.reset();
	int maxlen = g_ServerInfo.sp_buffer.getCapacity();
	size_t len = 0;
	while((len = p_read(fp, g_ServerInfo.sp_buffer.getWriteBuffer(), maxlen)) != 0)
	{
		response->appendContent(g_ServerInfo.sp_buffer.getWriteBuffer(), (int)len, (int)size);
	}
	fclose((FILE*)fp);
}

void SP_Http_iocp_Handler::api_process(SP_HttpRequest* request, SP_HttpResponse* response)
{
	std::string url = request->getURL();
	if(mg_match(url.c_str(), "/api/login"))
	{
		api_login(request, response);
	}
	else if(mg_match(url.c_str(), "/api/data"))
	{
		api_data(request, response);
	}
	else if(mg_match(url.c_str(), "/page/api/file-query"))
	{
		api_filequery(request, response);
	}
	else if(mg_match(url.c_str(), "/page/api/upload"))
	{
		api_upload(request, response);
	}
	else if(mg_match(url.c_str(), "/api/file-download"))
	{
		api_down(request, response);
	}
}

void SP_Http_iocp_Handler::api_login(SP_HttpRequest* request, SP_HttpResponse* response)
{
	const char* mData = request->getHeaderValue("Authorization");
	if(mData)
	{
		char user[256], pass[256];
		mg_str mstr = {mData,strlen(mData)};
		mg_http_creds(&mstr, user, strlen(user), pass, strlen(pass));
		bool bsuccess = false;
		struct user* u = NULL;
		if(user[0] != '\0' && pass[0] != '\0')
		{
			// Both user and password are set, search by user/password
			for(u = users; u->name != NULL; u++)
				if(strcmp(user, u->name) == 0 && strcmp(pass, u->pass) == 0)
				{
					bsuccess = true;
					break;
				}

		}
		else if(user[0] == '\0')
		{
			// Only password is set, search by token
			for(u = users; u->name != NULL; u++)
				if(strcmp(pass, u->token) == 0)
				{
					bsuccess = true;
					break;
				}
		}
		if(bsuccess)
		{
			response->setStatusCode(200);
			response->setReasonPhrase(mg_http_status_code_str(200));
			response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, "application/json");
			char buf[256] = {0};
			sprintf(buf, "user_name=%s; Secure, HttpOnly; SameSite = Lax; path = / ; max - age = 0", u->name);
			response->addHeader("Set-Cookie", buf);
			sprintf(buf, "access_token=%s; Secure, HttpOnly; SameSite = Lax; path = / ; max - age = 0", u->token);
			response->addHeader("Set-Cookie", buf);

			sprintf(buf, "{\"%s\":\"%s\",\"%s\":\"%s\"}", "user", u->name, "toekn", u->token);
			response->appendContent(buf);
		}
		else
		{
			response->setStatusCode(403);
			response->setReasonPhrase(mg_http_status_code_str(403));
		}
	}
	else
	{
		char buf[256] = {0};
		response->setStatusCode(403);
		response->setReasonPhrase(mg_http_status_code_str(403));
	}
}

void SP_Http_iocp_Handler::api_data(SP_HttpRequest* request, SP_HttpResponse* response)
{
	response->setStatusCode(200);
	response->setReasonPhrase(mg_http_status_code_str(200));
	response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, "application/json");
	char buf[256] = {0};
	sprintf(buf, "{\"%s\":\"%s\",\"%s\":\"%s\"}", "text", "Hello!", "data", "somedata");
	response->appendContent(buf);
}

void SP_Http_iocp_Handler::api_upload(SP_HttpRequest* request, SP_HttpResponse* response)
{
	const char* content_lenght = request->getHeaderValue("Content-Length");
	const char* content_type = request->getHeaderValue("Content-Type");
	const void* mcontent = request->getContent();

	struct mg_http_part part;
	size_t ofs = 0;
	mg_str mstr = {(const char*)mcontent,size_t(request->getContentLength())};
	while((ofs = mg_http_next_multipart(mstr, ofs, &part)) > 0)
	{
		if(part.filename.ptr)
		{
			char file_data[256] = {0};
			memcpy(file_data, part.filename.ptr, part.filename.len);
			file_data[part.filename.len] = '\0';
			FILE* file_ptr = fopen(file_data, "wb");
			if(fwrite(part.body.ptr, sizeof(char), part.body.len, file_ptr) != part.body.len)
			{
				fprintf(stderr, "写入文件时发生错误\n");
				fclose(file_ptr);
				return;
			}
			fclose(file_ptr);
			response->setStatusCode(200);
			response->setReasonPhrase(mg_http_status_code_str(200));
			response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, "application/json");
			g_ServerInfo.sp_buffer.reset();
			g_ServerInfo.sp_buffer.printf("{"
							 "\"%s\":%d,"
							 "\"%s\":\"%s\","
							 "\"%s\":{\"%s\":\"%s\"}}",
							 "code", 0,""
							 "msg", "上传成功",""
							 "data", "src", file_data);
			response->appendContent(g_ServerInfo.sp_buffer.getBuffer());
		}
	}
}

void SP_Http_iocp_Handler::api_down(SP_HttpRequest* request, SP_HttpResponse* response)
{
	const char* fileName = request->getHeaderValue("fileName");
	std::string fileNamePath = std::string(g_ServerInfo.sp_path) + "layuimini//page//" + fileName;

	void* fp = p_open(fileNamePath.c_str());
	if(fp == 0)
		return;

	response->setStatusCode(200);
	response->setReasonPhrase(mg_http_status_code_str(200));
	response->addHeader("Content-Type", "pplication/octet-stream");

	g_ServerInfo.sp_buffer.reset();
	g_ServerInfo.sp_buffer.printf("attachment; filename=\"%s\"",fileName);
	response->addHeader("Content-Disposition", (const char*)g_ServerInfo.sp_buffer.getBuffer());
	g_ServerInfo.sp_buffer.reset();
	int maxlen = g_ServerInfo.sp_buffer.getCapacity();
	size_t len = 0;

	time_t mtime = 0;
	size_t size;
	p_stat(fileNamePath.c_str(), &size, &mtime);

	while((len = p_read(fp, g_ServerInfo.sp_buffer.getWriteBuffer(), maxlen)) != 0)
	{
		response->appendContent(g_ServerInfo.sp_buffer.getWriteBuffer(), (int)len, (int)size);
		g_ServerInfo.sp_buffer.reset();
		len = 0;
	}
	fclose((FILE*)fp);
}

void SP_Http_iocp_Handler::api_filequery(SP_HttpRequest* request, SP_HttpResponse* response)
{
	std::string path = std::string(g_ServerInfo.sp_path) + "layuimini\\page\\";
	std::string dir = path + "*.html";

	// 打开目录
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile(dir.c_str(), &findFileData);

	if(hFind == INVALID_HANDLE_VALUE)
	{
		printf("No .html files found.\n");
	}
	else
	{
		int index = 0;
		std::string data_list;
		g_ServerInfo.sp_buffer.reset();
		//sp_buffer.printf("[");
		do
		{
			index++;
			std::string filepath = path + findFileData.cFileName;
			time_t mtime = 0;
			size_t size;
			p_stat(filepath.c_str(), &size, &mtime);
			
			g_ServerInfo.sp_buffer.printf(
					"{"
					"\"id\":\"%d\","
					"\"fileID\":\"%d\","
					"\"fileName\":\"%s\","
					"\"Size\":\"%zu\","
					"\"fileSize\":\"%zu\","
					"\"uploadTime\":\"%zu\","
					"\"fileStatus\":\"%d\""
					"},",
					index,         // 对应第一个 %d
					index,         // 对应第二个 %d
					findFileData.cFileName, // 对应 %s
					size,          // 对应第三个 %d
					size/1024,          // 对应第四个 %d
					mtime,         // 对应第五个 %d
					index % 2 == 0              // 对应第六个 %d
			);

		} 
		while(FindNextFile(hFind, &findFileData) != 0);
		if(index != 0)
		{
			int tmp_size = (int)g_ServerInfo.sp_buffer.getSize();
			//sp_buffer.erase(tmp_size);
			g_ServerInfo.sp_buffer.append_from_reverse(1,"\0",1);
			//sp_buffer.printf("[%s]", sp_buffer.getRawBuffer());
			g_ServerInfo.sp_buffer.printf(
				"{"
				"\"code\":%d,"
				"\"msg\":\"%s\","
				"\"count\":%d,"
				"\"data\":[%s]"
				"}",
				0,
				"",
				index,
				g_ServerInfo.sp_buffer.getRawBuffer()
			);
			g_ServerInfo.sp_buffer.erase(tmp_size);

			response->setStatusCode(200);
			response->setReasonPhrase(mg_http_status_code_str(200));
			response->addHeader(SP_HttpMessage::HEADER_CONTENT_TYPE, "application/json");
			response->setContent(g_ServerInfo.sp_buffer.getBuffer());
		}
		FindClose(hFind);
	}

}

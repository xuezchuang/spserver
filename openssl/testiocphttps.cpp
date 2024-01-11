/*
 * Copyright 2008 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include "spporting.hpp"

#include "sphttp.hpp"
#include "sphttpmsg.hpp"
#include "spiocpserver.hpp"
#include "spopenssl.hpp"
#include "spgetopt.h"

#include "../business/workHttp.h"

void sp_getbasepath_ipcp_https(char* path, int size)
{
	spwin32_getexefile(GetCurrentProcessId(), path, size);

	char* pos = strrchr(path, '\\');
	if(NULL != pos) *pos = '\0';
}

//int testiocphttps(int argc, char* argv[])
int main(int argc, char* argv[])
{
	int port = 8080, maxThreads = 10;

	extern char* optarg;
	int c;

	while((c = getopt(argc, argv, "p:t:v")) != EOF)
	{
		switch(c)
		{
		case 'p':
			port = atoi(optarg);
			break;
		case 't':
			maxThreads = atoi(optarg);
			break;
		case '?':
		case 'v':
			printf("Usage: %s [-p <port>] [-t <threads>]\n", argv[0]);
			exit(0);
		}
	}

	if(0 != sp_initsock()) assert(0);

	char basePath[256] = {0}, crtPath[256] = {0}, keyPath[256] = {0};
	sp_getbasepath_ipcp_https(basePath, sizeof(basePath));
	//snprintf(crtPath, sizeof(crtPath), "%s\\openssl\\demo.crt", basePath);
	//snprintf(keyPath, sizeof(keyPath), "%s\\openssl\\demo.key", basePath);
	//snprintf(crtPath, sizeof(crtPath), "%s\\openssl\\newcert.crt", basePath);
	//snprintf(keyPath, sizeof(keyPath), "%s\\openssl\\newkey.key", basePath);

	snprintf(crtPath, sizeof(crtPath), "%s\\openssl\\snowsome.com_public.crt", basePath);
	snprintf(keyPath, sizeof(keyPath), "%s\\openssl\\snowsome.com.key", basePath);
	
	

	SP_OpensslChannelFactory* opensslFactory = new SP_OpensslChannelFactory();
	if(0 != opensslFactory->init(crtPath, keyPath)) assert(0);

	SP_IocpServer server("", port, new SP_HttpHandlerAdapterFactory(new SP_Http_iocp_HandlerFactory()));

	server.setTimeout(60);
	server.setMaxThreads(maxThreads);
	server.setReqQueueSize(100, "HTTP/1.1 500 Sorry, server is busy now!\r\n");
	server.setIOChannelFactory(opensslFactory);

	server.runForever();

	return 0;
}


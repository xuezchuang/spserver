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
#include "spiocplfserver.hpp"

#include "sphttp.hpp"
#include "sphttpmsg.hpp"
#include "spserver.hpp"



#pragma comment(lib,"ws2_32")
#pragma comment(lib,"mswsock")

//#include <xstring>
#include <fstream>
#include <sstream>

#include "../business/workHttp.h"

//int testiocphttp(int argc, char* argv[])
int main(int argc, char* argv[])
{
	InitGlobalInfo();
	int port = 8080, maxThreads = 10;
	const char* serverType = "hahs";

	if(0 != sp_initsock()) assert(0);

	SP_IocpServer server("", port, new SP_HttpHandlerAdapterFactory(new SP_Http_iocp_HandlerFactory()));
	//SP_IocpLFServer server("", port, new SP_HttpHandlerAdapterFactory(new SP_Http_iocp_HandlerFactory()));

	server.setTimeout(60);
	server.setMaxThreads(maxThreads);
	server.setReqQueueSize(100, "HTTP/1.1 500 Sorry, server is busy now!\r\n");

	server.runForever();

	sp_closelog();

	return 0;
}

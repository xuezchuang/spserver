#pragma once

#include "sphttp.hpp"
#include <string>

void InitGlobalInfo();

class SP_Http_iocp_Handler : public SP_HttpHandler
{
public:
	SP_Http_iocp_Handler() {}
	virtual ~SP_Http_iocp_Handler() {}

	virtual void handle(SP_HttpRequest* request, SP_HttpResponse* response);

private:
	bool endsWith(const std::string& fullString, const std::string& ending);
	void static_handle(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_process(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_login(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_data(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_upload(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_down(SP_HttpRequest* request, SP_HttpResponse* response);
	void api_filequery(SP_HttpRequest* request, SP_HttpResponse* response);
};

class SP_Http_iocp_HandlerFactory : public SP_HttpHandlerFactory
{
public:
	SP_Http_iocp_HandlerFactory() {}
	virtual ~SP_Http_iocp_HandlerFactory() {}

	virtual SP_HttpHandler* create() const
	{
		return new SP_Http_iocp_Handler();
	}
};
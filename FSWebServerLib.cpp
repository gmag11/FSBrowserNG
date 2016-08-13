// 
// 
// 

#include "FSWebServerLib.h"

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port)
{
	
}

void AsyncFSWebServer::begin()
{
	AsyncWebServer::begin();
}

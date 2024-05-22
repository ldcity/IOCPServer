#include "PCH.h"
#include "ChatServer.h"

lib::CrashDump crashDump;


ChatServer* server;


int main()
{

	server = new ChatServer;

	server->ChatServerStart();



	return 0;
}
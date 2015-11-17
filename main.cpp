#include "Includes.h"
#include "server.h"


int main(int argc,char* argv[])
{

	try
	{
		Socket::initializeWinsock_();

		Server server("192.168.1.3","7000");
		//server.workWithClients();
		//some comments
	}
	catch (exception e)
	{
		cout << e.what() << endl;
		getchar();
	}
	Socket::closeWinsock();
	return 0;
}


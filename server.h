#ifndef SERVER_H
#define SERVER_H

#include "Connection.h"


class Server : public Connection
{
private:
	unique_ptr<ServerSocket> _servSock;
	unique_ptr<Socket> _contactSock;
	    
	unique_ptr<UDP_ServerSocket> _udpServerSocket;
	std::queue<int> _lastClientsId;
	std::list<unique_ptr<Socket>> _clients;
	size_t curClientInd;
public:
	Server(char* nodeName, char* serviceName, int nConnections = 5, int bufLen = 2048, int timeOut = 30) : Connection(bufLen,timeOut)
	{//ethernet frame = 1460 bytes
		_servSock.reset(new ServerSocket(nodeName,serviceName, nConnections));
		_contactSock = nullptr;

		_udpServerSocket.reset(new UDP_ServerSocket(nodeName, serviceName));

		fillCommandMap();
	}
   
	void clientMultiplex(int selTimeOut)
	{
		while (true)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			//add server socket
			FD_SET(_servSock->handle(), &readSet);
			
			for (unique_ptr<Socket>& sock : _clients)
				FD_SET(sock->handle(), &readSet);

			//set timeout
			//timeval timeout = getTimeOut(selTimeOut);
			SOCKET hMax = maxHandleValue();

			if (select(hMax + 1, &readSet, NULL, NULL, NULL) == SOCKET_ERROR)
				//we can't broke the server
				continue;

			//if new client try to connect
			if (FD_ISSET(_servSock->handle(), &readSet))
				acceptNewClient();

			/*for (unique_ptr<Socket>& sock : _clients)
				//if is client query
				if (FD_ISSET(sock->handle(), &readSet))
				*/
		}
	}
protected:

	SOCKET maxHandleValue()
	{
		auto compare = [](unique_ptr<Socket>& a, unique_ptr<Socket>& b) {return a->handle() < b->handle(); };
		list_ptr_it<Socket> maxClient = max_element(_clients.begin(), _clients.end(), compare);
		return std::max(_servSock->handle(), (*maxClient)->handle());
	}

	timeval getTimeOut(int sec_time)
	{
		timeval timeout;
		timeout.tv_sec = sec_time;
		timeout.tv_usec = 0;
		return timeout;
	}

	void registerNewClientId(int clientId)
	{
		if (_lastClientsId.size() == 2)
			_lastClientsId.pop();
		_lastClientsId.push(clientId);
	}
	void acceptNewClient()
	{
		_clients.emplace_back(_servSock->accept());

		int clientId;
		_clients.back()->receive(clientId);
		registerNewClientId(clientId);
		
		_clients.back()->makeUnblocked();
	}

	void clientQuery(unique_ptr<Socket>& sock)
	{
		string message = sock->receiveMessage();
		if (message.empty()) return;

		if (!checkStringFormat(message, "( )*[A-Za-z0-9_]+(( )+(.)+)?(\r\n|\n)"))
		{
			std::string errorMessage = string("invalid command format \"") + message;
			sock->sendMessage(errorMessage);
			return;
		}

		if (!catchCommand(message))
		{
			sock->sendMessage("unknown command");
			return;
		}

		if (std::regex_search(message, std::regex("quit|exit|close")))
		{
			_clients.remove_if([&sock](const unique_ptr<Socket>& s) {return s->handle() == sock->handle(); });
		}
	}

	void clientCommandsHandling()
	{
		while (true)
		{
			string message = _contactSock->receiveMessage();
			if (message.empty()) break;
	
			if (!checkStringFormat(message, "( )*[A-Za-z0-9_]+(( )+(.)+)?(\r\n|\n)"))
			{  
                std::string errorMessage = string("invalid command format \"") + message;
				_contactSock->sendMessage(errorMessage);
				continue;
			}

			if (!catchCommand(message))
			{
				_contactSock->sendMessage("unknown command");
				continue;
			}
			
			if (std::regex_search(message, std::regex("quit|exit|close")))
				break;

		}
	}

	//---------------------------------  ----------------------------------------//

	bool sendFile(string& message)
	{
		bool retVal = Connection::sendFile(_contactSock.get(), message, std::bind(&Server::tryToReconnect, this, std::placeholders::_1));
	  
		_contactSock->receiveAck();
	       
		retVal ? _contactSock->sendMessage("file downloaded\n") : _contactSock->sendMessage("fail to download the file\n");
		return retVal;
	}
	bool receiveFile(string& message)
	{
		bool retVal = Connection::receiveFile(_contactSock.get(), message, std::bind(&Server::tryToReconnect, this, std::placeholders::_1));
		retVal ? _contactSock->sendMessage("file uploaded\n") : _contactSock->sendMessage("fail to upload the file\n");
		return retVal;
	}

	bool sendFileUdp(string& message)
	{
		//get client address
		char arg;
		_udpServerSocket->receive<char>(arg);

		bool retVal = Connection::sendFile(_udpServerSocket.get(), message, std::bind(&Server::tryToReconnectUdp, this, std::placeholders::_1));

		_contactSock->receiveAck();

		retVal ? _contactSock->sendMessage("file downloaded\n") : _contactSock->sendMessage("fail to download the file\n");
		return retVal;
	}

	bool receiveFileUdp(string& message)
	{
		//get client address
		char arg;
		_udpServerSocket->receive<char>(arg);

		bool retVal = Connection::receiveFile(_udpServerSocket.get(), message, std::bind(&Server::tryToReconnectUdp, this, std::placeholders::_1));
		retVal ? _contactSock->sendMessage("file uploaded\n") : _contactSock->sendMessage("fail to upload the file\n");
		return retVal;
	}

	Socket* tryToReconnect(int timeOut)
	{    
	 
		if (!_servSock->makeUnblocked())
			return nullptr;
		//fcntl(_servSock->handle(),F_SETFL,O_NONBLOCK);
		if (!_servSock->select(Socket::Selection::ReadCheck ,timeOut))
		{	
			_servSock->makeBlocked();
			return nullptr ;
		}

		if (!_servSock->makeBlocked())
			return nullptr;

		acceptNewClient();
	
		if (_lastClientsId.front() == _lastClientsId.back())
			return _contactSock.get();
	
		return nullptr;
	}

	Socket* tryToReconnectUdp(int timeOut)
	{
		_udpServerSocket->setReceiveTimeOut(timeOut);
		//wait for client id (and client address)
		int clientId = 0;
		_udpServerSocket->receive<int>(clientId);
		_udpServerSocket->send(clientId);

		registerNewClientId(clientId);

		_udpServerSocket->disableReceiveTimeOut();

		//check if old client
		if (_lastClientsId.front() == _lastClientsId.back())
			return _udpServerSocket.get();

		return nullptr;
	}

	//-----------------------------------(),  ------------------------------//

	
	bool echo(string& message)
	{
		cutSuitableSubstring(message, "( )+");
		return _contactSock->sendMessage(message);
	}
	
	bool quit(string& message)
	{
		 //bool result = _contactSock->shutDown();
		//_contactSock->closeSocket();
		_contactSock.reset();
		return true;
	}
	bool time(string& message)
	{
		time_t curTime;
		curTime = std::time(NULL);
		return _contactSock->sendMessage(std::ctime(&curTime));
	}

	void fillCommandMap() override
	{
		
		_commandMap[string("echo")] = std::bind(&Server::echo, this, std::placeholders::_1);
		_commandMap[string("time")] = std::bind(&Server::time, this, std::placeholders::_1);
		_commandMap[string("quit")] = std::bind(&Server::quit, this, std::placeholders::_1);
		
		_commandMap[string("download")] = std::bind(&Server::sendFile, this, std::placeholders::_1);
		_commandMap[string("upload")] = std::bind(&Server::receiveFile, this, std::placeholders::_1);
		_commandMap[string("download_udp")] = std::bind(&Server::sendFileUdp, this, std::placeholders::_1);
		_commandMap[string("upload_udp")] = std::bind(&Server::receiveFileUdp, this, std::placeholders::_1);
	}



};


#endif //SERVER_H

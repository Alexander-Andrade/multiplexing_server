#ifndef SERVER_H
#define SERVER_H

#include "Connection.h"


class Server : public Connection
{
private:
	unique_ptr<ServerSocket> _servSock;
	    
	unique_ptr<UDP_ServerSocket> _udpServSock;
	std::queue<int> _lastClientsId;
	std::list<unique_ptr<Socket>> _clients;
	list_ptr_it<Socket> _curClientSock;
	size_t _flClientGone;
public:
	Server(char* nodeName, char* serviceName, int nConnections = 5, int bufLen = 2048, int timeOut = 30) : Connection(bufLen,timeOut)
	{//ethernet frame = 1460 bytes
		_servSock.reset(new ServerSocket(nodeName,serviceName, nConnections));
		
		_udpServSock.reset(new UDP_ServerSocket(nodeName, serviceName));

		fillCommandMap();
	}
   
	void clientMultiplex()
	{
		fd_set readSet;

		while (true)
		{
			FD_ZERO(&readSet);
			//add server socket
			FD_SET(_servSock->handle(), &readSet);
			
			for (unique_ptr<Socket>& sock : _clients)
				FD_SET(sock->handle(), &readSet);

			SOCKET hMax = maxHandleValue();

			if (select(hMax + 1, &readSet, NULL, NULL, NULL) == SOCKET_ERROR)
				//we can't broke the server
				continue;

			//if new client try to connect
			if (FD_ISSET(_servSock->handle(), &readSet))
				acceptNewClient();

			for (list_ptr_it<Socket> sock = _clients.begin(); sock != _clients.end(); )
			{    //if is client query
				if (FD_ISSET((*sock)->handle(), &readSet))
				{
					_flClientGone = false;
					_curClientSock = sock;
					clientQueryProcessing();
					//if remove client form the list and iterator can be invalid
					if (_flClientGone) {
						if (_curClientSock == _clients.end()) break;
						sock = _curClientSock;
						continue;
					}
				}
				++sock;
			}
		}
	}
protected:

	SOCKET maxHandleValue()
	{
		if (!_clients.empty())
		{
			auto compare = [](unique_ptr<Socket>& a, unique_ptr<Socket>& b) {return a->handle() < b->handle(); };
			list_ptr_it<Socket> maxClient = max_element(_clients.begin(), _clients.end(), compare);
			return std::max(_servSock->handle(), (*maxClient)->handle());
		}
		else
			return _servSock->handle();
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

	void getAndRegisterClientId(unique_ptr<Socket>& sock)
	{
		int clientId;
		sock->receive(clientId);
		registerNewClientId(clientId);
	}
	void acceptNewClient()
	{
		_clients.emplace_back(_servSock->accept());

		getAndRegisterClientId(_clients.back());

		_clients.back()->makeUnblocked();
	}
	void acceptOldClient()
	{
		(*_curClientSock).reset(_servSock->accept());

		getAndRegisterClientId((*_curClientSock));
	}

	void clientQueryProcessing()
	{
		(*_curClientSock)->makeBlocked();
		string message = (*_curClientSock)->receiveMessage();
		if (message.empty()) return;

		if (!checkStringFormat(message, "( )*[A-Za-z0-9_]+(( )+(.)+)?(\r\n|\n)"))
		{
			std::string errorMessage = string("invalid command format \"") + message;
			(*_curClientSock)->sendMessage(errorMessage);
			(*_curClientSock)->makeUnblocked();
			return;
		}

		if (!catchCommand(message))
		{
			(*_curClientSock)->sendMessage("unknown command");
			(*_curClientSock)->makeUnblocked();
			return;
		}

		if (std::regex_search(message, std::regex("quit|exit|close")))
		{	//delete this Socket from list (with calling it's destructor)
			_curClientSock = _clients.erase(_curClientSock);
			_flClientGone = true;
		}
		else
			(*_curClientSock)->makeUnblocked();
	}

	//---------------------------------  ----------------------------------------//

	bool sendFile(string& message)
	{
		bool retVal = Connection::sendFile((*_curClientSock).get(), message, std::bind(&Server::tryToReconnect, this, std::placeholders::_1));
	  
		(*_curClientSock)->receiveAck();
	       
		retVal ? (*_curClientSock)->sendMessage("file downloaded\n") : (*_curClientSock)->sendMessage("fail to download the file\n");
		return retVal;
	}
	bool receiveFile(string& message)
	{
		bool retVal = Connection::receiveFile((*_curClientSock).get(), message, std::bind(&Server::tryToReconnect, this, std::placeholders::_1));
		retVal ? (*_curClientSock)->sendMessage("file uploaded\n") : (*_curClientSock)->sendMessage("fail to upload the file\n");
		return retVal;
	}

	bool sendFileUdp(string& message)
	{
		//get client address
		char arg;
		_udpServSock->receive<char>(arg);

		bool retVal = Connection::sendFile(_udpServSock.get(), message, std::bind(&Server::tryToReconnectUdp, this, std::placeholders::_1));

		(*_curClientSock)->receiveAck();

		retVal ? (*_curClientSock)->sendMessage("file downloaded\n") : (*_curClientSock)->sendMessage("fail to download the file\n");
		return retVal;
	}

	bool receiveFileUdp(string& message)
	{
		//get client address
		char arg;
		_udpServSock->receive<char>(arg);

		bool retVal = Connection::receiveFile(_udpServSock.get(), message, std::bind(&Server::tryToReconnectUdp, this, std::placeholders::_1));
		retVal ? (*_curClientSock)->sendMessage("file uploaded\n") : (*_curClientSock)->sendMessage("fail to upload the file\n");
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

		acceptOldClient();
	
		if (_lastClientsId.front() == _lastClientsId.back())
			return (*_curClientSock).get();
	
		return nullptr;
	}

	Socket* tryToReconnectUdp(int timeOut)
	{
		_udpServSock->setReceiveTimeOut(timeOut);
		//wait for client id (and client address)
		int clientId = 0;
		_udpServSock->receive<int>(clientId);
		_udpServSock->send(clientId);

		registerNewClientId(clientId);

		_udpServSock->disableReceiveTimeOut();

		//check if old client
		if (_lastClientsId.front() == _lastClientsId.back())
			return _udpServSock.get();

		return nullptr;
	}

	//-----------------------------------(),  ------------------------------//

	
	bool echo(string& message)
	{
		cutSuitableSubstring(message, "( )+");
		return (*_curClientSock)->sendMessage(message);
	}
	
	bool quit(string& message)
	{
		 //bool result = _curClientSock->shutDown();
		//_curClientSock->closeSocket();
		(*_curClientSock).reset();
		return true;
	}
	bool time(string& message)
	{
		time_t curTime;
		curTime = std::time(NULL);
		return (*_curClientSock)->sendMessage(std::ctime(&curTime));
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

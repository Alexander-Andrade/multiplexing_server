#ifndef CONNECTION_H
#define CONNECTION_H

#include "Socket.h"

class FileWorker
{
private:
	//file r/w buffer
	vector<char> _buffer;
	int _timeOut;
	int _bufLen;

	//socket refs
	Socket* _socket;
	std::function<Socket*(int)> _tryToReconnect;
	string _fileName;
	int _fileLength;

	std::ifstream _rdFile;
	std::ofstream _wrFile;

	//totaly number of bytes accurately received
	int _totallyBytesReceived;
public:
	FileWorker(Socket* socket, std::function<Socket*(int)>& tryToReconnect, int bufLen, int timeOut) : _bufLen(bufLen), _timeOut(timeOut)
	{
		_socket = socket;
		_tryToReconnect = tryToReconnect;

		_totallyBytesReceived = 0;
		_fileLength = 0;
	}
	bool setupSendingSocket()
	{
		//send timeout less than receive timeout
		if (!_socket->setSendTimeOut(_timeOut >> 2)) return false;	//(/4)
																	//try to set system buffer size = _bufLen
		if (!_socket->setSendBufferSize(_bufLen)) return  false;
	}
	bool send(string& fileName)
	{
		_fileName = fileName;
		_rdFile.open(_fileName, ios::in | ios::binary);
		//file existance check
		if (!_rdFile.is_open())
		{
			_socket->sendRefuse();
			return false;
		}
		else
			_socket->sendConfirm();

		setupSendingSocket();
		//real system buffer size
		_bufLen = _socket->getSendBufferSize();
		//total size of the transmitting file
		_fileLength = getFileLength(_rdFile);

		//send hint data to the receiver
		if (!_socket->send(_bufLen)) return false;
		if (!_socket->send(_timeOut)) return  false;
		if (!_socket->send(_fileLength)) return false;

		if (_buffer.size() < _bufLen)
			_buffer.resize(_bufLen);

		int fileByteRead = 0;
		int bytesWrite = 0;

		while (true)
		{
			try
			{
				//file reading to buffer
				_rdFile.read(_buffer.data(), _bufLen);
				fileByteRead = _rdFile.gcount();
				if (!_rdFile.eof() && _bufLen != fileByteRead)
					return false;

				bytesWrite = _socket->send(_buffer.data(), fileByteRead);

				if (bytesWrite == SOCKET_ERROR)
					throw runtime_error("connection is lost");

				if (_rdFile.eof())
				{
					//timeOut / 2
					_socket->setSendTimeOut(_timeOut >> 1);
					//check bytes that client has received
					_socket->receive(_totallyBytesReceived);
					_socket->setSendTimeOut(_timeOut);

					if (_totallyBytesReceived == _fileLength)
						break;
					else
						throw runtime_error("connection is lost");

				}
			}
			catch (exception e)
			{
				if (!tryToRestoreConnectionFromTransmittingSide()) break;
			}
		}

		_rdFile.close();
		_socket->disableSendTimeOut();
		return true;
	}

	bool receive(string& fileName)
	{
		_fileName = fileName;
		//waiting for acknowledge
		if (!_socket->receiveAck())
		{//there is no such file
			cout << "there is no such file" << endl;
			return false;
		}
		_wrFile.open(_fileName, ios::out | ios::trunc | ios::binary);

		if (!_wrFile.is_open())
			//can't create file
			return false;

		//size of data portion
		if (!_socket->receive(_bufLen)) return false;
		if (!_socket->receive(_timeOut)) return false;
		if (!_socket->receive(_fileLength)) return false;

		if (_buffer.size() < _bufLen)
			_buffer.resize(_bufLen);

		_socket->setReceiveTimeOut(_timeOut);

		int bytesRead = 0;

		//file writing
		while (true)
		{
			try
			{
				bytesRead = _socket->receive(_buffer.data(), _bufLen);

				if (bytesRead == SOCKET_ERROR)
					throw runtime_error("connection is lost");

				else if (bytesRead == 0)
					//connection close
					break;
				//file writing
				_wrFile.write(_buffer.data(), bytesRead);
				_totallyBytesReceived += bytesRead;

				//end of transmittion check
				if (_totallyBytesReceived == _fileLength)
				{//file uploaded
				 //transmit to server bytes number that has received
					_socket->send(_totallyBytesReceived);
					break;
				}
			}
			catch (exception e)
			{
				if (!tryToRestoreConnectionFromReceivingSide()) break;
			}
		}

		_wrFile.close();
		_socket->disableReceiveTimeOut();
		return _fileLength == _totallyBytesReceived;
	}

private:

	bool tryToRestoreConnectionFromReceivingSide()
	{
		if ((_socket = _tryToReconnect(_timeOut)) == nullptr)
		{
			_wrFile.close();
			return false;
		}
		_socket->setReceiveTimeOut(_timeOut);
		return _socket->send(_totallyBytesReceived);
	}

	bool tryToRestoreConnectionFromTransmittingSide()
	{
		if ((_socket = _tryToReconnect(_timeOut)) == nullptr)
		{
			_rdFile.close();
			return false;
		}

		setupSendingSocket();
		//get bytes number that client managed to get
		_socket->receive(_totallyBytesReceived);

		_rdFile.seekg(_totallyBytesReceived, ios::beg);

		if (_rdFile.eof())
			_rdFile.clear();
		return true;
	}

	static int getFileLength(std::ifstream& file)
	{
		//cursor to the end of file
		file.seekg(0, ios::end);
		//get it position
		int fileEndPos = file.tellg();
		//cursor to the beginning
		file.seekg(0, ios::beg);
		//file length
		return fileEndPos;
	}

};


class Connection
{
	//contains mutual data end algorithms for server and client
protected:
	//comands and their functions map(dictionary)
	//реализуемые этими командами (const char* -> std::function<void(string)>)
	CommandMap _commandMap;

	//id of the client or server
	int _id;
	int _bufLen;
	int _timeOut;
	virtual void fillCommandMap() = 0;

public:
	Connection(int bufLen, int timeOut) : _bufLen(bufLen), _timeOut(timeOut)
	{
		_id = generateId<int>(0, std::numeric_limits<int>::max());
	}

	virtual ~Connection() {}

protected:

	bool catchCommand(string request)
	{
		//identifies command from request
		string command = cutSuitableSubstring(request, "[A-Za-z0-9]+");
		//check command
		if (checkCommandExistance(command))
		{
			//command execution
			_commandMap[command](request);
			return true;
		}
		//there is no such command
		return false;
	}

	static bool checkStringFormat(const string& message, const string& pattern)
	{//regexp match
		std::regex regExp(pattern);
		return std::regex_match(message, regExp);
	}

	bool checkCommandExistance(const string& command)
	{
		//command existance check
		auto it = _commandMap.find(command);
		return it != _commandMap.end();
	}
	//--------------------------find substring-----------------------------//
	static std::string cutSuitableSubstring(string &message, const string& pattern)
	{//cut first matching substring from message
		std::regex regExp(pattern);
		std::smatch matches;
		std::regex_search(message, matches, regExp);

		string result = matches.empty() ? string("") : matches[0];
		message = matches.suffix().str();

		return result;
	}

	static std::string getFirstPatternedSubstring(const string &message, const string& pattern)
	{//get first substring mathing to pattern 
		std::regex regExp(pattern);
		std::smatch matches;
		std::regex_search(message, matches, regExp);

		return matches.empty() ? string("") : matches[0];
	}

	//---------------------------------работа с файлами----------------------------------------//

	bool sendFile(Socket* socket, string& message, std::function<Socket*(int)> tryToReconnect)
	{
		string fileName = getFirstPatternedSubstring(message, "[A-Za-z0-9]+.[A-Za-z0-9]+");
		FileWorker fileWorker(socket, tryToReconnect, _bufLen, _timeOut);
		return fileWorker.send(fileName);
	}

	bool receiveFile(Socket* socket, string& message, std::function<Socket*(int)> tryToReconnect)
	{
		string fileName = getFirstPatternedSubstring(message, "[A-Za-z0-9]+.[A-Za-z0-9]+");
		FileWorker fileWorker(socket, tryToReconnect, _bufLen, _timeOut);
		return fileWorker.receive(fileName);
	}

	template<typename T>
	T generateId(int lowerBound = 0, int upperBound = 255)
	{
		std::default_random_engine generator;
		std::uniform_int_distribution<T> distribution(lowerBound, upperBound);
		generator.seed(time(NULL));
		return distribution(generator);
	}
};

#endif //CONNECTION_H
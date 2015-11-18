#ifndef INCLUDES_H
#define INCLUDES_H

#define UNIX



#if defined(WINDOWS)
#define _CRT_SECURE_NO_WARNINGS	//ctime unsafe
#define NOMINMAX	//windows.h -> define min,max

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Mstcpip.h>	//keep_alive
#include <Windows.h>

//for server windows,to link
#pragma comment(lib, "Ws2_32.lib")
//for client
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#elif defined(UNIX)

#include <sys/types.h>
#include <sys/time.h>	//timeval structure
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>    //SOL_TCP
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>

#endif

#include <stdio.h>
#include <iostream>
#include <sstream>	//std::stringstream
#include <string>
#include <exception>
#include <functional>
#include <algorithm>
#include <map>
#include <ctime>
#include <regex>
#include <cstring>
#include <fstream>
#include <queue>
#include <list>
#include <memory>
#include <time.h>
#include <random>
#include <memory>
#include <limits>
#include <cmath>

using namespace std;

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1024
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

#if defined(UNIX)
#define SOCKET int
#define SD_SEND SHUT_WR
#endif

using CommandMap = std::map< std::string, std::function<bool(string&)> >;
template<typename T> using list_ptr_it = typename std::list< unique_ptr<T> >::iterator;
//versions of Winsock;
//the older, limited version
#define SOCKET_V1 0x0101
//the latest edition
#define SOCKET_V2 0x0202

template<typename T>
std::string toString(T number)
{
	ostringstream stringStream;
	stringStream << number;
	return stringStream.str();
}

template<typename T>
T toNumber(std::string& str)
{
	T number = 0;
	istringstream(str.c_str()) >> number;
	return number;
}

#endif //INCLUDES_H

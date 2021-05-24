#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <process.h>

#pragma comment (lib, "Ws2_32.lib")
#pragma warning(disable:4996)

#define SERVER_ADDR "127.0.0.1"
#define BUFF_SIZE 2048
#define LOGIN_SUCCESS "200"
#define POST_SUCCESS "201"
#define LOGOUT_SUCCESS "202"
#define LOCK "401"
#define NOT_FOUND "402"
#define LOGIN "1"
#define POST "2"
#define LOGOUT "3"
#define DELIMITER "\r\n"

using namespace std;

SOCKET listenSock;
sockaddr_in serverAddr;
char clientIp[INET_ADDRSTRLEN];
int clientPort;

//Struct to save new session info
typedef struct session {
	SOCKET connSock;
	string username, clientIp;
	int clientPort;
	bool isLogin;
}session;

void sendMessage(SOCKET& connectedSocket, string& res);
void login(session& newSession, string& res, string payload);
void publish(string& res);
void logout(session& newSession, string& res);
void logFile(session& newSession, string buffData, string res);
string getCurrentTime();

int main(int argc, char* argv[])
{
	//Validate parameters
	if (argc != 2) 
	{
		std::cout << "Sample Pattern: Server.exe <Port Number>.\n";
		return 0;
	}
	int serverPort = std::stoi(argv[1]);

	//Initiate Winsock
	WSADATA wsaData;
	WORD version = MAKEWORD(2, 2);
	if (WSAStartup(version, &wsaData))
	{
		printf("Winsock 2 is not supported\n");
		exit(0);
	}
	//Construct Socket
	listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock == INVALID_SOCKET)
	{
		printf("Error %d: Cannot create server socket", WSAGetLastError());
		exit(0);
	}
	// Bind address to socket
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(serverPort);
	inet_pton(AF_INET, SERVER_ADDR, &serverAddr.sin_addr);
	if (bind(listenSock, (sockaddr*)&serverAddr, sizeof(serverAddr)))
	{
		printf("Error %d: Cannot associate a local  address with server socket.", WSAGetLastError());
		exit(0);
	}
	// Listen Request from client
	if (listen(listenSock, 10))
	{
		printf("Error %d: Cannot place server socket in state Listen", WSAGetLastError());
		exit(0);
	}
	printf("Server Started!\n");

	SOCKET client[FD_SETSIZE], connSock;
	fd_set readfds, initfds;
	sockaddr_in clientAddr;
	int ret, nEvents, clientAddrLen;
	char rcvBuff[BUFF_SIZE], sendBuff[BUFF_SIZE];
	char buffData[BUFF_SIZE];

	for (int i = 0; i < FD_SETSIZE; i++)
		client[i] = 0; // 0 indicates available entry
	FD_ZERO(&initfds);
	FD_SET(listenSock, &initfds);

	//Communicate with clients
	while (1)
	{
		readfds = initfds;
		nEvents = select(0, &readfds, 0, 0, 0);
		if (nEvents < 0)
		{
			printf("\nError! Cannot poll sockets: %d", WSAGetLastError());
			break;
		}

		//new client connection
		if (FD_ISSET(listenSock, &readfds)) {
			clientAddrLen = sizeof(clientAddr);
			if ((connSock = accept(listenSock, (sockaddr*)&clientAddr, &clientAddrLen)) < 0) 
			{
				printf("\nError! Cannot accept new connection: %d", WSAGetLastError());
				break;
			}
			else 
			{
				inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
				clientPort = ntohs(clientAddr.sin_port);
				printf("Receive from client %s:%d\n", clientIp, clientPort);
				int i;
				for (i = 0; i < FD_SETSIZE; i++) 
				{
					if (client[i] == 0) {
						client[i] = connSock;
						FD_SET(client[i], &initfds);
						break;
					}
				}
				if (i == FD_SETSIZE)
				{
					printf("\nToo many clients.");
					closesocket(connSock);
				}
				if (--nEvents == 0)
					continue; //no more event
			}
		}
		//receive data from clients
		for (int i = 0; i < FD_SETSIZE; i++)
		{
			if (client[i] == 0)
				continue;

			if (FD_ISSET(client[i], &readfds)) 
			{
				session newSession;
				newSession.connSock = client[i];
				newSession.clientIp = clientIp;
				newSession.clientPort = clientPort;
				newSession.isLogin = 0;
				memset(rcvBuff, 0, BUFF_SIZE);
				int ret = recv(client[i], rcvBuff, BUFF_SIZE, 0);
				if (ret == SOCKET_ERROR) 
				{
					if (WSAGetLastError() == 10054)
						continue;
					else 
						printf("Error %d: Cannot receive data\n", WSAGetLastError());
				}
				if (ret <= 0) 
				{
					FD_CLR(client[i], &initfds);
					closesocket(client[i]);
					client[i] = 0;
				}
				else if (ret > 0)
				{
					//handle received message from client
					string res, buffData, flag, payload;
					buffData = string(rcvBuff);
					auto endPos = buffData.find(DELIMITER);
					flag = buffData.substr(0, 1);
					payload = buffData.substr(1, endPos - 1);
					if (flag == LOGIN)
						login(newSession, res, payload);
					else if (flag == POST)
						publish(res);
					else if (flag == LOGOUT)
						logout(newSession, res);
					//log to file	
					logFile(newSession, buffData, res); 
					// send message to client
					sendMessage(client[i], res);
				}
				if (--nEvents <= 0)
					continue; //no more event
			}
		}
	}

	//Close Socket
	closesocket(listenSock);

	//Terminate Winsock
	WSACleanup();
	return 0;

}

/* The sendMessage function to send message to client
* @param1 SOCKET struct of connecting socket
* @param2 string of message to send to client
*/
void sendMessage(SOCKET& connectedSocket, string& res)
{
	int messageLen = res.size();
	int idx = 0, nLeft = messageLen;
	res += DELIMITER;
	while (nLeft > 0)
	{
		int ret = send(connectedSocket, &res.c_str()[idx], messageLen, 0);
		nLeft -= ret;
		idx += ret;
		if (ret == SOCKET_ERROR)
			cout << "Error " << WSAGetLastError() << ": Cannot send data.\n";
	}
}


/* The login function to handle login request
* @param1 struct of session to check login infomation
* @param2 string of message to send to client
* @param3 string of payload from message received from client
*/
void login(session& newSession, string& res, string payload)
{
	//read file account
	ifstream accountfile;
	accountfile.open("account.txt");
	string lineData;
	if (accountfile.fail())
		cout << "Cannot read file\n";
	else
	{
		int found = 0;
		while (!accountfile.eof())
		{
			getline(accountfile, lineData);
			size_t pos = lineData.find(" ");
			string username = lineData.substr(0, pos);
			string status = lineData.substr(pos + 1);
			if (username == payload)
			{
				found = 1;
				if (status == "1") {
					res = LOCK;
					break;
				}
				else
				{
					res = LOGIN_SUCCESS;
					newSession.isLogin = 1;
					newSession.username = username;
					break;
				}
			}
		}
		if (!found)
			res = NOT_FOUND;
	}
	accountfile.close();
}

/* The publish function to handle post request
* @param1 string of message to send to client
*/
void publish(string& res)
{
	res = POST_SUCCESS;
}

/* The logut function to handle logout request
* @param1 struct of session to save login infomation
* @param2 string of message to send to client
*/
void logout(session& newSession, string& res)
{
	res = LOGOUT_SUCCESS;
	newSession.isLogin = 0;
}

/* The logFile function log message to log_20183773.txt
* @param1 struct of session to get client info
* @param2 string of data received from client
* @param3 string as message to log
*/
void logFile(session& newSession, string buffData, string res) 
{
	fstream logFile;
	logFile.open("log_20183773.txt", ios::out | ios::app);
	string str = newSession.clientIp + ":" + to_string(newSession.clientPort);
	str += " " + getCurrentTime() + " ";
	string flag = buffData.substr(0, 1);

	if (flag == LOGIN)
		flag = "USER";
	else if (flag == POST)
		flag = "POST";
	else if (flag == LOGOUT)
		flag = "QUIT";

	str += "$ " + flag + " " + buffData.substr(1) + " $ " + res.substr(0, 3);
	cout << str << '\n';
	if (logFile.is_open())
	{
		logFile << str + "\n";
		logFile.close();
	}
	else cout << "Can't to open file\n";
}

// The getCurrentTime function return the formated current time as a string
string getCurrentTime()
{
	time_t rawtime;
	tm* timeinfo;
	char buffer[50];
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(buffer, 50, "[%d/%m/%Y %H:%M:%S]", timeinfo);
	return string(buffer);
}

#include "stdafx.h"
#include "BaseClient.h"
#include "NetworkSetup.h"
#include "CommonNetwork.h"
#include "..\RemoteDesktop_Library\SocketHandler.h"

RemoteDesktop::BaseClient::BaseClient(Delegate<void, std::shared_ptr<SocketHandler>&> c,
	Delegate<void, Packet_Header*, const char*, std::shared_ptr<SocketHandler>&> r,
	Delegate<void> d){
	Connected_CallBack = c;
	Receive_CallBack = r;
	Disconnect_CallBack = d;
}
RemoteDesktop::BaseClient::~BaseClient(){
	DEBUG_MSG("~BaseClient() Beg");
	Stop();//ensure threads have been stopped
	ShutDownNetwork();
	DEBUG_MSG("~BaseClient");
}
void RemoteDesktop::BaseClient::Connect(std::wstring host, std::wstring port){
	Stop();//ensure threads have been stopped
	_Host = host;
	_Port = port;
	Running = true;
	_BackGroundNetworkWorker = std::thread(&BaseClient::_RunWrapper, this, 3);//3 initial connect attempts, this is about 3-4 seconds

}

bool RemoteDesktop::BaseClient::_Connect(){
	DEBUG_MSG("Connecting to server . . . ");
	if (!StartupNetwork()) return false;
	SOCKET ConnectSocket = INVALID_SOCKET;
	struct addrinfoW *result = NULL, *ptr = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the server address and port

	auto iResult = GetAddrInfoW(_Host.c_str(), _Port.c_str(), &hints, &result);
	if (iResult != 0) return false;

	// Attempt to connect to an address until one succeeds
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) return false;
		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			DEBUG_MSG("Server Down....");
			continue;
		}
		break;
	}

	FreeAddrInfoW(result);

	if (ConnectSocket == INVALID_SOCKET) return false;

	//set socket to non blocking
	u_long iMode = 1;
	ioctlsocket(ConnectSocket, FIONBIO, &iMode);
	BOOL nodly = TRUE;
	if (setsockopt(ConnectSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodly, sizeof(nodly)) == SOCKET_ERROR){
		auto errmsg = WSAGetLastError();
		DEBUG_MSG("failed to sent TCP_NODELY with error = %", errmsg);
	}

	auto newevent = WSACreateEvent();
	WSAEventSelect(ConnectSocket, newevent, FD_CONNECT);
	auto Index = WSAWaitForMultipleEvents(1, &newevent, TRUE, 1000, FALSE);
	WSANETWORKEVENTS NetworkEvents;
	WSAEnumNetworkEvents(ConnectSocket, newevent, &NetworkEvents);

	if ((Index == WSA_WAIT_FAILED) || (Index == WSA_WAIT_TIMEOUT)) {
		DEBUG_MSG("Connect Failed!");
		return false;
	}

	WSACloseEvent(newevent);
	Socket = std::make_shared<SocketHandler>(ConnectSocket, true);

	Socket->Connected_CallBack = DELEGATE(&RemoteDesktop::BaseClient::_OnConnectHandler, this);
	Socket->Receive_CallBack = DELEGATE(&RemoteDesktop::BaseClient::_OnReceiveHandler, this);
	Socket->Disconnect_CallBack = DELEGATE(&RemoteDesktop::BaseClient::_OnDisconnectHandler, this);

	Socket->Exchange_Keys();
	return true;
}

void RemoteDesktop::BaseClient::_RunWrapper(int connectattempts){
	int counter = 0;

	while (Running && counter++ < connectattempts){
		if (!_Connect()){
			DEBUG_MSG("socket failed with error = %\n", WSAGetLastError());
		}
		else {
			counter = 0;//reset timer
			DisconnectReceived = false;
			connectattempts = 15;//set this to a specific value
			_Run();
		}
	}
	if (counter >= connectattempts)	Disconnect_CallBack();
	Running = false;

}
void RemoteDesktop::BaseClient::_Run(){
	auto newevent = WSACreateEvent();

		WSAEventSelect(Socket->get_Socket(), newevent, FD_CLOSE | FD_READ);

	WSANETWORKEVENTS NetworkEvents;
	DEBUG_MSG("Starting Loop");

	while (Running && !DisconnectReceived) {

		auto Index = WaitForSingleObject(newevent, 1000);

		if ((Index != WSA_WAIT_FAILED) && (Index != WSA_WAIT_TIMEOUT)) {

			WSAEnumNetworkEvents(Socket->get_Socket(), newevent, &NetworkEvents);
			if (((NetworkEvents.lNetworkEvents & FD_READ) == FD_READ)
				&& NetworkEvents.iErrorCode[FD_READ_BIT] == ERROR_SUCCESS){
				Socket->Receive();
			}
			else if (((NetworkEvents.lNetworkEvents & FD_CLOSE) == FD_CLOSE) && NetworkEvents.iErrorCode[FD_CLOSE_BIT] == ERROR_SUCCESS){
				break;// get out of loop and try reconnecting
			}
		}
	}	
	DEBUG_MSG("Ending Loop");
	WSACloseEvent(newevent);
	Socket.reset();
}

void RemoteDesktop::BaseClient::_OnDisconnectHandler(SocketHandler* socket){
	DisconnectReceived = true;
	DEBUG_MSG("Disconnect Received");
}
//the copies below of the socket are VERY IMPORTANT, this ensures the lifetime of the socket for the duration of the call!
void RemoteDesktop::BaseClient::_OnReceiveHandler(Packet_Header* p, const char* d, SocketHandler* s){
	if (Running){
		auto s = Socket;
		if (s) Receive_CallBack(p, d, s);
	}
}
void RemoteDesktop::BaseClient::_OnConnectHandler(SocketHandler* socket){
	if (Running){
		auto s = Socket;
		if (s) Connected_CallBack(s);
	}
}

void RemoteDesktop::BaseClient::Send(NetworkMessages m, NetworkMsg& msg){
	if (Running){
		auto s = Socket;
		if (s) s->Send(m, msg);
	}
}

void RemoteDesktop::BaseClient::Stop() {
	Running = false; 
	if (_BackGroundNetworkWorker.joinable()) _BackGroundNetworkWorker.join(); 
}
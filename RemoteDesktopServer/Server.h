#ifndef SERVER_H
#define SERVER_H
#include "BaseServer.h"
#include <memory>
#include "Image.h"
#include <mutex>

namespace RemoteDesktop{
	class ScreenCapture;
	class ImageCompression;
	class MouseCapture;
	class DesktopMonitor;

#if defined _DEBUG
	class CConsole;
#endif
	class Server : public BaseServer{
#if defined _DEBUG
		std::unique_ptr<CConsole> _DebugConsole;
#endif
		std::vector<SocketHandler> _NewClients;
		std::unique_ptr<ImageCompression> imagecompression;
		std::unique_ptr<MouseCapture> mousecapturing;
		std::unique_ptr<DesktopMonitor> _DesktopMonitor;

		std::mutex _NewClientLock;

		void _Run();
		std::thread _BackGroundWorker;
		void _HandleNewClients(std::vector<SocketHandler>& newclients, Image& img);
		void _HandleNewClients_and_ResolutionUpdates(Image& img, Image& _lastimg);
		void _Handle_ScreenUpdates(Image& img, Rect& rect, std::vector<unsigned char>& buffer);
		void _Handle_MouseUpdates(const std::unique_ptr<MouseCapture>& mousecapturing);

		void _Handle_MouseUpdate(SocketHandler& sh);

		bool _RunningAsService = false;

	public:
		Server();
		virtual ~Server() override;
		virtual void Stop() override;
		virtual void OnDisconnect(SocketHandler& sh) override;
		virtual void OnConnect(SocketHandler& sh) override;
		virtual void OnReceive(SocketHandler& sh)  override;
		virtual void Listen(unsigned short port) override;
	};

};


#endif
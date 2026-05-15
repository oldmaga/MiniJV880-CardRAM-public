#ifndef _netfileserver_h
#define _netfileserver_h

#include <string>
#include <circle/net/netsubsystem.h>

class CHTTPDaemon;
class CTFTPDaemon;

struct TNetFileServerConfig
{
	bool m_bDHCP = false;
	std::string m_IP;
	std::string m_Mask;
	std::string m_Gateway;
	unsigned m_nPort = 8080;
	std::string m_HostName;
	bool m_bWriteEnable = false;
	bool m_bExposePNJV80 = true;
	bool m_bExposeRoms = true;
	bool m_bTFTPEnable = false;
	unsigned m_nTFTPPort = 69;
	void *m_pFileSystem = 0;
};

class CNetFileServer
{
public:
	CNetFileServer (void);
	~CNetFileServer (void);

	bool Initialize (const TNetFileServerConfig& Config);
	bool InitializeWithNetSubSystem (const TNetFileServerConfig& Config, CNetSubSystem *pNetSubSystem);
	void Shutdown (void);
	void Process (void);

	bool IsInitialized (void) const;

private:
	static bool ParseIPv4 (const std::string& Text, u8 Address[4]);

private:
	bool m_bInitialized;
	bool m_bStartAttempted;
	bool m_bRunningLogged;
	bool m_bHTTPStarted;
	bool m_bTFTPStarted;
	bool m_bOwnNetSubSystem;
	TNetFileServerConfig m_Config;
	CNetSubSystem *m_pNetSubSystem;
	CHTTPDaemon *m_pHTTPDaemon;
	CTFTPDaemon *m_pTFTPServer;
};

#endif

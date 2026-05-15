#ifndef _wlanbringup_h
#define _wlanbringup_h

#include <circle/net/netsubsystem.h>

class CBcm4343Device;
class CWPASupplicant;

class CWLANBringup
{
public:
    CWLANBringup (void);
    ~CWLANBringup (void);

    bool Initialize (
        bool bDHCP,
        const char *pIP,
        const char *pMask,
        const char *pGateway,
        const char *pHostName);
    void Process (void);

    CNetSubSystem *GetNetSubSystem (void) const;
    bool IsReadyForServices (void) const;

private:
    bool m_bInitialized;
    bool m_bReadyLogged;
    bool m_bWaitingLogged;

    CNetSubSystem *m_pNetSubSystem;
    CNetDevice *m_pNetDevice;
    CBcm4343Device *m_pWLAN;
    CWPASupplicant *m_pWPASupplicant;
};

#endif

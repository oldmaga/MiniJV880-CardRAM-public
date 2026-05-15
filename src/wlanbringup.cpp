#include "wlanbringup.h"
#include "debug_tx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/timer.h>
#include <circle/net/socket.h>
#include <circle/net/in.h>
#include <circle/net/ipaddress.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

namespace
{
    static const char *kWLANFirmwarePath = "SD:firmware/";
    static const char *kWLANConfigFile   = "SD:wpa_supplicant.conf";
    static const char *kWLANFallbackHostName = "MiniJV880-WLAN";

    static const unsigned kDiagWaitLogIntervalMS = 15000;

    static const unsigned kActivationStartDelayMS = 1000;
    static const unsigned kActivationRetryIntervalMS = 1000;
    static const unsigned kActivationAttemptCount = 1;
    static const u16 kActivationPortPrimary = 80;
    static const u16 kActivationPortSecondary = 443;

    static bool ParseIPv4Text (const char *pText, u8 Address[4])
    {
        if (pText == 0 || pText[0] == '\0' || Address == 0)
        {
            return false;
        }

        unsigned A, B, C, D;
        char Tail;

        if (sscanf (pText, "%u.%u.%u.%u%c", &A, &B, &C, &D, &Tail) != 4)
        {
            return false;
        }

        if (A > 255 || B > 255 || C > 255 || D > 255)
        {
            return false;
        }

        Address[0] = (u8) A;
        Address[1] = (u8) B;
        Address[2] = (u8) C;
        Address[3] = (u8) D;
        return true;
    }

    static unsigned NowMS (void)
    {
        return CTimer::GetClockTicks () / 1000;
    }

    static bool s_bDiagStateKnown = false;
    static bool s_bDiagLastRunning = false;
    static bool s_bDiagLastConnected = false;
    static char s_DiagLastIP[64] = "";

    static unsigned s_nDiagInitMS = 0;
    static unsigned s_nDiagReadySinceMS = 0;
    static unsigned s_nDiagNextWaitLogMS = 0;
    static unsigned s_nDiagWPASinceMS = 0;
    static bool s_bDiagWPASeen = false;
    static bool s_bDiagRUNSeen = false;

    static bool s_bActivationBurstArmed = false;
    static unsigned s_nActivationNextAttemptMS = 0;
    static unsigned s_nActivationAttemptsLeft = 0;

    static void LogDiagState (
        bool bRunning,
        bool bConnected,
        const char *pIPText,
        const char *pTag)
    {
        char Buffer[192];
        const char *pText = (pIPText != 0 && pIPText[0] != '\0') ? pIPText : "(none)";
        const char *pReason = (pTag != 0 && pTag[0] != '\0') ? pTag : "change";

        unsigned nNowMS = NowMS ();
        unsigned nSinceInitMS = 0;
        if (nNowMS >= s_nDiagInitMS)
        {
            nSinceInitMS = nNowMS - s_nDiagInitMS;
        }

        int n = snprintf (
            Buffer, sizeof Buffer,
            "WLAN: diag t=%ums RUN=%d WPA=%d IP=%s tag=%s\r\n",
            nSinceInitMS,
            bRunning ? 1 : 0,
            bConnected ? 1 : 0,
            pText,
            pReason);

        if (n > 0 && (size_t) n < sizeof Buffer)
        {
            DebugTX::WriteString (Buffer);
        }
    }

    static void LogWaitMilestone (
        bool bRunning,
        bool bConnected,
        const char *pIPText)
    {
        char Buffer[192];
        const char *pText = (pIPText != 0 && pIPText[0] != '\0') ? pIPText : "(none)";

        unsigned nNowMS = NowMS ();
        unsigned nSinceInitMS = 0;
        if (nNowMS >= s_nDiagInitMS)
        {
            nSinceInitMS = nNowMS - s_nDiagInitMS;
        }

        int n = snprintf (
            Buffer, sizeof Buffer,
            "WLAN: wait t=%ums RUN=%d WPA=%d IP=%s\r\n",
            nSinceInitMS,
            bRunning ? 1 : 0,
            bConnected ? 1 : 0,
            pText);

        if (n > 0 && (size_t) n < sizeof Buffer)
        {
            DebugTX::WriteString (Buffer);
        }
    }

    static void LogWPAMilestone (void)
    {
        char Buffer[160];
        unsigned nNowMS = NowMS ();
        unsigned nSinceInitMS = 0;
        if (nNowMS >= s_nDiagInitMS)
        {
            nSinceInitMS = nNowMS - s_nDiagInitMS;
        }

        int n = snprintf (
            Buffer, sizeof Buffer,
            "WLAN: WPA connected after %u ms\r\n",
            nSinceInitMS);

        if (n > 0 && (size_t) n < sizeof Buffer)
        {
            DebugTX::WriteString (Buffer);
        }
    }

    static void LogRUNMilestone (const char *pIPText)
    {
        char Buffer[192];
        const char *pText = (pIPText != 0 && pIPText[0] != '\0') ? pIPText : "(none)";

        unsigned nNowMS = NowMS ();
        unsigned nSinceInitMS = 0;
        unsigned nSinceWPAMS = 0;

        if (nNowMS >= s_nDiagInitMS)
        {
            nSinceInitMS = nNowMS - s_nDiagInitMS;
        }

        if (s_bDiagWPASeen && nNowMS >= s_nDiagWPASinceMS)
        {
            nSinceWPAMS = nNowMS - s_nDiagWPASinceMS;
        }

        int n = snprintf (
            Buffer, sizeof Buffer,
            "WLAN: RUN reached after %u ms (delta from WPA=%u ms) IP=%s\r\n",
            nSinceInitMS,
            nSinceWPAMS,
            pText);

        if (n > 0 && (size_t) n < sizeof Buffer)
        {
            DebugTX::WriteString (Buffer);
        }
    }

    static int TryActivationConnect (CNetSubSystem *pNetSubSystem, u16 usPort, const CIPAddress &rGateway)
    {
        CSocket Socket (pNetSubSystem, IPPROTO_TCP);
        return Socket.Connect (rGateway, usPort);
    }

    static void RunActivationBurstAttempt (CNetSubSystem *pNetSubSystem)
    {
        DebugTX::WriteString ("WLAN: activation burst attempt\r\n");

        if (pNetSubSystem == 0 || pNetSubSystem->GetConfig() == 0)
        {
            DebugTX::WriteString ("WLAN: activation skipped (no net)\r\n");
            return;
        }

        const CIPAddress *pGateway = pNetSubSystem->GetConfig()->GetDefaultGateway ();
        if (pGateway == 0 || pGateway->IsNull ())
        {
            DebugTX::WriteString ("WLAN: activation skipped (no gateway)\r\n");
            return;
        }

        CString GatewayText;
        pGateway->Format (&GatewayText);

        int nResultPrimary = TryActivationConnect (
            pNetSubSystem,
            kActivationPortPrimary,
            *pGateway);

        if (nResultPrimary >= 0)
        {
            char Buffer[192];
            int n = snprintf (
                Buffer, sizeof Buffer,
                "WLAN: activation connect ok %s:%u\r\n",
                (const char *) GatewayText,
                (unsigned) kActivationPortPrimary);

            if (n > 0 && (size_t) n < sizeof Buffer)
            {
                DebugTX::WriteString (Buffer);
            }
            return;
        }

        int nResultSecondary = TryActivationConnect (
            pNetSubSystem,
            kActivationPortSecondary,
            *pGateway);

        if (nResultSecondary >= 0)
        {
            char Buffer[192];
            int n = snprintf (
                Buffer, sizeof Buffer,
                "WLAN: activation SECONDARY ok %s:%u rc=%d\r\n",
                (const char *) GatewayText,
                (unsigned) kActivationPortSecondary,
                nResultSecondary);

            if (n > 0 && (size_t) n < sizeof Buffer)
            {
                DebugTX::WriteString (Buffer);
            }
            return;
        }

        char Buffer[224];
        int n = snprintf (
            Buffer, sizeof Buffer,
           "WLAN: activation no TCP session %s:%u rc=%d %s:%u rc=%d\r\n",
            (const char *) GatewayText,
            (unsigned) kActivationPortPrimary,
            nResultPrimary,
            (const char *) GatewayText,
            (unsigned) kActivationPortSecondary,
            nResultSecondary);

        if (n > 0 && (size_t) n < sizeof Buffer)
        {
            DebugTX::WriteString (Buffer);
        }
    }
}

LOGMODULE ("wlanbringup");

CWLANBringup::CWLANBringup (void)
:   m_bInitialized (false),
    m_bReadyLogged (false),
    m_bWaitingLogged (false),
    m_pNetSubSystem (0),
    m_pNetDevice (0),
    m_pWLAN (0),
    m_pWPASupplicant (0)
{
}

CWLANBringup::~CWLANBringup (void)
{
    if (m_pWPASupplicant != 0)
    {
        delete m_pWPASupplicant;
        m_pWPASupplicant = 0;
    }

    if (m_pWLAN != 0)
    {
        delete m_pWLAN;
        m_pWLAN = 0;
    }

    if (m_pNetSubSystem != 0)
    {
        delete m_pNetSubSystem;
        m_pNetSubSystem = 0;
    }

    m_pNetDevice = 0;
    m_bInitialized = false;
    m_bReadyLogged = false;
    m_bWaitingLogged = false;
}

CNetSubSystem *CWLANBringup::GetNetSubSystem (void) const
{
    return m_pNetSubSystem;
}

bool CWLANBringup::IsReadyForServices (void) const
{
    if (!m_bInitialized || m_pNetSubSystem == 0 || m_pNetDevice == 0)
    {
        return false;
    }

    bool bReady = m_pNetSubSystem->IsRunning ();

    if (m_pNetDevice->GetType () == NetDeviceTypeWLAN)
    {
        bReady &= (m_pWPASupplicant != 0 && m_pWPASupplicant->IsConnected ());
    }

    return bReady;
}

bool CWLANBringup::Initialize (
    bool bDHCP,
    const char *pIP,
    const char *pMask,
    const char *pGateway,
    const char *pHostName)
{
    assert (m_pNetSubSystem == 0);
    assert (m_pWLAN == 0);
    assert (m_pWPASupplicant == 0);

    DebugTX::WriteString ("WLAN: init start\r\n");
    LOGNOTE ("WLAN bringup: starting");

    m_pWLAN = new CBcm4343Device (kWLANFirmwarePath);
    if (m_pWLAN == 0)
    {
        DebugTX::WriteString ("WLAN: alloc device failed\r\n");
        LOGERR ("WLAN bringup: cannot allocate CBcm4343Device");
        return false;
    }

    if (!m_pWLAN->Initialize ())
    {
        DebugTX::WriteString ("WLAN: device init failed\r\n");
        LOGERR ("WLAN bringup: CBcm4343Device init failed");
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    DebugTX::WriteString ("WLAN: device init ok\r\n");

    const char *pEffectiveHostName =
        (pHostName != 0 && pHostName[0] != '\0')
            ? pHostName
            : kWLANFallbackHostName;

    u8 IPAddress[4];
    u8 NetMask[4];
    u8 Gateway[4];
    u8 DNSServer[4] = {0, 0, 0, 0};

    if (bDHCP)
    {
        DebugTX::WriteString ("WLAN: using DHCP config\r\n");
        m_pNetSubSystem = new CNetSubSystem (
            0, 0, 0, 0,
            pEffectiveHostName,
            NetDeviceTypeWLAN);
    }
    else
    {
        if (!ParseIPv4Text (pIP, IPAddress)
            || !ParseIPv4Text (pMask, NetMask)
            || !ParseIPv4Text (pGateway, Gateway))
        {
            DebugTX::WriteString ("WLAN: invalid static config\r\n");
            LOGERR ("WLAN bringup: invalid static IP/mask/gateway config");
            delete m_pWLAN;
            m_pWLAN = 0;
            return false;
        }

        DebugTX::WriteString ("WLAN: using static config\r\n");
        m_pNetSubSystem = new CNetSubSystem (
            IPAddress,
            NetMask,
            Gateway,
            DNSServer,
            pEffectiveHostName,
            NetDeviceTypeWLAN);
    }

    if (m_pNetSubSystem == 0)
    {
        DebugTX::WriteString ("WLAN: alloc netsubsystem failed\r\n");
        LOGERR ("WLAN bringup: cannot allocate CNetSubSystem");
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    if (!m_pNetSubSystem->Initialize (FALSE))
    {
        DebugTX::WriteString ("WLAN: netsubsystem init failed\r\n");
        LOGERR ("WLAN bringup: CNetSubSystem init failed");
        delete m_pNetSubSystem;
        m_pNetSubSystem = 0;
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    DebugTX::WriteString ("WLAN: netsubsystem init ok\r\n");

    m_pWPASupplicant = new CWPASupplicant (kWLANConfigFile);
    if (m_pWPASupplicant == 0)
    {
        DebugTX::WriteString ("WLAN: alloc wpa failed\r\n");
        LOGERR ("WLAN bringup: cannot allocate CWPASupplicant");
        delete m_pNetSubSystem;
        m_pNetSubSystem = 0;
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    if (!m_pWPASupplicant->Initialize ())
    {
        DebugTX::WriteString ("WLAN: wpa init failed\r\n");
        LOGERR ("WLAN bringup: CWPASupplicant init failed");
        delete m_pWPASupplicant;
        m_pWPASupplicant = 0;
        delete m_pNetSubSystem;
        m_pNetSubSystem = 0;
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    DebugTX::WriteString ("WLAN: wpa init ok\r\n");

    m_pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeWLAN);
    if (m_pNetDevice == 0)
    {
        DebugTX::WriteString ("WLAN: get net device failed\r\n");
        LOGERR ("WLAN bringup: CNetDevice::GetNetDevice returned null");
        delete m_pWPASupplicant;
        m_pWPASupplicant = 0;
        delete m_pNetSubSystem;
        m_pNetSubSystem = 0;
        delete m_pWLAN;
        m_pWLAN = 0;
        return false;
    }

    m_bInitialized = true;
    m_bReadyLogged = false;
    m_bWaitingLogged = false;

    s_bDiagStateKnown = false;
    s_bDiagLastRunning = false;
    s_bDiagLastConnected = false;
    s_DiagLastIP[0] = '\0';

    s_nDiagInitMS = NowMS ();
    s_nDiagReadySinceMS = 0;
    s_nDiagNextWaitLogMS = s_nDiagInitMS + kDiagWaitLogIntervalMS;
    s_nDiagWPASinceMS = 0;
    s_bDiagWPASeen = false;
    s_bDiagRUNSeen = false;

    s_bActivationBurstArmed = false;
    s_nActivationNextAttemptMS = 0;
    s_nActivationAttemptsLeft = 0;

    DebugTX::WriteString ("WLAN: init done, waiting association\r\n");
    LOGNOTE ("WLAN bringup: initialized, waiting for association");

    return true;
}

void CWLANBringup::Process (void)
{
    if (!m_bInitialized || m_pNetSubSystem == 0 || m_pNetDevice == 0)
    {
        return;
    }

    bool bRunning = m_pNetSubSystem->IsRunning ();
    bool bConnected = true;

    if (m_pNetDevice->GetType () == NetDeviceTypeWLAN)
    {
        bConnected = (m_pWPASupplicant != 0 && m_pWPASupplicant->IsConnected ());
    }

    CString IPString;
    const char *pIPText = "";
    if (bRunning)
    {
        m_pNetSubSystem->GetConfig()->GetIPAddress()->Format (&IPString);
        pIPText = (const char *) IPString;
    }

    bool bIPChanged = strcmp (s_DiagLastIP, pIPText) != 0;

    if (!s_bDiagStateKnown
        || s_bDiagLastRunning != bRunning
        || s_bDiagLastConnected != bConnected
        || bIPChanged)
    {
        LogDiagState (
            bRunning,
            bConnected,
            pIPText,
            s_bDiagStateKnown ? "change" : "first");

        s_bDiagStateKnown = true;
        s_bDiagLastRunning = bRunning;
        s_bDiagLastConnected = bConnected;

        strncpy (s_DiagLastIP, pIPText, sizeof s_DiagLastIP - 1);
        s_DiagLastIP[sizeof s_DiagLastIP - 1] = '\0';
    }

    bool bReady = bRunning;
    if (m_pNetDevice->GetType () == NetDeviceTypeWLAN)
    {
        bReady &= bConnected;
    }

    unsigned nNowMS = NowMS ();

    if (bConnected && !s_bDiagWPASeen)
    {
        s_bDiagWPASeen = true;
        s_nDiagWPASinceMS = nNowMS;
        LogWPAMilestone ();
    }

    if (!bConnected)
    {
        s_bDiagWPASeen = false;
        s_nDiagWPASinceMS = 0;
        s_bDiagRUNSeen = false;
    }

    if (bRunning && bConnected && !s_bDiagRUNSeen)
    {
        s_bDiagRUNSeen = true;
        LogRUNMilestone (pIPText);
    }

    if (!bRunning || !bConnected)
    {
        s_bDiagRUNSeen = false;
    }

    if (bReady)
    {
        if (!m_bReadyLogged)
        {
            m_bReadyLogged = true;
            m_bWaitingLogged = false;
            s_nDiagReadySinceMS = nNowMS;

            char Buffer[192];
            const char *pText = (pIPText != 0 && pIPText[0] != '\0') ? pIPText : "(none)";
            unsigned nSinceInitMS = 0;
            if (nNowMS >= s_nDiagInitMS)
            {
                nSinceInitMS = nNowMS - s_nDiagInitMS;
            }

            int n = snprintf (
                Buffer, sizeof Buffer,
                "WLAN: associated and running IP=%s after %u ms\r\n",
                pText,
                nSinceInitMS);

            if (n > 0 && (size_t) n < sizeof Buffer)
            {
                DebugTX::WriteString (Buffer);
            }

            LOGNOTE ("WLAN bringup: associated and running");

            s_bActivationBurstArmed = true;
            s_nActivationNextAttemptMS = nNowMS + kActivationStartDelayMS;
            s_nActivationAttemptsLeft = kActivationAttemptCount;
            DebugTX::WriteString ("WLAN: activation burst armed\r\n");
        }
        else if (s_bActivationBurstArmed
              && s_nActivationAttemptsLeft > 0
              && (int) (nNowMS - s_nActivationNextAttemptMS) >= 0)
        {
            RunActivationBurstAttempt (m_pNetSubSystem);
            s_nActivationAttemptsLeft--;

            if (s_nActivationAttemptsLeft > 0)
            {
                s_nActivationNextAttemptMS = nNowMS + kActivationRetryIntervalMS;
            }
            else
            {
                s_bActivationBurstArmed = false;
                DebugTX::WriteString ("WLAN: activation burst complete\r\n");
            }
        }
    }
    else
    {
        if (m_bReadyLogged)
        {
            m_bReadyLogged = false;
            m_bWaitingLogged = false;
            s_bActivationBurstArmed = false;
            s_nActivationNextAttemptMS = 0;
            s_nActivationAttemptsLeft = 0;

            char Buffer[160];
            unsigned nReadyMS = 0;
            if (nNowMS >= s_nDiagReadySinceMS)
            {
                nReadyMS = nNowMS - s_nDiagReadySinceMS;
            }

            s_nDiagReadySinceMS = 0;
            s_nDiagNextWaitLogMS = nNowMS + kDiagWaitLogIntervalMS;

            int n = snprintf (
                Buffer, sizeof Buffer,
                "WLAN: association lost after %u ms ready\r\n",
                nReadyMS);

            if (n > 0 && (size_t) n < sizeof Buffer)
            {
                DebugTX::WriteString (Buffer);
            }

            LOGNOTE ("WLAN bringup: association lost");
        }
        else if (!m_bWaitingLogged)
        {
            m_bWaitingLogged = true;
            s_nDiagNextWaitLogMS = nNowMS + kDiagWaitLogIntervalMS;
            DebugTX::WriteString ("WLAN: waiting for association\r\n");
            LOGNOTE ("WLAN bringup: waiting for association");
        }
        else if ((int) (nNowMS - s_nDiagNextWaitLogMS) >= 0)
        {
            LogWaitMilestone (bRunning, bConnected, pIPText);

            if (bConnected && !bRunning)
            {
                unsigned nSinceWPAMS = 0;
                if (s_bDiagWPASeen && nNowMS >= s_nDiagWPASinceMS)
                {
                    nSinceWPAMS = nNowMS - s_nDiagWPASinceMS;
                }

                char Buffer[160];
                int n = snprintf (
                    Buffer, sizeof Buffer,
                    "WLAN: waiting for RUN/IP since WPA for %u ms\r\n",
                    nSinceWPAMS);

                if (n > 0 && (size_t) n < sizeof Buffer)
                {
                    DebugTX::WriteString (Buffer);
                }
            }

            s_nDiagNextWaitLogMS = nNowMS + kDiagWaitLogIntervalMS;
        }
    }
}

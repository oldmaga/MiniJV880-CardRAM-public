//
// kernel.cpp
//
// MiniDexed - Dexed FM synthesizer for bare metal Raspberry Pi
// Copyright (C) 2022  The MiniDexed Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "kernel.h"
#include <circle/logger.h>
#include <circle/synchronize.h>
#include <circle/gpiopin.h>
#include <circle/timer.h>
#include <assert.h>
#include <string.h>
#include <circle/usb/usbhcidevice.h>
#include "debug_tx.h"
#include "wlanbringup.h"

LOGMODULE ("kernel");

namespace
{
    static CWLANBringup *s_pWLANBringup = 0;
    static const unsigned kWLANServicesStartDelayMS = 3000;

    static unsigned KernelNowMS (void)
    {
        return CTimer::GetClockTicks () / 1000;
    }
}

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel (void)
:   CStdlibAppStdio ("minijv880"),
    m_Config (&mFileSystem),
    m_GPIOManager (&mInterrupt),
    m_I2CMaster (CMachineInfo::Get ()->GetDevice (DeviceI2CMaster), TRUE),
    m_pSPIMaster (nullptr),
    m_CPUThrottle (CPUSpeedMaximum),
    m_pJV880 (0),
    m_bUseWLAN (false),
    m_bWLANServicesStartAttempted (false),
    m_nWLANServicesReadySinceMS (0)
{
    s_pThis = this;
}

CKernel::~CKernel(void)
{
    if (s_pWLANBringup != 0)
    {
        delete s_pWLANBringup;
        s_pWLANBringup = 0;
    }

    s_pThis = 0;
}

bool CKernel::Initialize (void)
{
    if (!CStdlibAppStdio::Initialize ())
    {
        return FALSE;
    }

    LOGNOTE("=== BOOT OK ===");

    mLogger.RegisterPanicHandler (PanicHandler);

    if (!m_GPIOManager.Initialize ())
    {
        return FALSE;
    }

    // DebugTX: re-init AFTER GPIO manager (GPIO4 può essere stato resettato)
    DebugTX::Init();
    DebugTX::WriteString("DEBUGTX AFTER GPIO INIT\r\n");

    if (!m_I2CMaster.Initialize ())
    {
        return FALSE;
    }

    m_Config.Load ();

    DebugTX::WriteString("NETCFG ");
    DebugTX::WriteString(m_Config.GetNetEnabled() ? "EN=1 " : "EN=0 ");
    DebugTX::WriteString(m_Config.GetNetDHCP() ? "DHCP=1 " : "DHCP=0 ");
    DebugTX::WriteString(m_Config.GetNetWriteEnable() ? "WR=1 " : "WR=0 ");
    DebugTX::WriteString("IF=");
    DebugTX::WriteString(m_Config.GetNetInterface());
    DebugTX::WriteString("\r\n");

    m_bUseWLAN = m_Config.GetNetEnabled()
              && strcmp (m_Config.GetNetInterface(), "wlan") == 0;
    m_bWLANServicesStartAttempted = false;
    m_nWLANServicesReadySinceMS = 0;

    if (m_Config.GetNetEnabled())
    {
        if (!m_bUseWLAN)
        {
            DebugTX::WriteString("NETIF ethernet selected\r\n");

            TNetFileServerConfig NetCfg;
            NetCfg.m_bDHCP = m_Config.GetNetDHCP();
            NetCfg.m_IP = m_Config.GetNetIP();
            NetCfg.m_Mask = m_Config.GetNetMask();
            NetCfg.m_Gateway = m_Config.GetNetGateway();
            NetCfg.m_nPort = m_Config.GetNetPort();
            NetCfg.m_HostName = m_Config.GetNetHostName();
            NetCfg.m_bWriteEnable = m_Config.GetNetWriteEnable();
            NetCfg.m_bExposePNJV80 = m_Config.GetNetExposePNJV80();
            NetCfg.m_bExposeRoms = m_Config.GetNetExposeRoms();
            NetCfg.m_bTFTPEnable = m_Config.GetNetTFTPEnable();
            NetCfg.m_nTFTPPort = m_Config.GetNetTFTPPort();
            NetCfg.m_pFileSystem = &mFileSystem;

            if (m_NetFileServer.Initialize(NetCfg))
            {
                DebugTX::WriteString("NETFILE armed in kernel\r\n");
            }
            else
            {
                DebugTX::WriteString("NETFILE arm failed\r\n");
            }
        }
        else
        {
            DebugTX::WriteString("NETIF wlan selected\r\n");
            DebugTX::WriteString("NETFILE path forced off\r\n");
        }
    }

    unsigned nSPIMaster = m_Config.GetSPIBus();
    unsigned nSPIMode = m_Config.GetSPIMode();
    unsigned long nSPIClock = 1000 * m_Config.GetSPIClockKHz();

#if RASPPI<4
    if (nSPIMaster == 0)
#else
    if (nSPIMaster == 0 || nSPIMaster == 3 || nSPIMaster == 4 || nSPIMaster == 5 || nSPIMaster == 6)
#endif
    {
        unsigned nCPHA = (nSPIMode & 1) ? 1 : 0;
        unsigned nCPOL = (nSPIMode & 2) ? 1 : 0;
        m_pSPIMaster = new CSPIMaster (nSPIClock, nCPOL, nCPHA, nSPIMaster);
        if (!m_pSPIMaster->Initialize())
        {
            delete (m_pSPIMaster);
            m_pSPIMaster = nullptr;
        }
    }

    m_pUSB = new CUSBHCIDevice (&mInterrupt, &mTimer, TRUE);
    if (!m_pUSB->Initialize ())
    {
        return FALSE;
    }

    m_pJV880 = new CMiniJV880 (&m_Config, &mInterrupt, &m_GPIOManager,
                               &m_I2CMaster, m_pSPIMaster,
                               &mFileSystem, &mScreenUnbuffered);
    assert (m_pJV880);

    if (!m_pJV880->Initialize ())
    {
        return FALSE;
    }

    DebugTX::Init();
    DebugTX::WriteString("DEBUGTX AFTER JV880 INIT\r\n");

    DebugTX::WriteString("NETCFG ");
    DebugTX::WriteString(m_Config.GetNetEnabled() ? "EN=1 " : "EN=0 ");
    DebugTX::WriteString(m_Config.GetNetDHCP() ? "DHCP=1 " : "DHCP=0 ");
    DebugTX::WriteString(m_Config.GetNetWriteEnable() ? "WR=1 " : "WR=0 ");
    DebugTX::WriteString("IF=");
    DebugTX::WriteString(m_Config.GetNetInterface());
    DebugTX::WriteString("\r\n");

    if (m_Config.GetNetEnabled())
    {
        if (m_bUseWLAN)
        {
            DebugTX::WriteString("NETIF wlan selected\r\n");
            DebugTX::WriteString("NETFILE path forced off\r\n");
        }
        else
        {
            DebugTX::WriteString("NETIF ethernet selected\r\n");
            if (m_NetFileServer.IsInitialized())
            {
                DebugTX::WriteString("NETFILE armed in kernel\r\n");
            }
        }
    }

    if (m_bUseWLAN)
    {
        if (s_pWLANBringup == 0)
        {
            s_pWLANBringup = new CWLANBringup;
        }

        if (s_pWLANBringup == 0)
        {
            DebugTX::WriteString("WLAN bringup alloc failed\r\n");
        }
        else if (!s_pWLANBringup->Initialize(
             m_Config.GetNetDHCP(),
             m_Config.GetNetIP(),
             m_Config.GetNetMask(),
             m_Config.GetNetGateway(),
             m_Config.GetNetHostName()))
        {
            DebugTX::WriteString("WLAN bringup init failed\r\n");
        }
        else
        {
            DebugTX::WriteString("WLAN bringup init ok\r\n");
        }
    }

    return TRUE;
}

CStdlibApp::TShutdownMode CKernel::Run (void)
{
    assert (m_pJV880);

    static bool once = false;

    while (42 == 42)
    {
        if (!once)
        {
            once = true;
            DebugTX::WriteString("ENTER RUN LOOP\r\n");
        }

        boolean bUpdated = m_pUSB->UpdatePlugAndPlay ();

        m_pJV880->Process(bUpdated);

        if (m_bUseWLAN)
        {
            if (s_pWLANBringup != 0)
            {
                s_pWLANBringup->Process ();

                if (!m_bWLANServicesStartAttempted)
                {
                    if (s_pWLANBringup->IsReadyForServices ())
                    {
                        unsigned nNowMS = KernelNowMS ();

                        if (m_nWLANServicesReadySinceMS == 0)
                        {
                            m_nWLANServicesReadySinceMS = nNowMS;
                            DebugTX::WriteString ("NETFILE wlan holdoff started\r\n");
                        }
                        else if ((int) (nNowMS - m_nWLANServicesReadySinceMS) >= (int) kWLANServicesStartDelayMS)
                        {
                            m_bWLANServicesStartAttempted = true;

                            TNetFileServerConfig NetCfg;
                            NetCfg.m_bDHCP = m_Config.GetNetDHCP();
                            NetCfg.m_IP = m_Config.GetNetIP();
                            NetCfg.m_Mask = m_Config.GetNetMask();
                            NetCfg.m_Gateway = m_Config.GetNetGateway();
                            NetCfg.m_nPort = m_Config.GetNetPort();
                            NetCfg.m_HostName = m_Config.GetNetHostName();
                            NetCfg.m_bWriteEnable = m_Config.GetNetWriteEnable();
                            NetCfg.m_bExposePNJV80 = m_Config.GetNetExposePNJV80();
                            NetCfg.m_bExposeRoms = m_Config.GetNetExposeRoms();
                            NetCfg.m_bTFTPEnable = m_Config.GetNetTFTPEnable();
                            NetCfg.m_nTFTPPort = m_Config.GetNetTFTPPort();
                            NetCfg.m_pFileSystem = &mFileSystem;

                            if (m_NetFileServer.InitializeWithNetSubSystem (
                                    NetCfg,
                                    s_pWLANBringup->GetNetSubSystem ()))
                            {
                                DebugTX::WriteString ("NETFILE armed in kernel (wlan external stack)\r\n");
                            }
                            else
                            {
                                DebugTX::WriteString ("NETFILE arm failed (wlan external stack)\r\n");
                            }
                        }
                    }
                    else
                    {
                        m_nWLANServicesReadySinceMS = 0;
                    }
                }
            }

            if (m_NetFileServer.IsInitialized ())
            {
                if (s_pWLANBringup->IsReadyForServices ())
                {
                    CNetSubSystem *pWLANNet = s_pWLANBringup->GetNetSubSystem ();
                    if (pWLANNet != 0)
                    {
                        pWLANNet->Process ();
                    }
                }

                m_NetFileServer.Process ();
            }
        }
        else
        {
            if (m_NetFileServer.IsInitialized ())
            {
                m_NetFileServer.Process ();
            }
        }

        DebugTX::Poll();   // core0, frequente, sicuro

        if (mbScreenAvailable)
        {
            mScreen.Update ();
        }

        m_CPUThrottle.Update ();
        m_Scheduler.Yield ();
    }

    return ShutdownHalt;
}

void CKernel::PanicHandler (void)
{
    LOGNOTE ("panic!");

    EnableIRQs ();

    if (s_pThis->mbScreenAvailable)
    {
        s_pThis->mScreen.Update (4096);
    }
}


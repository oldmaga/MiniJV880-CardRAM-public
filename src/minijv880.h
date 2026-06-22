//
// minidexed.h
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
#ifndef _minijv880_h
#define _minijv880_h

#ifndef ARM_ALLOW_MULTI_CORE
#define ARM_ALLOW_MULTI_CORE
#endif

#include "config.h"
#include "userinterface.h"
#include "emulator/mcu.h"
#include <circle/gpiomanager.h>
#include <circle/i2cmaster.h>
#include <circle/interrupt.h>
#include <circle/multicore.h>
#include <circle/screen.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/spimaster.h>
#include <circle/spinlock.h>
#include <circle/types.h>
#include <circle/usb/usbmidi.h>
#include <fatfs/ff.h>
#include <stdint.h>
#include <circle/serial.h>
#include <circle/writebuffer.h>
#include <atomic>

    // ============================================================
    // SR Scan Result (Immediate minimal patch)
    // ============================================================

    enum class SRScanCode : uint8_t {
        Ok = 0,
        ErrRomsDirMissingOrUnreadable,
        ErrNoSRFound,
        ErrNoValidSR,
    };

    struct SRScanResult {
        SRScanCode code = SRScanCode::Ok;
        char shortMsg[32] = "SR READY";
    };


class CMIDISerialDevice : public CSerialDevice
{
public:
    CMIDISerialDevice(CInterruptSystem *pInterruptSystem, boolean bUseFIQ)
    : CSerialDevice(pInterruptSystem, bUseFIQ)
    {
    }

    unsigned GetAvailableForWrite()
    {
        return AvailableForWrite();
    }
};

class CMiniJV880 : public CMultiCoreSupport
{
public:
    CMiniJV880(CConfig *pConfig, CInterruptSystem *pInterrupt,
               CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster,
               FATFS *pFileSystem, CScreenDevice *mScreenUnbuffered);
               
    enum class EPlayMode : uint8_t { Unknown=0, Patch, Performance };
    
    bool Initialize(void);
    void Process(bool bPlugAndPlayUpdated);
    virtual void Run(unsigned nCore) override;
    void FlushCardRAMIfNeeded();

    static void USBMIDIMessageHandler(unsigned nCable, u8 *pPacket, unsigned nLength);
    static void DeviceRemovedHandler(CDevice *pDevice, void *pContext);
    static void ParseMIDIData(CMiniJV880* pThis, const u8* pData, unsigned nLength);
    static bool HandleMCUUARTTX(void *context, uint8_t data);

    static CMiniJV880* GetInstance() { return s_pThis; }
    void ShowKernelRebootMessage();

    bool IsSRMenuActive() const { return m_bSRMenuActive; }
    bool IsRD500MenuActive() const { return m_bRD500MenuActive; }
    bool IsRD500PatchBrowseActive() const { return m_bRD500PatchBrowseActive; }
    bool IsSYXMenuActive() const { return m_bSYXMenuActive; }
    bool IsRD500Available() const { return m_bRD500Available; }

    bool ConsumeBlockedRD500Enter()
    {
        if (!m_bRD500WaitEnterRelease)
            return false;

        m_bRD500WaitEnterRelease = false;
        return true;
    }
    bool LoadSyxFromFile(const char* path);
    bool ScanSYXDirectory(const char* dirPath);
    bool IsSRReentryBlocked() const { return m_BlockSRReentry; }
    bool IsRD500Selected() const;
    int  GetRD500BankIndex() const { return m_nRD500BankIndex; }
    int  GetRD500PatchIndex() const { return m_nRD500PatchIndex; }
    void NextRD500Bank();
    void PrevRD500Bank();
    void NextRD500Patch();
    void PrevRD500Patch();

    // ===== SR Menu =====
    void OpenSRMenu()
    {
        m_bSRMenuActive = true;
        m_nSRIndex = 0;                  // reset focus to the first SR slot
        m_bForceLCDRefresh = true;       // force display refresh
        mcu.mcu_button_pressed = 0;      // reset leftover button events
        m_Serial.Write("SR OPEN\n", 8);  // log to minicom
    }

    void CloseSRMenu()
    {
        m_bSRMenuActive = false;
        
        // Overwrite the first line of the SR menu
        const char* title = "Select SR + ENTER";
        size_t len = strlen(title);
        memset(&mcu.lcd.LCD_Data[0], ' ', len);
        //mcu.mcu_button_pressed = 0;      // reset any leftovers
        //m_Serial.Write("SR CLOSE\n", 9); // log to minicom
        
        // Inject UTILITY press (bit 8)
        m_InjectedButtonMask = (1 << 8);   // Utility
    }
// ======================================

    void NextSR();
    void PrevSR();
    void NextSYX();
    void PrevSYX();
    void ApplySelectedSR();
    bool ApplySelectedRD500Patch();

    void OpenRD500Menu()
    {
        m_bSRMenuActive = false;
        m_bRD500MenuActive = true;
        m_nRD500BankIndex = 0;
        EnsureRD500ResourcesLoaded();
        m_bForceLCDRefresh = true;
        mcu.mcu_button_pressed = 0;
        m_Serial.Write("RD500 OPEN\n", 11);
    }

    void CloseRD500Menu()
    {
        m_bRD500MenuActive = false;
        m_bSRMenuActive = true;
        m_bForceLCDRefresh = true;
        mcu.mcu_button_pressed = 0;
        m_Serial.Write("RD500 CLOSE\n", 12);
    }

    void OpenRD500PatchBrowse()
    {
        m_bRD500MenuActive = false;
        m_bRD500PatchBrowseActive = true;
        m_nRD500PatchIndex = 0;
        m_bForceLCDRefresh = true;
        mcu.mcu_button_pressed = 0;
        m_Serial.Write("RD500 BROWSE OPEN\n", 18);
    }

    void CloseRD500PatchBrowse()
    {
        m_bRD500PatchBrowseActive = false;
        m_bRD500MenuActive = true;
        m_bForceLCDRefresh = true;
        mcu.mcu_button_pressed = 0;
        m_Serial.Write("RD500 BROWSE CLOSE\n", 19);
    }

    std::atomic<bool> core2_idle{false};
    std::atomic<bool> core3_idle{false};

    MCU mcu;

    bool HasSRError() const;
    bool HasSRList() const;
    
    void RequestCloseSRMenu();
    bool IsPatchWriteScreen() const;
    bool GetPatchWriteDestination(char *group, int *slot) const;
    void TogglePatchWriteCardDestinationExperimental();

    bool IsPerformanceWriteScreen() const;
    bool GetPerformanceWriteDestination(char *group, int *slot) const;
    void TogglePerformanceWriteCardDestinationExperimental();

    bool IsPatchCopyScreen() const;
    void TogglePatchCopySourceCardExperimental();

    bool IsPerformanceCopyScreen() const;
    void TogglePerformanceCopySourceCardExperimental();

private:
    CConfig *m_pConfig;
    FATFS *m_pFileSystem;

    CUSBMIDIDevice *volatile m_pMIDIDevice = nullptr;
    CMIDISerialDevice m_Serial;
    CWriteBufferDevice m_MIDISendBuffer;

    int lastEncoderPos = 0;
    CSoundBaseDevice *m_pSoundDevice;
    CScreenDevice *screenUnbuffered;
    bool m_bChannelsSwapped;
    unsigned m_nQueueSizeFrames;

    CUserInterface m_UI;

    unsigned m_lastTick;
    unsigned m_lastTick1;

    static CMiniJV880 *s_pThis;
    unsigned n_mMCUcycles = 9;

    // ===== SR / RD500 MENU STATE =====
    bool m_bSRMenuActive = false;
    bool m_bRD500MenuActive = false;
    bool m_bRD500PatchBrowseActive = false;
    int  m_nSRIndex = 0;
    int  m_nRD500BankIndex = 0;   // 0=A, 1=B, 2=C
    int  m_nRD500PatchIndex = 0;  // 0..63 within selected bank
    bool m_bForceLCDRefresh = false;
    // ===============================

    // ===== RD-500 RESOURCES =====
    static const int RD500_PATCH_COUNT = 192;
    static const int RD500_PATCH_NAME_LEN = 17; // 16 + terminator

    bool     m_bRD500Available = false;
    bool     m_bRD500ExpansionApplied = false;
    uint8_t* m_pRD500Expansion = nullptr;
    uint8_t* m_pRD500Patches   = nullptr;
    char     m_szRD500Status[32] = "RD500 NOT LOADED";
    char     m_szRD500PatchNames[RD500_PATCH_COUNT][RD500_PATCH_NAME_LEN] = {{0}};

    bool        EnsureRD500ResourcesLoaded();
    void        SetRD500Status(const char* msg);
    const char* GetRD500PatchName(int absoluteIndex) const;
    // ===========================
    
    // ===== SYX MENU STATE =====
    static const int DEFAULT_SYX_FILES   = 32;
    static const int MAX_SYX_FILES       = 128;  // hard safety cap
    static const int MAX_SYX_NAME_LEN    = 96;   // visible label, includes terminator
    static const int MAX_SYX_PATH_LEN    = 128;  // full path, includes terminator

    enum class SYXItemType : uint8_t
    {
        File = 0,
        Folder = 1
    };

    bool m_bSYXMenuActive = false;
    int  m_nSYXIndex = 0;
    bool m_bSYXWaitEnterRelease = false;
    bool m_bRD500WaitEnterRelease = false;
    int  m_nSYXFileCount = 0;
    int  m_nSYXMaxFiles = DEFAULT_SYX_FILES;    // to be read from .ini

    // New structured SYX menu state for folder-aware browsing.
    char        m_szSYXDisplay[MAX_SYX_FILES][MAX_SYX_NAME_LEN] = {{0}};
    char        m_szSYXPath[MAX_SYX_FILES][MAX_SYX_PATH_LEN] = {{0}};
    SYXItemType m_SYXItemType[MAX_SYX_FILES] = {};
    char        m_szSYXCurrentDir[MAX_SYX_PATH_LEN] = "PN-JV80";
    char        m_szSYXLastLoadedDir[MAX_SYX_PATH_LEN] = "";
    int         m_nSYXLastLoadedIndex = -1;
    bool        m_bSYXAtRoot = true;
    // ==========================
    
    // --- Button injection ---
    uint32_t m_InjectedButtonMask = 0;

    // --- Double Utility sequence ---
    uint32_t m_UtilityInjectedTick = 0;
    int      m_SYXExitSeqState = 0;
    uint32_t m_SYXExitSeqTick = 0;
    bool     m_SYXExitSeqArmed = false;
    bool     m_UtilitySecondPressPending = false;
    
    // --- UTILITY sequence (ON/OFF/ON/OFF) ---
    uint8_t  m_UtilitySeqState = 0;   // 0=idle, 1=ON1, 2=OFF1, 3=ON2, 4=OFF2
    uint8_t  m_UtilitySeqFrames = 0;  // frame counter per step

    // --- Temporary DATA block ---
    bool     m_BlockDataInput = false;
    unsigned m_BlockDataUntilTick = 0;
    
    // NEW: block DATA until it is released (debounce)
    bool     m_BlockDataUntilRelease = false;

    // --- SR block (optional) ---
    bool m_BlockSRReentry = false;
    unsigned m_BlockSRReentryUntilTick = 0;

    void RenderSRMenu();
    
    // ===== PLAY MODE TRACKING =====
    EPlayMode m_CurrentPlayMode = EPlayMode::Unknown;
    EPlayMode m_BaseModeBeforeMenu = EPlayMode::Unknown;

    bool     m_bWasInFwMenu = false;
    unsigned m_LastPlayModeDetectTick = 0;

    // Helpers (declarations only, implemented later)
    EPlayMode DetectPlayModeFromLCD() const;
    bool      IsFirmwareMenuScreen() const;
    void      PlayModeTrackingTick();
    // ==============================
    
    // --- PATCH/PERF sequence (corrective toggle) ---
    uint8_t  m_PatchPerfSeqState  = 0;   // 0=idle, 1=ON, 2=OFF
    uint8_t  m_PatchPerfSeqFrames = 0;   // frames per step
    
};
#endif

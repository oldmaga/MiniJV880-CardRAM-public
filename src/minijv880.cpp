//
// minidexed.cpp
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
#include "minijv880.h"
#include "userinterface.h"
#include <assert.h>
#include <circle/devicenameservice.h>
#include <circle/gpiopin.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/usb/usbmidihost.h>
#include <stdio.h>
#include <string.h>
#include <cstring>   // memcpy / memmove
#include <algorithm>
#include <string>
#include <vector>
#include <cstdlib>
#include "debug_tx.h"

// ===== GLOBAL SR STORAGE =====
static std::vector<uint8_t*> g_SRData;
static std::vector<std::string> g_SRNames;
static const char* kSpecialSR_RD500 = "RD-500";
// =============================

SRScanResult g_SRScanResult;

static void SR_SetOverlayError(const char* msg, SRScanCode code)
{
    g_SRScanResult.code = code;
    strncpy(g_SRScanResult.shortMsg, msg, sizeof(g_SRScanResult.shortMsg)-1);
    g_SRScanResult.shortMsg[sizeof(g_SRScanResult.shortMsg)-1] = 0;
}

CMiniJV880 *CMiniJV880::s_pThis = 0;

// ------------------------------------------------------------
// CARDRAM active file selection state
// ------------------------------------------------------------
//
// Default/legacy behavior:
//
//   SD:/jv880_cardram.bin
//
// Future collection behavior:
//
//   SD:/CARD-RAM/current.txt
//   SD:/CARD-RAM/<selected-card>.bin
//
// At this step these values are only declared and initialized to
// the current legacy behavior. No functional change is introduced.
//
static const char kLegacyCardRamPath[]    = "jv880_cardram.bin";
static const char kLegacyCardRamTmpPath[] = "jv880_cardram.tmp";

static const char kCardRamDir[]           = "CARD-RAM";
static const char kCardRamCurrentPath[]   = "CARD-RAM/current.txt";

static char g_CardRamActivePath[128]      = "jv880_cardram.bin";
static char g_CardRamTmpPath[132]         = "jv880_cardram.tmp";
static bool g_CardRamUsingCollection      = false;

static bool CardRamHasPathSeparator(const char* s)
{
    if (!s)
        return true;

    for (; *s; ++s)
    {
        if (*s == '/' || *s == '\\')
            return true;
    }

    return false;
}

static bool CardRamIsDotName(const char* s)
{
    if (!s)
        return true;

    return strcmp(s, ".") == 0 || strcmp(s, "..") == 0;
}

static char CardRamLowerASCII(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');

    return c;
}

static bool CardRamEndsWithBinCI(const char* name)
{
    if (!name)
        return false;

    size_t len = strlen(name);

    // Require at least one character before ".bin".
    if (len < 5)
        return false;

    const char* ext = name + len - 4;

    return ext[0] == '.'
        && CardRamLowerASCII(ext[1]) == 'b'
        && CardRamLowerASCII(ext[2]) == 'i'
        && CardRamLowerASCII(ext[3]) == 'n';
}

static bool CardRamIsValidCardFileName(const char* name)
{
    if (!name || name[0] == '\0')
        return false;

    if (CardRamIsDotName(name))
        return false;

    if (CardRamHasPathSeparator(name))
        return false;

    if (!CardRamEndsWithBinCI(name))
        return false;

    size_t nameLen = strlen(name);

    // Future collection path:
    //   CARD-RAM/<name>
    if (strlen(kCardRamDir) + 1 + nameLen >= sizeof(g_CardRamActivePath))
        return false;

    // Future temp path:
    //   CARD-RAM/<name>.tmp
    if (strlen(kCardRamDir) + 1 + nameLen + 4 >= sizeof(g_CardRamTmpPath))
        return false;

    return true;
}

static void CardRamSetLegacyActivePath(const char* reason)
{
    strncpy(g_CardRamActivePath, kLegacyCardRamPath, sizeof(g_CardRamActivePath) - 1);
    g_CardRamActivePath[sizeof(g_CardRamActivePath) - 1] = '\0';

    strncpy(g_CardRamTmpPath, kLegacyCardRamTmpPath, sizeof(g_CardRamTmpPath) - 1);
    g_CardRamTmpPath[sizeof(g_CardRamTmpPath) - 1] = '\0';

    g_CardRamUsingCollection = false;

    if (reason && reason[0] != '\0')
    {
        DebugTX::WriteString(reason);
        DebugTX::WriteString("\r\n");
    }

    DebugTX::WriteString("CARDRAM SELECT: using legacy jv880_cardram.bin\r\n");
}

static void CardRamTrimCurrentName(char* s)
{
    if (!s)
        return;

    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;

    if (start != s)
        memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 &&
           (s[len - 1] == ' ' ||
            s[len - 1] == '\t' ||
            s[len - 1] == '\r' ||
            s[len - 1] == '\n'))
    {
        s[len - 1] = '\0';
        --len;
    }
}

static bool CardRamBuildCollectionPaths(const char* name)
{
    if (!CardRamIsValidCardFileName(name))
        return false;

    int n = snprintf(g_CardRamActivePath,
                     sizeof(g_CardRamActivePath),
                     "%s/%s",
                     kCardRamDir,
                     name);

    if (n < 0 || (size_t)n >= sizeof(g_CardRamActivePath))
        return false;

    n = snprintf(g_CardRamTmpPath,
                 sizeof(g_CardRamTmpPath),
                 "%s.tmp",
                 g_CardRamActivePath);

    if (n < 0 || (size_t)n >= sizeof(g_CardRamTmpPath))
        return false;

    g_CardRamUsingCollection = true;
    return true;
}

static void ResolveActiveCardRamPath()
{
    CardRamSetLegacyActivePath("");

    FIL f;
    FRESULT fr = f_open(&f, kCardRamCurrentPath, FA_READ | FA_OPEN_EXISTING);

    if (fr != FR_OK)
    {
        DebugTX::WriteString("CARDRAM SELECT: current.txt not found\r\n");
        return;
    }

    char selectedName[96];
    memset(selectedName, 0, sizeof(selectedName));

    UINT br = 0;
    fr = f_read(&f, selectedName, sizeof(selectedName) - 1, &br);

    FRESULT frClose = f_close(&f);

    if (fr == FR_OK && frClose != FR_OK)
        fr = frClose;

    if (fr != FR_OK)
    {
        DebugTX::WriteString("CARDRAM SELECT: current.txt read error\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after read error");
        return;
    }

    selectedName[sizeof(selectedName) - 1] = '\0';
    CardRamTrimCurrentName(selectedName);

    if (!CardRamIsValidCardFileName(selectedName))
    {
        DebugTX::WriteString("CARDRAM SELECT: invalid current.txt value\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after invalid name");
        return;
    }

    char candidatePath[128];
    int n = snprintf(candidatePath,
                     sizeof(candidatePath),
                     "%s/%s",
                     kCardRamDir,
                     selectedName);

    if (n < 0 || (size_t)n >= sizeof(candidatePath))
    {
        DebugTX::WriteString("CARDRAM SELECT: selected path too long\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after long path");
        return;
    }

    FILINFO info;
    fr = f_stat(candidatePath, &info);

    if (fr != FR_OK)
    {
        DebugTX::WriteString("CARDRAM SELECT: selected card not found\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after missing card");
        return;
    }

    if (info.fattrib & AM_DIR)
    {
        DebugTX::WriteString("CARDRAM SELECT: selected card is a directory\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after directory target");
        return;
    }

    if (info.fsize != CARDRAM_SIZE)
    {
        char b[128];
        snprintf(b, sizeof(b),
                 "CARDRAM SELECT: selected card wrong size=%lu\r\n",
                 (unsigned long)info.fsize);
        DebugTX::WriteString(b);

        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after wrong size");
        return;
    }

    if (!CardRamBuildCollectionPaths(selectedName))
    {
        DebugTX::WriteString("CARDRAM SELECT: cannot build collection path\r\n");
        CardRamSetLegacyActivePath("CARDRAM SELECT: fallback after path build error");
        return;
    }

    DebugTX::WriteString("CARDRAM SELECT: using collection card ");
    DebugTX::WriteString(g_CardRamActivePath);
    DebugTX::WriteString("\r\n");
}

extern "C" const char* MiniJV880_GetCardRamActivePath(void)
{
    return g_CardRamActivePath;
}

extern "C" const char* MiniJV880_GetCardRamTmpPath(void)
{
    return g_CardRamTmpPath;
}

extern "C" int MiniJV880_GetCardRamUsingCollection(void)
{
    return g_CardRamUsingCollection ? 1 : 0;
}

extern "C" const char* MiniJV880_GetCardRamCollectionDir(void)
{
    return kCardRamDir;
}

extern "C" const char* MiniJV880_GetCardRamCurrentPath(void)
{
    return kCardRamCurrentPath;
}

extern "C" const char* MiniJV880_GetCardRamLegacyPath(void)
{
    return kLegacyCardRamPath;
}

extern "C" void MiniJV880_FlushCardRamIfNeededNow(void)
{
    CMiniJV880 *pThis = CMiniJV880::GetInstance();

    if (pThis == 0)
    {
        DebugTX::WriteString("CARDRAM FLUSH: MiniJV880 instance missing\r\n");
        return;
    }

    DebugTX::WriteString("CARDRAM FLUSH: manual flush requested\r\n");
    pThis->FlushCardRAMIfNeeded();
}

extern "C" void MiniJV880_ShowKernelRebootMessage(void)
{
    if (CMiniJV880::GetInstance() != 0)
    {
        CMiniJV880::GetInstance()->ShowKernelRebootMessage();
    }
}

LOGMODULE("minijv880");

CMiniJV880::CMiniJV880(CConfig *pConfig, CInterruptSystem *pInterrupt,
                       CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster,
                       FATFS *pFileSystem, CScreenDevice *mScreenUnbuffered)
    : CMultiCoreSupport(CMemorySystem::Get()), m_pConfig(pConfig),
      m_pFileSystem(pFileSystem), 
      m_Serial(pInterrupt, TRUE),
      m_pSoundDevice(0),
      screenUnbuffered(mScreenUnbuffered),
      m_bChannelsSwapped(pConfig->GetChannelsSwapped()),
      m_UI(this, pGPIOManager, pI2CMaster, pSPIMaster, pConfig),
      m_lastTick(0),
      m_lastTick1(0) {
  
      assert(m_pConfig);

      s_pThis = this;

      __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELAXED);

  // select the sound device
  const char *pDeviceName = pConfig->GetSoundDevice();
  if (strcmp(pDeviceName, "i2s") == 0) {
    LOGNOTE("I2S mode");
    m_pSoundDevice = new CI2SSoundBaseDevice(
        pInterrupt, 32000, pConfig->GetChunkSize(), false, pI2CMaster,
        pConfig->GetDACI2CAddress(), CI2SSoundBaseDevice::DeviceModeTXOnly,
        2); // 2 channels - L+R
  } else if (strcmp(pDeviceName, "hdmi") == 0) {
#if RASPPI == 5
    LOGNOTE("HDMI mode NOT supported on RPI 5.");
#else
    LOGNOTE("HDMI mode");

    m_pSoundDevice =
        new CHDMISoundBaseDevice(pInterrupt, 32000, pConfig->GetChunkSize());

    // The channels are swapped by default in the HDMI sound driver.
    // TODO: Remove this line, when this has been fixed in the driver.
    m_bChannelsSwapped = !m_bChannelsSwapped;
#endif
  } else {
    LOGNOTE("PWM mode");

    m_pSoundDevice =
        new CPWMSoundBaseDevice(pInterrupt, 32000, pConfig->GetChunkSize());
  }
};


static std::string MakeSRDisplayName(const std::string& filename)
{
    std::string name = filename;

    // 1) remove .bin extension
    if (name.size() > 4 &&
        name.substr(name.size() - 4) == ".bin")
    {
        name = name.substr(0, name.size() - 4);
    }

    std::string number = "??";
    std::string title;

    const std::string prefix = "SR-JV80-";

    // 2) check the expected filename format
    if (name.rfind(prefix, 0) == 0 && name.size() > prefix.size() + 2)
    {
        number = name.substr(prefix.size(), 2);

        if (name.size() > prefix.size() + 3)
            title = name.substr(prefix.size() + 3);
    }
    else
    {
        // fallback
        title = name;
    }

    // 3) replace underscores with spaces
    for (auto& c : title)
    {
        if (c == '_')
            c = ' ';
    }

    // 4) limit title to 10 characters (16 total - 6 prefix)
    const size_t MAX_TITLE = 10;

    if (title.size() > MAX_TITLE)
        title = title.substr(0, MAX_TITLE);

    // 5) build final string
    return "SR-" + number + "-" + title;
}

static void RD500_CopyName(char* dst, size_t dstSize, const uint8_t* src, size_t srcLen)
{
    if (!dst || dstSize == 0)
        return;

    size_t out = 0;
    for (size_t i = 0; i < srcLen && out + 1 < dstSize; ++i)
    {
        char c = (char)src[i];
        if (c == 0)
            break;

        if ((unsigned char)c < 32 || (unsigned char)c > 126)
            break;

        dst[out++] = c;
    }

    // trim trailing spaces
    while (out > 0 && dst[out - 1] == ' ')
        --out;

    dst[out] = 0;
}

void CMiniJV880::SetRD500Status(const char* msg)
{
    if (!msg)
        msg = "RD-500 error";

    strncpy(m_szRD500Status, msg, sizeof(m_szRD500Status) - 1);
    m_szRD500Status[sizeof(m_szRD500Status) - 1] = 0;
}

void CMiniJV880::ShowKernelRebootMessage()
{
    memset(mcu.lcd.LCD_Data, ' ', 80);

    const char* l1 = "Network reboot";
    const char* l2 = "   Please wait...";

    memcpy(&mcu.lcd.LCD_Data[0],  l1, strlen(l1));
    memcpy(&mcu.lcd.LCD_Data[40], l2, strlen(l2));

    m_bForceLCDRefresh = true;
    mcu.mcu_button_pressed = 0;

    DebugTX::WriteString("LCD: reboot message\r\n");
}

const char* CMiniJV880::GetRD500PatchName(int absoluteIndex) const
{
    if (absoluteIndex < 0 || absoluteIndex >= RD500_PATCH_COUNT)
        return "Invalid patch";

    if (m_szRD500PatchNames[absoluteIndex][0] == 0)
        return "Unnamed patch";

    return m_szRD500PatchNames[absoluteIndex];
}

bool CMiniJV880::EnsureRD500ResourcesLoaded()
{
    if (m_bRD500Available && m_pRD500Expansion && m_pRD500Patches)
        return true;

    // reset state
    m_bRD500Available = false;
    memset(m_szRD500PatchNames, 0, sizeof(m_szRD500PatchNames));

    if (m_pRD500Expansion)
    {
        free(m_pRD500Expansion);
        m_pRD500Expansion = nullptr;
    }

    if (m_pRD500Patches)
    {
        free(m_pRD500Patches);
        m_pRD500Patches = nullptr;
    }

    const char* expPath = "RD-500/rd500_expansion.bin";
    const char* patPath = "RD-500/rd500_patches.bin";

    FIL f;
    UINT br = 0;

    // ----- expansion ROM -----
    m_pRD500Expansion = (uint8_t*)malloc(0x800000);
    if (!m_pRD500Expansion)
    {
        SetRD500Status("RD-500 no RAM");
        return false;
    }

    if (f_open(&f, expPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        SetRD500Status("Expansion missing");
        free(m_pRD500Expansion);
        m_pRD500Expansion = nullptr;
        return false;
    }

    br = 0;
    f_read(&f, m_pRD500Expansion, 0x800000, &br);
    f_close(&f);

    if (br != 0x800000)
    {
        SetRD500Status("Expansion invalid");
        free(m_pRD500Expansion);
        m_pRD500Expansion = nullptr;
        return false;
    }

    // ----- patch ROM -----
    m_pRD500Patches = (uint8_t*)malloc(0x20000);
    if (!m_pRD500Patches)
    {
        SetRD500Status("RD-500 no RAM");
        free(m_pRD500Expansion);
        m_pRD500Expansion = nullptr;
        return false;
    }

    if (f_open(&f, patPath, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        SetRD500Status("Patch data missing");
        free(m_pRD500Expansion);
        free(m_pRD500Patches);
        m_pRD500Expansion = nullptr;
        m_pRD500Patches = nullptr;
        return false;
    }

    br = 0;
    f_read(&f, m_pRD500Patches, 0x20000, &br);
    f_close(&f);

    if (br != 0x20000)
    {
        SetRD500Status("Patch data invalid");
        free(m_pRD500Expansion);
        free(m_pRD500Patches);
        m_pRD500Expansion = nullptr;
        m_pRD500Patches = nullptr;
        return false;
    }

    // Patch blocks: A/B/C = 3 x 64 patches
    const uint32_t bankBase[3] = { 0x0CE0, 0x8370, 0x12B82 };
    const uint32_t patchSize = 0x16A;
    const uint32_t patchNameLen = 12;

    for (int bank = 0; bank < 3; ++bank)
    {
        for (int i = 0; i < 64; ++i)
        {
            int absoluteIndex = bank * 64 + i;
            uint32_t off = bankBase[bank] + (uint32_t)i * patchSize;

            if (off + patchNameLen <= 0x20000)
            {
                RD500_CopyName(m_szRD500PatchNames[absoluteIndex],
                               RD500_PATCH_NAME_LEN,
                               m_pRD500Patches + off,
                               patchNameLen);
            }

            if (m_szRD500PatchNames[absoluteIndex][0] == 0)
            {
                snprintf(m_szRD500PatchNames[absoluteIndex],
                         RD500_PATCH_NAME_LEN,
                         "Patch %03d",
                         absoluteIndex + 1);
            }
        }
    }

    SetRD500Status("RD-500 ready");
    m_bRD500Available = true;

    DebugTX::WriteString("RD500: resources loaded\r\n");
    return true;
}

bool CMiniJV880::Initialize(void) {
  assert(m_pConfig);
  assert(m_pSoundDevice);

  n_mMCUcycles = m_pConfig->GetMCUcycles ();
  LOGNOTE("MCU cycles %d", n_mMCUcycles);

  m_nSYXMaxFiles = (int) m_pConfig->GetSYXMaxFiles();
  if (m_nSYXMaxFiles <= 0)
      m_nSYXMaxFiles = DEFAULT_SYX_FILES;
  if (m_nSYXMaxFiles > MAX_SYX_FILES)
      m_nSYXMaxFiles = MAX_SYX_FILES;

  LOGNOTE("SYX max files %d", m_nSYXMaxFiles);

  if (!m_UI.Initialize ())
  {
    LOGERR("Failed to initialize UI");
    return false;
  }

  assert (m_pConfig);
  if (!m_Serial.Initialize(m_pConfig->GetMIDIBaudRate ())) 
  {
      LOGERR("Failed to initialize Serial MIDI");
      return false;
  }
  unsigned ser_options = m_Serial.GetOptions();
  ser_options &= ~(SERIAL_OPTION_ONLCR);
  m_Serial.SetOptions(ser_options);
  LOGNOTE("Serial MIDI Initialized");

  LOGNOTE("Loading emu files");

// ===== LOAD ALL SR FROM /roms =====
DBG("=== SR LOAD START ===");

g_SRScanResult.code = SRScanCode::Ok;
strcpy(g_SRScanResult.shortMsg, "SR ready");

uint32_t srBinCandidates = 0;   // how many .bin files we found in roms/
uint32_t srLoadedOk = 0;        // how many valid SRs (correct size) were loaded

DIR dir;
FILINFO fno;

FRESULT fr = f_opendir(&dir, "roms");
if (fr == FR_OK)
{
    while (true)
    {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
            break;

        if (!(fno.fattrib & AM_DIR))
        {
            std::string filename = fno.fname;

            if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".bin")
            {
                srBinCandidates++;
                
                char path[256];
                sprintf(path, "roms/%s", fno.fname);

                FIL file;
                unsigned int br = 0;

                uint8_t* buffer = (uint8_t*)malloc(0x800000);
                if (!buffer)
                    continue;

                if (f_open(&file, path, FA_READ | FA_OPEN_EXISTING) == FR_OK)
                {
                    f_read(&file, buffer, 0x800000, &br);
                    f_close(&file);

                    if (br == 0x800000)
                    {
                        g_SRData.push_back(buffer);
                        g_SRNames.push_back(MakeSRDisplayName(filename));
                        srLoadedOk++;
                        
                        // 🔹 Debug log
                        std::string srMsg = "SR name: " + g_SRNames.back();
                        DBG(srMsg.c_str());
                        DBG_HEX("Loaded SR slot", g_SRNames.size() - 1);
                    }
                    else
                    {
                        free(buffer);
                        std::string errMsg = "Skipping SR (wrong size): " + filename;
                        DBG(errMsg.c_str());
                    }
                }
                else
                {
                    free(buffer);
                    std::string errMsg = "Cannot open SR file: " + filename;
                    DBG(errMsg.c_str());
                }
            }
        }
    }

    f_closedir(&dir);  
}

else
{
    g_SRScanResult.code = SRScanCode::ErrRomsDirMissingOrUnreadable;
    strcpy(g_SRScanResult.shortMsg, "SR roms not found");
}


DBG_HEX("Total SR slots", g_SRNames.size());
DBG("=== SR LOAD END ===");

// If we did not load any valid SR
if (g_SRScanResult.code == SRScanCode::Ok)
{
    if (srLoadedOk == 0)
    {
        if (srBinCandidates == 0)
        {
            g_SRScanResult.code = SRScanCode::ErrNoSRFound;
            strcpy(g_SRScanResult.shortMsg, "No SR found");
        }
        else
        {
            g_SRScanResult.code = SRScanCode::ErrNoValidSR;
            strcpy(g_SRScanResult.shortMsg, "No valid SR");
        }
    }
}

  // ===== SORT SR =====
  struct SRItem
  {
      int number;
      std::string name;
      uint8_t* data;
  };

  std::vector<SRItem> temp;

  for (size_t i = 0; i < g_SRNames.size(); ++i)
  {
      std::string n = g_SRNames[i];
      int number = 0;

      if (n.size() >= 5)
          number = std::atoi(n.substr(3, 2).c_str());

      temp.push_back({number, g_SRNames[i], g_SRData[i]});
  }

  std::sort(temp.begin(), temp.end(),
      [](const SRItem& a, const SRItem& b)
      {
          return a.number < b.number;
      });

  g_SRNames.clear();
  g_SRData.clear();

  for (const auto& item : temp)
  {
      g_SRNames.push_back(item.name);
      g_SRData.push_back(item.data);
  }

  DBG("=== SR LOAD COMPLETE ===");
  DBG_HEX("Total SR slots", g_SRNames.size());

  // ===== LOAD CORE ROMS =====
  uint8_t *rom1 = (uint8_t *)malloc(ROM1_SIZE);
  uint8_t *rom2 = (uint8_t *)malloc(ROM2_SIZE);
  uint8_t *nvram = (uint8_t *)malloc(NVRAM_SIZE);
  uint8_t *pcm1 = (uint8_t *)malloc(0x200000);
  uint8_t *pcm2 = (uint8_t *)malloc(0x200000);
  uint8_t *pcm_exp = (uint8_t *)malloc(0x800000);

  FIL f;
  unsigned int nBytesRead = 0;

  // ROM1
  if (f_open(&f, "jv880_rom1.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
      LOGERR("Cannot open jv880_rom1.bin");
      return false;
  }
  f_read(&f, rom1, ROM1_SIZE, &nBytesRead);
  f_close(&f);

  // ROM2
  if (f_open(&f, "jv880_rom2.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
      LOGERR("Cannot open jv880_rom2.bin");
      return false;
  }
  f_read(&f, rom2, ROM2_SIZE, &nBytesRead);
  f_close(&f);

  // NVRAM
  if (f_open(&f, "jv880_nvram.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
      LOGERR("Cannot open jv880_nvram.bin");
      return false;
  }
  f_read(&f, nvram, NVRAM_SIZE, &nBytesRead);
  f_close(&f);

  // PCM1
  if (f_open(&f, "jv880_waverom1.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
      LOGERR("Cannot open jv880_waverom1.bin");
      return false;
  }
  f_read(&f, pcm1, 0x200000, &nBytesRead);
  f_close(&f);

  // PCM2
  if (f_open(&f, "jv880_waverom2.bin", FA_READ | FA_OPEN_EXISTING) != FR_OK) {
      LOGERR("Cannot open jv880_waverom2.bin");
      return false;
  }
  f_read(&f, pcm2, 0x200000, &nBytesRead);
  f_close(&f);
  
  // ============================================================
  // CARDRAM (JV880 memory card emulation) - LOAD ONLY (no SD write here)
  // Active file is resolved before load.
  // Legacy fallback:
  //   SD:/jv880_cardram.bin
  // Future collection:
  //   SD:/CARD-RAM/<selected-card>.bin
  // Actual persistence is delegated to FlushCardRAMIfNeeded() (atomic).
  // ============================================================
  {
      ResolveActiveCardRamPath();

      const char* cardFile = g_CardRamActivePath;

      DebugTX::WriteString("CARDRAM: active file=");
      DebugTX::WriteString(cardFile);
      DebugTX::WriteString("\r\n");

      auto TX_FR = [](const char* tag, FRESULT fr) {
          char b[96];
          snprintf(b, sizeof(b), "%s%d\r\n", tag, (int)fr);
          DebugTX::WriteString(b);
      };
      auto TX_U32 = [](const char* tag, uint32_t v) {
          char b[96];
          snprintf(b, sizeof(b), "%s%lu\r\n", tag, (unsigned long)v);
          DebugTX::WriteString(b);
      };

      auto crc32_ieee = [](const uint8_t* data, UINT len) -> uint32_t {
          static bool table_init = false;
          static uint32_t table[256];

          if (!table_init)
          {
              for (uint32_t i = 0; i < 256; ++i)
              {
                  uint32_t c = i;
                  for (int k = 0; k < 8; ++k)
                      c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                  table[i] = c;
              }
              table_init = true;
          }

          uint32_t crc = 0xFFFFFFFFu;
          for (UINT i = 0; i < len; ++i)
              crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

          return crc ^ 0xFFFFFFFFu;
      };

      auto CardMetricsTX = [&](const char* tag) {
          uint32_t nonFF = 0;
          for (uint32_t i = 0; i < (uint32_t)CARDRAM_SIZE; ++i)
              if (((uint8_t*)mcu.cardram)[i] != 0xFF) nonFF++;

          uint32_t crc = crc32_ieee((const uint8_t*)mcu.cardram, (UINT)CARDRAM_SIZE);

          DebugTX::WriteString(tag);
          TX_U32("  nonFF=", nonFF);

          char bb[96];
          snprintf(bb, sizeof(bb), "  crc32=%08lX\r\n", (unsigned long)crc);
          DebugTX::WriteString(bb);
      };

      // Default: card "blank" in RAM
      memset(mcu.cardram, 0xFF, CARDRAM_SIZE);

      FIL fCard;
      UINT br = 0;

      FRESULT fr = f_open(&fCard, cardFile, FA_READ | FA_OPEN_EXISTING);
      TX_FR("CARDRAM: f_open fr=", fr);

      if (fr == FR_OK)
      {
          fr = f_read(&fCard, mcu.cardram, CARDRAM_SIZE, &br);
          TX_FR("CARDRAM: f_read fr=", fr);
          TX_U32("CARDRAM: bytes_read=", (uint32_t)br);

          FRESULT frClose = f_close(&fCard);
          TX_FR("CARDRAM: f_close fr=", frClose);

          if (fr == FR_OK && frClose != FR_OK) fr = frClose;

          if (fr == FR_OK && br == CARDRAM_SIZE)
          {
              DebugTX::WriteString("CARDRAM: loaded into RAM\r\n");
              CardMetricsTX("CARDRAM: metrics (after load)\r\n");
              
          }
          else
          {
              DebugTX::WriteString("CARDRAM: invalid file -> keep blank RAM\r\n");
              memset(mcu.cardram, 0xFF, CARDRAM_SIZE);
              CardMetricsTX("CARDRAM: metrics (blank)\r\n");
          }
      }
      else
      {
          DebugTX::WriteString("CARDRAM: file not found -> keep blank RAM\r\n");
          CardMetricsTX("CARDRAM: metrics (blank)\r\n");
      }

  }  //-----------------END CARDRAM
  
  // Expansion SR
  bool exp_present = false;

  if (f_open(&f, "sr.bin", FA_READ | FA_OPEN_EXISTING) == FR_OK) {
      f_read(&f, pcm_exp, 0x800000, &nBytesRead);
      f_close(&f);

      if (nBytesRead == 0x800000) {
          LOGNOTE("Expansion SR loaded");
          exp_present = true;
      } else {
          LOGERR("Expansion SR wrong size");
      }
  } else {
      LOGNOTE("No expansion SR found");
  }

  LOGNOTE("Emu files loaded");

  int ret = mcu.startSC55(
      rom1,
      rom2,
      pcm1,
      pcm2,
      exp_present ? pcm_exp : nullptr,
      nvram
  );

  LOGNOTE("startSC55 returned: %d", ret);

  free(rom1);
  free(rom2);
  free(nvram);
  free(pcm1);
  free(pcm2);
  free(pcm_exp);

  // setup and start the sound device
  int Channels = 2; // 16-bit Stereo
  if (!m_pSoundDevice->AllocateQueueFrames(2 * m_pConfig->GetChunkSize() / Channels)) {
      LOGERR("Cannot allocate sound queue");
      return false;
  }

  m_pSoundDevice->SetWriteFormat(SoundFormatSigned16, Channels);
  m_nQueueSizeFrames = m_pSoundDevice->GetQueueSizeFrames();
  m_pSoundDevice->Start();

  CMultiCoreSupport::Initialize();
  DebugTX::WriteString("SYX TEST: autoload disabled\r\n");
  LOGNOTE("initialised");
  
  return true;
}

static constexpr uint32_t BTN_TONE_SELECT_MASK = 0x00000004; // TONE SELECT = bit 2
static constexpr uint32_t BTN_TONE_SW1_MASK    = 0x00000008; // shared TONE SW1 / MUTE = bit 3
static constexpr uint32_t BTN_DATA_MASK        = 0x00000010; // DATA = bit 4
static constexpr uint32_t BTN_TONE_SW2_MASK    = 0x00000020; // shared TONE SW2 / MONITOR = bit 5
static constexpr uint32_t BTN_TONE_SW3_MASK    = 0x00000040; // shared TONE SW3 / COMPARE = bit 6
static constexpr uint32_t BTN_ENTER_MASK       = 0x00000080; // shared TONE SW4 / ENTER = bit 7
static constexpr uint32_t BTN_UTILITY_MASK     = 0x00000100; // UTILITY = bit 8
static constexpr uint32_t BTN_PREVIEW_MASK     = 0x00000200; // PREVIEW = bit 9
static constexpr uint32_t BTN_PATCH_PERF_MASK = 0x00000400; // PATCH/PERF

// Persistent MIDI-held button bits.
// This is intentionally separate from GPIO raw masks, DATA/SR/SYX handling
// and the one-frame injected sequence mask.
static uint32_t g_MIDIHeldButtonMask = 0;

// One-shot MIDI ENTER-long command support.
// A dedicated MIDI CC can request a timed ENTER hold without exposing
// ENTER DOWN/UP as user-facing commands.
static uint32_t g_MIDIEnterLongHoldUntilTick = 0;

// Short timed ENTER tap used only by local overlays when needed.
static uint32_t g_MIDIEnterTapHoldUntilTick = 0;

static void ClearMIDIHeldButtonsAndTimers()
{
    __atomic_store_n(&g_MIDIHeldButtonMask, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_MIDIEnterLongHoldUntilTick, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&g_MIDIEnterTapHoldUntilTick, 0u, __ATOMIC_RELAXED);
}

static uint32_t GetMIDIHeldButtonMaskWithTimedEnter()
{
    uint32_t mask = __atomic_load_n(&g_MIDIHeldButtonMask, __ATOMIC_RELAXED);
    const uint32_t now = CTimer::Get()->GetTicks();

    const uint32_t longUntilTick =
        __atomic_load_n(&g_MIDIEnterLongHoldUntilTick, __ATOMIC_RELAXED);

    if (longUntilTick != 0)
    {
        if (now < longUntilTick)
        {
            mask |= BTN_ENTER_MASK;
        }
        else
        {
            __atomic_store_n(&g_MIDIEnterLongHoldUntilTick, 0u, __ATOMIC_RELAXED);
        }
    }

    const uint32_t tapUntilTick =
        __atomic_load_n(&g_MIDIEnterTapHoldUntilTick, __ATOMIC_RELAXED);

    if (tapUntilTick != 0)
    {
        if (now < tapUntilTick)
        {
            mask |= BTN_ENTER_MASK;
        }
        else
        {
            __atomic_store_n(&g_MIDIEnterTapHoldUntilTick, 0u, __ATOMIC_RELAXED);
        }
    }

    return mask;
}


void CMiniJV880::Process(bool bPlugAndPlayUpdated)
{
    // ----------------------------------------
    /// 1. Reset injection mask
    // ----------------------------------------
    m_InjectedButtonMask = 0;

    // Lightweight heartbeat: confirms that Process() is running at runtime
    static uint32_t hb = 0;
    if ((++hb & 0x3FF) == 0)
        DebugTX::WriteString("HB Process\r\n");

    // ----------------------------------------
    // 2. Firmware UI always active
    // ----------------------------------------
    m_UI.Process();

        // --- Data card BATLOW detector (UI-local replacement only) ---
    if (!m_bSRMenuActive)
    {
        const char* hay = (const char*)mcu.lcd.LCD_Data;
        constexpr size_t hayLen = 80;
        const char* needle = "Data card battery low";
        const size_t nLen = strlen(needle);

        auto upperASCII = [](char c) -> char {
            if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
            return c;
        };

        bool found = false;
        size_t foundPos = 0;

        for (size_t i = 0; i + nLen <= hayLen; ++i)
        {
            bool ok = true;
            for (size_t j = 0; j < nLen; ++j)
            {
                if (upperASCII(hay[i + j]) != upperASCII(needle[j])) { ok = false; break; }
            }
            if (ok) { found = true; foundPos = i; break; }
        }

        if (found)
        {
            const char* msg = "DataCard: please wait";
            const size_t msgLen = strlen(msg);

            memset(&mcu.lcd.LCD_Data[foundPos], ' ', nLen);
            memcpy(&mcu.lcd.LCD_Data[foundPos], msg, (msgLen < nLen) ? msgLen : nLen);
        }
    }
    // -------------------------------------------------------

    // ----------------------------------------
    // 3. Read DATA button
    // ----------------------------------------
    static bool dataPressedLastFrame = false;

    // Patch Play DATA long-press experiment:
    // short press = SR menu, long press = pass DATA through to the JV-880 firmware.
    static bool s_dataSRShortPending = false;
    static bool s_dataLongPassThrough = false;
    static uint32_t s_dataDownTick = 0;

    static constexpr uint32_t kDataLongPressMs = 600;
    static constexpr uint32_t kDataLongPressTicks = (kDataLongPressMs + 9) / 10; // GetTicks(): ~10 ms

    bool dataPressedNow = false;
    if (m_UI.GetUIButtons())
    {
        dataPressedNow = (m_UI.GetUIButtons()->GetRawMask() & (1 << 4)); // DATA = bit 4
    }

    const bool dataPressedEdge  = (dataPressedNow && !dataPressedLastFrame);
    const bool dataReleasedEdge = (!dataPressedNow && dataPressedLastFrame);

    const bool canUseDataLongPressInPatchPlay =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        !IsFirmwareMenuScreen() &&
        (DetectPlayModeFromLCD() == EPlayMode::Patch);

    const bool canUseDataLongPressInPerformancePlay =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        !IsFirmwareMenuScreen() &&
        (DetectPlayModeFromLCD() == EPlayMode::Performance);

    const bool canUseDataLongPressInPatchWrite =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        IsPatchWriteScreen();

    const bool canUseDataLongPressInPerformanceWrite =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        IsPerformanceWriteScreen();

    const bool canUseDataLongPressInPatchCopy =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        IsPatchCopyScreen();

    const bool canUseDataLongPressInPerformanceCopy =
        !m_bSRMenuActive &&
        !m_bSYXMenuActive &&
        !m_bRD500MenuActive &&
        !m_bRD500PatchBrowseActive &&
        IsPerformanceCopyScreen();

    const bool canUseDataLongPress =
        canUseDataLongPressInPatchPlay ||
        canUseDataLongPressInPerformancePlay ||
        canUseDataLongPressInPatchWrite ||
        canUseDataLongPressInPerformanceWrite ||
        canUseDataLongPressInPatchCopy ||
        canUseDataLongPressInPerformanceCopy;

    // ----------------------------------------
    // 4. DATA handling:
    //    - SR active: DATA closes SR immediately, as before.
    //    - Patch/Performance Play and Write/Copy contexts:
    //      DATA short opens SR on release; DATA long passes through.
    //    - Other contexts: DATA opens SR immediately, as before.
    // ----------------------------------------
    if (dataPressedEdge)
    {
        if (!m_bSRMenuActive)
        {
            if (canUseDataLongPress)
            {
                s_dataSRShortPending = true;
                s_dataLongPassThrough = false;
                s_dataDownTick = CTimer::Get()->GetTicks();

            }
            else
            {
                DBG("SR_OPEN_BY_DATA_EDGE");

                // Open SR overlay here (final handling is in Process, not in the UI handler)
                m_bSRMenuActive = true;

                // optional: reset cursor/index if you want stable behavior
                // m_nSRIndex = 0;
            }
        }
        else
        {
            // CLOSE with your sequence (utility inject)
            DBG("SR_CLOSE_BY_DATA_EDGE");

            memset(&mcu.lcd.LCD_Data[0], ' ', 80);

            // block DATA until it is released (definitive debounce)
            m_BlockDataUntilRelease = true;

            m_UtilitySeqState = 1;     // ON1
            m_UtilitySeqFrames = 2;    // keep ON for 2 frames (safe)
            DBG("UTILITY_SEQ_START");

            m_bSRMenuActive = false;

            // Prevent the UI (BtnEventData) from reopening the overlay immediately
            m_BlockSRReentry = true;
            DBG("SR_REENTRY_BLOCK_ON");
        }
    }

    if (s_dataSRShortPending && dataPressedNow && !s_dataLongPassThrough)
    {
        const uint32_t heldTicks = CTimer::Get()->GetTicks() - s_dataDownTick;

        if (heldTicks >= kDataLongPressTicks)
        {
            s_dataSRShortPending = false;
            s_dataLongPassThrough = true;

        }
    }

    if (dataReleasedEdge)
    {
        if (s_dataSRShortPending)
        {
            s_dataSRShortPending = false;
            s_dataDownTick = 0;


            m_bSRMenuActive = true;
        }

        if (s_dataLongPassThrough)
        {
            s_dataLongPassThrough = false;
            s_dataDownTick = 0;

        }
    }

    dataPressedLastFrame = dataPressedNow;

    // ----------------------------------------
    // 5. UTILITY sequence (ON/OFF/ON/OFF) per frame
    // ----------------------------------------
    if (m_UtilitySeqState != 0)
    {
        // Apply mask according to the current state
        if (m_UtilitySeqState == 1 || m_UtilitySeqState == 3)
        {
            m_InjectedButtonMask |= (1 << 8); // UTILITY ON
        }
        // states 2 and 4 = UTILITY OFF (we do not set the bit)

        // Count frames and advance the state
        if (m_UtilitySeqFrames > 0)
            --m_UtilitySeqFrames;

        if (m_UtilitySeqFrames == 0)
        {
            switch (m_UtilitySeqState)
            {
                case 1: // ON1 -> OFF1
                    m_UtilitySeqState = 2;
                    m_UtilitySeqFrames = 2;
                    DBG("UTILITY_SEQ_OFF1");
                    break;

                case 2: // OFF1 -> ON2
                    m_UtilitySeqState = 3;
                    m_UtilitySeqFrames = 2;
                    DBG("UTILITY_SEQ_ON2");
                    break;

                case 3: // ON2 -> OFF2
                    m_UtilitySeqState = 4;
                    m_UtilitySeqFrames = 2;
                    DBG("UTILITY_SEQ_OFF2");
                    break;

                case 4: // done
                default:
                    m_UtilitySeqState = 0;
                    m_UtilitySeqFrames = 0;
                    DBG("UTILITY_SEQ_DONE");
                    break;
            }
        }
    }

    // ----------------------------------------
    // 5B. PATCH/PERF sequence (ON/OFF) per frame
    // ----------------------------------------
    if (m_PatchPerfSeqState != 0)
    {
        if (m_PatchPerfSeqState == 1)
        {
            if (BTN_PATCH_PERF_MASK != 0)
                m_InjectedButtonMask |= BTN_PATCH_PERF_MASK;
        }

        if (m_PatchPerfSeqFrames > 0)
            --m_PatchPerfSeqFrames;

        if (m_PatchPerfSeqFrames == 0)
        {
            if (m_PatchPerfSeqState == 1)
            {
                m_PatchPerfSeqState  = 2;   // OFF
                m_PatchPerfSeqFrames = 2;
            }
            else
            {
                m_PatchPerfSeqState  = 0;
                m_PatchPerfSeqFrames = 0;
            }
        }
    }

    // ----------------------------------------
    // 5-1. PN menu rendering and handling
    // ----------------------------------------
if (m_bSYXMenuActive)
{
    static bool s_prevEnter = false;
    static bool s_enterTracking = false;
    static bool s_enterLongFired = false;
    static uint32_t s_enterDownTick = 0;

    auto exitSYXMenuToPatchUI = [&]()
    {
        m_bSYXMenuActive = false;
        DebugTX::WriteString("SYX MENU: exit\r\n");

        memset(mcu.lcd.LCD_Data, ' ', 80);

        m_UtilitySeqState  = 1;   // ON1
        m_UtilitySeqFrames = 2;

        DebugTX::WriteString("SYX EXIT SEQ: double UTIL start\r\n");
    };

    auto resetSYXEnterState = [&]()
    {
        s_enterTracking = false;
        s_enterLongFired = false;
        s_enterDownTick = 0;
    };

    memset(mcu.lcd.LCD_Data, ' ', 80);

    const char* l1 = "SysEx Load";
    memcpy(&mcu.lcd.LCD_Data[0], l1, strlen(l1));

    if (m_nSYXFileCount > 0)
    {
       if (m_nSYXIndex < 0 || m_nSYXIndex >= m_nSYXFileCount)
          m_nSYXIndex = 0;

       const char* itemName = m_szSYXDisplay[m_nSYXIndex];

       size_t len = strlen(itemName);
       if (len > 24)
           len = 24;

       memcpy(&mcu.lcd.LCD_Data[40], itemName, len);
    }
    else
    {
       memcpy(&mcu.lcd.LCD_Data[40], "Folder empty", 12);
    }

    const uint32_t syxInputMask =
        m_UI.GetUIButtons()->GetRawMask() |
        GetMIDIHeldButtonMaskWithTimedEnter();

    bool enterNow = (syxInputMask & BTN_ENTER_MASK) != 0;

    uint32_t syxLongPressMs = m_pConfig->GetSYXMenuLongPressTimeout();
    uint32_t syxLongPressTicks = (syxLongPressMs + 9) / 10;   // ms -> 10 ms ticks
    if (syxLongPressTicks == 0)
        syxLongPressTicks = 1;

    if (m_bSYXWaitEnterRelease)
    {
        if (!enterNow)
        {
            m_bSYXWaitEnterRelease = false;
            s_prevEnter = false;
            resetSYXEnterState();
        }
        else
        {
            s_prevEnter = true;
        }
    }
    else
    {
        if (enterNow && !s_prevEnter)
        {
            s_enterTracking = true;
            s_enterLongFired = false;
            s_enterDownTick = CTimer::Get()->GetTicks();
        }

        if (s_enterTracking && enterNow && !s_enterLongFired)
        {
            uint32_t heldTicks = CTimer::Get()->GetTicks() - s_enterDownTick;

            if (heldTicks >= syxLongPressTicks)
            {
                s_enterLongFired = true;
                s_enterTracking = false;
                s_enterDownTick = 0;

                if (!m_bSYXAtRoot)
                {
                    DebugTX::WriteString("SYX LONGPRESS: back to root\r\n");

                    if (ScanSYXDirectory("PN-JV80"))
                    {
                        m_nSYXIndex = 0;
                    }
                    else
                    {
                        DebugTX::WriteString("SYX BACK: root scan failed\r\n");
                    }
                }
                else
                {
                    DebugTX::WriteString("SYX LONGPRESS: exit menu\r\n");
                    exitSYXMenuToPatchUI();
                }
            }
        }

        if (!enterNow && s_prevEnter)
        {
            if (s_enterTracking && !s_enterLongFired)
            {
                if (m_nSYXFileCount > 0)
                {
                    if (m_nSYXIndex < 0 || m_nSYXIndex >= m_nSYXFileCount)
                        m_nSYXIndex = 0;

                    char selectedPath[sizeof(m_szSYXPath[0])] = {0};

                    if (m_szSYXPath[m_nSYXIndex][0] != 0)
                    {
                        strncpy(selectedPath, m_szSYXPath[m_nSYXIndex], sizeof(selectedPath) - 1);
                        selectedPath[sizeof(selectedPath) - 1] = '\0';
                    }

                    const char* path = selectedPath[0] ? selectedPath : nullptr;

                    if (path == nullptr)
                    {
                        DebugTX::WriteString("SYX: missing path\r\n");
                    }
                    else if (m_SYXItemType[m_nSYXIndex] == SYXItemType::Folder)
                    {
                        DebugTX::WriteString("SYX ENTER FOLDER: ");
                        DebugTX::WriteString(path);
                        DebugTX::WriteString("\r\n");

                        if (ScanSYXDirectory(path))
                        {
                            m_nSYXIndex = 0;
                        }
                        else
                        {
                            DebugTX::WriteString("SYX ENTER FOLDER: scan failed\r\n");
                        }
                    }
                    else
                    {
                        DebugTX::WriteString("SYX LOAD: selected ");
                        DebugTX::WriteString(path);
                        DebugTX::WriteString("\r\n");

                        const bool loadOk = LoadSyxFromFile(path);

                        if (loadOk)
                        {
                            strncpy(m_szSYXLastLoadedDir, m_szSYXCurrentDir,
                                    sizeof(m_szSYXLastLoadedDir) - 1);
                            m_szSYXLastLoadedDir[sizeof(m_szSYXLastLoadedDir) - 1] = '\0';
                            m_nSYXLastLoadedIndex = m_nSYXIndex;
                        }

                        exitSYXMenuToPatchUI();
                    }
                }
                else
                {
                    DebugTX::WriteString("SYX LOAD: empty list\r\n");
                }
            }

            resetSYXEnterState();
        }

        s_prevEnter = enterNow;
    }

    return;
}

    // ----------------------------------------
    // 6. Draw overlay menus
    // ----------------------------------------
    if (m_bRD500PatchBrowseActive)
    {
        memset(mcu.lcd.LCD_Data, ' ', 80);

        char line1[41];
        const char* line2 = "RD-500 not ready";

        const char bankLetter = (m_nRD500BankIndex == 0) ? 'A'
                              : (m_nRD500BankIndex == 1) ? 'B'
                                                         : 'C';

        const int absolutePatch = m_nRD500BankIndex * 64 + m_nRD500PatchIndex + 1;

        snprintf(line1, sizeof(line1), "RD500 %c%03d", bankLetter, absolutePatch);

        if (m_bRD500Available)
            line2 = GetRD500PatchName(absolutePatch - 1);
        else
            line2 = m_szRD500Status;

        memcpy(&mcu.lcd.LCD_Data[0], line1, strlen(line1));

        size_t len2 = strlen(line2);
        if (len2 > 40) len2 = 40;
        memcpy(&mcu.lcd.LCD_Data[40], line2, len2);
    }
    else if (m_bRD500MenuActive)
    {
        memset(mcu.lcd.LCD_Data, ' ', 80);

        if (!m_bRD500Available)
        {
            const char* title = "RD-500 error";
            memcpy(&mcu.lcd.LCD_Data[0], title, strlen(title));

            size_t len = strlen(m_szRD500Status);
            if (len > 40) len = 40;
            memcpy(&mcu.lcd.LCD_Data[40], m_szRD500Status, len);
        }
        else
        {
            const char* title = "Select RD-500 Bank";
            memcpy(&mcu.lcd.LCD_Data[0], title, strlen(title));

            const char* bankMsg = "A 001-064";
            if (m_nRD500BankIndex == 1)
                bankMsg = "B 065-120";
            else if (m_nRD500BankIndex == 2)
                bankMsg = "C 129-192";

            memcpy(&mcu.lcd.LCD_Data[40], bankMsg, strlen(bankMsg));
        }
    }
    else if (m_bSRMenuActive)
    {
        memset(mcu.lcd.LCD_Data, ' ', 80);

        const char* title = "Select SR + ENTER";
        memcpy(&mcu.lcd.LCD_Data[0], title, strlen(title));

        const int totalItems = (int)g_SRNames.size() + 1; // +1 = RD-500

        if (totalItems > 0)
        {
            if (m_nSRIndex < 0 || m_nSRIndex >= totalItems)
                m_nSRIndex = 0;

            if (m_nSRIndex == (int)g_SRNames.size())
            {
                memcpy(&mcu.lcd.LCD_Data[40], kSpecialSR_RD500, strlen(kSpecialSR_RD500));
            }
            else if (!g_SRNames.empty())
            {
                const std::string& name = g_SRNames[m_nSRIndex];

                size_t copyLen = name.length();
                if (copyLen > 40)
                    copyLen = 40;

                memcpy(&mcu.lcd.LCD_Data[40], name.c_str(), copyLen);
            }  
            else
            {
                memcpy(&mcu.lcd.LCD_Data[40], kSpecialSR_RD500, strlen(kSpecialSR_RD500));
            }
        }
    }

    // ----------------------------------------
    // 7. Button handling (real + injected)
    // ----------------------------------------
    if (m_UI.GetUIButtons())
    {
        uint32_t realMask = m_UI.GetUIButtons()->GetRawMask();

        const uint32_t midiHeldMask =
            GetMIDIHeldButtonMaskWithTimedEnter();

        realMask |= midiHeldMask;
        
        const bool enterPressedNow = (realMask & (1 << 7)) != 0;

        const bool canOpenSyxMenu =
            !m_bSRMenuActive &&
            !m_bRD500MenuActive &&
            !m_bRD500PatchBrowseActive &&
            !IsFirmwareMenuScreen() &&
            (DetectPlayModeFromLCD() == EPlayMode::Patch);

        static bool s_enterPrev = false;
        static bool s_enterTracking = false;
        static bool s_enterLongFired = false;
        static uint32_t s_enterDownTick = 0;

        if (enterPressedNow && !s_enterPrev)
        {
            if (canOpenSyxMenu)
            {
                s_enterTracking = true;
                s_enterLongFired = false;
                s_enterDownTick = CTimer::Get()->GetTicks();
                DebugTX::WriteString("SYX CANDIDATE: ENTER down in PATCH base\r\n");
            }
            else
            {
                s_enterTracking = false;
                s_enterLongFired = false;
            }
        }

          if (s_enterTracking && enterPressedNow && !s_enterLongFired)
          {
              uint32_t heldTicks = CTimer::Get()->GetTicks() - s_enterDownTick;

              uint32_t syxLongPressMs = m_pConfig->GetSYXMenuLongPressTimeout();
              uint32_t syxLongPressTicks = (syxLongPressMs + 9) / 10;   // round up ms -> 10 ms ticks
              if (syxLongPressTicks == 0)
                  syxLongPressTicks = 1;

              if (heldTicks >= syxLongPressTicks)
              {
                s_enterLongFired = true;

                DebugTX::WriteString("SYX LONGPRESS: open menu\r\n");

                m_bSYXMenuActive = true;
                m_nSYXIndex = 0;
                m_bSYXWaitEnterRelease = true;

                const char* reopenDir =
                    (m_szSYXCurrentDir[0] != 0) ? m_szSYXCurrentDir : "PN-JV80";

                if (!ScanSYXDirectory(reopenDir))
                {
                    DebugTX::WriteString("SYX REOPEN: fallback to root\r\n");
                    ScanSYXDirectory("PN-JV80");
                }

                if (strcmp(m_szSYXCurrentDir, m_szSYXLastLoadedDir) == 0 &&
                    m_nSYXLastLoadedIndex >= 0 &&
                    m_nSYXLastLoadedIndex < m_nSYXFileCount)
                {
                    m_nSYXIndex = m_nSYXLastLoadedIndex;
                }
                else
                {
                    m_nSYXIndex = 0;
                }
             }
          }

          if (!enterPressedNow && s_enterPrev)
          {
              s_enterTracking = false;
              s_enterLongFired = false;
          }

          s_enterPrev = enterPressedNow;

          // RD-500 overlay: long ENTER = go back
          // Important: one longpress must perform only ONE back step,
          // then remain blocked until ENTER is released.
          {
              static bool s_rd500PrevEnter = false;
              static bool s_rd500EnterTracking = false;
              static uint32_t s_rd500EnterDownTick = 0;

              const bool rd500OverlayActive = m_bRD500MenuActive || m_bRD500PatchBrowseActive;

              if (rd500OverlayActive)
              {
                  // After a longpress action, ignore ENTER until it is released.
                  if (m_bRD500WaitEnterRelease)
                  {
                      if (!enterPressedNow)
                      {
                          m_bRD500WaitEnterRelease = false;
                          s_rd500PrevEnter = false;
                          s_rd500EnterTracking = false;
                          s_rd500EnterDownTick = 0;
                      }
                      else
                      {
                          s_rd500PrevEnter = true;
                      }
                  }
                  else
                  {
                      if (enterPressedNow && !s_rd500PrevEnter)
                      {
                          s_rd500EnterTracking = true;
                          s_rd500EnterDownTick = CTimer::Get()->GetTicks();
                      }

                      if (s_rd500EnterTracking && enterPressedNow)
                      {
                          uint32_t heldTicks = CTimer::Get()->GetTicks() - s_rd500EnterDownTick;

                          uint32_t rd500LongPressMs = m_pConfig->GetSYXMenuLongPressTimeout();
                          uint32_t rd500LongPressTicks = (rd500LongPressMs + 9) / 10; // ms -> 10 ms ticks
                          if (rd500LongPressTicks == 0)
                              rd500LongPressTicks = 1;

                          if (heldTicks >= rd500LongPressTicks)
                          {
                              m_bRD500WaitEnterRelease = true;
                              s_rd500EnterTracking = false;
                              s_rd500EnterDownTick = 0;

                              if (m_bRD500PatchBrowseActive)
                              {
                                  DebugTX::WriteString("RD500 LONGPRESS: browse -> bank\r\n");
                                  CloseRD500PatchBrowse();
                              }
                              else if (m_bRD500MenuActive)
                              {
                                  DebugTX::WriteString("RD500 LONGPRESS: bank -> SR\r\n");
                                  CloseRD500Menu();
                              }
                          }
                      }

                      if (!enterPressedNow && s_rd500PrevEnter)
                      {
                          s_rd500EnterTracking = false;
                          s_rd500EnterDownTick = 0;
                      }

                      s_rd500PrevEnter = enterPressedNow;
                  }
              }
              else
              {
                  // Do NOT clear m_bRD500WaitEnterRelease here.
                  // It must survive until UIButtonsEventHandler consumes
                  // the ENTER release via ConsumeBlockedRD500Enter().
                  s_rd500PrevEnter = false;
                  s_rd500EnterTracking = false;
                  s_rd500EnterDownTick = 0;
              }
          }

        if (s_dataSRShortPending)
        {
            // In DATA long-press candidate contexts, do not let the firmware
            // see DATA until the long-press threshold is reached. If the
            // button is released before that, SR opens as a short press instead.
            realMask &= ~(1 << 4);
        }

        if (m_BlockDataUntilRelease)
        {
            realMask &= ~(1 << 4); // always remove DATA

            if (!dataPressedNow)   // physically released
            {
                m_BlockDataUntilRelease = false;
                DBG("DATA_RELEASED_UNBLOCK");

                if (m_BlockSRReentry)
                {
                    m_BlockSRReentry = false;
                    DBG("SR_REENTRY_BLOCK_OFF");
                }

                if (m_BlockDataInput)
                {
                    m_BlockDataInput = false;
                    DBG("DATA_BLOCK_FORCE_OFF");
                }
            }
        }

        if (m_BlockDataInput)
        {
            if (CTimer::GetClockTicks() >= m_BlockDataUntilTick)
            {
                m_BlockDataInput = false;
                DBG("DATA_BLOCK_END");
            }
            else
            {
                realMask &= ~(1 << 4); // clear DATA bit
            }
        }

        mcu.mcu_button_pressed = realMask | m_InjectedButtonMask;
        
        // Reset ring buffer when entering "Util:Data card" (this way we capture PRE-ENTER events)
        {
            if (!m_bSRMenuActive)
            {
                const char* l1 = (const char*)mcu.lcd.LCD_Data + 0;
                const bool inDataCard = (memcmp(l1, "Util:Data card", 13) == 0);

                static bool s_prevInDataCard = false;
                s_prevInDataCard = inDataCard;
            }
        }

        static uint32_t last = 0xFFFFFFFF;
        uint32_t now = mcu.mcu_button_pressed;
        if (now != last)
        {
            {
                const bool isMenu = IsFirmwareMenuScreen();
                const EPlayMode pm = DetectPlayModeFromLCD();

                char s[64];
                snprintf(s, sizeof(s), "[PMCHK] menu=%u pm=%u\r\n",
                         (unsigned)isMenu, (unsigned)pm);
                DebugTX::WriteString(s);

                auto dump24 = [](const char* p, const char* tag)
                {
                    char line[40];
                    unsigned j = 0;
                    while (*tag && j < sizeof(line)-1) line[j++] = *tag++;
                    if (j < sizeof(line)-1) line[j++] = ':';
                    if (j < sizeof(line)-1) line[j++] = ' ';

                    for (unsigned i = 0; i < 24 && j < sizeof(line)-3; ++i)
                    {
                        char c = p[i];
                        if (c < 32 || c > 126) c = '.';
                        line[j++] = c;
                    }
                    line[j++] = '\r';
                    line[j++] = '\n';
                    line[j] = 0;
                    DebugTX::WriteString(line);
                };

                dump24((const char*)mcu.lcd.LCD_Data + 0,  "LCD1");
                dump24((const char*)mcu.lcd.LCD_Data + 40, "LCD2");
            }

            last = now;
            DBG_HEX("BTN", now);
            DBG_HEX("REAL", realMask);
            DBG_HEX("INJ", m_InjectedButtonMask);
        }
    } // <-- chiude if (m_UI.GetUIButtons())

    PlayModeTrackingTick();
    
    {
        static uint32_t s_flush_throttle = 0;
        if ((++s_flush_throttle & 0x3FF) == 0)
        {
            FlushCardRAMIfNeeded();
        }
    }

    // ----------------------------------------
    // 8. USB MIDI
    // ----------------------------------------
    if (!bPlugAndPlayUpdated)
        return;

    if (m_pMIDIDevice == 0)
    {
        m_pMIDIDevice =
            (CUSBMIDIDevice *)CDeviceNameService::Get()->GetDevice("umidi1", FALSE);
        if (m_pMIDIDevice != 0)
        {
            m_pMIDIDevice->RegisterPacketHandler(USBMIDIMessageHandler);
            m_pMIDIDevice->RegisterRemovedHandler(DeviceRemovedHandler, this);
        }
    }
}

void CMiniJV880::USBMIDIMessageHandler(unsigned nCable, u8 *pPacket,
                                       unsigned nLength) {
  // LOGERR("CMiniJV880::USBMIDIMessageHandler");
  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(s_pThis);
  if (!pPacket || nLength == 0) return;
  ParseMIDIData(pThis, pPacket, nLength);
  //pThis->mcu.postMidiSC55(pPacket, nLength);
}


void CMiniJV880::ParseMIDIData(CMiniJV880* pThis, const u8* pData, unsigned nLength)
{
    for (unsigned i = 0; i < nLength; i++)
    {
        u8 status = pData[i];

        if ((status & 0xF0) == 0xB0 && i + 2 < nLength) 
        {
            u8 ccNumber = pData[i + 1];
            u8 ccValue  = pData[i + 2];

            if (pThis->m_UI.m_nMIDIButtonChannel != 0) 
            {
                u8 channel         = status & 0x0F;
                u8 expectedChannel = pThis->m_UI.m_nMIDIButtonChannel - 1;

                // OMNI (17) 
                if (pThis->m_UI.m_nMIDIButtonChannel == 17 || expectedChannel == channel) 
                {
                    auto handleSimpleInjectedButton = [&](u8 confCC, uint32_t mask) {
                        if (confCC != 0 && ccNumber == confCC) {
                            if (ccValue < 64) {
                                pThis->m_InjectedButtonMask |= mask;
                            } else {
                                pThis->m_InjectedButtonMask &= ~mask;
                            }
                            i += 2;
                            return true;
                        }
                        return false;
                    };

                    // MIDI simple buttons: use the existing injected-mask path only.
                    // Do not feed MIDI into GPIO raw masks or DATA/SR long-press paths.
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDILeft,         (1u << MCU_BUTTON_CURSOR_L)))      continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIRight,        (1u << MCU_BUTTON_CURSOR_R)))      continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIPatchPerform, (1u << MCU_BUTTON_PATCH_PERFORM))) continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIEdit,         (1u << MCU_BUTTON_EDIT)))          continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDISystem,       (1u << MCU_BUTTON_SYSTEM)))        continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIRhythm,       (1u << MCU_BUTTON_RHYTHM)))        continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIUtility,      (1u << MCU_BUTTON_UTILITY)))       continue;

                    auto handleMIDIHeldPhysicalKey = [&](u8 confCC, uint32_t mask) {
                        if (confCC != 0 && ccNumber == confCC) {
                            if (ccValue < 64)
                                __atomic_or_fetch(&g_MIDIHeldButtonMask, mask, __ATOMIC_RELAXED);
                            else
                                __atomic_and_fetch(&g_MIDIHeldButtonMask, ~mask, __ATOMIC_RELAXED);

                            i += 2;
                            return true;
                        }
                        return false;
                    };

                    // DATA, TONE SELECT and PREVIEW are real holds.
                    // DATA needs press/release visibility for the existing
                    // DATA short/long state machine.
                    if (handleMIDIHeldPhysicalKey(pThis->m_UI.m_nMIDIData,       BTN_DATA_MASK))        continue;
                    if (handleMIDIHeldPhysicalKey(pThis->m_UI.m_nMIDIToneSelect, BTN_TONE_SELECT_MASK)) continue;
                    if (handleMIDIHeldPhysicalKey(pThis->m_UI.m_nMIDIPreview,    BTN_PREVIEW_MASK))     continue;

                    // TONE SW1/2/3 are tap-style shared physical keys.
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIMute,    BTN_TONE_SW1_MASK)) continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDIMonitor, BTN_TONE_SW2_MASK)) continue;
                    if (handleSimpleInjectedButton(pThis->m_UI.m_nMIDICompare, BTN_TONE_SW3_MASK)) continue;

                    // MIDI ENTER is special.
                    // For SR/RD500 overlays, mirror the remote ENTER tap behavior and
                    // let the overlay UI handler consume ENTER locally.
                    // In normal JV-880 UI, use the injected button mask so ENTER
                    // behaves like a real press/release instead of a one-shot event.
                    if (pThis->m_UI.m_nMIDIEnter != 0 &&
                        ccNumber == pThis->m_UI.m_nMIDIEnter)
                    {
                        // SYX browser local ENTER tap:
                        // Short press/release must be visible to syxInputMask.
                        // Ignore the MIDI release; the timed tap will release itself.
                        if (pThis->m_bSYXMenuActive)
                        {
                            if (ccValue < 64)
                            {
                                const uint32_t tapTicks = 15; // ~150 ms, below long-press threshold
                                __atomic_store_n(&g_MIDIEnterTapHoldUntilTick,
                                                  CTimer::Get()->GetTicks() + tapTicks,
                                                  __ATOMIC_RELAXED);
                            }

                            i += 2;
                            continue;
                        }

                        if (pThis->m_bSRMenuActive ||
                            pThis->m_bRD500MenuActive ||
                            pThis->m_bRD500PatchBrowseActive)
                        {
                            if (ccValue < 64)
                            {
                                pThis->m_UI.TriggerUIButtonEvent(CUIButton::BtnEventEnter);
                            }
                            else
                            {
                                pThis->m_UI.TriggerUIButtonEvent(CUIButton::BtnEventNone);
                            }
                        }
                        else
                        {
                            if (ccValue < 64)
                            {
                                pThis->m_InjectedButtonMask |= BTN_ENTER_MASK;
                            }
                            else
                            {
                                pThis->m_InjectedButtonMask &= ~BTN_ENTER_MASK;
                            }
                        }

                        i += 2;
                        continue;
                    }

                    // MiniJV880 extension commands.
                    if (pThis->m_UI.m_nMIDIAllRelease != 0 &&
                        ccNumber == pThis->m_UI.m_nMIDIAllRelease &&
                        ccValue < 64)
                    {
                        ClearMIDIHeldButtonsAndTimers();

                        pThis->m_InjectedButtonMask = 0;
                        pThis->mcu.mcu_button_pressed &= ~(
                            BTN_DATA_MASK |
                            BTN_PREVIEW_MASK |
                            BTN_TONE_SELECT_MASK |
                            BTN_ENTER_MASK
                        );

                        i += 2;
                        continue;
                    }

                    // ENTER long is a one-shot command: it schedules a timed ENTER hold
                    // long enough to be detected by the existing long-press logic.
                    if (pThis->m_UI.m_nMIDIEnterLong != 0 &&
                        ccNumber == pThis->m_UI.m_nMIDIEnterLong &&
                        ccValue < 64)
                    {
                        uint32_t holdMs = pThis->m_pConfig->GetSYXMenuLongPressTimeout() + 500;
                        if (holdMs < 1500)
                            holdMs = 1500;

                        uint32_t holdTicks = (holdMs + 9) / 10;
                        if (holdTicks == 0)
                            holdTicks = 1;

                        __atomic_store_n(&g_MIDIEnterLongHoldUntilTick,
                                          CTimer::Get()->GetTicks() + holdTicks,
                                          __ATOMIC_RELAXED);

                        i += 2;
                        continue;
                    }

                    // Keep SR overlay separate from MIDIButtonData.
                    if (pThis->m_UI.m_nMIDISROverlay != 0 &&
                        ccNumber == pThis->m_UI.m_nMIDISROverlay &&
                        ccValue < 64)
                    {
                        if (pThis->m_bSRMenuActive)
                        {
                            pThis->RequestCloseSRMenu();
                        }
                        else
                        {
                            pThis->OpenSRMenu();
                        }
                        i += 2;
                        continue;
                    }

                    // DATA dial / encoder.
                    // In normal JV-880 screens, MIDI DATA CW/CCW is forwarded to
                    // the emulated encoder. Local MiniJV880 overlays must consume it
                    // directly, otherwise SR/SYX/RD-500 lists do not move.
                    auto handleOverlayOrEncoderDataDial = [&](int direction) {
                        if (pThis->m_bSYXMenuActive) {
                            if (direction > 0)
                                pThis->NextSYX();
                            else
                                pThis->PrevSYX();
                        } else if (pThis->m_bRD500PatchBrowseActive) {
                            if (direction > 0)
                                pThis->NextRD500Patch();
                            else
                                pThis->PrevRD500Patch();
                        } else if (pThis->m_bRD500MenuActive) {
                            if (direction > 0)
                                pThis->NextRD500Bank();
                            else
                                pThis->PrevRD500Bank();
                        } else if (pThis->m_bSRMenuActive) {
                            if (direction > 0)
                                pThis->NextSR();
                            else
                                pThis->PrevSR();
                        } else {
                            pThis->mcu.MCU_EncoderTrigger(direction > 0 ? 1 : 0);
                        }
                    };

                    if (ccNumber == pThis->m_UI.m_nMIDIUp && ccValue < 64) {
                        pThis->m_UI.TriggerUIButtonEvent(CUIButton::BtnEventNone);
                        handleOverlayOrEncoderDataDial(1);
                        i += 2;
                        continue;
                    }
                    if (ccNumber == pThis->m_UI.m_nMIDIDown && ccValue < 64) {
                        pThis->m_UI.TriggerUIButtonEvent(CUIButton::BtnEventNone);
                        handleOverlayOrEncoderDataDial(-1);
                        i += 2;
                        continue;
                    }
                }
            }

            i += 2;
        }
    }

    pThis->mcu.postMidiSC55(pData, nLength);
}


static bool IsSyxFilename(const char* name)
{
    if (name == nullptr)
        return false;

    const int len = strlen(name);
    return len > 4 && strcasecmp(&name[len - 4], ".syx") == 0;
}

static bool SYXDirectoryHasSyxFiles(const char* dirPath)
{
    if (dirPath == nullptr || dirPath[0] == '\0')
        return false;

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, dirPath) != FR_OK)
        return false;

    bool found = false;

    while (true)
    {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
            break;

        if (fno.fattrib & AM_DIR)
            continue;

        const char* name = fno.fname;

        if (IsSyxFilename(name))
        {
            found = true;
            break;
        }
    }

    f_closedir(&dir);
    return found;
}



bool CMiniJV880::ScanSYXDirectory(const char* dirPath)
{
    if (dirPath == nullptr || dirPath[0] == '\0')
        return false;

    DebugTX::WriteString("SYX SCAN: begin\r\n");

    m_nSYXFileCount = 0;
    memset(m_szSYXDisplay, 0, sizeof(m_szSYXDisplay));
    memset(m_szSYXPath,    0, sizeof(m_szSYXPath));
    memset(m_SYXItemType,  0, sizeof(m_SYXItemType));

    strncpy(m_szSYXCurrentDir, dirPath, sizeof(m_szSYXCurrentDir) - 1);
    m_szSYXCurrentDir[sizeof(m_szSYXCurrentDir) - 1] = '\0';
    m_bSYXAtRoot = (strcmp(m_szSYXCurrentDir, "PN-JV80") == 0);

    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, m_szSYXCurrentDir) != FR_OK)
    {
        DebugTX::WriteString("SYX SCAN: directory not found\r\n");
        return false;
    }

    int count = 0;
    int storedSyxFiles = 0;
    
    while (true)
    {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
            break;

        const char* name = fno.fname;

        // Skip pseudo entries / hidden-style names if any appear
        if (name[0] == '.')
            continue;

        const bool isDir = (fno.fattrib & AM_DIR) != 0;

        bool accept = false;
        SYXItemType itemType = SYXItemType::File;

        if (isDir)
        {
            char childPath[sizeof(m_szSYXPath[0])];
            snprintf(childPath, sizeof(childPath), "%s/%s", m_szSYXCurrentDir, name);

            if (SYXDirectoryHasSyxFiles(childPath))
            {
                accept = true;
                itemType = SYXItemType::Folder;
            }
        }
        else if (IsSyxFilename(name))
        {
            accept = true;
            itemType = SYXItemType::File;
        }

        if (!accept)
            continue;

        count++;

        const bool canStoreFolder =
            (itemType == SYXItemType::Folder) &&
            (m_nSYXFileCount < MAX_SYX_FILES);

        const bool canStoreFile =
            (itemType == SYXItemType::File) &&
            (storedSyxFiles < m_nSYXMaxFiles) &&
            (m_nSYXFileCount < MAX_SYX_FILES);

        if (canStoreFolder || canStoreFile)
        {
            const int idx = m_nSYXFileCount;

            if (itemType == SYXItemType::Folder)
            {
                snprintf(m_szSYXDisplay[idx], sizeof(m_szSYXDisplay[idx]), "[%s]", name);
            }
            else
            {
                strncpy(m_szSYXDisplay[idx], name, sizeof(m_szSYXDisplay[idx]) - 1);
                m_szSYXDisplay[idx][sizeof(m_szSYXDisplay[idx]) - 1] = '\0';
            }

            snprintf(m_szSYXPath[idx], sizeof(m_szSYXPath[idx]), "%s/%s",
                     m_szSYXCurrentDir, name);

            m_SYXItemType[idx] = itemType;
            m_nSYXFileCount++;

            if (itemType == SYXItemType::File)
                storedSyxFiles++;

            DebugTX::WriteString(itemType == SYXItemType::Folder
                                 ? "SYX SCAN: folder "
                                 : "SYX SCAN: file ");
            DebugTX::WriteString(name);
            DebugTX::WriteString("\r\n");
        }
    }

    f_closedir(&dir);

    // Sort: folders first, then alphabetical
    for (int i = 0; i < m_nSYXFileCount - 1; i++)
    {
        for (int j = i + 1; j < m_nSYXFileCount; j++)
        {
            bool swap = false;

            if (m_SYXItemType[i] != m_SYXItemType[j])
            {
                if (m_SYXItemType[i] == SYXItemType::File &&
                    m_SYXItemType[j] == SYXItemType::Folder)
                {
                    swap = true;
                }
            }
            else if (strcasecmp(m_szSYXDisplay[i], m_szSYXDisplay[j]) > 0)
            {
                swap = true;
            }

            if (swap)
            {
                char tmpDisplay[sizeof(m_szSYXDisplay[0])];
                char tmpPath[sizeof(m_szSYXPath[0])];
                SYXItemType tmpType = m_SYXItemType[i];

                strncpy(tmpDisplay, m_szSYXDisplay[i], sizeof(tmpDisplay));
                strncpy(tmpPath,    m_szSYXPath[i],    sizeof(tmpPath));

                strncpy(m_szSYXDisplay[i], m_szSYXDisplay[j], sizeof(m_szSYXDisplay[i]));
                strncpy(m_szSYXPath[i],    m_szSYXPath[j],    sizeof(m_szSYXPath[i]));
                m_SYXItemType[i] = m_SYXItemType[j];

                strncpy(m_szSYXDisplay[j], tmpDisplay, sizeof(m_szSYXDisplay[j]));
                strncpy(m_szSYXPath[j],    tmpPath,    sizeof(m_szSYXPath[j]));
                m_SYXItemType[j] = tmpType;

                tmpDisplay[sizeof(tmpDisplay) - 1] = '\0';
                tmpPath[sizeof(tmpPath) - 1] = '\0';

                m_szSYXDisplay[i][sizeof(m_szSYXDisplay[i]) - 1] = '\0';
                m_szSYXPath[i][sizeof(m_szSYXPath[i]) - 1] = '\0';

                m_szSYXDisplay[j][sizeof(m_szSYXDisplay[j]) - 1] = '\0';
                m_szSYXPath[j][sizeof(m_szSYXPath[j]) - 1] = '\0';
            }
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "SYX SCAN: total %d (stored %d)\r\n",
             count, m_nSYXFileCount);
    DebugTX::WriteString(buf);

    if (m_nSYXIndex < 0 || m_nSYXIndex >= m_nSYXFileCount)
        m_nSYXIndex = 0;

    return true;
}

bool CMiniJV880::LoadSyxFromFile(const char* path)
{
    FIL f;
    FRESULT fr;
    UINT br = 0;
    uint8_t b = 0;

    // A PN/JV packet we have seen stays well below this threshold.
    // If something abnormal ever arrives, it is better to stop than to send corrupted data.
    uint8_t syx[512];
    unsigned syxLen = 0;
    bool inSyx = false;

    if (path == nullptr)
        return false;

    fr = f_open(&f, path, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK)
    {
        DebugTX::WriteString("SYX LOAD: open failed\r\n");
        return false;
    }

    DebugTX::WriteString("SYX LOAD: start\r\n");

    while (true)
    {
        br = 0;
        fr = f_read(&f, &b, 1, &br);

        if (fr != FR_OK)
        {
            DebugTX::WriteString("SYX LOAD: read failed\r\n");
            f_close(&f);
            return false;
        }

        if (br == 0)
            break;

        if (!inSyx)
        {
            if (b == 0xF0)
            {
                inSyx = true;
                syxLen = 0;
                syx[syxLen++] = b;
            }
            continue;
        }

        if (syxLen >= sizeof(syx))
        {
            DebugTX::WriteString("SYX LOAD: packet too large\r\n");
            f_close(&f);
            return false;
        }

        syx[syxLen++] = b;

        if (b == 0xF7)
        {
            CMiniJV880::ParseMIDIData(this, syx, syxLen);

            // Pause between one complete SysEx message and the next.
            // This is mainly needed for large banks.
            CTimer::SimpleMsDelay(15);

            inSyx = false;
            syxLen = 0;
        }
    }

    if (inSyx)
    {
        DebugTX::WriteString("SYX LOAD: truncated packet\r\n");
        f_close(&f);
        return false;
    }

    f_close(&f);
    DebugTX::WriteString("SYX LOAD: end\r\n");
    return true;
}

// ============================================================
// SR helpers
// ============================================================

bool CMiniJV880::HasSRError() const
{
    return g_SRScanResult.code != SRScanCode::Ok;
}

bool CMiniJV880::HasSRList() const
{
    return !g_SRNames.empty();
}

bool CMiniJV880::IsRD500Selected() const
{
    return m_nSRIndex == (int)g_SRNames.size();
}

void CMiniJV880::NextRD500Bank()
{
    m_nRD500BankIndex++;
    if (m_nRD500BankIndex > 2)
        m_nRD500BankIndex = 0;
}

void CMiniJV880::PrevRD500Bank()
{
    m_nRD500BankIndex--;
    if (m_nRD500BankIndex < 0)
        m_nRD500BankIndex = 2;
}

void CMiniJV880::NextRD500Patch()
{
    const int maxIndex = (m_nRD500BankIndex == 1) ? 55 : 63;

    m_nRD500PatchIndex++;
    if (m_nRD500PatchIndex > maxIndex)
        m_nRD500PatchIndex = 0;
}

void CMiniJV880::PrevRD500Patch()
{
    const int maxIndex = (m_nRD500BankIndex == 1) ? 55 : 63;

    m_nRD500PatchIndex--;
    if (m_nRD500PatchIndex < 0)
        m_nRD500PatchIndex = maxIndex;
}

void CMiniJV880::RequestCloseSRMenu()
{
    DBG("SR_CLOSE_REQUEST");

    // Clear the LCD buffer (80 characters), as in DATA close
    memset(&mcu.lcd.LCD_Data[0], ' ', 80);

    // Block DATA until it is released
    m_BlockDataUntilRelease = true;

    // Start Utility sequence (identical to DATA-edge close)
    m_UtilitySeqState  = 1;  // ON 1
    m_UtilitySeqFrames = 2;  // keep ON for 2 frames
    DBG("UTILITY_SEQ_START");

    m_bSRMenuActive = false;

    // Prevent immediate reopening
    m_BlockSRReentry = true;
    DBG("SR_REENTRY_BLOCK_ON");
}

void CMiniJV880::DeviceRemovedHandler(CDevice *pDevice, void *pContext) {
  LOGERR("CMiniJV880::DeviceRemovedHandler");

  CMiniJV880 *pThis = static_cast<CMiniJV880 *>(pContext);
  assert(pThis != 0);

  if (pDevice == pThis->m_pMIDIDevice)
    pThis->m_pMIDIDevice = 0;
}

void CMiniJV880::Run(unsigned nCore) {
    assert(1 <= nCore && nCore < CORES);
    //int nSamples = 0;
    u8 buffer[64];

    if (nCore == 1) { // 1st core - serial MIDI
        while (true) {
            int nRead = m_Serial.Read(buffer, sizeof(buffer));
            if (nRead > 0)
                ParseMIDIData(this, buffer, nRead);
            CTimer::SimpleMsDelay(1);
        }
    } 
    
else if (nCore == 2) { // 2nd core - MCU + audio output  

    const int MCU_INSTR_BURST = 64;
    static int16_t out_buf[AUDIO_BUFFER_SIZE];

    while (true) {

        // ===== GLOBAL PAUSE HANDSHAKE =====
        if (mcu.m_globalPause.load(std::memory_order_acquire)) {
            core2_idle.store(true, std::memory_order_release);
            CTimer::SimpleMsDelay(1);
            continue;
        }

        core2_idle.store(false, std::memory_order_release);

        unsigned nFrames =
            m_nQueueSizeFrames - m_pSoundDevice->GetQueueFramesAvail();

        if (nFrames < m_nQueueSizeFrames / 2) {
            CTimer::SimpleMsDelay(1);
            continue;
        }

        int nSamples = (int)nFrames * 2;
        if (nSamples >= (int)AUDIO_BUFFER_SIZE)
            nSamples = (int)AUDIO_BUFFER_SIZE - 2;

        int out_pos = 0;

        while (out_pos < nSamples) {

            // cooperative pause also in the inner loop
            if (mcu.m_globalPause.load(std::memory_order_acquire)) {
                break;
            }

            uint64_t w =
                __atomic_load_n(&sample_write_idx, __ATOMIC_ACQUIRE);
            uint64_t r =
                __atomic_load_n(&sample_read_idx, __ATOMIC_RELAXED);

            uint64_t avail = w - r;

            if (avail > 0) {

                uint32_t need =
                    (uint32_t)(nSamples - out_pos);

                uint32_t to_copy =
                    (avail < need) ? (uint32_t)avail : need;

                uint32_t idx =
                    (uint32_t)(r & AUDIO_BUFFER_MASK);

                uint32_t first =
                    AUDIO_BUFFER_SIZE - idx;

                if (first > to_copy)
                    first = to_copy;

                memcpy(&out_buf[out_pos],
                       &sample_buffer[idx],
                       first * sizeof(int16_t));

                out_pos += first;
                r += first;

                uint32_t rem = to_copy - first;

                if (rem) {
                    memcpy(&out_buf[out_pos],
                           &sample_buffer[r & AUDIO_BUFFER_MASK],
                           rem * sizeof(int16_t));

                    out_pos += rem;
                    r += rem;
                }

                __atomic_store_n(&sample_read_idx,
                                 r,
                                 __ATOMIC_RELEASE);

                continue;
            }

            // ===== Advance MCU if buffer is empty =====
            int instr = 0;

            while (instr < MCU_INSTR_BURST) {

                if (mcu.m_globalPause.load(std::memory_order_acquire))
                    break;

                if (!mcu.mcu.ex_ignore)
                    mcu.MCU_Interrupt_Handle();
                else
                    mcu.mcu.ex_ignore = 0;

                if (!mcu.mcu.sleep)
                    mcu.MCU_ReadInstruction();

                mcu.mcu.cycles += n_mMCUcycles;

                __atomic_store_n(&mcu.mcu.cycles,
                                 mcu.mcu.cycles,
                                 __ATOMIC_RELEASE);

                mcu.TIMER_Clock(mcu.mcu.cycles);
                mcu.MCU_UpdateUART_RX();
                mcu.MCU_UpdateUART_TX();
                mcu.MCU_UpdateAnalog(mcu.mcu.cycles);

                ++instr;
            }
        }

        int len = nSamples * sizeof(int16_t);

        if (m_pSoundDevice->Write(out_buf, len) != len) {
            LOGERR("Sound data dropped");
        }
    }
  }


 
else if (nCore == 3) { // 3rd core - PCM Update
    
    constexpr uint64_t MCU_CLOCK_HZ = 12000000ull;
    constexpr uint32_t AUDIO_RATE   = 32000u;
    constexpr uint64_t CYCLES_PER_SAMPLE = MCU_CLOCK_HZ / AUDIO_RATE;
    const uint32_t MAX_SAMPLES_PER_ITER = 128;

    uint64_t last_generated_cycles =
        __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_RELAXED);

    while (true) {

        // ===== GLOBAL PAUSE HANDSHAKE =====
        if (mcu.m_globalPause.load(std::memory_order_acquire)) {
            core3_idle.store(true, std::memory_order_release);

            // realign to avoid mismatch after reset
            last_generated_cycles =
                __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_ACQUIRE);

            CTimer::SimpleMsDelay(1);
            continue;
        }

        core3_idle.store(false, std::memory_order_release);

        uint64_t cycles_target =
            __atomic_load_n(&mcu.mcu.cycles, __ATOMIC_ACQUIRE);

        // handle reset or wrap
        if (cycles_target < last_generated_cycles) {
            last_generated_cycles = cycles_target;
        }

        if (cycles_target <= last_generated_cycles) {
            CTimer::SimpleMsDelay(0);
            continue;
        }

        uint64_t cycles_avail =
            cycles_target - last_generated_cycles;

        uint64_t samples_to_gen =
            cycles_avail / CYCLES_PER_SAMPLE;

        while (samples_to_gen > 0) {

            if (mcu.m_globalPause.load(std::memory_order_acquire))
                break;

            uint32_t gen =
                (samples_to_gen > MAX_SAMPLES_PER_ITER)
                ? MAX_SAMPLES_PER_ITER
                : (uint32_t)samples_to_gen;

            uint64_t pcm_target =
                last_generated_cycles + gen * CYCLES_PER_SAMPLE;

            mcu.pcm.PCM_Update(pcm_target);

            last_generated_cycles = pcm_target;
            samples_to_gen -= gen;

            CTimer::SimpleMsDelay(0);
        }
    }
  }

}

void CMiniJV880::NextSR()
{
    const int totalItems = (int)g_SRNames.size() + 1; // +1 = RD-500
    if (totalItems <= 0)
        return;

    if (m_nSRIndex < 0 || m_nSRIndex >= totalItems)
        m_nSRIndex = 0;
    else
        m_nSRIndex = (m_nSRIndex + 1) % totalItems;
}

void CMiniJV880::PrevSR()
{
    const int totalItems = (int)g_SRNames.size() + 1; // +1 = RD-500
    if (totalItems <= 0)
        return;

    if (m_nSRIndex < 0 || m_nSRIndex >= totalItems)
    {
        m_nSRIndex = totalItems - 1;
        return;
    }

    m_nSRIndex--;
    if (m_nSRIndex < 0)
        m_nSRIndex = totalItems - 1;
}

void CMiniJV880::NextSYX()
{
    if (m_nSYXFileCount <= 0)
    {
        m_nSYXIndex = 0;
        return;
    }

    if (m_nSYXIndex < 0 || m_nSYXIndex >= m_nSYXFileCount)
        m_nSYXIndex = 0;
    else
        m_nSYXIndex = (m_nSYXIndex + 1) % m_nSYXFileCount;
}

void CMiniJV880::PrevSYX()
{
    if (m_nSYXFileCount <= 0)
    {
        m_nSYXIndex = 0;
        return;
    }

    if (m_nSYXIndex < 0 || m_nSYXIndex >= m_nSYXFileCount)
    {
        m_nSYXIndex = m_nSYXFileCount - 1;
        return;
    }

    m_nSYXIndex--;
    if (m_nSYXIndex < 0)
        m_nSYXIndex = m_nSYXFileCount - 1;
}

void CMiniJV880::ApplySelectedSR()
{
    m_bRD500ExpansionApplied = false;
    mcu.ReplaceExpansionSafe(g_SRData[m_nSRIndex]); 
}

bool CMiniJV880::ApplySelectedRD500Patch()
{
    static const uint32_t bankBase[3] = { 0x0CE0, 0x8370, 0x12B82 };
    static const uint32_t patchSize   = 0x16A;
    static const uint32_t patchDst    = 0x0D70;
    static const uint32_t modeOffset  = 0x0011;

    if (!EnsureRD500ResourcesLoaded())
        return false;

    if (m_nRD500BankIndex < 0 || m_nRD500BankIndex > 2)
        return false;

    const int maxIndex = (m_nRD500BankIndex == 1) ? 55 : 63;

    if (m_nRD500PatchIndex < 0 || m_nRD500PatchIndex > maxIndex)
        return false;

    const uint32_t off = bankBase[m_nRD500BankIndex] + (uint32_t)m_nRD500PatchIndex * patchSize;
    if (off + patchSize > 0x20000)
        return false;

    if (!m_pRD500Expansion || !m_pRD500Patches)
        return false;

    const bool needExpansionSwap = !m_bRD500ExpansionApplied;
    const bool wasPatchMode = (mcu.nvram[modeOffset] == 1);

    if (needExpansionSwap)
    {
        mcu.ReplaceExpansionSafe(m_pRD500Expansion);
        m_bRD500ExpansionApplied = true;
    }

    memcpy(&mcu.nvram[patchDst], m_pRD500Patches + off, patchSize);
    mcu.nvram[modeOffset] = 1;

    if (!needExpansionSwap && wasPatchMode)
    {
        uint8_t buffer[2] = { 0xC0, 0x00 };
        mcu.postMidiSC55(buffer, sizeof(buffer));
    }
    else
    {
        uint8_t buffer[2] = { 0xC0, 0x00 };
        mcu.postMidiSC55(buffer, sizeof(buffer));
    }

    DebugTX::WriteString("RD500: patch applied to NVRAM working buffer\r\n");
    return true;
}

static inline char ToUpperASCII(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

// ------------------------------------------------------------
// CARDRAM persist helpers (atomic write to SD)
// ------------------------------------------------------------
static uint32_t crc32_ieee(const uint8_t* data, UINT len)
{
    static bool table_init = false;
    static uint32_t table[256];

    if (!table_init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        table_init = true;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (UINT i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFu;
}

static void cardram_metrics(const uint8_t* data, UINT size, uint32_t* out_nonFF, uint32_t* out_crc)
{
    uint32_t nonFF = 0;
    for (UINT i = 0; i < size; ++i)
        if (data[i] != 0xFF) nonFF++;

    uint32_t crc = crc32_ieee(data, size);

    if (out_nonFF) *out_nonFF = nonFF;
    if (out_crc)   *out_crc = crc;
}

static bool WriteCardRamFileAtomic(const uint8_t* data, UINT size, FRESULT* outFr)
{
    auto setFr = [&](FRESULT fr) { if (outFr) *outFr = fr; };

    const char* tmpName   = g_CardRamTmpPath;
    const char* finalName = g_CardRamActivePath;

    FIL f;
    FRESULT fr = f_open(&f, tmpName, FA_CREATE_ALWAYS | FA_WRITE);
    {
        char b[96];
        snprintf(b, sizeof(b), "CARDRAM PERSIST f_open(tmp) fr=%d\r\n", (int)fr);
        DebugTX::WriteString(b);
    }
    if (fr != FR_OK) { setFr(fr); return false; }

    UINT bw = 0;
    fr = f_write(&f, data, size, &bw);
    {
        char b[120];
        snprintf(b, sizeof(b),
                 "CARDRAM PERSIST f_write fr=%d bw=%lu\r\n",
                 (int)fr, (unsigned long)bw);
        DebugTX::WriteString(b);
    }
    if (fr != FR_OK || bw != size)
    {
        (void)f_close(&f);
        (void)f_unlink(tmpName);
        setFr(fr != FR_OK ? fr : FR_INT_ERR);
        DebugTX::WriteString("CARDRAM PERSIST FAIL (write/size)\r\n");
        return false;
    }

    fr = f_sync(&f);
    {
        char b[96];
        snprintf(b, sizeof(b), "CARDRAM PERSIST f_sync fr=%d\r\n", (int)fr);
        DebugTX::WriteString(b);
    }
    if (fr != FR_OK)
    {
        (void)f_close(&f);
        (void)f_unlink(tmpName);
        setFr(fr);
        DebugTX::WriteString("CARDRAM PERSIST FAIL (sync)\r\n");
        return false;
    }

    fr = f_close(&f);
    {
        char b[96];
        snprintf(b, sizeof(b), "CARDRAM PERSIST f_close fr=%d\r\n", (int)fr);
        DebugTX::WriteString(b);
    }

    // remove old final (ignore "no file")
    FRESULT frUnlink = f_unlink(finalName);
    {
        char b[96];
        snprintf(b, sizeof(b), "CARDRAM PERSIST f_unlink(final) fr=%d\r\n", (int)frUnlink);
        DebugTX::WriteString(b);
    }

    fr = f_rename(tmpName, finalName);
    {
        char b[96];
        snprintf(b, sizeof(b), "CARDRAM PERSIST f_rename(tmp->final) fr=%d\r\n", (int)fr);
        DebugTX::WriteString(b);
    }
    if (fr != FR_OK)
    {
        (void)f_unlink(tmpName);
        setFr(fr);
        DebugTX::WriteString("CARDRAM PERSIST FAIL (rename)\r\n");
        return false;
    }

    setFr(FR_OK);
    return true;
}     //    END CARDRAM persist helpers (atomic write to SD)

void CMiniJV880::FlushCardRAMIfNeeded()
{
    if (!mcu.cardram_dirty.load(std::memory_order_relaxed))
        return;

    static uint8_t snap[CARDRAM_SIZE];

    // Attempt a coherent snapshot (up to 3 tries)
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const uint32_t s1 = mcu.cardram_seq.load(std::memory_order_relaxed);
        memcpy(snap, mcu.cardram, CARDRAM_SIZE);
        const uint32_t s2 = mcu.cardram_seq.load(std::memory_order_relaxed);
        if (s1 == s2) break;
    }

    FRESULT fr = FR_OK;
    const bool ok = WriteCardRamFileAtomic(snap, CARDRAM_SIZE, &fr);
    if (ok)
    {
        mcu.cardram_dirty.store(false, std::memory_order_relaxed);
    }
    else
    {
        // minimal log (no hex helper)
        char b[64];
        snprintf(b, sizeof(b), "FLUSH FAIL fr=%d\r\n", (int)fr);
        DebugTX::WriteString(b);
    }
}

static bool LCDContains(const char *hay, size_t hayLen, const char *needle)
{
    if (!hay || !needle) return false;
    const size_t nLen = strlen(needle);
    if (nLen == 0 || nLen > hayLen) return false;

    for (size_t i = 0; i + nLen <= hayLen; ++i)
    {
        bool match = true;
        for (size_t j = 0; j < nLen; ++j)
        {
            const char a = ToUpperASCII(hay[i + j]);
            const char b = ToUpperASCII(needle[j]);
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

CMiniJV880::EPlayMode CMiniJV880::DetectPlayModeFromLCD() const
{
    const char *lcd = (const char*) mcu.lcd.LCD_Data;
    constexpr size_t LCDLEN = 80;   // consistent with your ghost-fix (clear 80)

    const bool hasPatch = LCDContains(lcd, LCDLEN, "PATCH");
    const bool hasPerf  = LCDContains(lcd, LCDLEN, "PERF");

    if (hasPatch && !hasPerf) return EPlayMode::Patch;
    if (hasPerf  && !hasPatch) return EPlayMode::Performance;

    return EPlayMode::Unknown;
}

bool CMiniJV880::IsFirmwareMenuScreen() const
{
    const char *lcd = (const char*) mcu.lcd.LCD_Data;
    constexpr size_t LCDLEN = 80;

    if (LCDContains(lcd, LCDLEN, "UTILITY")) return true;
    if (LCDContains(lcd, LCDLEN, "SYSTEM"))  return true;
    if (LCDContains(lcd, LCDLEN, "EDIT"))    return true;
    if (LCDContains(lcd, LCDLEN, "RHYTHM"))  return true;

    return false;
}

bool CMiniJV880::IsPatchWriteScreen() const
{
    const char *line1 = (const char*) mcu.lcd.LCD_Data + 0;

    return memcmp(line1, "Util:Patch write", 16) == 0;
}

bool CMiniJV880::IsPerformanceWriteScreen() const
{
    const char *line1 = (const char*) mcu.lcd.LCD_Data + 0;

    return memcmp(line1, "Util:Perf write", 15) == 0;
}

bool CMiniJV880::IsPatchCopyScreen() const
{
    const char *line1 = (const char*) mcu.lcd.LCD_Data + 0;

    return memcmp(line1, "Util:Patch copy", 15) == 0;
}

bool CMiniJV880::IsPerformanceCopyScreen() const
{
    const char *line1 = (const char*) mcu.lcd.LCD_Data + 0;

    return memcmp(line1, "Util:Perf copy", 14) == 0;
}

static const char* PlayModeToStr(CMiniJV880::EPlayMode pm)
{
    switch (pm)
    {
        case CMiniJV880::EPlayMode::Patch:       return "PATCH";
        case CMiniJV880::EPlayMode::Performance: return "PERF";
        default:                                 return "UNK";
    }
}

static void DebugDumpLCD24(const char *p, const char *tag)
{
    char line[32];
    unsigned j = 0;

    // tag + ": "
    while (*tag && j < sizeof(line)-1) line[j++] = *tag++;
    if (j < sizeof(line)-1) line[j++] = ':';
    if (j < sizeof(line)-1) line[j++] = ' ';

    for (unsigned i = 0; i < 24 && j < sizeof(line)-3; ++i)
    {
        char c = p[i];
        if (c < 32 || c > 126) c = '.';
        line[j++] = c;
    }

    line[j++] = '\r';
    line[j++] = '\n';
    line[j] = 0;

    DebugTX::WriteString(line);
}

void CMiniJV880::PlayModeTrackingTick()
{
    // Do not interfere with the SR menu
    if (m_bSRMenuActive) return;

    const bool inMenu = IsFirmwareMenuScreen();

    // edge: base -> menu
    if (inMenu && !m_bWasInFwMenu)
    {
        // latch the base state: use the last valid play mode seen in base
        if (m_CurrentPlayMode != EPlayMode::Unknown)
            m_BaseModeBeforeMenu = m_CurrentPlayMode;

        DebugTX::WriteString("[PM] enter menu base=");
        DebugTX::WriteString(PlayModeToStr(m_BaseModeBeforeMenu));
        DebugTX::WriteString("\r\n");
        DebugDumpLCD24((const char*)mcu.lcd.LCD_Data + 0,  "LCD1");
        DebugDumpLCD24((const char*)mcu.lcd.LCD_Data + 40, "LCD2");
    }

    // edge: menu -> base
    if (!inMenu && m_bWasInFwMenu)
    {
        const EPlayMode now = DetectPlayModeFromLCD();
        if (now != EPlayMode::Unknown)
            m_CurrentPlayMode = now;

        DebugTX::WriteString("[PM] exit menu now=");
        DebugTX::WriteString(PlayModeToStr(now));
        DebugTX::WriteString(" base=");
        DebugTX::WriteString(PlayModeToStr(m_BaseModeBeforeMenu));
        DebugTX::WriteString("\r\n");
        DebugDumpLCD24((const char*)mcu.lcd.LCD_Data + 0,  "LCD1");
        DebugDumpLCD24((const char*)mcu.lcd.LCD_Data + 40, "LCD2"); 
        
        // If the firmware exited in the wrong play mode, correct it with a PATCH/PERF toggle
        if (m_BaseModeBeforeMenu != EPlayMode::Unknown &&
            now != EPlayMode::Unknown &&
            now != m_BaseModeBeforeMenu)
        {
            // start ON/OFF sequence (a single short press)
            m_PatchPerfSeqState  = 1;   // ON
            m_PatchPerfSeqFrames = 2;   // keep pressed for 2 frames
            DebugTX::WriteString("[PM] mismatch -> PATCH/PERF toggle\r\n");
        }  
    }

    m_bWasInFwMenu = inMenu;

    // continuous update in base mode (no debounce: latch reliability is needed)
    if (!inMenu)
    {
        const EPlayMode pm = DetectPlayModeFromLCD();
        if (pm != EPlayMode::Unknown)
            m_CurrentPlayMode = pm;
    }
}


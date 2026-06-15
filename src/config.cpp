//
// config.cpp
//
// MiniDexed - Dexed FM synthesizer for bare metal Raspberry Pi
// Copyright (C) 2022  The MiniDexed Team
//
// Original author of this class:
//	R. Stange <rsta2@o2online.de>
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
#include "config.h"

CConfig::CConfig (FATFS *pFileSystem)
:	m_Properties ("minijv880.ini", pFileSystem)
{
}

CConfig::~CConfig (void)
{
}

void CConfig::Load (void)
{
	m_Properties.Load ();
	
	m_nMCUcycles = m_Properties.GetNumber ("MCUcycles", 11);
	m_SoundDevice = m_Properties.GetString ("SoundDevice", "pwm");

	if (m_SoundDevice == "hdmi") {
		m_nChunkSize = m_Properties.GetNumber ("ChunkSize", 384*6);
	}
	else
	{
#ifdef ARM_ALLOW_MULTI_CORE
		m_nChunkSize = m_Properties.GetNumber ("ChunkSize", 256);  // 128 per channel
#else
		m_nChunkSize = m_Properties.GetNumber ("ChunkSize", 1024);
#endif
	}
	m_nDACI2CAddress = m_Properties.GetNumber ("DACI2CAddress", 0);
	m_bChannelsSwapped = m_Properties.GetNumber ("ChannelsSwapped", 0) != 0;

	m_nMIDIBaudRate = m_Properties.GetNumber ("MIDIBaudRate", 31250);


	m_bLCDEnabled = m_Properties.GetNumber ("LCDEnabled", 0) != 0;
	m_nLCDPinEnable = m_Properties.GetNumber ("LCDPinEnable", 4);
	m_nLCDPinRegisterSelect = m_Properties.GetNumber ("LCDPinRegisterSelect", 27);
	m_nLCDPinReadWrite = m_Properties.GetNumber ("LCDPinReadWrite", 0);
	m_nLCDPinData4 = m_Properties.GetNumber ("LCDPinData4", 22);
	m_nLCDPinData5 = m_Properties.GetNumber ("LCDPinData5", 23);
	m_nLCDPinData6 = m_Properties.GetNumber ("LCDPinData6", 24);
	m_nLCDPinData7 = m_Properties.GetNumber ("LCDPinData7", 25);
	m_nLCDI2CAddress = m_Properties.GetNumber ("LCDI2CAddress", 0);

	m_nSSD1306LCDI2CAddress = m_Properties.GetNumber ("SSD1306LCDI2CAddress", 0);
	m_nSSD1306LCDWidth = m_Properties.GetNumber ("SSD1306LCDWidth", 128);
	m_nSSD1306LCDHeight = m_Properties.GetNumber ("SSD1306LCDHeight", 32);
	m_bSSD1306LCDRotate = m_Properties.GetNumber ("SSD1306LCDRotate", 0) != 0;
	m_bSSD1306LCDMirror = m_Properties.GetNumber ("SSD1306LCDMirror", 0) != 0;

	m_nSPIBus = m_Properties.GetNumber ("SPIBus", SPI_INACTIVE);  // Disabled by default
	m_nSPIMode = m_Properties.GetNumber ("SPIMode", SPI_DEF_MODE);
	m_nSPIClockKHz = m_Properties.GetNumber ("SPIClockKHz", SPI_DEF_CLOCK);

	m_bST7789Enabled = m_Properties.GetNumber ("ST7789Enabled", 0) != 0;
	m_nST7789Data = m_Properties.GetNumber ("ST7789Data", 0);
	m_nST7789Select = m_Properties.GetNumber ("ST7789Select", 0);
	m_nST7789Reset = m_Properties.GetNumber ("ST7789Reset", 0);  // optional
	m_nST7789Backlight = m_Properties.GetNumber ("ST7789Backlight", 0);  // optional
	m_nST7789Width = m_Properties.GetNumber ("ST7789Width", 240);
	m_nST7789Height = m_Properties.GetNumber ("ST7789Height", 240);
	m_nST7789Rotation = m_Properties.GetNumber ("ST7789Rotation", 0);
	m_bST7789SmallFont = m_Properties.GetNumber ("ST7789SmallFont", 0) != 0;

	m_nLCDColumns = m_Properties.GetNumber ("LCDColumns", 16);
	m_nLCDRows = m_Properties.GetNumber ("LCDRows", 2);

	m_nButtonPinPreview = m_Properties.GetNumber ("ButtonPinPreview", 0);
	m_nButtonPinLeft = m_Properties.GetNumber ("ButtonPinLeft", 0);
	m_nButtonPinRight = m_Properties.GetNumber ("ButtonPinRight", 0);
	m_nButtonPinData = m_Properties.GetNumber ("ButtonPinData", 0);
	m_nButtonPinToneSelect = m_Properties.GetNumber ("ButtonPinToneSelect", 0);
	m_nButtonPinPatchPerform = m_Properties.GetNumber ("ButtonPinPatchPerform", 0);
	m_nButtonPinEdit = m_Properties.GetNumber ("ButtonPinEdit", 0);
	m_nButtonPinSystem = m_Properties.GetNumber ("ButtonPinSystem", 0);
	m_nButtonPinRhythm = m_Properties.GetNumber ("ButtonPinRhythm", 0);
	m_nButtonPinUtility = m_Properties.GetNumber ("ButtonPinUtility", 0);
	m_nButtonPinMute = m_Properties.GetNumber ("ButtonPinMute", 0);
	m_nButtonPinMonitor = m_Properties.GetNumber ("ButtonPinMonitor", 0);
	m_nButtonPinCompare = m_Properties.GetNumber ("ButtonPinCompare", 0);
	m_nButtonPinEnter = m_Properties.GetNumber ("ButtonPinEnter", 11);

	m_ButtonActionPreview = m_Properties.GetString ("ButtonActionPreview", "");
	m_ButtonActionLeft = m_Properties.GetString ("ButtonActionLeft", "");
	m_ButtonActionRight = m_Properties.GetString ("ButtonActionRight", "");
	m_ButtonActionData = m_Properties.GetString ("ButtonActionData", "");
	m_ButtonActionToneSelect = m_Properties.GetString ("ButtonActionToneSelect", "");
	m_ButtonActionPatchPerform = m_Properties.GetString ("ButtonActionPatchPerform", "");
	m_ButtonActionEdit = m_Properties.GetString ("ButtonActionEdit", "");
	m_ButtonActionSystem = m_Properties.GetString ("ButtonActionSystem", "");
	m_ButtonActionRhythm = m_Properties.GetString ("ButtonActionRhythm", "");
	m_ButtonActionUtility = m_Properties.GetString ("ButtonActionUtility", "");
	m_ButtonActionMute = m_Properties.GetString ("ButtonActionMute", "");
	m_ButtonActionMonitor = m_Properties.GetString ("ButtonActionMonitor", "");
	m_ButtonActionCompare = m_Properties.GetString ("ButtonActionCompare", "");
	m_ButtonActionEnter = m_Properties.GetString("ButtonActionEnter", "click");

	// MIDI buttons
	m_nMIDIButtonCh = m_Properties.GetNumber ("MIDIButtonCh", 0);
	m_nMIDIButtonPreview = m_Properties.GetNumber("MIDIButtonPreview", 0);
	m_nMIDIButtonLeft = m_Properties.GetNumber("MIDIButtonLeft", 0);
	m_nMIDIButtonRight = m_Properties.GetNumber("MIDIButtonRight", 0);
	m_nMIDIButtonData = m_Properties.GetNumber("MIDIButtonData", 0);
	m_nMIDIButtonToneSelect = m_Properties.GetNumber("MIDIButtonToneSelect", 0);
	m_nMIDIButtonPatchPerform = m_Properties.GetNumber("MIDIButtonPatchPerform", 0);
	m_nMIDIButtonEdit = m_Properties.GetNumber("MIDIButtonEdit", 0);
	m_nMIDIButtonSystem = m_Properties.GetNumber("MIDIButtonSystem", 0);
	m_nMIDIButtonRhythm = m_Properties.GetNumber("MIDIButtonRhythm", 0);
	m_nMIDIButtonUtility = m_Properties.GetNumber("MIDIButtonUtility", 0);
	m_nMIDIButtonMute = m_Properties.GetNumber("MIDIButtonMute", 0);
	m_nMIDIButtonMonitor = m_Properties.GetNumber("MIDIButtonMonitor", 0);
	m_nMIDIButtonCompare = m_Properties.GetNumber("MIDIButtonCompare", 0);
	m_nMIDIButtonEnter = m_Properties.GetNumber("MIDIButtonEnter", 0);
	m_nMIDIButtonUp = m_Properties.GetNumber("MIDIButtonUp", 0);
	m_nMIDIButtonDown = m_Properties.GetNumber("MIDIButtonDown", 0);
	m_nMIDIButtonSROverlay = m_Properties.GetNumber("MIDIButtonSROverlay", 0);
	m_nMIDIButtonEnterLong = m_Properties.GetNumber("MIDIButtonEnterLong", 0);
	m_nMIDIButtonAllRelease = m_Properties.GetNumber("MIDIButtonAllRelease", 0);

	m_nDoubleClickTimeout = m_Properties.GetNumber ("DoubleClickTimeout", 400);
	m_nLongPressTimeout = m_Properties.GetNumber ("LongPressTimeout", 600);
	m_nSYXMenuLongPressTimeout = m_Properties.GetNumber ("SYXMenuLongPressTimeout", 1200);

	m_bEncoderEnabled = m_Properties.GetNumber ("EncoderEnabled", 0) != 0;
	m_nEncoderPinClock = m_Properties.GetNumber ("EncoderPinClock", 10);
	m_nEncoderPinData = m_Properties.GetNumber ("EncoderPinData", 9);

	m_nSYXMaxFiles = m_Properties.GetNumber ("SYXMaxFiles", 32);
        if (m_nSYXMaxFiles == 0)
	    m_nSYXMaxFiles = 32;
        if (m_nSYXMaxFiles > 128)
	    m_nSYXMaxFiles = 128;

        // Network
        m_bNetEnabled = m_Properties.GetNumber ("NetEnabled", 0) != 0;

        m_NetInterface = m_Properties.GetString ("NetInterface", "ethernet");
        for (size_t i = 0; i < m_NetInterface.length (); i++)
        {
            char& ch = m_NetInterface[i];
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = (char) (ch - 'A' + 'a');
            }
        }
        if (m_NetInterface != "ethernet" && m_NetInterface != "wlan")
        {
            m_NetInterface = "ethernet";
        }

        m_bNetDHCP = m_Properties.GetNumber ("NetDHCP", 0) != 0;
        m_NetIP = m_Properties.GetString ("NetIP", "192.168.1.88");
        m_NetMask = m_Properties.GetString ("NetMask", "255.255.255.0");
        m_NetGateway = m_Properties.GetString ("NetGateway", "192.168.1.1");
        m_nNetPort = m_Properties.GetNumber ("NetPort", 8080);
        if (m_nNetPort == 0)
	    m_nNetPort = 8080;
        if (m_nNetPort > 65535)
	    m_nNetPort = 65535;
        m_NetHostName = m_Properties.GetString ("NetHostName", "minijv880");
        if (m_NetHostName.empty ())
	    m_NetHostName = "minijv880";
        m_bNetWriteEnable = m_Properties.GetNumber ("NetWriteEnable", 0) != 0;
        m_bNetExposePNJV80 = m_Properties.GetNumber ("NetExposePNJV80", 1) != 0;
        m_bNetExposeRoms = m_Properties.GetNumber ("NetExposeRoms", 1) != 0;
        m_bNetTFTPEnable = m_Properties.GetNumber ("NetTFTPEnable", 0) != 0;
        m_nNetTFTPPort = m_Properties.GetNumber ("NetTFTPPort", 69);
        if (m_nNetTFTPPort == 0)
            m_nNetTFTPPort = 69;
        if (m_nNetTFTPPort > 65535)
            m_nNetTFTPPort = 69;

        m_bProfileEnabled = m_Properties.GetNumber ("ProfileEnabled", 0) != 0;
}

unsigned CConfig::GetMCUcycles (void) const
{
	return m_nMCUcycles;
}

const char *CConfig::GetSoundDevice (void) const
{
	return m_SoundDevice.c_str ();
}

unsigned CConfig::GetChunkSize (void) const
{
	return m_nChunkSize;
}

unsigned CConfig::GetDACI2CAddress (void) const
{
	return m_nDACI2CAddress;
}

bool CConfig::GetChannelsSwapped (void) const
{
	return m_bChannelsSwapped;
}

unsigned CConfig::GetMIDIBaudRate (void) const
{
	return m_nMIDIBaudRate;
}


bool CConfig::GetLCDEnabled (void) const
{
	return m_bLCDEnabled;
}

unsigned CConfig::GetLCDPinEnable (void) const
{
	return m_nLCDPinEnable;
}

unsigned CConfig::GetLCDPinRegisterSelect (void) const
{
	return m_nLCDPinRegisterSelect;
}

unsigned CConfig::GetLCDPinReadWrite (void) const
{
	return m_nLCDPinReadWrite;
}

unsigned CConfig::GetLCDPinData4 (void) const
{
	return m_nLCDPinData4;
}

unsigned CConfig::GetLCDPinData5 (void) const
{
	return m_nLCDPinData5;
}

unsigned CConfig::GetLCDPinData6 (void) const
{
	return m_nLCDPinData6;
}

unsigned CConfig::GetLCDPinData7 (void) const
{
	return m_nLCDPinData7;
}

unsigned CConfig::GetLCDI2CAddress (void) const
{
	return m_nLCDI2CAddress;
}

unsigned CConfig::GetSSD1306LCDI2CAddress (void) const
{
	return m_nSSD1306LCDI2CAddress;
}

unsigned CConfig::GetSSD1306LCDWidth (void) const
{
	return m_nSSD1306LCDWidth;
}

unsigned CConfig::GetSSD1306LCDHeight (void) const
{
	return m_nSSD1306LCDHeight;
}

bool CConfig::GetSSD1306LCDRotate (void) const
{
	return m_bSSD1306LCDRotate;
}

bool CConfig::GetSSD1306LCDMirror (void) const
{
	return m_bSSD1306LCDMirror;
}

unsigned CConfig::GetSPIBus (void) const
{
	return m_nSPIBus;
}

unsigned CConfig::GetSPIMode (void) const
{
	return m_nSPIMode;
}

unsigned CConfig::GetSPIClockKHz (void) const
{
	return m_nSPIClockKHz;
}

bool CConfig::GetST7789Enabled (void) const
{
	return m_bST7789Enabled;
}

unsigned CConfig::GetST7789Data (void) const
{
	return m_nST7789Data;
}

unsigned CConfig::GetST7789Select (void) const
{
	return m_nST7789Select;
}

unsigned CConfig::GetST7789Reset (void) const
{
	return m_nST7789Reset;
}

unsigned CConfig::GetST7789Backlight (void) const
{
	return m_nST7789Backlight;
}

unsigned CConfig::GetST7789Width (void) const
{
	return m_nST7789Width;
}

unsigned CConfig::GetST7789Height (void) const
{
	return m_nST7789Height;
}

unsigned CConfig::GetST7789Rotation (void) const
{
	return m_nST7789Rotation;
}

bool CConfig::GetST7789SmallFont (void) const
{
	return m_bST7789SmallFont;
}
unsigned CConfig::GetLCDColumns (void) const
{
	return m_nLCDColumns;
}

unsigned CConfig::GetLCDRows (void) const
{
	return m_nLCDRows;
}

unsigned CConfig::GetButtonPinPreview (void) const
{
	return m_nButtonPinPreview;
}

unsigned CConfig::GetButtonPinLeft (void) const
{
	return m_nButtonPinLeft;
}

unsigned CConfig::GetButtonPinRight (void) const
{
	return m_nButtonPinRight;
}

unsigned CConfig::GetButtonPinData (void) const
{
	return m_nButtonPinData;
}

unsigned CConfig::GetButtonPinToneSelect (void) const
{
	return m_nButtonPinToneSelect;
}

unsigned CConfig::GetButtonPinPatchPerform (void) const
{
	return m_nButtonPinPatchPerform;
}

unsigned CConfig::GetButtonPinEdit (void) const
{
	return m_nButtonPinEdit;
}

unsigned CConfig::GetButtonPinSystem (void) const
{
	return m_nButtonPinSystem;
}

unsigned CConfig::GetButtonPinRhythm (void) const
{
	return m_nButtonPinRhythm;
}

unsigned CConfig::GetButtonPinUtility (void) const
{
	return m_nButtonPinUtility;
}

unsigned CConfig::GetButtonPinMute (void) const
{
	return m_nButtonPinMute;
}

unsigned CConfig::GetButtonPinMonitor (void) const
{
	return m_nButtonPinMonitor;
}

unsigned CConfig::GetButtonPinCompare (void) const
{
	return m_nButtonPinCompare;
}

unsigned CConfig::GetButtonPinEnter (void) const
{
	return m_nButtonPinEnter;
}

const char *CConfig::GetButtonActionPreview (void) const
{
	return m_ButtonActionPreview.c_str();
}

const char *CConfig::GetButtonActionLeft (void) const
{
	return m_ButtonActionLeft.c_str();
}

const char *CConfig::GetButtonActionRight (void) const
{
	return m_ButtonActionRight.c_str();
}

const char *CConfig::GetButtonActionData (void) const
{
	return m_ButtonActionData.c_str();
}

const char *CConfig::GetButtonActionToneSelect (void) const
{
	return m_ButtonActionToneSelect.c_str();
}

const char *CConfig::GetButtonActionPatchPerform (void) const
{
	return m_ButtonActionPatchPerform.c_str();
}

const char *CConfig::GetButtonActionEdit (void) const
{
	return m_ButtonActionEdit.c_str();
}

const char *CConfig::GetButtonActionSystem (void) const
{
	return m_ButtonActionSystem.c_str();
}

const char *CConfig::GetButtonActionRhythm (void) const
{
	return m_ButtonActionRhythm.c_str();
}

const char *CConfig::GetButtonActionUtility (void) const
{
	return m_ButtonActionUtility.c_str();
}

const char *CConfig::GetButtonActionMute (void) const
{
	return m_ButtonActionMute.c_str();
}

const char *CConfig::GetButtonActionMonitor (void) const
{
	return m_ButtonActionMonitor.c_str();
}

const char *CConfig::GetButtonActionCompare (void) const
{
	return m_ButtonActionCompare.c_str();
}

const char *CConfig::GetButtonActionEnter (void) const
{
	return m_ButtonActionEnter.c_str();
}

unsigned CConfig::GetMIDIButtonCh (void) const
{
	return m_nMIDIButtonCh;
}

unsigned CConfig::GetMIDIButtonPreview (void) const
{
    return m_nMIDIButtonPreview;
}

unsigned CConfig::GetMIDIButtonLeft (void) const
{
    return m_nMIDIButtonLeft;
}

unsigned CConfig::GetMIDIButtonRight (void) const
{
    return m_nMIDIButtonRight;
}

unsigned CConfig::GetMIDIButtonData (void) const
{
    return m_nMIDIButtonData;
}

unsigned CConfig::GetMIDIButtonToneSelect (void) const
{
    return m_nMIDIButtonToneSelect;
}

unsigned CConfig::GetMIDIButtonPatchPerform (void) const
{
    return m_nMIDIButtonPatchPerform;
}

unsigned CConfig::GetMIDIButtonEdit (void) const
{
    return m_nMIDIButtonEdit;
}

unsigned CConfig::GetMIDIButtonSystem (void) const
{
    return m_nMIDIButtonSystem;
}

unsigned CConfig::GetMIDIButtonRhythm (void) const
{
    return m_nMIDIButtonRhythm;
}

unsigned CConfig::GetMIDIButtonUtility (void) const
{
    return m_nMIDIButtonUtility;
}

unsigned CConfig::GetMIDIButtonMute (void) const
{
    return m_nMIDIButtonMute;
}

unsigned CConfig::GetMIDIButtonMonitor (void) const
{
    return m_nMIDIButtonMonitor;
}

unsigned CConfig::GetMIDIButtonCompare (void) const
{
    return m_nMIDIButtonCompare;
}

unsigned CConfig::GetMIDIButtonEnter (void) const
{
    return m_nMIDIButtonEnter;
}

unsigned CConfig::GetMIDIButtonUp (void) const
{
    return m_nMIDIButtonUp;
}

unsigned CConfig::GetMIDIButtonDown (void) const
{
    return m_nMIDIButtonDown;
}

unsigned CConfig::GetMIDIButtonSROverlay (void) const
{
	return m_nMIDIButtonSROverlay;
}

unsigned CConfig::GetMIDIButtonEnterLong (void) const
{
	return m_nMIDIButtonEnterLong;
}

unsigned CConfig::GetMIDIButtonAllRelease (void) const
{
	return m_nMIDIButtonAllRelease;
}

bool CConfig::GetEncoderEnabled (void) const
{
	return m_bEncoderEnabled;
}

unsigned CConfig::GetEncoderPinClock (void) const
{
	return m_nEncoderPinClock;
}

unsigned CConfig::GetEncoderPinData (void) const
{
	return m_nEncoderPinData;
}

unsigned CConfig::GetSYXMaxFiles (void) const
{
	return m_nSYXMaxFiles;
}

bool CConfig::GetNetEnabled (void) const
{
	return m_bNetEnabled;
}

const char *CConfig::GetNetInterface (void) const
{
	return m_NetInterface.c_str ();
}

bool CConfig::GetNetDHCP (void) const
{
	return m_bNetDHCP;
}

const char *CConfig::GetNetIP (void) const
{
	return m_NetIP.c_str ();
}

const char *CConfig::GetNetMask (void) const
{
	return m_NetMask.c_str ();
}

const char *CConfig::GetNetGateway (void) const
{
	return m_NetGateway.c_str ();
}

unsigned CConfig::GetNetPort (void) const
{
	return m_nNetPort;
}

const char *CConfig::GetNetHostName (void) const
{
	return m_NetHostName.c_str ();
}

bool CConfig::GetNetWriteEnable (void) const
{
	return m_bNetWriteEnable;
}

bool CConfig::GetNetExposePNJV80 (void) const
{
	return m_bNetExposePNJV80;
}

bool CConfig::GetNetExposeRoms (void) const
{
	return m_bNetExposeRoms;
}

bool CConfig::GetNetTFTPEnable (void) const
{
	return m_bNetTFTPEnable;
}

unsigned CConfig::GetNetTFTPPort (void) const
{
	return m_nNetTFTPPort;
}

unsigned CConfig::GetDoubleClickTimeout (void) const
{
	return m_nDoubleClickTimeout;
}

unsigned CConfig::GetLongPressTimeout (void) const
{
	return m_nLongPressTimeout;
}

unsigned CConfig::GetSYXMenuLongPressTimeout (void) const
{
	return m_nSYXMenuLongPressTimeout;
}


bool CConfig::GetProfileEnabled (void) const
{
	return m_bProfileEnabled;
}

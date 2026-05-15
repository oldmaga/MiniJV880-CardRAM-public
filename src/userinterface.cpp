//
// userinterface.cpp
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
#include "userinterface.h"
#include "version.h"
#include "minijv880.h"
#include "emulator/mcu.h"
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/startup.h>
#include <circle/timer.h>
#include <string.h>
#include <assert.h>
#include <chrono>
#include "debug_tx.h"

LOGMODULE ("ui");


CUserInterface::CUserInterface (CMiniJV880 *pMiniJV880, CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, CSPIMaster *pSPIMaster, CConfig *pConfig)
:	m_pMiniJV880 (pMiniJV880),
	m_pGPIOManager (pGPIOManager),
	m_pI2CMaster (pI2CMaster),
	m_pSPIMaster (pSPIMaster),
	m_pConfig (pConfig),
	m_pLCD (0),
	m_pLCDBuffered (0),
	m_pUIButtons (0),
	m_pRotaryEncoder (0),
	m_bSwitchPressed (false),
	m_lastTick (0)
{
}

CUserInterface::~CUserInterface (void)
{
	delete m_pRotaryEncoder;
	delete m_pUIButtons;
	delete m_pLCDBuffered;
	delete m_pLCD;
}

bool CUserInterface::Initialize (void)
{
	assert (m_pConfig);

	// Cache MIDI button configuration
    m_nMIDIButtonChannel = 	m_pConfig->GetMIDIButtonCh();
    m_nMIDIPreview = 		m_pConfig->GetMIDIButtonPreview() & 0x7F;
    m_nMIDILeft = 			m_pConfig->GetMIDIButtonLeft() & 0x7F;
    m_nMIDIRight = 			m_pConfig->GetMIDIButtonRight() & 0x7F;
    m_nMIDIData = 			m_pConfig->GetMIDIButtonData() & 0x7F;
    m_nMIDIToneSelect = 	m_pConfig->GetMIDIButtonToneSelect() & 0x7F;
    m_nMIDIPatchPerform = 	m_pConfig->GetMIDIButtonPatchPerform() & 0x7F;
    m_nMIDIEdit = 			m_pConfig->GetMIDIButtonEdit() & 0x7F;
    m_nMIDISystem = 		m_pConfig->GetMIDIButtonSystem() & 0x7F;
    m_nMIDIRhythm = 		m_pConfig->GetMIDIButtonRhythm() & 0x7F;
    m_nMIDIUtility = 		m_pConfig->GetMIDIButtonUtility() & 0x7F;
    m_nMIDIMute = 			m_pConfig->GetMIDIButtonMute() & 0x7F;
    m_nMIDIMonitor = 		m_pConfig->GetMIDIButtonMonitor() & 0x7F;
    m_nMIDICompare = 		m_pConfig->GetMIDIButtonCompare() & 0x7F;
    m_nMIDIEnter = 			m_pConfig->GetMIDIButtonEnter() & 0x7F; 
	m_nMIDIUp = 			m_pConfig->GetMIDIButtonUp() & 0x7F; 
	m_nMIDIDown = 			m_pConfig->GetMIDIButtonDown() & 0x7F; 

	
	if (!LCDInit()) {
		LOGNOTE("Display init error");
		return false;
	}

	m_pUIButtons = new CUIButtons (
                  m_pConfig->GetButtonPinPreview (), m_pConfig->GetButtonActionPreview (),
									m_pConfig->GetButtonPinLeft (), m_pConfig->GetButtonActionLeft (),
									m_pConfig->GetButtonPinRight (), m_pConfig->GetButtonActionRight (),
									m_pConfig->GetButtonPinData (), m_pConfig->GetButtonActionData (),
									m_pConfig->GetButtonPinToneSelect (), m_pConfig->GetButtonActionToneSelect (),
                  m_pConfig->GetButtonPinPatchPerform (), m_pConfig->GetButtonActionPatchPerform (),
									m_pConfig->GetButtonPinEdit (), m_pConfig->GetButtonActionEdit (),
									m_pConfig->GetButtonPinSystem (), m_pConfig->GetButtonActionSystem (),
									m_pConfig->GetButtonPinRhythm (), m_pConfig->GetButtonActionRhythm (),
									m_pConfig->GetButtonPinUtility (), m_pConfig->GetButtonActionUtility (),
                  m_pConfig->GetButtonPinMute (), m_pConfig->GetButtonActionMute (),
                  m_pConfig->GetButtonPinMonitor (), m_pConfig->GetButtonActionMonitor (),
                  m_pConfig->GetButtonPinCompare (), m_pConfig->GetButtonActionCompare (),
									m_pConfig->GetButtonPinEnter (), m_pConfig->GetButtonActionEnter (),
									m_pConfig->GetDoubleClickTimeout (), m_pConfig->GetLongPressTimeout ()
								  );
	assert (m_pUIButtons);

	if (!m_pUIButtons->Initialize ())
	{
		return false;
	}

	m_pUIButtons->RegisterEventHandler (UIButtonsEventStub, this);

	LOGDBG ("Button User Interface initialized");

	if (m_pConfig->GetEncoderEnabled ())
	{
		m_pRotaryEncoder = new CKY040 (m_pConfig->GetEncoderPinClock (),
					       m_pConfig->GetEncoderPinData (),
					       //m_pConfig->GetButtonPinEnter (),
					       0,    //---modifica---//
					       m_pGPIOManager);
		assert (m_pRotaryEncoder);

		if (!m_pRotaryEncoder->Initialize ())
		{
			return false;
		}

		m_pRotaryEncoder->RegisterEventHandler (EncoderEventStub, this);

		LOGDBG ("Rotary encoder initialized");
	}
   
	return true;
}

void CUserInterface::Process (void)
{
    m_pMiniJV880->mcu.lcd.LCD_Update();

    if (m_pLCDBuffered)
    {
        m_pLCDBuffered->Update();
    }

    if (m_pUIButtons)
    {
        // Button update only
        // Propagation to the engine happens
        // exclusively inside UIButtonsEventHandler
        m_pUIButtons->Update();
    }
	

// SCROLL ROUTINE
int displayCols = m_pConfig->GetLCDColumns(); // 24
CString Msg("\x1B[H\E[?25l"); // clear screen + hide cursor

unsigned long currentTime = CTimer::GetClockTicks();

// Compute actual length of each row
int rowLen[2];
for (int r = 0; r < 2; r++) {
    rowLen[r] = 0;
    for (int c = ACTUAL_COLS - 1; c >= 0; c--) {
        if (m_pMiniJV880->mcu.lcd.LCD_Data[r * 40 + c] != ' ') {
            rowLen[r] = c + 1;
            break;
        }
    }
}

// Scroll logic
for (int r = 0; r < 2; r++) {
    if (rowLen[r] > displayCols) {
        if (currentTime - m_lastScrollTime >= SCROLL_INTERVAL) {
            m_scrollPosition[r] += m_scrollDir[r];
            if (m_scrollPosition[r] <= 0) {
                m_scrollPosition[r] = 0;
                m_scrollDir[r] = +1;
            } else if (m_scrollPosition[r] >= rowLen[r] - displayCols) {
                m_scrollPosition[r] = rowLen[r] - displayCols;
                m_scrollDir[r] = -1;
            }
        }
    } else {
        m_scrollPosition[r] = 0; // no scroll if text fits
    }
}
if (currentTime - m_lastScrollTime >= SCROLL_INTERVAL) m_lastScrollTime = currentTime;

// Render display
for (int row = 0; row < 2; row++) {
    int startPos = m_scrollPosition[row];
    int cursorRow = m_pMiniJV880->mcu.lcd.LCD_DD_RAM / 0x40;
    int cursorCol = m_pMiniJV880->mcu.lcd.LCD_DD_RAM % 0x40;
    bool cursorEnabled = m_pMiniJV880->mcu.lcd.LCD_C != 0;

    for (int col = 0; col < displayCols; col++) {
        int sourcePos = col + startPos;
        uint8_t ch = (sourcePos < ACTUAL_COLS) ? m_pMiniJV880->mcu.lcd.LCD_Data[row * 40 + sourcePos] : ' ';

        // replace characters
        if (ch == 0x09) ch = 0x7C; // vertical bar
        else if (ch < 32 || ch > 126) ch = ' ';

        // cursor as space
        if (cursorEnabled && row == cursorRow && col == cursorCol - startPos && col >= 0 && col < displayCols) {
            Msg.Append(" ");
        } else {
            char buf[2] = { (char)ch, 0 };
            Msg.Append(buf);
        }
    }
}

LCDWrite(Msg);



/*
    int displayCols = m_pConfig->GetLCDColumns();
    CString Msg ("\x1B[H\E[?25l");

    unsigned long currentTime = CTimer::GetClockTicks();

    // Need Scroll?
    bool needScroll = false;
    if (ACTUAL_COLS > displayCols) {
        bool allSpaces = true;
        for (int r = 0; r < 2; r++) {
            for (int c = displayCols; c < ACTUAL_COLS; c++) {
                if (m_pMiniJV880->mcu.lcd.LCD_Data[r * 40 + c] != ' ') {
                    allSpaces = false;
                    break;
                }
            }
            if (!allSpaces) break;
        }
        needScroll = !allSpaces;
    }

    // Scroll logic
    if (needScroll && (currentTime - m_lastScrollTime >= SCROLL_INTERVAL)) {
        for (int r = 0; r < 2; r++) {
            m_scrollPosition[r] += m_scrollDir[r];
            if (m_scrollPosition[r] <= 0) {
                m_scrollPosition[r] = 0;
                m_scrollDir[r] = +1;
            } else if (m_scrollPosition[r] >= ACTUAL_COLS - displayCols) {
                m_scrollPosition[r] = ACTUAL_COLS - displayCols;
                m_scrollDir[r] = -1;
            }
        }
        m_lastScrollTime = currentTime;
    }

    // Strings
    for (int i = 0; i < 2; i++) {
        int startPos = needScroll ? m_scrollPosition[i] : 0;

        // cursor
        int cursorRow = m_pMiniJV880->mcu.lcd.LCD_DD_RAM / 0x40;
        int cursorCol = m_pMiniJV880->mcu.lcd.LCD_DD_RAM % 0x40;
        bool cursorEnabled = m_pMiniJV880->mcu.lcd.LCD_C != 0;

        for (int j = 0; j < displayCols; j++) {
            int sourcePos = (j + startPos) % ACTUAL_COLS;
            uint8_t ch = m_pMiniJV880->mcu.lcd.LCD_Data[i * 40 + sourcePos];

            // cursor in window?
            bool cursorHere = false;
            if (cursorEnabled && cursorRow == i) {
                int rel = cursorCol - startPos;
                if (rel < 0) rel += ACTUAL_COLS;
                if (rel >= 0 && rel < displayCols && j == rel) {
                    cursorHere = true;
                }
            }

            if (cursorHere) {
                Msg.Append("_");
            } else {
                char buf[2] = { (char)ch, 0 };
                Msg.Append(buf);
            }
        }
    }

    LCDWrite(Msg);
*/

}

bool CUserInterface::LCDInit()
{
if (m_pConfig->GetLCDEnabled ())
	{
		unsigned i2caddr = m_pConfig->GetLCDI2CAddress ();
		unsigned ssd1306addr = m_pConfig->GetSSD1306LCDI2CAddress ();
		bool st7789 = m_pConfig->GetST7789Enabled ();
		if (ssd1306addr != 0) {
			m_pSSD1306 = new CSSD1306Device (m_pConfig->GetSSD1306LCDWidth (), m_pConfig->GetSSD1306LCDHeight (),
											 m_pI2CMaster, ssd1306addr,
											 m_pConfig->GetSSD1306LCDRotate (), m_pConfig->GetSSD1306LCDMirror ());
			if (!m_pSSD1306->Initialize ())
			{
				LOGDBG("LCD: SSD1306 initialization failed");
				return false;
			}
			LOGDBG ("LCD: SSD1306");
			m_pLCD = m_pSSD1306;
		}
		else if (st7789)
		{
			if (m_pSPIMaster == nullptr)
			{
				LOGDBG("LCD: ST7789 Enabled but SPI Initialisation Failed");
				return false;
			}

			unsigned long nSPIClock = 1000 * m_pConfig->GetSPIClockKHz();
			unsigned nSPIMode = m_pConfig->GetSPIMode();
			unsigned nCPHA = (nSPIMode & 1) ? 1 : 0;
			unsigned nCPOL = (nSPIMode & 2) ? 1 : 0;
			LOGDBG("SPI: CPOL=%u; CPHA=%u; CLK=%u",nCPOL,nCPHA,nSPIClock);
			m_pST7789Display = new CST7789Display (m_pSPIMaster,
							m_pConfig->GetST7789Data(),
							m_pConfig->GetST7789Reset(),
							m_pConfig->GetST7789Backlight(),
							m_pConfig->GetST7789Width(),
							m_pConfig->GetST7789Height(),
							nCPOL, nCPHA, nSPIClock,
							m_pConfig->GetST7789Select());
			if (m_pST7789Display->Initialize())
			{
				m_pST7789Display->SetRotation (m_pConfig->GetST7789Rotation());
				//bool bLargeFont = !(m_pConfig->GetST7789SmallFont());
				m_pST7789 = new CST7789Device (m_pSPIMaster, m_pST7789Display, m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows (), Font8x16, false, false);
				if (m_pST7789->Initialize())
				{
					LOGDBG ("LCD: ST7789");
					m_pLCD = m_pST7789;
				}
				else
				{
					LOGDBG ("LCD: Failed to initalize ST7789 character device");
					delete (m_pST7789);
					delete (m_pST7789Display);
					m_pST7789 = nullptr;
					m_pST7789Display = nullptr;
					return false;
				}
			}
			else
			{
				LOGDBG ("LCD: Failed to initialize ST7789 display");
				delete (m_pST7789Display);
				m_pST7789Display = nullptr;
				return false;
			}
		}
		else if (i2caddr == 0)
		{
			m_pHD44780 = new CHD44780Device (m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows (),
							 m_pConfig->GetLCDPinData4 (),
							 m_pConfig->GetLCDPinData5 (),
							 m_pConfig->GetLCDPinData6 (),
							 m_pConfig->GetLCDPinData7 (),
							 m_pConfig->GetLCDPinEnable (),
							 m_pConfig->GetLCDPinRegisterSelect (),
							 m_pConfig->GetLCDPinReadWrite ());
			if (!m_pHD44780->Initialize ())
			{
				LOGDBG("LCD: HD44780 initialization failed");
				return false;
			}
			LOGDBG ("LCD: HD44780");
			m_pLCD = m_pHD44780;
		}
		else
		{
			m_pHD44780 = new CHD44780Device (m_pI2CMaster, i2caddr,
							m_pConfig->GetLCDColumns (), m_pConfig->GetLCDRows ());
			if (!m_pHD44780->Initialize ())
			{
				LOGDBG("LCD: HD44780 (I2C) initialization failed");
				return false;
			}
			LOGDBG ("LCD: HD44780 I2C");
			m_pLCD = m_pHD44780;
		}
		assert (m_pLCD);

		m_pLCDBuffered = new CWriteBufferDevice (m_pLCD);
		assert (m_pLCDBuffered);

		LCDWrite ("\x1B[?25l\x1B""d+");		// cursor off, autopage mode

		{
			unsigned nCols = m_pConfig->GetLCDColumns ();
			if (nCols == 0 || nCols > 40)
			{
				nCols = 40;
			}

			char BlankLine[41];
			memset (BlankLine, ' ', nCols);
			BlankLine[nCols] = '\0';

			for (unsigned nPass = 0; nPass < 5; nPass++)
			{
				LCDWrite ("\x1B[H");
				LCDWrite (BlankLine);
				LCDWrite ("\n");
				LCDWrite (BlankLine);
				m_pLCDBuffered->Update ();
				CTimer::SimpleMsDelay (120);
			}

			CTimer::SimpleMsDelay (150);
		}

		{
			unsigned nCols = m_pConfig->GetLCDColumns ();
			if (nCols == 0 || nCols > 40)
			{
				nCols = 40;
			}

			char Line1[41];
			char Line2[41];

			memset (Line1, ' ', nCols);
			memset (Line2, ' ', nCols);

			Line1[nCols] = '\0';
			Line2[nCols] = '\0';

			const char *pLine1 = "Start MiniJV880";
			const char *pLine2Prefix = "version ";

			size_t nLine1Len = strlen (pLine1);
			if (nLine1Len > nCols)
			{
				nLine1Len = nCols;
			}
			memcpy (Line1, pLine1, nLine1Len);

			size_t nLine2PrefixLen = strlen (pLine2Prefix);
			if (nLine2PrefixLen > nCols)
			{
				nLine2PrefixLen = nCols;
			}
			memcpy (Line2, pLine2Prefix, nLine2PrefixLen);

			if (nLine2PrefixLen < nCols)
			{
				size_t nVersionLen = strlen (VERSION_SHORT);
				if (nVersionLen > nCols - nLine2PrefixLen)
				{
					nVersionLen = nCols - nLine2PrefixLen;
				}
				memcpy (&Line2[nLine2PrefixLen], VERSION_SHORT, nVersionLen);
			}

			LCDWrite ("\x1B[H");
			LCDWrite (Line1);
			LCDWrite (Line2);
			m_pLCDBuffered->Update ();
		}

		LOGDBG ("LCD initialized");
	}	
	return true;
}

void CUserInterface::LCDWrite (const char *pString)
{
	if (m_pLCDBuffered)
	{
		m_pLCDBuffered->Write (pString, strlen (pString));
	}
}

void CUserInterface::EncoderEventHandler(CKY040::TEvent Event)
{

    // ==========================================
    // === OVERLAY MENUS MODE (SR / SYX) ========
    // ==========================================
    if (m_pMiniJV880->IsSRMenuActive() || m_pMiniJV880->IsRD500MenuActive() || m_pMiniJV880->IsRD500PatchBrowseActive() || m_pMiniJV880->IsSYXMenuActive())
    {
        switch (Event)
        {
        case CKY040::EventClockwise:
            if (m_pMiniJV880->IsSYXMenuActive())
            {
                m_pMiniJV880->NextSYX();
            }
            else if (m_pMiniJV880->IsSRMenuActive())
            {
                m_pMiniJV880->NextSR();
            }
            else if (m_pMiniJV880->IsRD500MenuActive())
            {
                m_pMiniJV880->NextRD500Bank();
            }
            else
            {
                m_pMiniJV880->NextRD500Patch();

                m_pMiniJV880->mcu.m_globalPause.store(true, std::memory_order_release);

                while (!m_pMiniJV880->core2_idle.load(std::memory_order_acquire) ||
                       !m_pMiniJV880->core3_idle.load(std::memory_order_acquire))
                {
                    CTimer::SimpleMsDelay(1);
                }

                m_pMiniJV880->ApplySelectedRD500Patch();

                __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELEASE);
                __atomic_store_n(&sample_read_idx,  0u, __ATOMIC_RELEASE);
                memset(sample_buffer, 0, sizeof(sample_buffer));

                m_pMiniJV880->mcu.m_globalPause.store(false, std::memory_order_release);
            }
            break;

        case CKY040::EventCounterclockwise:
            if (m_pMiniJV880->IsSYXMenuActive())
            {
                m_pMiniJV880->PrevSYX();
            }
            else if (m_pMiniJV880->IsSRMenuActive())
            {
                m_pMiniJV880->PrevSR();
            }
            else if (m_pMiniJV880->IsRD500MenuActive())
            {
                m_pMiniJV880->PrevRD500Bank();
            }
            else
            {
                m_pMiniJV880->PrevRD500Patch();

                m_pMiniJV880->mcu.m_globalPause.store(true, std::memory_order_release);

                while (!m_pMiniJV880->core2_idle.load(std::memory_order_acquire) ||
                       !m_pMiniJV880->core3_idle.load(std::memory_order_acquire))
                {
                    CTimer::SimpleMsDelay(1);
                }

                m_pMiniJV880->ApplySelectedRD500Patch();

                __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELEASE);
                __atomic_store_n(&sample_read_idx,  0u, __ATOMIC_RELEASE);
                memset(sample_buffer, 0, sizeof(sample_buffer));

                m_pMiniJV880->mcu.m_globalPause.store(false, std::memory_order_release);
            }
            break;

        case CKY040::EventSwitchDown:
            m_bSwitchPressed = true;
            break;

        case CKY040::EventSwitchUp:
            m_bSwitchPressed = false;
            break;

        case CKY040::EventSwitchHold:
            if (m_pRotaryEncoder->GetHoldSeconds() >= 120)
            {
                delete m_pLCD;
                reboot();
            }
            break;

        default:
            break;
        }

        return;   // 🚫 Do not propagate to the firmware
    }
    
    // ==================================
    // === NORMAL FIRMWARE BEHAVIOUR ===
    // ==================================
    switch (Event)
    {
    case CKY040::EventSwitchDown:
        m_bSwitchPressed = true;
        break;

    case CKY040::EventSwitchUp:
        m_bSwitchPressed = false;
        break;

    case CKY040::EventClockwise:
        if (m_bSwitchPressed)
        {
            m_pUIButtons->ResetButton(m_pConfig->GetButtonPinEnter());
        }
        else
        {
            m_pMiniJV880->mcu.MCU_EncoderTrigger(1);
        }
        break;

    case CKY040::EventCounterclockwise:
        if (m_bSwitchPressed)
        {
            m_pUIButtons->ResetButton(m_pConfig->GetButtonPinEnter());
        }
        else
        {
            m_pMiniJV880->mcu.MCU_EncoderTrigger(0);
        }
        break;

    case CKY040::EventSwitchHold:
        if (m_pRotaryEncoder->GetHoldSeconds() >= 120)
        {
            delete m_pLCD;
            reboot();
        }
        break;

    default:
        break;
    }
}


void CUserInterface::EncoderEventStub (CKY040::TEvent Event, void *pParam)
{
	CUserInterface *pThis = static_cast<CUserInterface *> (pParam);
	assert (pThis != 0);

	pThis->EncoderEventHandler (Event);
}

void CUserInterface::UIButtonsEventHandler (CUIButton::BtnEvent Event)
{
    if (Event == CUIButton::BtnEventNone)
        return;
        
    //char buf[32];
    //sprintf(buf, "EV=%d\r\n", (int)Event);
    //DebugTX::WriteString(buf);      

    uint32_t btn = 0;

    switch (Event)
    {
        case CUIButton::BtnEventPreview:
            btn = 1 << MCU_BUTTON_PREVIEW;
            break;

        case CUIButton::BtnEventLeft:
            btn = 1 << MCU_BUTTON_CURSOR_L;
            break;

        case CUIButton::BtnEventRight:
            btn = 1 << MCU_BUTTON_CURSOR_R;
            break;

        case CUIButton::BtnEventData:
        {
            // DATA is handled at a low level in CMiniJV880::Process() via raw edge.
            // Here we consume it only to avoid sending it to the firmware.
            DBG("DATA_EVENT_CONSUMED_UI");
            return;
        }
            
        case CUIButton::BtnEventToneSelect:
            btn = 1 << MCU_BUTTON_TONE_SELECT;
            break;

        case CUIButton::BtnEventPatchPerform:
        {   
            DBG("EV_PATCHPERF");
            btn = 1 << MCU_BUTTON_PATCH_PERFORM;
            DBG_HEX("BTN_FROM_PATCHPERF", btn);
            break;
        }   

        case CUIButton::BtnEventEdit:
            btn = 1 << MCU_BUTTON_EDIT;
            break;

        case CUIButton::BtnEventSystem:
        {
            DBG("EV_SYSTEM");
            btn = 1 << MCU_BUTTON_SYSTEM;
            DBG_HEX("BTN_FROM_SYSTEM", btn);
            break;
        }

        case CUIButton::BtnEventRhythm:
        {
            DBG("EV_RHYTHM");
            btn = 1 << MCU_BUTTON_RHYTHM;
            DBG_HEX("BTN_FROM_RHYTHM", btn);
            break;
        }

        case CUIButton::BtnEventUtility:
            btn = 1 << MCU_BUTTON_UTILITY;
            break;

        case CUIButton::BtnEventMute:
            btn = 1 << MCU_BUTTON_MUTE;
            break;


        case CUIButton::BtnEventCompare:
            btn = 1 << MCU_BUTTON_COMPARE;
            break;

        case CUIButton::BtnEventEnter:
        {
              if (m_pMiniJV880->ConsumeBlockedRD500Enter())
              {
                  return; // swallow ENTER release after RD-500 longpress back
              }

              // If the SR menu overlay is active
              if (m_pMiniJV880->IsSRMenuActive())
              {
                // Special entry: RD-500
                if (m_pMiniJV880->IsRD500Selected())
                {
                    m_pMiniJV880->OpenRD500Menu();
                    return; // Do not send ENTER to the firmware
                }

                // Real SR entries only
                if (m_pMiniJV880->HasSRError() || !m_pMiniJV880->HasSRList())
                {
                   m_pMiniJV880->RequestCloseSRMenu();
                   return; // Do not send ENTER to the firmware
                }          
         
                // Pause the cores
                m_pMiniJV880->mcu.m_globalPause.store(true, std::memory_order_release);

                // Wait until cores 2 and 3 are actually idle
                while (!m_pMiniJV880->core2_idle.load(std::memory_order_acquire) ||
                      !m_pMiniJV880->core3_idle.load(std::memory_order_acquire))
                {
                  CTimer::SimpleMsDelay(1);
                }

              // Now it is safe to modify state / ROM / reset
              m_pMiniJV880->ApplySelectedSR();

              // Reset audio buffer
              __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELEASE);
              __atomic_store_n(&sample_read_idx,  0u, __ATOMIC_RELEASE);
              memset(sample_buffer, 0, sizeof(sample_buffer));

              // Resume
              m_pMiniJV880->mcu.m_globalPause.store(false, std::memory_order_release);

              m_pMiniJV880->CloseSRMenu();
              return; // Do not send ENTER to the firmware
          }

          // ENTER on RD-500 bank menu -> open patch browse only if resources are valid
          if (m_pMiniJV880->IsRD500MenuActive())
          {
              if (m_pMiniJV880->IsRD500Available())
                  m_pMiniJV880->OpenRD500PatchBrowse();

              return; // Do not send ENTER to the firmware
          }

          // In RD-500 patch browse, short ENTER applies the selected patch.
          // Back navigation is handled by long ENTER in CMiniJV880::Process().
          if (m_pMiniJV880->IsRD500PatchBrowseActive())
          {
              m_pMiniJV880->mcu.m_globalPause.store(true, std::memory_order_release);

              while (!m_pMiniJV880->core2_idle.load(std::memory_order_acquire) ||
                     !m_pMiniJV880->core3_idle.load(std::memory_order_acquire))
              {
                  CTimer::SimpleMsDelay(1);
              }

              m_pMiniJV880->ApplySelectedRD500Patch();

              __atomic_store_n(&sample_write_idx, 0u, __ATOMIC_RELEASE);
              __atomic_store_n(&sample_read_idx,  0u, __ATOMIC_RELEASE);
              memset(sample_buffer, 0, sizeof(sample_buffer));

              m_pMiniJV880->mcu.m_globalPause.store(false, std::memory_order_release);
              return; // Do not send ENTER to the firmware
          }

          // Normal behavior (firmware ENTER)
          btn = 1 << MCU_BUTTON_ENTER;
          break;
        }
    } 
    // THIS must stay INSIDE the function
    m_pMiniJV880->mcu.mcu_button_pressed = btn;
}

void CUserInterface::UIButtonsEventStub (CUIButton::BtnEvent Event, void *pParam)
{
	CUserInterface *pThis = static_cast<CUserInterface *> (pParam);
	assert (pThis != 0);

	pThis->UIButtonsEventHandler (Event);
}

void CUserInterface::TriggerUIButtonEvent(CUIButton::BtnEvent event)
{
    if (event != CUIButton::BtnEventNone)
    {
        UIButtonsEventHandler(event);
    }
}


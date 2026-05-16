#include "robustencoder.h"

#include <assert.h>

CRobustEncoder::CRobustEncoder (unsigned nCLKPin,
                                unsigned nDTPin,
                                unsigned nSWPin,
                                CGPIOManager *pGPIOManager)
:	m_CLKPin (nCLKPin, GPIOModeInputPullUp, pGPIOManager),
	m_DTPin (nDTPin, GPIOModeInputPullUp, pGPIOManager),
	m_bPollingMode (!pGPIOManager),
	m_bInterruptConnected (FALSE),
	m_pEventHandler (nullptr),
	m_pEventParam (nullptr),
	m_nLastCode (3),
	m_nAccumulator (0)
{
	// MiniJV880 handles ENTER/switch separately via CUIButtons.
	// Keep the constructor shape close to CKY040 for easy integration.
	(void) nSWPin;
}

CRobustEncoder::~CRobustEncoder (void)
{
	if (m_bInterruptConnected)
	{
		m_pEventHandler = nullptr;

		m_CLKPin.DisableInterrupt2 ();
		m_CLKPin.DisableInterrupt ();
		m_CLKPin.DisconnectInterrupt ();

		m_DTPin.DisableInterrupt2 ();
		m_DTPin.DisableInterrupt ();
		m_DTPin.DisconnectInterrupt ();
	}
}

boolean CRobustEncoder::Initialize (void)
{
	unsigned nCLK = m_CLKPin.Read ();
	unsigned nDT = m_DTPin.Read ();
	assert (nCLK <= 1);
	assert (nDT <= 1);

	m_nLastCode = (nCLK << 1) | nDT;
	m_nAccumulator = 0;

	if (!m_bPollingMode)
	{
		assert (!m_bInterruptConnected);
		m_bInterruptConnected = TRUE;

		m_CLKPin.ConnectInterrupt (EncoderInterruptHandler, this);
		m_DTPin.ConnectInterrupt (EncoderInterruptHandler, this);

		m_CLKPin.EnableInterrupt (GPIOInterruptOnFallingEdge);
		m_CLKPin.EnableInterrupt2 (GPIOInterruptOnRisingEdge);

		m_DTPin.EnableInterrupt (GPIOInterruptOnFallingEdge);
		m_DTPin.EnableInterrupt2 (GPIOInterruptOnRisingEdge);
	}

	return TRUE;
}

void CRobustEncoder::RegisterEventHandler (TEventHandler *pHandler, void *pParam)
{
	assert (!m_pEventHandler);
	m_pEventHandler = pHandler;
	assert (m_pEventHandler);
	m_pEventParam = pParam;
}

unsigned CRobustEncoder::GetHoldSeconds (void) const
{
	return 0;
}

void CRobustEncoder::Update (void)
{
	assert (m_bPollingMode);

	EncoderInterruptHandler (this);
}

void CRobustEncoder::EncoderInterruptHandler (void *pParam)
{
	CRobustEncoder *pThis = static_cast<CRobustEncoder *> (pParam);
	assert (pThis != 0);

	unsigned nCLK = pThis->m_CLKPin.Read ();
	unsigned nDT = pThis->m_DTPin.Read ();
	assert (nCLK <= 1);
	assert (nDT <= 1);

	unsigned nCode = (nCLK << 1) | nDT;
	unsigned nIndex = (pThis->m_nLastCode << 2) | nCode;

	// Observed MiniJV880 wiring:
	// CW:  11 -> 01 -> 00 -> 10 -> 11
	// CCW: 11 -> 10 -> 00 -> 01 -> 11
	static const int Delta[16] =
	{
		 0, -1, +1,  0,
		+1,  0,  0, -1,
		-1,  0,  0, +1,
		 0, +1, -1,  0
	};

	int nDelta = Delta[nIndex];
	pThis->m_nLastCode = nCode;

	if (nDelta != 0)
	{
		pThis->m_nAccumulator += nDelta;
	}

	// Emit only when the encoder returns to idle 11.
	// Accept +/-3 to tolerate one missed/noisy transition.
	if (nCode != 3)
	{
		return;
	}

	TEvent Event = EventUnknown;

	if (pThis->m_nAccumulator >= 3)
	{
		Event = EventClockwise;
	}
	else if (pThis->m_nAccumulator <= -3)
	{
		Event = EventCounterclockwise;
	}

	// End of detent: reset also for too-small/noisy movements.
	pThis->m_nAccumulator = 0;

	if (   Event != EventUnknown
	    && pThis->m_pEventHandler)
	{
		(*pThis->m_pEventHandler) (Event, pThis->m_pEventParam);
	}
}

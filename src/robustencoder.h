#ifndef _minijv880_robustencoder_h
#define _minijv880_robustencoder_h

#include <circle/gpiomanager.h>
#include <circle/gpiopin.h>
#include <circle/types.h>

// Local MiniJV880 rotary encoder driver.
//
// This intentionally avoids modifying Circle's CKY040 driver.
// It uses a Gray-code transition accumulator and emits one event
// when the encoder returns to idle CLK=1 / DT=1.
// Threshold +/-3 tolerates one missed/noisy transition per detent.
class CRobustEncoder
{
public:
	enum TEvent
	{
		EventClockwise,
		EventCounterclockwise,

		EventSwitchDown,
		EventSwitchUp,
		EventSwitchClick,
		EventSwitchDoubleClick,
		EventSwitchTripleClick,
		EventSwitchHold,

		EventUnknown
	};

	typedef void TEventHandler (TEvent Event, void *pParam);

public:
	CRobustEncoder (unsigned nCLKPin,
	                unsigned nDTPin,
	                unsigned nSWPin,
	                CGPIOManager *pGPIOManager = 0);

	~CRobustEncoder (void);

	boolean Initialize (void);

	void RegisterEventHandler (TEventHandler *pHandler, void *pParam = 0);

	unsigned GetHoldSeconds (void) const;

	void Update (void);

private:
	static void EncoderInterruptHandler (void *pParam);

private:
	CGPIOPin m_CLKPin;
	CGPIOPin m_DTPin;

	boolean m_bPollingMode;
	boolean m_bInterruptConnected;

	TEventHandler *m_pEventHandler;
	void *m_pEventParam;

	unsigned m_nLastCode;
	int m_nAccumulator;
};

#endif

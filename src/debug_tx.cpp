#include "debug_tx.h"

#include <circle/gpiopin.h>
#include <circle/timer.h>
#include <circle/synchronize.h>   // DisableIRQs(), EnableIRQs()

// =====================
// Config
// =====================
#define DEBUG_GPIO 4

// Più tollerante con jitter post-splash (audio/IRQ/multicore)
#define BAUD_RATE 38400

#define BIT_TIME_US (1000000 / BAUD_RATE)

// Ring buffer: deve essere una potenza di 2
static constexpr uint32_t TXBUF_SIZE = 2048;
static char s_TxBuf[TXBUF_SIZE];
static volatile uint32_t s_TxW = 0;
static volatile uint32_t s_TxR = 0;

static CGPIOPin* s_Pin = nullptr;

static inline bool TxBufEmpty()
{
    return s_TxW == s_TxR;
}

static inline bool TxBufFull()
{
    return ((s_TxW + 1) & (TXBUF_SIZE - 1)) == s_TxR;
}

static inline char hex_digit(uint32_t v)
{
    v &= 0xF;
    return (v < 10) ? (char)('0' + v) : (char)('A' + (v - 10));
}

// =====================
// DebugTX
// =====================
void DebugTX::Init()
{
    // Fix 1: Init idempotente (puoi richiamarlo dopo GPIOManager o altre init)
    if (s_Pin)
    {
        delete s_Pin;
        s_Pin = nullptr;
    }

    s_Pin = new CGPIOPin(DEBUG_GPIO, GPIOModeOutput);
    s_Pin->Write(1); // idle HIGH
}

void DebugTX::TxCharBlocking(char c)
{
    if (!s_Pin)
        return;

    // Fix 2 (parte A): proteggi il bit-bang dal jitter degli IRQ
    DisableIRQs();

    // Start bit (LOW)
    s_Pin->Write(0);
    CTimer::SimpleusDelay(BIT_TIME_US);

    // 8 data bits, LSB first
    for (int i = 0; i < 8; i++)
    {
        s_Pin->Write((c >> i) & 1);
        CTimer::SimpleusDelay(BIT_TIME_US);
    }

    // Stop bit (HIGH)
    s_Pin->Write(1);
    CTimer::SimpleusDelay(BIT_TIME_US);

    EnableIRQs();
}

void DebugTX::WriteChar(char c)
{
    // Fix 2 (parte B): non bloccare -> accoda nel ring buffer
    if (TxBufFull())
    {
        // Buffer pieno: droppa (meglio perdere log che glitchare realtime)
        return;
    }

    uint32_t w = s_TxW;
    s_TxBuf[w] = c;
    s_TxW = (w + 1) & (TXBUF_SIZE - 1);
}

void DebugTX::WriteString(const char* s)
{
    if (!s) return;

    while (*s)
        WriteChar(*s++);
}

void DebugTX::WriteHex(const char* prefix, uint32_t value)
{
    if (prefix && *prefix)
        WriteString(prefix);

    WriteString(": 0x");

    // 8 nibbles = 32 bit
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        WriteChar(hex_digit(value >> shift));
    }

    WriteString("\r\n");
}

void DebugTX::Poll()
{
    // Fix 2 (parte C): flush controllato (quota per giro)
    // 64 char per iterazione è un buon compromesso.
    for (int i = 0; i < 64; i++)
    {
        if (TxBufEmpty())
            break;

        char c = s_TxBuf[s_TxR];
        s_TxR = (s_TxR + 1) & (TXBUF_SIZE - 1);

        TxCharBlocking(c);
    }
}

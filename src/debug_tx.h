#pragma once
#include <stdint.h>
#include <stddef.h>

class DebugTX
{
public:
    static void Init();

    // Accoda (non blocca)
    static void WriteChar(char c);
    static void WriteString(const char* s);

    // Stampa: "<prefix>: 0x1234ABCD\r\n"
    static void WriteHex(const char* prefix, uint32_t value);

    // Da chiamare spesso nel loop principale (core0)
    static void Poll();

private:
    static void TxCharBlocking(char c);   // bit-bang vero e proprio (protetto)
};

// DBG base
#define DBG(x) do { DebugTX::WriteString(x); DebugTX::WriteString("\r\n"); } while(0)

// DBG_HEX usato dal tuo minijv880.cpp
#define DBG_HEX(prefix, value) do { DebugTX::WriteHex((prefix), (uint32_t)(value)); } while(0)

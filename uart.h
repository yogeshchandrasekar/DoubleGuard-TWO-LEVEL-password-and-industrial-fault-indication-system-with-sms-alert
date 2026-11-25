#ifndef UART_H
#define UART_H

#include <lpc21xx.h>

void uart_config(void)
{
    PINSEL0 |= 0x05;          // P0.0 = TXD0, P0.1 = RXD0
    U0LCR = 0x83;             // 8N1, DLAB = 1
    U0DLL = 97;               // 9600 baud @ PCLK = 15 MHz
    U0DLM = 0x00;
    U0LCR = 0x03;             // DLAB = 0
}

void uart_tx(unsigned char tx)
{
    while (!(U0LSR & 0x20));  // wait for THRE
    U0THR = tx;
}

void uart_str(const unsigned char *s)
{
    while (*s)
        uart_tx(*s++);
}

void uart_int(unsigned int n)
{
    unsigned char buf[10];
    int i = 0;

    if (n == 0) { uart_tx('0'); return; }

    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    while (i > 0) uart_tx(buf[--i]);
}

#endif

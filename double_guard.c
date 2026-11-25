#include "lcd.h"
#include "I2C.H"
#include "EEPROM.H"
#include "KEYPAD.H"
#include "random_number.h"
#include "uart.h"

#define MAX_ATTEMPTS 4
#define PASS_LEN     6
#define OTP_LEN      4
#define MAGIC_ADDR   0x20
#define MAGIC_VAL    0xA5
#define PHONE        "+916381731967"   // country code, no spaces

#define TEST_CALL    1                 // 1 = ring the phone at startup, 0 = skip

/* ---------- MCP3204 + LM35 (hardware SPI0) ---------- */
#define ADC_CS_PIN       (1<<7)        // P0.7 chip select
#define ADC_VREF_MV      3300UL        // MCP3204 VREF in mV
#define LM35_CH          0             // MCP3204 CH0 (pin 1)

#define TEMP_ALERT_T10   400           // alert limit in tenths of a degree (400 = 40.0 C)
#define SMS_STR(s)       uart_str((const unsigned char *)(s))

unsigned char attempts = 0;
unsigned long entered  = 0;
unsigned int  otp      = 0;

void LCD_SCROLL_TITLE(char *title, unsigned int speed_ms)
{
    unsigned char len = 0;
    unsigned char i, j;
    int idx;

    while (title[len]) len++;               // string length

    for (i = 0; i < (16 + len); i++)
    {
        LCD_COMMAND(0x80);                  // line 1, column 0

        for (j = 0; j < 16; j++)
        {
            idx = (int)i - 15 + j;
            if (idx >= 0 && idx < len)
                LCD_DATA(title[idx]);
            else
                LCD_DATA(' ');
        }
        delay_ms(speed_ms);
    }
}

//servo code init

void servo_init(void)
{
    // P0.21 as PWM5 (PINSEL1 bits 11:10 = 01)
    PINSEL1 &= ~(3 << 10);
    PINSEL1 |=  (1 << 10);

    PWMPR  = 14;            // 15 MHz / 15 = 1 MHz -> 1 us per tick

    PWMMR0 = 20000;         // 20 ms period (50 Hz)
    PWMMR5 = 1500;          // 1.5 ms pulse = center

    PWMMCR = (1 << 1);      // reset TC on match with MR0

    PWMLER = (1 << 0) | (1 << 5);   // latch MR0 and MR5

    PWMPCR = (1 << 13);     // enable PWM5 output (single edge)

    PWMTCR = (1 << 1);                  // reset counter
    PWMTCR = (1 << 0) | (1 << 3);       // counter enable + PWM enable
}

void servo_write(unsigned int pulse_us)
{
    PWMMR5 = pulse_us;
    PWMLER = (1 << 5);      // latch new value at next period
}

void servo_test(void)
{
    servo_write(1000);      // 0 degrees
    delay_ms(1000);

    servo_write(1500);      // 90 degrees
    delay_ms(1000);

    servo_write(2000);      // 180 degrees
    delay_ms(1000);
}

/* ================= MCP3204 over hardware SPI0 ================= */

void SPI0_INIT(void)
{
    PINSEL0 &= ~0x0000FF00;     // clear P0.4-P0.7 function bits (P0.7 = GPIO for CS)
    PINSEL0 |=  0x00001500;     // P0.4 SCK0, P0.5 MISO0, P0.6 MOSI0
    S0SPCCR  = 150;             // 15 MHz / 150 = 100 kHz
    S0SPCR   = 0x20;            // master, CPOL=0, CPHA=0, MSB first
    IODIR0  |= ADC_CS_PIN;
    IOSET0   = ADC_CS_PIN;      // CS idle high
}

unsigned char SPI0_XFER(unsigned char data)
{
    unsigned char stat = S0SPSR;    // read status to clear SPIF
    (void)stat;
    S0SPDR = data;
    while (!(S0SPSR & 0x80));       // wait for transfer complete
    return S0SPDR;
}

unsigned int Read_ADC_MCP3204(unsigned char ch)
{
    unsigned char hByte, lByte;

    IOCLR0 = ADC_CS_PIN;                        // select
    SPI0_XFER(0x06);                            // start=1, SGL=1, D2=0
    hByte = SPI0_XFER((ch & 0x03) << 6);        // D1 D0 -> returns B11..B8
    lByte = SPI0_XFER(0x00);                    // returns B7..B0
    IOSET0 = ADC_CS_PIN;                        // deselect

    return ((hByte & 0x0F) << 8) | lByte;       // 0 - 4095
}

// Returns tenths of a degree C (305 = 30.5 C)
unsigned int LM35_READ_T10(void)
{
    unsigned long sum = 0;
    unsigned char i;

    for (i = 0; i < 8; i++)                     // average 8 samples
    {
        sum += Read_ADC_MCP3204(LM35_CH);
        delay_ms(5);
    }
    sum /= 8;

    // LM35 = 10 mV/C, so mV value equals tenths of a degree
    return (unsigned int)((sum * ADC_VREF_MV) / 4095UL);
}

/* ================= Temperature SMS ================= */

static void sms_begin(void)
{
    SMS_STR("AT\r");
    delay_ms(3000);
    SMS_STR("AT+CMGF=1\r");
    delay_ms(3000);
    SMS_STR("AT+CMGS=\"" PHONE "\"\r");
    delay_ms(3000);                         // wait for the '>' prompt
}

static void sms_end(void)
{
    uart_tx(0x1A);                          // Ctrl+Z sends the message
    delay_ms(8000);                         // wait for the network
}

// Sends a value in tenths of a degree, e.g. 305 -> "30.5 C"
static void sms_put_temp(unsigned int t)
{
    if (t >= 1000) uart_tx((t / 1000) % 10 + '0');
    uart_tx((t / 100) % 10 + '0');
    uart_tx((t / 10)  % 10 + '0');
    uart_tx('.');
    uart_tx(t % 10 + '0');
    SMS_STR(" C");
}

void SEND_TEMP_SMS(void)
{
    unsigned int t = LM35_READ_T10();

    LCD_COMMAND(0x01);
    LCD_COMMAND(0x80);
    LCD_STRING("SENDING TEMP");
    LCD_COMMAND(0xC0);
    LCD_STRING("SMS...");

    sms_begin();

    if (t >= TEMP_ALERT_T10)
    {
        SMS_STR("ALERT! High temperature: ");
        sms_put_temp(t);
        SMS_STR(" (limit ");
        sms_put_temp(TEMP_ALERT_T10);
        SMS_STR(")");
    }
    else
    {
        SMS_STR("Temperature: ");
        sms_put_temp(t);
    }

    sms_end();

    LCD_COMMAND(0x01);
    LCD_COMMAND(0x80);
    if (t >= TEMP_ALERT_T10)
        LCD_STRING("ALERT SMS SENT");
    else
        LCD_STRING("TEMP SMS SENT");
    delay_ms(1500);
}

// Read a four/six digit code from keypad
void READ_CODE(char *prompt, unsigned char len, unsigned long *code)
{
    unsigned char digits = 0;
    unsigned char value;

    *code = 0;

    LCD_COMMAND(0x01);
    LCD_COMMAND(0x80);
    LCD_STRING(prompt);
    LCD_COMMAND(0xC0);

    while (1)
    {
        value = keyscan();

        if (value == '=')
        {
            if (digits == len)
                return;
        }
        else if (value < 10)
        {
            if (digits < len)
            {
                LCD_COMMAND(0xC0 + digits);
                LCD_DATA('*');

                *code = (*code * 10) + value;
                digits++;
            }
        }
    }
}

int main(void)
{
    TIMER1_INIT();
    LCD_INIT();
    I2C_INIT();
    servo_init();
    uart_config();
    SPI0_INIT();                            // MCP3204 on hardware SPI0

    LCD_COMMAND(0x01);                      // clear display

    LCD_COMMAND(0xC0);
    LCD_SCROLL_TITLE("DOUBLE GUARD TWO PASSWORD VERIFICATION", 300);

    delay_ms(500);
    LCD_COMMAND(0x01);
/*
#if TEST_CALL
    modem_test_call();
#endif
*/
    // Initialize EEPROM if required
    if (i2c_eeprom_read(0x50, MAGIC_ADDR) != MAGIC_VAL)
    {
        save_password(123456UL);
        delay_ms(10);
        i2c_eeprom_write(0x50, MAGIC_ADDR, MAGIC_VAL);
    }

    while (1)
    {
        if (attempts >= MAX_ATTEMPTS)
        {
            LCD_COMMAND(0x01);              // clear
            LCD_COMMAND(0x80);
            LCD_STRING("SYSTEM LOCKED");
            LCD_COMMAND(0xC0);
            LCD_STRING("RESET REQUIRED");   // 14 chars, fits the 16x2 LCD
            while (1);                      // halt until hardware reset
        }

        // PASSWORD STAGE
        READ_CODE("ENTER PASSWORD", PASS_LEN, &entered);

        if (read_password() == entered)
        {
            LCD_COMMAND(0x01);
            LCD_COMMAND(0x80);
            LCD_STRING("ACCESS OK");
            delay_ms(1500);

            // ---- generate OTP ----
            RANDOM_INIT();
            otp = RANDOM_NUMBER() % 10000;

            // ---- modem setup + send SMS ----
            LCD_COMMAND(0x01);
            LCD_COMMAND(0x80);
            LCD_STRING("SENDING OTP...");

            uart_str((const unsigned char *)"AT\r");
            delay_ms(3000);
            uart_str((const unsigned char *)"AT+CMGF=1\r");
            delay_ms(3000);

            uart_str((const unsigned char *)"AT+CMGS=\"" PHONE "\"\r");
            delay_ms(3000);                       // wait for the '>' prompt

            uart_str((const unsigned char *)"Your OTP is ");
            uart_tx((otp / 1000) % 10 + '0');
            uart_tx((otp / 100)  % 10 + '0');
            uart_tx((otp / 10)   % 10 + '0');
            uart_tx( otp         % 10 + '0');
            uart_tx(0x1A);                        // Ctrl+Z sends the message

            delay_ms(8000);                       // wait for the network

            LCD_COMMAND(0x01);
            LCD_COMMAND(0x80);
            LCD_STRING("OTP SENT");
            delay_ms(1000);

            // ---- OTP verification ----
            READ_CODE("ENTER OTP", OTP_LEN, &entered);

            if (entered == otp)
            {
                attempts = 0;

                LCD_COMMAND(0x01);
                LCD_COMMAND(0x80);
                LCD_STRING("OTP VERIFIED");
                LCD_COMMAND(0xC0);
                LCD_STRING("ACCESS GRANTED");
                delay_ms(1500);

                SEND_TEMP_SMS();                  // temperature (or alert) by SMS

                servo_test();
            }
            else
            {
                attempts++;

                LCD_COMMAND(0x01);
                LCD_COMMAND(0x80);
                LCD_STRING("WRONG OTP");
                delay_ms(2000);
            }
        }
        else
        {
            attempts++;

            LCD_COMMAND(0x01);
            LCD_COMMAND(0x80);
            LCD_STRING("WRONG PASSWORD");
            delay_ms(1500);
        }
    }
}

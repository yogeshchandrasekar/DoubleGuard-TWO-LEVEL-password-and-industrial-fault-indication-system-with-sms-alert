#include <LPC21XX.H>
#include "delay.h"

#define LCD_SHIFT  10
#define LCD_D      (0x0F << LCD_SHIFT)   // P0.10 to P0.13 -> LCD D4-D7
#define RS         (1 << 8)              // P0.8
#define E          (1 << 9)              // P0.9

void LCD_INIT(void);
void LCD_COMMAND(unsigned char);
void LCD_DATA(unsigned char);
void LCD_STRING(char *);
void LCD_INT(unsigned int);
void LCD_WRITE(unsigned char);

void LCD_INIT(void)
{
    PINSEL0 &= ~0x0FFF0000;          // P0.8 to P0.13 as GPIO (bits 27:16 only)
    IODIR0  |= LCD_D | RS | E;

    delay_ms(20);

    IOCLR0 = RS | E;

    // 4-bit initialization sequence
    LCD_WRITE(0x03);
    delay_ms(5);

    LCD_WRITE(0x03);
    delay_ms(2);

    LCD_WRITE(0x03);
    delay_ms(2);

    LCD_WRITE(0x02);

    LCD_COMMAND(0x28);  // 4-bit, 2-line, 5x7
    LCD_COMMAND(0x0C);  // Display ON, cursor OFF
    LCD_COMMAND(0x06);  // Auto increment
    LCD_COMMAND(0x01);  // Clear display

    delay_ms(2);
}

void LCD_WRITE(unsigned char data)
{
    IOCLR0 = LCD_D;
    IOSET0 = (unsigned int)(data & 0x0F) << LCD_SHIFT;   // was << 4

    IOSET0 = E;
    delay_ms(1);
    IOCLR0 = E;
}

void LCD_COMMAND(unsigned char c)
{
    IOCLR0 = RS;

    LCD_WRITE(c >> 4);     // higher nibble (was >> 10)
    LCD_WRITE(c & 0x0F);   // lower nibble

    delay_ms(2);
}

void LCD_DATA(unsigned char d)
{
    IOSET0 = RS;

    LCD_WRITE(d >> 4);     // higher nibble (was >> 10)
    LCD_WRITE(d & 0x0F);   // lower nibble

    delay_ms(2);
}

void LCD_STRING(char *str)
{
    while (*str != '\0')
    {
        LCD_DATA(*str++);
    }
}

void LCD_INT(unsigned int val)
{
    unsigned char arr[10];
    int j = 0;

    if (val == 0)
    {
        LCD_DATA('0');
        return;
    }

    while (val != 0)
    {
        arr[j++] = val % 10;
        val /= 10;
    }

    for (j--; j >= 0; j--)
    {
        LCD_DATA(arr[j] + '0');
    }
}

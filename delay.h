void delay_ms(unsigned int ms)
{
T0PR=15000-1;
T0TCR=0X01;
while(T0TC<ms);
T0TCR=0X03;
T0TCR=0X00;
}
void TIMER1_INIT(void)
{
    T1TCR = 0x02;  // Reset
    T1PR = 0;      // No prescaling
    T1TCR = 0x01;  // Start
}

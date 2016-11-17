#include <msp430.h>
#include <intrinsics.h>
#define LED1 BIT0
#define LED2 BIT6
#define UART_TXD 0x02 // TXD on P1.1 (Timer0_A.OUT0)
#define UART_RXD 0x04 // RXD on P1.2 (Timer0_A.CCI1A)
#define UART_TBIT_DIV_2     (1000000 / (9600 * 2))
#define UART_TBIT           (1000000 / 9600)
unsigned int txData;  // UART internal TX variable
volatile unsigned int mode, light, dark, hot;

void TimerA_UART_init(void);
void TimerA_UART_tx(unsigned char byte);
void TimerA_UART_print(char *string);
void flash(char id, int on, int off);
void temp(int interval, int times);

void main(void)
{
	WDTCTL = WDTPW + WDTHOLD;  // Stop watchdog timer

	ADC10CTL1 = INCH_10 + SHS_1 + CONSEQ_2;
	ADC10CTL0 = SREF_1 + ADC10SHT_3 + REFON + ADC10ON + ADC10IE;
	__delay_cycles(2000);

	DCOCTL = 0x00;             // Set DCOCLK to 1MHz
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;
	BCSCTL3 |= LFXT1S_2;

	P1OUT = 0x00;       // Initialize all GPIO
	P1SEL = UART_TXD + UART_RXD; // Use TXD/RXD pins
	P1DIR = 0xFF & ~UART_RXD; // Set pins to output
	P1DIR |= LED1 + LED2;

	__enable_interrupt();

	for (;;)
	{
		flash(1, 500, 1500);
		temp(1000, 1);
		__bis_SR_register(LPM0_bits + GIE);
		flash(0, 200, 300);
		TimerA_UART_init();
		hot = 1;
		while(hot)
		{
			TimerA_UART_print("Hot!\r\n");
			__bis_SR_register(LPM0_bits + GIE);
		}
	}
}

void TimerA_UART_print(char *string)
{
	while (*string) TimerA_UART_tx(*string++);
}

void TimerA_UART_init(void)
{
	TA0CCTL0 = OUT;   // Set TXD idle as '1'
	TA0CCTL1 = SCS + CM1 + CAP + CCIE; // CCIS1 = 0
	// Set RXD: sync, neg edge, capture, interrupt
	TA0CTL = TASSEL_2 + MC_2; // SMCLK, continuous mode
	TA0CCR1 = 0;
}
void TimerA_UART_tx(unsigned char byte)
{
	int i, parity=0;
	while (TACCTL0 & CCIE); // Ensure last char TX'd
	TA0CCR0 = TAR;      // Current state of TA counter
	TA0CCR0 += UART_TBIT; // One bit time till 1st bit
	TA0CCTL0 = OUTMOD0 + CCIE; // Set TXD on EQU0, Int
	txData = byte;       // Load global variable
	for(i=0; i<8; i++)
	{
		parity ^= (byte & 0x01);
		byte >>= 1;
	}
	if(parity) txData |= 0x80; //parity
	txData |= 0x100;    // Add stop bit to TXData
	txData <<= 1;       // Add start bit
}

#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A0_ISR(void)
{
	static unsigned char txBitCnt = 10;
	TA0CCR0 += UART_TBIT; // Set TACCR0 for next intrpt
	if (txBitCnt == 0)  // All bits TXed?
	{
		TA0CCTL0 &= ~CCIE;  // Yes, disable intrpt
		txBitCnt = 10;      // Re-load bit counter
	}
	else
	{
		if (txData & 0x01) // Check next bit to TX
		{
			TA0CCTL0 &= ~OUTMOD2; // TX '1¡¦ using OUTMODE0
		}
		else
		{
			TA0CCTL0 |= OUTMOD2; // TX '0¡¥
		}
		txData >>= 1;
		txBitCnt--;
	}
}

#pragma vector = TIMER0_A1_VECTOR
__interrupt void Timer_A1_ISR(void)
{
	static unsigned char rxBitCnt = 8;
	static unsigned char rxData = 0;
	static unsigned int count = 0;
	switch (__even_in_range(TA0IV, TA0IV_TAIFG))
	{
		case TA0IV_TACCR1:     // TACCR1 CCIFG - UART RX
			TA0CCR1 += UART_TBIT;// Set TACCR1 for next int
			if (TA0CCTL1 & CAP)  // On start bit edge
			{
				TA0CCTL1 &= ~CAP;   // Switch to compare mode
				TA0CCR1 += UART_TBIT_DIV_2;// To middle of D0
			}
			else // Get next data bit
			{
				rxData >>= 1;
				if (TA0CCTL1 & SCCI)  // Get bit from latch
				{
					rxData |= 0x80;
				}
				rxBitCnt--;
				if (rxBitCnt == 0) // All bits RXed?
				{
					rxData &= ~0x80;
					rxBitCnt = 8;       // Re-load bit counter
					TACCTL1 |= CAP;     // Switch to capture
					if(count==0)
					{
						if(rxData=='A') count++;
						else count = 0;
					}
					else if(count==1)
					{
						if(rxData=='c') count++;
						else count = 0;
					}
					else if(count==2)
					{
						if(rxData=='k') count++;
						else count = 0;
					}
					else if(count==3)
					{
						if(rxData=='!')
						{
							hot = 0;
							__bic_SR_register_on_exit(LPM0_bits);
						}
						count = 0;
					}
				}
			}
			break;
	}
}

void flash(char id, int on, int off)
{
	if(id==0)
	{
		P1OUT &= ~LED2;
		P1OUT |= LED1;
		mode = 0;
	}
	else
	{
		P1OUT &= ~LED1;
		P1OUT |= LED2;
		mode = 1;
	}

	light = on*3;
	dark = off*3;
	TA1CCR0 = light;
	TA1CCTL0 |= CCIE;
	TA1CTL = MC_1|ID_2|TASSEL_1|TACLR; //3000Hz
}


#pragma vector = TIMER1_A0_VECTOR
__interrupt void TA1_ISR (void)
{
	static unsigned int count=0;
	if(mode==0) //hot
	{
		P1OUT ^= LED1;
		count++;
		if(count==4)
		{
			count = 0;
			__bic_SR_register_on_exit(LPM0_bits);
		}
	}
	else P1OUT ^= LED2;
	if(TA1CCR0==light) TA1CCR0 = dark;
	else TA1CCR0 = light;
	TA1CTL |= TACLR;
}

void temp(int interval, int times)
{
    TA0CCR0 = interval*3;
    TA0CCR1 = interval*3-1;
    TA0CCTL1 = OUTMOD_3;
    TA0CTL = MC_1|ID_2|TASSEL_1|TACLR; //3000 Hz
    ADC10CTL0 |= ENC;
}

#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
	static unsigned int count=0, all=0;
	if(count<2)
	{
		all = all + ADC10MEM;
		count++;
	}
	if(count==2)
	{
		if(all>1474)
		{
			ADC10CTL0 &= ~ENC;
			__bic_SR_register_on_exit(LPM0_bits);
		}
		all = 0;
		count = 0;
	}
}

#include "hardware.h"
#include "string.h"
#define EN_USART1_RX 1


void usartEnbISR(void) {
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQ;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = TRUE;
    nvicInit(&NVIC_InitStructure);
}

void initUASRT(u32 pclk2 ,u32 bound) {
    float temp;
    u16 mantissa;
    u16 fraction;
    u32 rwmVal;

    temp=(float)(pclk2*1000000)/(bound*16);
    mantissa=temp;
    fraction=(temp-mantissa)*16;
    mantissa<<=4;
    mantissa+=fraction;

    rwmVal =  GET_REG(RCC_APB2ENR);
    rwmVal |= 1 << 2;   /* Enable GPIOA*/
    rwmVal |= 1 << 14;  /* Enable UASRT*/
    SET_REG(RCC_APB2ENR, rwmVal);

    /* Setup GPIOA Pin 9\10 IO status */
    rwmVal =  GET_REG(GPIO_CRH(GPIOA));
    rwmVal &= 0xFFFFF00F;
    rwmVal |= 0x000008B0;
    SET_REG(GPIO_CRH(GPIOA), rwmVal);

    /* Setup USART */
    rwmVal = mantissa;
    SET_REG(USART_BRR(USART1), rwmVal);
    rwmVal = GET_REG(USART_CR1(USART1));
    rwmVal |= 0x200C;
    SET_REG(USART_CR1(USART1), rwmVal);
#if EN_USART1_RX
    /* If enable usart rx */
    rwmVal = GET_REG(USART_CR1(USART1));
    rwmVal |= 1 << 8;
    rwmVal |= 1 << 5;
    SET_REG(USART_CR1(USART1), rwmVal);
    usartEnbISR();
#endif
}

int fputc(int ch )
{
    u32 rwmVal;
    u32 status;    
    rwmVal = (u8) ch;
    status = GET_REG(USART_SR(USART1));
    while((status & 0x40) == 0)
        status = GET_REG(USART_SR(USART1));

    SET_REG(USART_DR(USART1), rwmVal);
    return ch;
}

void fputs(u8 *ch)
{
    int i = 0;
    while(ch[i] != 0)
        fputc(ch[i++]);
}


u8 usart_buf[140];
u8 usart_i;

void USART1_IRQHandler(void)
{
    u32 rwmVal;
    u8 res = 0;
    rwmVal = GET_REG(USART_SR(USART1));
    if(rwmVal & (1 << 5)) {
	res = (u8)GET_REG(USART_DR(USART1));
	if (usart_i < 140)
	    usart_buf[usart_i++] = res;
    }
}

u8 hex_srt[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
void printf_hex(u8 *buf)
{
    u8 *p;
    p = buf;
    int i = 0;
    while(*p) {
	fputc((hex_srt[0x0f & (*p >> 4)]));
	fputc((hex_srt[0x0f & *p]));
	p++;
    }
}

#define SOH	0x01
#define EOT	0x04
#define ACK	0x06
#define NAK	0x15
#define CAN	0x18

void x_modem2flash(u32 addr,u8 *buf)
{
    int i = 0;
    u32 sum;
    flashUnlock();
    while(i < 128) {
        sum = buf[i+3]<<24 | buf[i+2] << 16 | buf[i+1] << 8 | buf[i];
        flashWriteWord(addr ,sum);
        addr += 4;
        i += 4;
    }
    flashLock();
}

#define APP_ADDR 0x08005000

int time3;
void set_time_out(int time)
{
    time3 = time;
    usart_i = 0;
}

int get_time_out(void)
{
    return time3;
}

int get_x_modem(void) 
{
    if(usart_i)
    	return 1;
    else
    	return 0;
}

int x_modem(void)
{
    u32 i ;
    i = APP_ADDR;
    time3 = 0;
    memset(usart_buf, 0, 140);
    usart_i = 0;
    flashUnlock();
    if (flashErasePages(i ,108) == TRUE) {
    	fputs("Flash Erase Pages ok\n\r");
    } else {
	fputs("Flash Erase Pages error\n\r");
    }
    flashLock();

    while(1) {
        time3 = 200;
	while(time3 && (usart_i < 132)) ;
	if(usart_i == 0) {
	    memset(usart_buf, 0, 140);
	    usart_i = 0;
	    fputc(NAK);
	} else {
	    if(usart_buf[0] == SOH) {
		if (i < 0x08020000) {
		    x_modem2flash(i ,&usart_buf[3]);
		    i += 128;
		}
		memset(usart_buf, 0, 140);
		usart_i = 0;
		fputc(ACK);
	    } else if(usart_buf[0] == EOT) {
		fputc(ACK);
		time3 = 20;
		while (time3) ;
		fputc(ACK);
		return i;
	    } else {
		fputc(NAK);
	    }
	}
    }
}


void time3_init(u16 arr ,u16 psc)
{
    u32 rwmVal;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* Enable timer3 clock */
    rwmVal = GET_REG(RCC_APB1ENR);
    rwmVal |= 1 << 1;
    SET_REG(RCC_APB1ENR ,rwmVal);	
    /* Set arr */
    rwmVal = arr;
    SET_REG(TIM3_ARR ,rwmVal);
    rwmVal = psc;
    SET_REG(TIM3_PSC ,rwmVal);

    rwmVal = GET_REG(TIM3_DIER);
    rwmVal |= 1 << 0;
    SET_REG(TIM3_DIER ,rwmVal);

    rwmVal = GET_REG(TIM3_CR1);
    rwmVal |= 0x01;
    SET_REG(TIM3_CR1 ,rwmVal);

    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQ;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = TRUE;
    nvicInit(&NVIC_InitStructure);
}

void TIM3_IRQHandler(void)
{
    u32 rwmVal;
    rwmVal = GET_REG(TIM3_SR);
    if(rwmVal & 0x0001) {
        if(time3 > 0)
	    time3--;
    }
    rwmVal &=~(1 << 0);
    SET_REG(TIM3_SR, rwmVal);
}


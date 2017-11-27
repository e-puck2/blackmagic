/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/stm32/can.h>

#include "general.h"
#include "cdcacm.h"

#define USBUART_TIMER_FREQ_HZ 1000000U /* 1us per tick */
#define USBUART_RUN_FREQ_HZ 5000U /* 200us (or 100 characters at 2Mbps) */

#define FIFO_SIZE 128

#ifdef EPUCK2
extern uint32_t uartUsed;
#else
uint32_t uartUsed = USBUSART;
#endif /* EPUCK2 */


/* RX Fifo buffer */
static uint8_t buf_rx[FIFO_SIZE];
/* Fifo in pointer, writes assumed to be atomic, should be only incremented within RX ISR */
static uint8_t buf_rx_in;
/* Fifo out pointer, writes assumed to be atomic, should be only incremented outside RX ISR */
static uint8_t buf_rx_out;

static void usbuart_run(void);

void usbuart_init(void)
{
	UART_PIN_SETUP();

#ifdef EPUCK2 //setup specific uart ports for e-puck2
	rcc_periph_clock_enable(USBUSART_ESP_CLK);

	/* Setup UART ESP parameters. */
	usart_set_baudrate(USBUSART_ESP, 38400);
	usart_set_databits(USBUSART_ESP, 8);
	usart_set_stopbits(USBUSART_ESP, USART_STOPBITS_1);
	usart_set_mode(USBUSART_ESP, USART_MODE_TX_RX);
	usart_set_parity(USBUSART_ESP, USART_PARITY_NONE);
	usart_set_flow_control(USBUSART_ESP, USART_FLOWCONTROL_NONE);
	/* Enable interrupts */
	USBUSART_ESP_CR1 |= USART_CR1_RXNEIE;
	nvic_set_priority(USBUSART_ESP_IRQ, IRQ_PRI_USBUSART);

	rcc_periph_clock_enable(USBUSART_407_CLK);

	/* Setup UART 407 parameters. */
	usart_set_baudrate(USBUSART_407, 38400);
	usart_set_databits(USBUSART_407, 8);
	usart_set_stopbits(USBUSART_407, USART_STOPBITS_1);
	usart_set_mode(USBUSART_407, USART_MODE_TX_RX);
	usart_set_parity(USBUSART_407, USART_PARITY_NONE);
	usart_set_flow_control(USBUSART_407, USART_FLOWCONTROL_NONE);

	/* Enable interrupts */
	USBUSART_407_CR1 |= USART_CR1_RXNEIE;
	nvic_set_priority(USBUSART_407_IRQ, IRQ_PRI_USBUSART);

	// if(uartUsed == USBUSART_ESP){
	// 	/* Finally enable the USART. */
	// 	usart_enable(USBUSART_ESP);
	// 	nvic_enable_irq(USBUSART_ESP_IRQ);

	// }else if(uartUsed == USBUSART_407){
	// 	/* Finally enable the USART. */
	// 	usart_enable(USBUSART_407);
	// 	nvic_enable_irq(USBUSART_407_IRQ);
	// }
#else //setup standard UART for other platforms
	rcc_periph_clock_enable(USBUSART_CLK);

	/* Setup UART parameters. */
	usart_set_baudrate(USBUSART, 38400);
	usart_set_databits(USBUSART, 8);
	usart_set_stopbits(USBUSART, USART_STOPBITS_1);
	usart_set_mode(USBUSART, USART_MODE_TX_RX);
	usart_set_parity(USBUSART, USART_PARITY_NONE);
	usart_set_flow_control(USBUSART, USART_FLOWCONTROL_NONE);
	/* Enable interrupts */
	USBUSART_CR1 |= USART_CR1_RXNEIE;
	nvic_set_priority(USBUSART_IRQ, IRQ_PRI_USBUSART);

	usart_enable(USBUSART);
	nvic_enable_irq(USBUSART_IRQ);
#endif /* EPUCK2 */

	/* Setup timer for running deferred FIFO processing */
	USBUSART_TIM_CLK_EN();
	timer_reset(USBUSART_TIM);
	timer_set_mode(USBUSART_TIM, TIM_CR1_CKD_CK_INT,
			TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(USBUSART_TIM,
			rcc_apb2_frequency / USBUART_TIMER_FREQ_HZ * 2 - 1);
	timer_set_period(USBUSART_TIM,
			USBUART_TIMER_FREQ_HZ / USBUART_RUN_FREQ_HZ - 1);

	/* Setup update interrupt in NVIC */
	nvic_set_priority(USBUSART_TIM_IRQ, IRQ_PRI_USBUSART_TIM);
	nvic_enable_irq(USBUSART_TIM_IRQ);

	/* turn the timer on */
	timer_enable_counter(USBUSART_TIM);
}

/*
 * Runs deferred processing for usb uart rx, draining RX FIFO by sending
 * characters to host PC via CDCACM.  Allowed to read from FIFO in pointer,
 * but not write to it. Allowed to write to FIFO out pointer.
 */
static void usbuart_run(void)
{
	/* forcibly empty fifo if no USB endpoint */
	if (cdcacm_get_config() != 1)
	{
		buf_rx_out = buf_rx_in;
	}

	/* if fifo empty, nothing further to do */
	if (buf_rx_in == buf_rx_out) {
		/* turn off LED, disable IRQ */
		timer_disable_irq(USBUSART_TIM, TIM_DIER_UIE);
		gpio_clear(LED_PORT_UART, LED_UART);
	}
	else
	{
		uint8_t packet_buf[CDCACM_PACKET_SIZE];
		uint8_t packet_size = 0;
		uint8_t buf_out = buf_rx_out;

		/* copy from uart FIFO into local usb packet buffer */
		while (buf_rx_in != buf_out && packet_size < CDCACM_PACKET_SIZE)
		{
			packet_buf[packet_size++] = buf_rx[buf_out++];

			/* wrap out pointer */
			if (buf_out >= FIFO_SIZE)
			{
				buf_out = 0;
			}

		}

		/* advance fifo out pointer by amount written */
		buf_rx_out += usbd_ep_write_packet(usbdev,
				CDCACM_UART_ENDPOINT, packet_buf, packet_size);
		buf_rx_out %= FIFO_SIZE;
	}
}

void usbuart_set_line_coding(struct usb_cdc_line_coding *coding)
{
	usart_set_baudrate(uartUsed, coding->dwDTERate);

	if (coding->bParityType)
		usart_set_databits(uartUsed, coding->bDataBits + 1);
	else
		usart_set_databits(uartUsed, coding->bDataBits);

	switch(coding->bCharFormat) {
	case 0:
		usart_set_stopbits(uartUsed, USART_STOPBITS_1);
		break;
	case 1:
		usart_set_stopbits(uartUsed, USART_STOPBITS_1_5);
		break;
	case 2:
		usart_set_stopbits(uartUsed, USART_STOPBITS_2);
		break;
	}

	switch(coding->bParityType) {
	case 0:
		usart_set_parity(uartUsed, USART_PARITY_NONE);
		break;
	case 1:
		usart_set_parity(uartUsed, USART_PARITY_ODD);
		break;
	case 2:
		usart_set_parity(uartUsed, USART_PARITY_EVEN);
		break;
	}
}

void usbuart_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;

	char buf[CDCACM_PACKET_SIZE];
	int len = usbd_ep_read_packet(dev, CDCACM_UART_ENDPOINT,
					buf, CDCACM_PACKET_SIZE);

#if defined(BLACKMAGIC)
	/* Don't bother if uart is disabled.
	 * This will be the case on mini while we're being debugged.
	 */
	if(!(RCC_APB2ENR & RCC_APB2ENR_USART1EN))
		return;
#endif

	gpio_set(LED_PORT_UART, LED_UART);
	// for(int i = 0; i < len; i++)
	// 	usart_send_blocking(uartUsed, buf[i]);
	// gpio_clear(LED_PORT_UART, LED_UART);
	uint8_t count = len;
	uint8_t len2 = 0;
	uint8_t* pt_buf = (uint8_t*)buf;

	while(count > 0){
		len2 = count;
		if(len2 > 8){
			len2 = 8;
		}
		while(can_transmit(CAN1,
			 0,     /* (EX/ST)ID: CAN ID */
			 false, /* IDE: CAN ID extended? */
			 false, /* RTR: Request transmit? */
			 len2,     /* DLC: Data length */
			 pt_buf) == -1){
			usbd_ep_write_packet(usbdev,
				CDCACM_UART_ENDPOINT, "transmit failed \n", 17);
		}

		pt_buf+=len2;
		count-=len2;
	}

}

#ifdef USBUART_DEBUG
int usbuart_debug_write(const char *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\n') {
			buf_rx[buf_rx_in++] = '\r';
			buf_rx_in %= FIFO_SIZE;
		}
		buf_rx[buf_rx_in++] = buf[i];
		buf_rx_in %= FIFO_SIZE;
	}
	/* enable deferred processing if we put data in the FIFO */
	timer_enable_irq(USBUSART_TIM, TIM_DIER_UIE);
	return len;
}
#endif

void usbuart_usb_in_cb(usbd_device *dev, uint8_t ep)
{
	(void) dev;
	(void) ep;
}

/*
 * Read a character from the UART RX and stuff it in a software FIFO.
 * Allowed to read from FIFO out pointer, but not write to it.
 * Allowed to write to FIFO in pointer.
 */
void usbuart_isr(void){
	uint32_t err = USART_SR(uartUsed);
	char c = usart_recv(uartUsed);
	if (err & (USART_SR_ORE | USART_SR_FE))
		return;

	/* Turn on LED */
	gpio_set(LED_PORT_UART, LED_UART);

	/* If the next increment of rx_in would put it at the same point
	* as rx_out, the FIFO is considered full.
	*/
	if (((buf_rx_in + 1) % FIFO_SIZE) != buf_rx_out)
	{
		/* insert into FIFO */
		buf_rx[buf_rx_in++] = c;

		/* wrap out pointer */
		if (buf_rx_in >= FIFO_SIZE)
		{
			buf_rx_in = 0;
		}

		/* enable deferred processing if we put data in the FIFO */
		timer_enable_irq(USBUSART_TIM, TIM_DIER_UIE);
	}
}

void USBUSART_ESP_ISR(void)
{
	usbuart_isr();
}

void USBUSART_407_ISR(void)
{
	usbuart_isr();
}

void USBUSART_TIM_ISR(void)
{
	/* need to clear timer update event */
	timer_clear_flag(USBUSART_TIM, TIM_SR_UIF);

	/* process FIFO */
	usbuart_run();
}

void CAN_RX_ISR(void)
{
	int8_t i = 0;
	uint32_t id;
	bool ext, rtr;
	uint8_t fmi, length, data[8];

	can_receive(CAN1, 0, true, &id, &ext, &rtr, &fmi, &length, data, NULL);


	for(i = 0 ; i < length ; i++){
		/* Turn on LED */
		gpio_set(LED_PORT_UART, LED_UART);

		/* If the next increment of rx_in would put it at the same point
		* as rx_out, the FIFO is considered full.
		*/
		if (((buf_rx_in + 1) % FIFO_SIZE) != buf_rx_out)
		{
			/* insert into FIFO */
			buf_rx[buf_rx_in++] = data[i];

			/* wrap out pointer */
			if (buf_rx_in >= FIFO_SIZE)
			{
				buf_rx_in = 0;
			}

			/* enable deferred processing if we put data in the FIFO */
			timer_enable_irq(USBUSART_TIM, TIM_DIER_UIE);
		}
	}

}

void usbcan_init(void)
{
	/* Enable peripheral clocks. */
	rcc_periph_clock_enable(RCC_CAN1);

	CAN_PIN_SETUP();

	/* NVIC setup. */
	nvic_enable_irq(NVIC_CAN1_RX0_IRQ);
	nvic_set_priority(NVIC_CAN1_RX0_IRQ, IRQ_PRI_CAN_RX);

	/* Reset CAN. */
	can_reset(CAN1);

	/* CAN cell init.
     * Setting the bitrate to 1MBit. APB1 = 48MHz,
     * prescaler = 3 -> 16MHz time quanta frequency.
     * 1tq sync + 9tq bit segment1 (TS1) + 6tq bit segment2 (TS2) =
     * 16time quanta per bit period, therefor 16MHz/16 = 1MHz
     */
    if (can_init(CAN1,          // Interface
             false,             // Time triggered communication mode.
             true,              // Automatic bus-off management.
             false,             // Automatic wakeup mode.
             false,             // No automatic retransmission.
             false,             // Receive FIFO locked mode.
             false,             // Transmit FIFO priority.
             CAN_BTR_SJW_1TQ,   // Resynchronization time quanta jump width
             CAN_BTR_TS1_9TQ,  	// Time segment 1 time quanta width
             CAN_BTR_TS2_6TQ,   // Time segment 2 time quanta width
             3,                 // Prescaler
             false,             // Loopback
             false)) {          // Silent
    }

    can_filter_id_mask_32bit_init(
        CAN1,
        0,      // nr
        0,      // id
        0,      // mask
        0,      // fifo
        true    // enable
    ); // match any id

	/* Enable CAN RX interrupt. */
	can_enable_irq(CAN1, CAN_IER_FMPIE0);
}

#ifdef ENABLE_DEBUG
enum {
	RDI_SYS_OPEN = 0x01,
	RDI_SYS_WRITE = 0x05,
	RDI_SYS_ISTTY = 0x09,
};

int rdi_write(int fn, const char *buf, size_t len)
{
	(void)fn;
	if (debug_bmp)
		return len - usbuart_debug_write(buf, len);

	return 0;
}

struct ex_frame {
	union {
		int syscall;
		int retval;
	};
	const int *params;
	uint32_t r2, r3, r12, lr, pc;
};

void debug_monitor_handler_c(struct ex_frame *sp)
{
	/* Return to after breakpoint instruction */
	sp->pc += 2;

	switch (sp->syscall) {
	case RDI_SYS_OPEN:
		sp->retval = 1;
		break;
	case RDI_SYS_WRITE:
		sp->retval = rdi_write(sp->params[0], (void*)sp->params[1], sp->params[2]);
		break;
	case RDI_SYS_ISTTY:
		sp->retval = 1;
		break;
	default:
		sp->retval = -1;
	}

}

asm(".globl debug_monitor_handler\n"
    ".thumb_func\n"
    "debug_monitor_handler: \n"
    "    mov r0, sp\n"
    "    b debug_monitor_handler_c\n");

#endif

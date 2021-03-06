/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include <libopencm3/stm32/f4/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/otg_fs.h>

#pragma message "Platform options : " PLATFORM_OPTIONS

jmp_buf fatal_error_jmpbuf;
extern uint32_t _ebss;

void platform_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);		// Necessary for other GPIOA
	rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_48MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
//	rcc_periph_clock_enable(RCC_GPIOD);
	rcc_periph_clock_enable(RCC_CRC);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9 |GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);

	GPIOC_OSPEEDR &=~0xF30;
	GPIOC_OSPEEDR |= 0xA20;
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE,
			TMS_PIN | TCK_PIN | TDI_PIN);

/* Can be needed for TRACESWO but NO_JTAG yet
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT,
			GPIO_PUPD_NONE,
			TDO_PIN);
*/

	gpio_set(SRST_PORT, SRST_PIN);
	gpio_set_output_options(SRST_PORT,GPIO_OTYPE_OD,GPIO_OSPEED_50MHZ,SRST_PIN);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SRST_PIN);

	SET_IDLE_STATE(false);
	SET_ERROR_STATE(false);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT,
			GPIO_PUPD_NONE,
			LED_UART | LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);


	platform_timing_init();
#ifndef PLATFORM_HAS_NO_SERIAL
	usbuart_init();
#endif
	cdcacm_init();

	/* To correct the Pull-UP on D+ management
	Done in the cdcacm_init code by call of stm32f107_usbd_init :
	// Enable VBUS sensing in device mode and power down the PHY.
	OTG_FS_GCCFG |= OTG_FS_GCCFG_VBUSBSEN | OTG_FS_GCCFG_PWRDWN;
	But for the STM32F413, FS_GCCFG is not exactly the same as the F103 one.
	Then we correct the bad OTG_FS_GCCFG_VBUSBSEN bit
	*/
	OTG_FS_GCCFG &= ~OTG_FS_GCCFG_VBUSBSEN;
	/* and use the right one */
	OTG_FS_GCCFG |= (1<<21);
	/* Disconnect/Reconnect the USB device with a delay in order to respect the bigger delay of 1,025 ms (see Table 217 of F413 Ref. Man.) */
	OTG_FS_DCTL |= OTG_FS_DCTL_SDIS;
	platform_delay(2);
	OTG_FS_DCTL &= ~OTG_FS_DCTL_SDIS;

}

void platform_srst_set_val(bool assert)
{
	volatile int i;
	if (assert) {
		gpio_clear(SRST_PORT, SRST_PIN);
		for(i = 0; i < 10000; i++) asm("nop");
	} else {
		gpio_set(SRST_PORT, SRST_PIN);
	}
}

bool platform_srst_get_val(void)
{
	return gpio_get(SRST_PORT, SRST_PIN) == 0;
}

bool platform_vbus(void)
{
	return gpio_get(VBUS_PORT, VBUS_PIN) != 0;
}

const char *platform_target_voltage(void)
{
	return "ABSENT!";
}

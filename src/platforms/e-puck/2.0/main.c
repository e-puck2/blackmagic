#include <stdio.h>
#include <string.h>

#include "main.h"

#include <usb_hub.h>
#include <power_button.h>
#include <leds.h>
#include "gdb.h"
#include "uc_usage.h"
#include "leds_states.h"

int main(void) {

	/**
	 * Special function to handle the turn on if we pressed the button without
	 * the usb cable plugged. Called before everything to catch the button pressed.
	 */
	powerButtonStartSequence();

	/*
	* System initializations.
	* - HAL initialization, this also initializes the configured device drivers
	*   and performs the board-specific initializations.
	* - Kernel initialization, the main() function becomes a thread and the
	*   RTOS is active.
	*/
	halInit();
	chSysInit();

	/*
	* Starts the handling of the power button
	*/
	powerButtonStart();

	/**
	* Starts the leds states thread
	*/
	ledsStatesStart();

	/*
	* Initializes two serial-over-USB CDC drivers and starts and connects the USB.
	*/
	usbSerialStart();

	/*
	* Starts the thread managing the USB hub
	*/
	usbHubStart();

	/*
	* Starts the GDB system
	*/
	gdbStart();

	while (true) {
		// static systime_t time_before = 0;
		// static systime_t time = 0;
		// time_before = time;
		// time = chVTGetSystemTime();

		// chprintf((BaseSequentialStream *) &SDU1,"hello 1 %d\n",time-time_before);
		printUcUsage((BaseSequentialStream *) &SDU2);
	}
}

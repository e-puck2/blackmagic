
#include "platform.h"

#include "general.h"
#include "gdb_if.h"
#include "gdb_main.h"
#include "target.h"
#include "exception.h"
#include "gdb_packet.h"
#include "morse.h"
#include "gdb.h"

//Event source used to send events to other threads
event_source_t gdb_status_event;

static THD_WORKING_AREA(gdb_thd_wa, 20480);
static THD_FUNCTION(gdb_thd, arg)
{
	(void) arg;

	chRegSetThreadName("GDB Server");

	while(1){
		volatile struct exception e;
		TRY_CATCH(e, EXCEPTION_ALL) {
			gdb_main();
		}
		if (e.type) {
			gdb_putpacketz("EFF");
			target_list_free();
			morse("TARGET LOST.", 1);
		}		
		chThdSleepMilliseconds(1);
	}
}



void gdbStart(void){
	/**
	 * Starts a Systick emulation for GDB
	 */
	platform_timing_init();

	/**
	 * Starts the GDB thread
	 */
	chThdCreateStatic(gdb_thd_wa, sizeof(gdb_thd_wa), NORMALPRIO, gdb_thd, NULL);

}

void gdbSetFlag(eventflags_t flags){
	chEvtBroadcastFlags(&gdb_status_event, flags);
}
#if defined(PLATFORM_COMMANDS_DEFINE)
/********************************************/
/* Begining of platform dedicated commands. */
/********************************************/

static bool cmd_en_esp32(target *t, int argc, const char **argv);
static bool cmd_gpio0_esp32(target *t, int argc, const char **argv);
static bool cmd_pwr_on_btn(target *t, int argc, const char **argv);
static bool cmd_vbus_hub(target *t, int argc, const char **argv);
static bool cmd_usb_charge(target *t, int argc, const char **argv);
static bool cmd_usb_500(target *t, int argc, const char **argv);
static bool cmd_reset_F407(target *t, int argc, const char **argv);
static bool cmd_esp32(target *t, int argc, const char **argv);
static bool cmd_select_mode(target *t, int argc, const char **argv);

/***************************************/
/* End of platform dedicated commands. */
/***************************************/
#undef PLATFORM_COMMANDS_DEFINE
#endif

#if defined(PLATFORM_COMMANDS_LIST)
/****************************************************/
/* Begining of List of platform dedicated commands. */
/* IMPORTANT : Each line MUST finish with "\"       */
/****************************************************/

	{"esp32", (cmd_handler)cmd_esp32, "Return the state of EN_ESP32 and GPIO0_ESP32 pins" }, \
	{"esp32_en", (cmd_handler)cmd_en_esp32, "(ON|OFF|) Set the EN_ESP32 pin or return the state of this one" }, \
	{"esp32_gpio0", (cmd_handler)cmd_gpio0_esp32, "(ON|OFF|) Set the GPIO0_ESP32 pin or return the state of this one" }, \
	{"pwr_on_btn", (cmd_handler)cmd_pwr_on_btn, "(ON|SHUTDOWN|) Power On or Shutdown the robot or return the state of Power On button" }, \
	{"vbus_hub", (cmd_handler)cmd_vbus_hub, "Return the state of VBus generated by the hub" }, \
	{"usb_charge", (cmd_handler)cmd_usb_charge, "(ON|OFF|) Set the USB_CHARGE pin or return the state of this one" }, \
	{"usb_500", (cmd_handler)cmd_usb_500, "(ON|OFF|) Set the USB_500 pin or return the state of this one" }, \
	{"reset_F407", (cmd_handler)cmd_reset_F407, "(ON|OFF|) Force the reset of F407" }, \
	{"select_mode", (cmd_handler)cmd_select_mode, "(1|2|3) Select the use of the second virtual com port over USB :\n1 = Serial monitor of the main processor and GDB over USB and Bluetooth,\n2 = Serial monitor of the ESP and GDB over USB,\n3 = ASEBA CAN-USB translator and GDB over USB and Bluetooth"}, \

/***********************************************/
/* End of List of platform dedicated commands. */
/***********************************************/
#undef PLATFORM_COMMANDS_LIST
#endif

#if defined(PLATFORM_COMMANDS_CODE)
/****************************************************/
/* Begining of Code of platform dedicated commands. */
/****************************************************/

static bool cmd_en_esp32(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("EN_ESP32 state: %s\n",
			 platform_get_en_esp32() ? "ON" : "OFF");
	else
		platform_set_en_esp32(strcmp(argv[1], "ON") == 0);
	return true;
}

static bool cmd_gpio0_esp32(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("GPIO0_ESP32 state: %s\n",
			 platform_get_gpio0_esp32() ? "ON" : "OFF");
	else
		platform_set_gpio0_esp32(strcmp(argv[1], "ON") == 0);
	return true;
}

static bool cmd_esp32(target *t, int argc, const char **argv)
{
	(void)t;
	(void) argv;
	if (argc == 1)
		gdb_outf("ESP32 state: EN %s - IO0 %s\n",
			 platform_get_en_esp32() ? "ON" : "OFF",
			 platform_get_gpio0_esp32() ? "ON" : "OFF");
	return true;
}

static bool cmd_pwr_on_btn(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("Power On button: %s\n",
			 platform_pwr_on_btn_pressed() ? "Pressed" : "Released");
	else if (strcmp(argv[1], "SHUTDOWN") == 0)
		platform_pwr_on(false);
	else if (strcmp(argv[1], "ON") == 0)
		platform_pwr_on(true);
	return true;
}

static bool cmd_vbus_hub(target *t, int argc, const char **argv)
{
	(void)t;
	(void)argv;
	if (argc == 1)
		gdb_outf("VBus: %s\n", platform_vbus_hub() ? "ON" : "OFF");
	return true;
}

static bool cmd_usb_charge(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("USB_CHARGE state: %s\n",
			 platform_get_usb_charge() ? "ON" : "OFF");
	else
		platform_set_usb_charge(strcmp(argv[1], "ON") == 0);
	return true;
}

static bool cmd_usb_500(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("USB_500 state: %s\n",
			 platform_get_usb_500() ? "ON" : "OFF");
	else
		platform_set_usb_500(strcmp(argv[1], "ON") == 0);
	return true;
}

static bool cmd_reset_F407(target *t, int argc, const char **argv)
{
	(void)t;
	if (argc == 1)
		gdb_outf("Reset F407 state: %s\n",
			gpio_get(SRST_PORT, SRST_PIN) ? "OFF" : "ON");
	else if (strcmp(argv[1], "ON") == 0)
		gpio_clear(SRST_PORT, SRST_PIN);
 	else
		gpio_set(SRST_PORT, SRST_PIN);
	return true;
}

static bool cmd_select_mode(target *t, int argc, const char **argv){
	(void)t;
	char error_message[] = "You must choose between mode 1, 2 or 3\n";
	if (argc == 1)
		gdb_outf("%s",error_message);
	else if (strcmp(argv[1], "1") == 0){
		platform_switch_monitor_to(1);
		gdb_outf("Switched to mode 1\n");
	}else if (strcmp(argv[1], "2") == 0){
 		platform_switch_monitor_to(2);
		gdb_outf("Switched to mode 2\n");
 	}else if (strcmp(argv[1], "3") == 0){
 		platform_switch_monitor_to(3);
		gdb_outf("Switched to mode 3\n");
 	}else{
 		gdb_outf("%s",error_message);
 	}
 	return true;
}

/***********************************************/
/* End of Code of platform dedicated commands. */
/***********************************************/
#undef PLATFORM_COMMANDS_CODE
#endif

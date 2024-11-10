#include <gpiod.h>

#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//! Minimum time length needed to keep pressing the button, in seconds.
#define MIN_PRESS_TIME 3

bool run = true;

//! Helper function to convert user-provided string into proper GPIO number.
//! @param[in] arg User string, possibly from application argument.
//! @return GPIO number, or UINT_MAX if number is invalid.
unsigned int convert_user_num(const char *arg) {
	char *ptr;
	unsigned long line_offset = strtoul(arg, &ptr, 10);
	
	if (ptr && (*ptr != '\0')) {
		syslog(LOG_ERR, "GPIO number parameter has invalid character '%s'.\n", ptr);
		line_offset = UINT_MAX;
	}
	//the latter is not reliable; there can be no conversion but errno is not set at all
	else if ((line_offset == ULONG_MAX) || ((!line_offset) && (errno == EINVAL))) {
		syslog(LOG_ERR, "Invalid GPIO number parameter: '%s'", arg);
		line_offset = UINT_MAX;
	}
	
	return line_offset;
}

int setup_chip(const char *chip_path, unsigned int gpio_num, struct gpiod_line_request **line_req) {
	int result = 0;
	
	struct gpiod_chip *chip = gpiod_chip_open(chip_path);
	if (!chip) {
		syslog(LOG_ERR, "Error opening chip device file: %s\n", strerror(errno));
		return errno;
	}
	
	//print chip info for user confirmation
	//**********************************************************************************
	struct gpiod_chip_info *chip_info = gpiod_chip_get_info(chip);
	
	if (chip_info) {
		syslog(LOG_INFO, "Chip name: %s\n", gpiod_chip_info_get_name(chip_info));
		syslog(LOG_INFO, "Chip label: %s\n", gpiod_chip_info_get_label(chip_info));
		syslog(LOG_INFO, "Chip line count: %zu\n", gpiod_chip_info_get_num_lines(chip_info));
		gpiod_chip_info_free(chip_info);
	}
	else {
		result = errno;
		syslog(LOG_ERR, "Error getting chip info: %s\n", strerror(result));
		goto err_1;
	}
	//**********************************************************************************
	
	//print line info for user confirmation
	//**********************************************************************************
	struct gpiod_line_info *line_info = gpiod_chip_get_line_info(chip, gpio_num);
	
	if (line_info) {
		syslog(LOG_INFO, "GPIO %u name: %s\n", gpio_num, gpiod_line_info_get_name(line_info));
		syslog(LOG_INFO, "GPIO %u direction: %d\n", gpio_num, gpiod_line_info_get_direction(line_info));
		syslog(LOG_INFO, "GPIO %u bias: %d\n", gpio_num, gpiod_line_info_get_bias(line_info));
		syslog(LOG_INFO, "GPIO %u active low: %u\n", gpio_num, gpiod_line_info_is_active_low(line_info));
		if (gpiod_line_info_is_debounced(line_info)) {
			syslog(LOG_INFO, "GPIO %u debounced at %lu usec(s).\n", gpio_num,
				gpiod_line_info_get_debounce_period_us(line_info));
		}
		else {
			syslog(LOG_INFO, "GPIO %u not debounced.\n", gpio_num);
		}
		if (gpiod_line_info_is_used(line_info)) {
			syslog(LOG_INFO, "GPIO %u in use by '%s'.\n", gpio_num, gpiod_line_info_get_consumer(line_info));
		}
		else {
			syslog(LOG_INFO, "GPIO %u not in use.\n", gpio_num);
		}
		
		gpiod_line_info_free(line_info);
	}
	else {
		result = errno;
		syslog(LOG_ERR, "Error getting line info: %s\n", strerror(result));
		goto err_1;
	}
	//**********************************************************************************
	
	//setting up line settings
	//**********************************************************************************
	struct gpiod_line_settings *line_settings = gpiod_line_settings_new();
	
	if (!line_settings) {
		result = errno;
		syslog(LOG_ERR, "Error creating line settings: %s\n", strerror(result));
		goto err_1;
	}
	
	gpiod_line_settings_set_bias(line_settings, GPIOD_LINE_BIAS_PULL_DOWN);
	gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT);
	//**********************************************************************************
	
	//setting up line config
	//**********************************************************************************
	struct gpiod_line_config *line_cfg = gpiod_line_config_new();
	
	if (!line_cfg) {
		result = errno;
		syslog(LOG_ERR, "Error creating line config: %s\n", strerror(result));
		goto err_2;
	}
	
	if (gpiod_line_config_add_line_settings(line_cfg, &gpio_num, 1, line_settings)) {
		result = errno;
		syslog(LOG_ERR, "Error adding line settings to config: %s\n", strerror(result));
		goto err_3;
	}
	
	//**********************************************************************************
	
	*line_req = gpiod_chip_request_lines(chip, NULL, line_cfg);
	if (!*line_req) {
		result = errno;
		syslog(LOG_ERR, "Error creating line request: %s\n", strerror(result));
	}
	
err_3:
	gpiod_line_config_free(line_cfg);
err_2:
	gpiod_line_settings_free(line_settings);
err_1:
	gpiod_chip_close(chip);
	chip = NULL;
	
	return result;
}

void signal_handler(int code) {
	syslog(LOG_WARNING, "Got interrupt '%d'.", code);
	run = false;
}

int main(int argc, char *argv[]) {
	int result = 0;
	
	openlog(NULL, 0, LOG_USER);
	
	if (argc <= 2) {
		syslog(LOG_INFO, "Usage: %s <path to chip device file> <GPIO number>\n", argv[0]);
		goto err_1;
	}
	
	struct gpiod_line_request *line_req;
	unsigned int line_offset = convert_user_num(argv[2u]);
	
	if (line_offset == UINT_MAX) {
		goto err_1;
	}
	
	result = setup_chip(argv[1u], line_offset, &line_req);
	if (result) {
		goto err_1;
	}
	
	{																//Ctrl-C and 'kill' interrupt handler
		struct sigaction sigProp;
		
		sigemptyset(&sigProp.sa_mask);
		sigProp.sa_handler = signal_handler;
		sigProp.sa_flags = 0;
		
		if (sigaction(SIGTERM, &sigProp, NULL)) {
			syslog(LOG_ERR, "Error setting up interrupt handler.");
			run = false;
		}
	}
	
	while (run) {
		switch (gpiod_line_request_get_value(line_req, line_offset)) {
		case GPIOD_LINE_VALUE_ACTIVE:
			if (result++ >= MIN_PRESS_TIME) {
				syslog(LOG_NOTICE, "Poweroff now.");
				system("poweroff");
				run = false;
			}
			break;
		case GPIOD_LINE_VALUE_INACTIVE:
			result = 0;
			break;
		default:
			syslog(LOG_WARNING, "Error reading line value.");
		}
		
		sleep(1u);
	}
	
	gpiod_line_request_release(line_req);
	result = 0;
	
err_1:
	closelog();
	
	return result;
}

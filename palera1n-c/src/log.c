#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <pthread.h>
#include <assert.h>

#include <ANSI-color-codes.h>
#include <palerain.h>

int p1_log(log_level_t loglevel, const char *fname, int lineno, const char *fxname, const char *__restrict format, ...)
{
    if (verbose >= 5 
#ifdef DEV_BUILD
	&& !checkrain_option_enabled(host_flags, host_option_tui)
#endif
	) fprintf(stderr, "p1_log: loglevel %d from %s:%d:%s()\n", loglevel, fname, lineno, fxname);
	int ret = 0;
	char type[0x10];
	char colour[0x10];
	char colour_bold[0x10];
	va_list args;
	va_start(args, format);
	if (verbose < (loglevel - 3) && loglevel > LOG_INFO) {
        if (verbose >= 5
#ifdef DEV_BUILD
		&& !tui_started
#endif
) fprintf(stderr, "p1_log: hid log with high log level (%d < %d)\n", verbose, loglevel - 3);
        return 0;
    }
	switch (loglevel) {
	case LOG_FATAL:
		snprintf(type, 0x10, "%s", "!");
		snprintf(colour, 0x10, "%s", RED);
		snprintf(colour_bold, 0x10, "%s", BRED);
		break;
	case LOG_ERROR:
		snprintf(type, 0x10, "%s", "Error");
		snprintf(colour, 0x10, "%s", RED);
		snprintf(colour_bold, 0x10, "%s", BRED);
		break;
	case LOG_WARNING:
		snprintf(type, 0x10, "%s", "Warning");
		snprintf(colour, 0x10, "%s", YEL);
		snprintf(colour_bold, 0x10, "%s", BYEL);
		break;
	case LOG_INFO:
		snprintf(type, 0x10, "%s", "Info");
		snprintf(colour, 0x10, "%s", CYN);
		snprintf(colour_bold, 0x10, "%s", BCYN);
		break;
	default:
		assert(loglevel >= 0);
		snprintf(type, 0x10, "%s", "Verbose");
		snprintf(colour, 0x10, "%s", WHT);
		snprintf(colour_bold, 0x10, "%s", BWHT);
		break;
	}
#ifdef DEV_BUILD
	if (checkrain_option_enabled(host_flags, host_option_tui) && tui_started) {
		newtComponent co = get_tui_log();
		if (co == NULL) {
			return 0; /* 0 bytes printed */
		}
		char printbuf[0x200];
		ret = vsnprintf(printbuf, 0x200, format, args);
	    pthread_mutex_lock(&log_mutex);
		newtTextboxSetText(co, printbuf);
	} else
#endif
	{
		pthread_mutex_lock(&log_mutex);
		char timestring[0x80];
		time_t curtime;
		time(&curtime);
		struct tm* timeinfo = localtime(&curtime);
		snprintf(timestring, 0x80, "%s[%s%02d/%02d/%d %02d:%02d:%02d%s]", CRESET, HBLK, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_year - 100, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, CRESET);
		if (verbose >= 2) {
			printf("%s| - %s%s <%s> " CRESET "%s" HBLU "%s" CRESET ":" BLU "%d" CRESET ":" BMAG "%s()" CRESET ": \n%s| ----> ", colour_bold, timestring, colour_bold, type, WHT, fname, lineno, fxname, colour_bold);
		} else {
			printf(" - %s %s<%s>%s: ", timestring, colour_bold, type, CRESET);
		}
		printf("%s", colour);
		ret = vprintf(format, args);
		va_end(args);
	
		if (verbose < 2)
			printf(CRESET "\n");
		else
			printf("\n%s-%s\n", colour, CRESET);
		fflush(stdout);
	}
	pthread_mutex_unlock(&log_mutex);
	return ret;
}

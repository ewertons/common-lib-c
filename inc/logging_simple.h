#ifndef LOGGING_SIMPLE_H
#define LOGGING_SIMPLE_H

#include <stdio.h>
#include <ansi_colors.h>

#define LOG_APP_INFO BLUE("[APP]")
#define LOG_APP_ERROR RED("[APP]")

#define log_info(m, ...) printf(LOG_APP_INFO m "\n", ##__VA_ARGS__)
#define log_error(m, ...) printf(LOG_APP_ERROR m "\n", ##__VA_ARGS__)

#endif // LOGGING_SIMPLE_H

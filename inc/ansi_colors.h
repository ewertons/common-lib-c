#ifndef ANSI_COLORS_H
#define ANSI_COLORS_H

#define ASCII_ESC "\x1B"

#define ANSI_COLOR_PLAIN "0"
#define ANSI_COLOR_BOLD "1"

#define ANSI_COLOR_BLACK_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";30m"
#define ANSI_COLOR_RED_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";31m"
#define ANSI_COLOR_GREEN_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";32m"
#define ANSI_COLOR_YELLOW_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";33m"
#define ANSI_COLOR_BLUE_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";34m"
#define ANSI_COLOR_PURPLE_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";35m"
#define ANSI_COLOR_CYAN_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";36m"
#define ANSI_COLOR_WHITE_PLAIN ASCII_ESC "[" ANSI_COLOR_PLAIN ";37m"

#define ANSI_COLOR_BLACK_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";30m"
#define ANSI_COLOR_RED_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";31m"
#define ANSI_COLOR_GREEN_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";32m"
#define ANSI_COLOR_YELLOW_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";33m"
#define ANSI_COLOR_BLUE_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";34m"
#define ANSI_COLOR_PURPLE_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";35m"
#define ANSI_COLOR_CYAN_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";36m"
#define ANSI_COLOR_WHITE_BOLD ASCII_ESC "[" ANSI_COLOR_BOLD ";37m"

#define ANSI_COLOR_RESET ASCII_ESC "[0m"

#define BLACK(text) ANSI_COLOR_BLACK_PLAIN text ANSI_COLOR_RESET
#define BLACK_BOLD(text) ANSI_COLOR_BLACK_BOLD text ANSI_COLOR_RESET

#define RED(text) ANSI_COLOR_RED_PLAIN text ANSI_COLOR_RESET
#define RED_BOLD(text) ANSI_COLOR_RED_BOLD text ANSI_COLOR_RESET

#define GREEN(text) ANSI_COLOR_GREEN_PLAIN text ANSI_COLOR_RESET
#define GREEN_BOLD(text) ANSI_COLOR_GREEN_BOLD text ANSI_COLOR_RESET

#define YELLOW(text) ANSI_COLOR_YELLOW_PLAIN text ANSI_COLOR_RESET
#define YELLOW_BOLD(text) ANSI_COLOR_YELLOW_BOLD text ANSI_COLOR_RESET

#define BLUE(text) ANSI_COLOR_BLUE_PLAIN text ANSI_COLOR_RESET
#define BLUE_BOLD(text) ANSI_COLOR_BLUE_BOLD text ANSI_COLOR_RESET

#define PURPLE(text) ANSI_COLOR_PURPLE_PLAIN text ANSI_COLOR_RESET
#define PURPLE_BOLD(text) ANSI_COLOR_PURPLE_BOLD text ANSI_COLOR_RESET

#define CYAN(text) ANSI_COLOR_CYAN_PLAIN text ANSI_COLOR_RESET
#define CYAN_BOLD(text) ANSI_COLOR_CYAN_BOLD text ANSI_COLOR_RESET

#define WHITE(text) ANSI_COLOR_WHITE_PLAIN text ANSI_COLOR_RESET
#define WHITE_BOLD(text) ANSI_COLOR_WHITE_BOLD text ANSI_COLOR_RESET

// usage example:
// printf(BLUE("APP: ") "the rest of my message\n");

#endif // ANSI_COLORS_H

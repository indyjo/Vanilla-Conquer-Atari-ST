/*
 * Word-wrap console output (ST serial / VT52 friendly).
 */

#include "st_text.h"

#include <mint/osbind.h>

#include <stdio.h>

void st_wrap_puts(const char *paragraph, int maxcol)
{
	if (!paragraph || maxcol < 8)
		return;

	const char *s = paragraph;
	for (;;) {
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
			return;

		if (*s == '\n') {
			putchar('\n');
			s++;
			continue;
		}

		const char *chunk = s;
		const char *last_break = NULL;
		int len = 0;

		while (*s && *s != '\n') {
			if (*s == ' ' || *s == '\t')
				last_break = s;
			if (len == maxcol) {
				if (last_break && last_break > chunk) {
					printf("%.*s\n", (int)(last_break - chunk), chunk);
					s = last_break;
					while (*s == ' ' || *s == '\t')
						s++;
				} else {
					printf("%.*s\n", maxcol, chunk);
					s = chunk + maxcol;
				}
				goto next_para_line;
			}
			len++;
			s++;
		}

		if (s > chunk)
			printf("%.*s\n", (int)(s - chunk), chunk);
		if (*s == '\n')
			s++;

	next_para_line:
		;
	}
}

int st_read_yes_no(void)
{
	(void)Crawcin();
	return 1;
}

int st_read_yes_no_silent(void)
{
	return st_read_yes_no();
}

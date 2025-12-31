/*
 * direct.cpp - Implementation of Windows-style directory functions for Atari ST/MiNT
 */

#include "direct.h"
#include <string.h>
#include <stdio.h>

/* Windows _makepath compatibility */
void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
	if (!path) return;
	
	path[0] = '\0';
	
	/* Drive (ignored on Unix-like systems) */
	if (drive && drive[0]) {
		/* On Unix, we can ignore the drive letter */
	}
	
	/* Directory */
	if (dir && dir[0]) {
		strcat(path, dir);
		/* Ensure trailing slash */
		if (path[strlen(path)-1] != '/' && path[strlen(path)-1] != '\\') {
			strcat(path, "/");
		}
	}
	
	/* Filename */
	if (fname && fname[0]) {
		strcat(path, fname);
	}
	
	/* Extension */
	if (ext && ext[0]) {
		/* Add dot if not present */
		if (ext[0] != '.') {
			strcat(path, ".");
		}
		strcat(path, ext);
	}
}


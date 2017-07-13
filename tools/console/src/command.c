#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "../include/command.h"

// Underline command is for non-mandatory command
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

static char* strupr(char* s) {
	unsigned char *ucs = (unsigned char*)s;
	for(; *ucs != '\0'; ucs++)
		*ucs = toupper(*ucs);

	return s;
}

void help(Command* cmd) {
	printf("PacketNgin Utility %s, %s\n", cmd->name, cmd->desc);  

	char* name = strdup(cmd->name);
	printf("Usage: %s ", strupr(name));
	free(name);

	Option* option = cmd->options;
	char buf[256];
	for(; option->name != NULL; option++) {
		name = strdup(option->name);
		if(option->required) {
			printf("%s ", strupr(name));
		} else {
			snprintf(buf, 256, 
					ANSI_UNDERLINED_PRE "%s" ANSI_UNDERLINED_POST, 
					strupr(name));
			printf("%s ", buf);
		}
		free(name);
	}
	printf("\n\n");

	printf("Options:\n");
	option = cmd->options;
	for(; option->name != NULL; option++) {
		name = strdup(option->name);
		printf("\t%-8.8s  ", strupr(name));
		printf("-%c, ", option->flag);
		printf("--%-10.10s  ", option->name);
		printf("%s", option->desc);
		printf("\n");
		free(name);
	}
	printf("\n");
}

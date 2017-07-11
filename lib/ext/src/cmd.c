#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include <util/cmd.h>
#include <util/map.h>
#include <util/fifo.h>

static Command __commands[CMD_MAX] = {};
static size_t __commands_size = 0;
static Map* __variables = NULL;
static char* variable = NULL;
CommandHistory cmd_history;
char cmd_result[CMD_RESULT_SIZE];

static int cmd_echo(int argc, char** argv, void(*callback)(char* result, int exit_status));
static Command cmds[] = {
	{
		.name = "help",
		.desc = "Show this message.",
		.func = cmd_help
	},
	{
		.name = "echo",
		.desc = "Echo arguments.",
		.args = "[variable: string]*",
		.func = cmd_echo
	},
};

static int cmd_print(char* name) {
	if(__commands_size == 0)
		return -1;

	size_t command_len = 0;

	for(size_t i = 0; i < __commands_size; ++i) {
		Command* c = &__commands[i];
		size_t len = strlen(c->name);
		command_len = len > command_len ? len : command_len;
	}

	bool found = false;
	for(size_t i = 0; i < __commands_size; ++i) {
		Command* c = &__commands[i];
		if(name && strcmp(c->name, name) != 0)
			continue;

		// Name
		printf("%s", c->name);
		int len = strlen(c->name);
		len = command_len - len + 2;
		for(int j = 0; j < len; j++)
			putchar(' ');

		// Description
		printf("%s\n", c->desc);

		// Arguments
		if(c->args != NULL) {
			for(size_t j = 0; j < command_len + 2; j++)
				putchar(' ');

			printf("%s\n", c->args);
		}

		if(!found)
			found = true;
	}

	if(!found)
		// Requested command not found
		return -2;

	return 0;

}

int cmd_help(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc == 1) {
		// NULL prints all commands
		cmd_print(NULL);

	} else if(argc == 2) {
		if(cmd_print(argv[1]) < 0) {
			printf("No help topic matched '%s'\n", argv[1]);
			goto fail;
		}

	} else if(argc >= 3) {
		printf("Wrong number of arguments");
		goto fail;
	}

	if(callback)
		callback((char*)"true", 0);

	return 0;

fail:
	if(callback)
		callback((char*)"false", 0);

	return 0;
}

static int cmd_echo(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	int pos = 0;
	for(int i = 1; i < argc; i++) {
		pos += sprintf(cmd_result + pos, "%s", argv[i]) - 1;
		if(i + 1 < argc) {
			cmd_result[pos++] = ' ';
		}
	}
	printf("%s\n", cmd_result);
	callback(cmd_result, 0);

	return 0;
}

static int cmd_parse_line(char* line, char** argv) {
	int argc = 0;
	bool is_start = true;
	char quotation = 0;
	int str_len =  strlen(line);
	char* start = line;
	for(int i = 0;i < (str_len + 1); i++) {
		if(quotation != 0) {
			if(line[i] == quotation) {
				quotation = 0;
				line[i] = '\0';
				argv[argc++] = start;
				is_start = true;
			}
		} else {
			switch(line[i]) {
				case '\'':
				case '"':
					quotation = line[i];
					line[i] = '\0';
					start = &line[i + 1];
					break;
				case ' ':
				case '\0':
				case '\t':
					if(is_start == false) {
						line[i] = '\0';
						argv[argc++] = start;
						is_start = true;
					}
					break;
				default:
					if(is_start == true) {
						start = &line[i];
						is_start = false;
					}
					break;
			}
		}
	}

	return argc;
}

static void cmd_parse_var(int* argc, char** argv) {
	if(*argc >= 2) {
		if((argv[0][0] == '$') && (argv[1][0] == '=')) {
			if(variable) {
				free(variable);
			}
			variable = strdup(argv[0]);
			memmove(&argv[0], &argv[2], sizeof(char*) * (*argc - 2));
			*argc -= 2;
		}
	}
}

static bool cmd_parse_arg(int argc, char** argv) {
	for(int i = 0; i < argc; i++) {
		if(argv[i][0] == '$') {
			if(!map_contains(__variables, argv[i])) {
				return false;
			} else {
				argv[i] = map_get(__variables, argv[i]);
			}
		}
	}
	return true;
}

static Command* cmd_get(int argc, char** argv) {
	if(!argc || !argv[0])
		return NULL;

	for(size_t i = 0 ; i < __commands_size; ++i) {
		if(strcmp(__commands[i].name, argv[0]) == 0)
			return &__commands[i];
	}
	return NULL;
}

void cmd_update_var(char* result, int exit_status) {
	char buf[16];
	sprintf(buf, "%d", exit_status);

	free(map_get(__variables, "$?"));
	map_update(__variables, "$?", strdup(buf));

	if(exit_status == 0) {
		if(variable) {
			char* value = strlen(result) ? strdup(result) : strdup("(nil)");
			if(map_contains(__variables, variable)) {
				free(map_get(__variables, variable));
				map_update(__variables, variable, value);
			} else {
				map_put(__variables, variable, value);
			}
		}
	}

	if(variable) {
		free(variable);
		variable = NULL;
	}
}

static void cmd_history_reset() {
	cmd_history.index = -1;
}

static bool cmd_history_using() {
	return (cmd_history.index != -1);
}

static int cmd_history_save(char* line) {
	int len = strlen(line);
	if(len <= 0)
		return -1;

	FIFO* histories = cmd_history.histories;
	char* buf = malloc(len + 1);
	strcpy(buf, line);
	if(!fifo_available(histories)) {
		char* str = fifo_pop(histories);
		free(str);
	}

	fifo_push(histories, buf);
	return 0;
}

static int cmd_history_count() {
	return fifo_size(cmd_history.histories);
}

static char* cmd_history_get(size_t idx) {
	FIFO* histories = cmd_history.histories;
	if(fifo_empty(histories))
		return NULL;

	if(idx >= (fifo_size(histories) - 1))
		idx = fifo_size(histories) - 1;

	// Return reversely
	idx = fifo_size(histories) - 1 - idx;
	return fifo_peek(histories, idx);
}

static char* cmd_history_get_past() {
	if(cmd_history.index >= (cmd_history_count() - 1))
		return cmd_history_get(cmd_history.index);

	return cmd_history_get(++cmd_history.index);
}

static char* cmd_history_get_current() {
	if(cmd_history.index == -1)
		return NULL;

	return cmd_history_get(cmd_history.index);
}

static char* cmd_history_get_later() {
	if(cmd_history.index <= 0)
		return NULL;

	return cmd_history_get(--cmd_history.index);
}

static void cmd_sort(size_t base) {
	while(base < __commands_size) {
		Command command = __commands[base];
		ssize_t insert_point = 0;

		// Find where to insert the command
		for(int i = base-1; i >= 0; --i) {
			if(strcmp(__commands[i].name, command.name) < 0) {
				insert_point = i+1;
				break;
			}
		}

		// Push the existing command
		for(int i = base; i > insert_point; --i)
			__commands[i] = __commands[i-1];

		// Assign a new command
		__commands[insert_point] = command;

		base += 1;
	}
}


void cmd_init(void) {
	__variables = map_create(16, map_string_hash, map_string_equals, NULL);
	map_put(__variables, strdup("$?"), strdup("(nil)"));
	map_put(__variables, strdup("$nil"), strdup("(nil)"));

	cmd_history.index	= -1;
	cmd_history.using	= cmd_history_using;
	cmd_history.reset	= cmd_history_reset;
	cmd_history.save	= cmd_history_save;
	cmd_history.count	= cmd_history_count;
	cmd_history.get_past	= cmd_history_get_past;
	cmd_history.get_current	= cmd_history_get_current;
	cmd_history.get_later	= cmd_history_get_later;
	cmd_history.histories	= fifo_create(CMD_HISTORY_SIZE, NULL);

	cmd_register(cmds, sizeof(cmds) / sizeof(cmds[0]));
}

int cmd_register(Command* commands, size_t length) {
	/* Note: At this point, kernel is not ready to perform
	 * print and/or malloc function */
	if(!commands || !length) return -1;

	size_t oldsize = __commands_size;

	// Merge two arrays
	for(size_t i = 0; i < length; ++i) {
		if(commands[i].name) {
			__commands[__commands_size++] = commands[i];
		}
	}

	cmd_sort(oldsize);

	return 0;
}

void cmd_unregister(Command* command) {
	if(__commands_size < 1) return;
	if(!command || !command->name) return;

	ssize_t index = -1;

	// Find the index of the given command object
	for(size_t i = 0; i < __commands_size; ++i)
		if(strcmp(__commands[i].name, command->name) == 0)
			index = (ssize_t)i;
	if(index == -1)
		return;

	// Pack the commands array
	for(size_t i = (size_t)index; i < __commands_size-1; ++i)
		__commands[i] = __commands[i+1];
	__commands_size -= 1;
}

int cmd_exec(char* line, void(*callback)(char* result, int exit_status)) {
	int argc;
	char* argv[CMD_MAX_ARGC];

	argc = cmd_parse_line(line, argv);
	if(argc == 0 || argv[0][0] == '#')
		return 0;

	cmd_parse_var(&argc, argv);

	if(cmd_parse_arg(argc, argv) == false) {
		return CMD_VARIABLE_NOT_FOUND;
	}

	Command* cmd = cmd_get(argc, argv);
	if(cmd) {
		cmd_result[0] = '\0';
		int exit_status = cmd->func(argc, argv, callback);
		switch(exit_status) {
			case CMD_WRONG_NUMBER_OF_ARGS:
				printf("Error: Commands wrong number of arguments\n");
				break;
			case CMD_WRONG_TYPE_OF_ARGS:
				printf("Error: Commands wrong type of arguments\n");
				break;
			case CMD_WRONG_OPTIONS:
				printf("Error: Commands wrong options\n");
				break;
			case CMD_ERROR:
				printf("Error\n");
				break;
		}

		if(exit_status && cmd->args) printf("%s\n", cmd->args);
	}

	return 0;
}

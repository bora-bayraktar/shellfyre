#define _GNU_SOURCE

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h> 
#include <ctype.h>
#include <fcntl.h>

const char *sysname = "shellfyre";
char cdh_file[1024];
char todo_file[1024];
int module_inserted = 0;

enum return_codes
{
    SUCCESS = 0,
    EXIT = 1,
    UNKNOWN = 2,
};

struct command_t
{
    char *name;
    bool background;
    bool auto_complete;
    int arg_count;
    char **args;
    char *redirects[3];		// in/out redirection
    struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
    int i = 0;
    printf("Command: <%s>\n", command->name);
    printf("\tIs Background: %s\n", command->background ? "yes" : "no");
    printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
    printf("\tRedirects:\n");
    for (i = 0; i < 3; i++)
        printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
    printf("\tArguments (%d):\n", command->arg_count);
    for (i = 0; i < command->arg_count; ++i)
        printf("\t\tArg %d: %s\n", i, command->args[i]);
    if (command->next)
    {
        printf("\tPiped to:\n");
        print_command(command->next);
    }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
    if (command->arg_count)
    {
        for (int i = 0; i < command->arg_count; ++i)
            free(command->args[i]);
        free(command->args);
    }
    for (int i = 0; i < 3; ++i)
        if (command->redirects[i])
            free(command->redirects[i]);
    if (command->next)
    {
        free_command(command->next);
        command->next = NULL;
    }
    free(command->name);
    free(command);
    return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
    char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
    getcwd(cwd, sizeof(cwd));
    printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
    return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
    const char *splitters = " \t"; // split at whitespace
    int index, len;
    len = strlen(buf);
    while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
    {
        buf++;
        len--;
    }
    while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
        buf[--len] = 0; // trim right whitespace

    if (len > 0 && buf[len - 1] == '?') // auto-complete
        command->auto_complete = true;
    if (len > 0 && buf[len - 1] == '&') // background
        command->background = true;

    char *pch = strtok(buf, splitters);
    command->name = (char *)malloc(strlen(pch) + 1);
    if (pch == NULL)
        command->name[0] = 0;
    else
        strcpy(command->name, pch);

    command->args = (char **)malloc(sizeof(char *));

    int redirect_index;
    int arg_index = 0;
    char temp_buf[1024], *arg;

    while (1)
    {
        // tokenize input on splitters
        pch = strtok(NULL, splitters);
        if (!pch)
            break;
        arg = temp_buf;
        strcpy(arg, pch);
        len = strlen(arg);

        if (len == 0)
            continue;										 // empty arg, go for next
        while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
        {
            arg++;
            len--;
        }
        while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
            arg[--len] = 0; // trim right whitespace
        if (len == 0)
            continue; // empty arg, go for next

        // piping to another command
        if (strcmp(arg, "|") == 0)
        {
            struct command_t *c = malloc(sizeof(struct command_t));
            int l = strlen(pch);
            pch[l] = splitters[0]; // restore strtok termination
            index = 1;
            while (pch[index] == ' ' || pch[index] == '\t')
                index++; // skip whitespaces

            parse_command(pch + index, c);
            pch[l] = 0; // put back strtok termination
            command->next = c;
            continue;
        }

        // background process
        if (strcmp(arg, "&") == 0)
            continue; // handled before

        // handle input redirection
        redirect_index = -1;
        if (arg[0] == '<')
            redirect_index = 0;
        if (arg[0] == '>')
        {
            if (len > 1 && arg[1] == '>')
            {
                redirect_index = 2;
                arg++;
                len--;
            }
            else
                redirect_index = 1;
        }
        if (redirect_index != -1)
        {
            command->redirects[redirect_index] = malloc(len);
            strcpy(command->redirects[redirect_index], arg + 1);
            continue;
        }

        // normal arguments
        if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
        {
            arg[--len] = 0;
            arg++;
        }
        command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
        command->args[arg_index] = (char *)malloc(len + 1);
        strcpy(command->args[arg_index++], arg);
    }
    command->arg_count = arg_index;
    return 0;
}

void prompt_backspace()
{
    putchar(8);	  // go back 1
    putchar(' '); // write empty over
    putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
    int index = 0;
    char c;
    char buf[4096];
    static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    // FIXME: backspace is applied before printing chars
    show_prompt();
    int multicode_state = 0;
    buf[0] = 0;

    while (1)
    {
        c = getchar();
        // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

        if (c == 9) // handle tab
        {
            buf[index++] = '?'; // autocomplete
            break;
        }

        if (c == 127) // handle backspace
        {
            if (index > 0)
            {
                prompt_backspace();
                index--;
            }
            continue;
        }
        if (c == 27 && multicode_state == 0) // handle multi-code keys
        {
            multicode_state = 1;
            continue;
        }
        if (c == 91 && multicode_state == 1)
        {
            multicode_state = 2;
            continue;
        }
        if (c == 65 && multicode_state == 2) // up arrow
        {
            int i;
            while (index > 0)
            {
                prompt_backspace();
                index--;
            }
            for (i = 0; oldbuf[i]; ++i)
            {
                putchar(oldbuf[i]);
                buf[i] = oldbuf[i];
            }
            index = i;
            continue;
        }
        else
            multicode_state = 0;
        putchar(c); // echo the character

        buf[index++] = c;
        if (index >= sizeof(buf) - 1)
            break;
        if (c == '\n') // enter key
            break;
        if (c == 4) // Ctrl+D
            return EXIT;
    }
    if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
        index--;
    buf[index++] = 0; // null terminate string

    strcpy(oldbuf, buf);

    parse_command(buf, command);

    // print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
    return SUCCESS;
}

int process_command(struct command_t *command);
char* find_path(char *command_name);
void search_file(char *file, char *dir_name, char *option, char *file_list[]);
void print_files(char *file_list[], size_t size);
void open_files(char *file_list[], size_t size);
void append_history_file();
void read_print_history();
void show_todo();
void add_todo();
void remove_todo();
void pstraverse(struct command_t *command);

int main()
{
    getcwd(cdh_file, sizeof(cdh_file));
    strcat(cdh_file, "/cdh_history.txt");

    getcwd(todo_file, sizeof(todo_file));
    strcat(todo_file, "/todo_list.txt");

    while (1)
    {
        struct command_t *command = malloc(sizeof(struct command_t));
        memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

        int code;
        code = prompt(command);
        if (code == EXIT)
            break;

        code = process_command(command);
        if (code == EXIT)
            break;

        free_command(command);
    }

    printf("\n");
    return 0;
}

int process_command(struct command_t *command)
{
    int r;
    if (strcmp(command->name, "") == 0)
        return SUCCESS;

    if (strcmp(command->name, "exit") == 0) {
        if (module_inserted) {
            char *path = find_path("sudo");
            char *args[] = {"sudo", "rmmod", "pstraverse.ko", NULL};
            execv(path, args);
        }
        return EXIT;
    }

    if (strcmp(command->name, "cd") == 0)
    {
        if (command->arg_count > 0)
        {
            r = chdir(command->args[0]);
            if (r == -1) {
                printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
            } else {
                append_history_file();
            }

            return SUCCESS;
        }
    }

    // TODO: Implement your custom commands here

    if (strcmp(command->name, "filesearch") == 0) {
        int size = 1024;
        char *file_list[size];
        for (int i = 0; i < size; i++) {
            file_list[i] = (char *) calloc(128, 8);
        }

        if (command->arg_count == 1) {
            search_file(command->args[0], ".", NULL, file_list);
            print_files(file_list, size);
        } else if (command->arg_count == 2) {
            if (strcmp(command->args[1], "-r") == 0) {
                search_file(command->args[0], ".", "r", file_list);
                print_files(file_list, size);
            } else if (strcmp(command->args[1], "-o") == 0) {
                search_file(command->args[0], ".", NULL, file_list);
                print_files(file_list, size);
                open_files(file_list, size);
            }
        } else if (command->arg_count == 3) {
            if ((strcmp(command->args[1], "-r") == 0 && strcmp(command->args[2], "-o") == 0) || (strcmp(command->args[1], "-o") == 0 && strcmp(command->args[2], "-r") == 0)) {
                search_file(command->args[0], ".", "r", file_list);
                print_files(file_list, size);
                open_files(file_list, size);
            }
        } else {
            printf("Missing arguments.\n");
        }

        for (int i = 0; i < 1024; i++) {
            free(file_list[i]);
        }

        return SUCCESS;
    }

    if (strcmp(command->name, "cdh") == 0) {
        read_print_history();
        append_history_file();
        return SUCCESS;
    }

    if (strcmp(command->name, "take") == 0) {
        if (command->arg_count == 1) {
            char *token = strtok(command->args[0], "/");

            while (token != NULL) {
                mkdir(token, 0700);
                chdir(token);
                append_history_file();
                token = strtok(NULL, "/");
            }
        }

        return SUCCESS;
    }

    if (strcmp(command->name, "joker") == 0) {
        FILE *fp = fopen("crontab_joker.txt", "w");

        fputs("*/15 * * * * XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send Joke \"$(curl -s https://icanhazdadjoke.com/)\"\n", fp);
        fclose(fp);

        char *args[] = {"crontab", "crontab_joker.txt", NULL};
        char *path = find_path("crontab");

        pid_t pid = fork();
        if (pid == 0) {
            execv(path, args);
        } else {
            wait(NULL);
        }

        free(path);
        remove("crontab_joker.txt");

        return SUCCESS;
    }

    // My own command -> Author: Kemal Bora Bayraktar
    if (strcmp(command->name, "todo") == 0) {
        if (command->arg_count == 0) {
            show_todo();
        } else if (command->arg_count == 1) {
            if (strcmp(command->args[0], "add") == 0) {
                add_todo();
            } else if (strcmp(command->args[0], "remove") == 0) {
                remove_todo();
            }
        }

        return SUCCESS;
    }

    if (strcmp(command->name, "pstraverse") == 0) {
        pstraverse(command);

        return SUCCESS;
    }

    pid_t pid = fork();

    if (pid == 0) // child
    {
        // increase args size by 2
        command->args = (char **)realloc(
                command->args, sizeof(char *) * (command->arg_count += 2));

        // shift everything forward by 1
        for (int i = command->arg_count - 2; i > 0; --i)
            command->args[i] = command->args[i - 1];

        // set args[0] as a copy of name
        command->args[0] = strdup(command->name);
        // set args[arg_count-1] (last) to NULL
        command->args[command->arg_count - 1] = NULL;

        /// TODO: do your own exec with path resolving using execv()

        char *path = find_path(command->name);
        if (path != NULL) {
            execv(path, command->args);
        } else {
            printf("-%s: %s: command not found\n", sysname, command->name);
        }

        exit(0);
    }
    else
    {
        /// TODO: Wait for child to finish if command is not running in background

        if (!command->background) {
            wait(NULL);
        }

        return SUCCESS;
    }

    printf("-%s: %s: command not found\n", sysname, command->name);
    return UNKNOWN;
}

char* find_path(char *command_name) {

    char PATH[1024];
    strcpy(PATH, getenv("PATH"));

    char *token = strtok(PATH, ":");
    struct stat *file_properties = malloc(sizeof(struct stat));

    char *command_path = malloc(512);
    while (token != NULL) {
        strcpy(command_path, "");

        strcat(command_path, token);
        strcat(command_path, "/");
        strcat(command_path, command_name);

        int exists = stat(command_path, file_properties);

        if ((exists == 0) && (file_properties->st_mode & S_IXUSR)) {
            free(file_properties);
            return command_path;
        }

        token = strtok(NULL, ":");
    }

    free(command_path);
    free(file_properties);
    return NULL;
}

void search_file(char *file, char *dir_name, char *option, char *file_list[]) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(dir_name);

    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, file) && entry->d_type == DT_REG) {
                int index = 0;
                while (strcmp(file_list[index], "") != 0) {
                    index++;
                }

                int size = 1024;
                char *temp = (char *) malloc(size);
                snprintf(temp, size, "%s/%s", dir_name, entry->d_name);

                file_list[index] = temp;
            }

            if (option != NULL && strcmp(option, "r") == 0) {
                if (entry->d_type == DT_DIR) {
                    char path[1024];

                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;

                    snprintf(path, sizeof(path), "%s/%s", dir_name, entry->d_name);
                    search_file(file, path, "r", file_list);
                }
            }

        }
        closedir(dir);
    }
}

void print_files(char *file_list[], size_t size) {
    for (int i = 0; i < size; i++) {
        if (strcmp(file_list[i], "") == 0) {
            int index = i - 1;

            while (index >= 0) {
                printf("\t%s\n", file_list[index]);
                index--;
            }

            break;
        }
    }
}

void open_files(char *file_list[], size_t size) {
    for (int i = 0; i < size; i++) {
        if (strcmp(file_list[i], "") != 0) {
            char *args[] = {"xdg-open", file_list[i], NULL};

            pid_t pid = fork();
            if (pid == 0) {
                char *path = find_path("xdg-open");
                execv(path, args);
                free(path);
            } else {
                wait(NULL);
            }
        }
    }
}

void append_history_file() {
    FILE *fp = fopen(cdh_file, "a+");

    if (fp) {
        char line[1024];
        char cwd[800];

        getcwd(cwd, sizeof(cwd));
        snprintf(line, sizeof(line), "%s\n", cwd);
        fputs(line, fp);
    }

    fclose(fp);
}

void read_print_history() {
    FILE *fp = fopen(cdh_file, "r");
    if (!fp) {
        printf("You didn't visited any directory yet.\n");
    } else {
        char lines[100][1024];

        int number_of_lines = 0;
        while (fgets(lines[number_of_lines], 1024, fp)) number_of_lines++;

        char last_ten_dir[10][1024];
        int counter = 0;
        int number_of_dir = 0;
        while (number_of_lines >= 0 && counter <= 10) {
            strcpy(last_ten_dir[number_of_dir], lines[number_of_lines]);
            number_of_lines--;
            number_of_dir++;
            counter++;
        }

        int i = 0;
        while (i < number_of_dir - 1) {
            char output[1024];
            strcpy(output, last_ten_dir[number_of_dir - i - 1]);
            char *loc = strstr(output, getenv("HOME"));

            if (loc) {
                int n = 0;
                while (loc[n] == getenv("HOME")[n]) n++;
                printf("%c %d) ~%s", 95 + number_of_dir - i, number_of_dir - i - 1, loc + n);
            } else {
                printf("%c %d) %s", 95 + number_of_dir - i, number_of_dir - i - 1, output);
            }

            i++;
        }

        char input[10];
        printf("Select directory by letter or number: ");
        fgets(input, 10, stdin);
        input[strlen(input) - 1] = '\0';

        int valid = 1;
        for (int i = 0; i < strlen(input); i++) {
            if (!isdigit(input[i])) {
                valid = 0;
            }
        }

        if (valid && atoi(input) < number_of_dir) {
            char *path = last_ten_dir[atoi(input)];
            path[strlen(path) - 1] = '\0';
            chdir(path);
        } else if (strlen(input) == 1 && input[0] > 96 && input[0] < 97 + number_of_dir - 1) {
            char *path = last_ten_dir[input[0] - 96];
            path[strlen(path) - 1] = '\0';
            chdir(path);
        }
    }
}

void show_todo() {
    FILE *fp = fopen(todo_file, "r");

    if (!fp) {
        printf("There is no task to do.\n");
    } else {
        char line[200];

        int i = 1;
        while (fgets(line, sizeof(line), fp)) {
            printf("%d) %s", i, line);
            i++;
        }

        if (i == 1) {
            printf("There is no task to do.\n");
        }
        fclose(fp);
    }
}

void add_todo() {
    FILE *fp = fopen(todo_file, "a+");

    if (fp) {
        char task[1024];
        printf("Task to add: ");
        fgets(task, sizeof(task), stdin);

        fputs(task, fp);
    }

    fclose(fp);
}

void remove_todo() {
    FILE *fp = fopen(todo_file, "r");
    FILE *temp = fopen("temp.txt", "w");

    char line[200];

    if (!fp) {
        printf("There is no task to remove.\n");
    } else {
        char input[10];
        printf("Index of task to remove: ");
        fgets(input, sizeof(input), stdin);
        int line_number = atoi(input);

        int i = 1;
        while(fgets(line, sizeof(line), fp)) {
            if (i != line_number) {
                fputs(line, temp);
            }
            i++;
        }
        fclose(temp);
        fclose(fp);

        fp = fopen(todo_file, "w");
        temp = fopen("temp.txt", "r");
        while(fgets(line, sizeof(line), temp)) {
            fputs(line, fp);
        }
        fclose(temp);
        fclose(fp);
        remove("temp.txt");
    }
}

void pstraverse(struct command_t *command) {
    char pid[10];
    char option[10];

    snprintf(pid, sizeof(pid), "pid=%s", command->args[0]);
    snprintf(option, sizeof(option), "option=%s", command->args[1]);

    if (!module_inserted) {
        pid_t current_pid = fork();
        if (current_pid == 0) {
            char *path = find_path("sudo");
            char *args[] = {"sudo", "insmod", "pstraverse.ko", pid, option, NULL};
            execv(path, args);
        } else {
            module_inserted = 1;
            wait(NULL);
        }
    } else {
        int fp = open("/dev/my_device", O_RDWR);

        if (fp >= 0) {
            int8_t write_buf[20];
            strcpy(write_buf, command->args[0]);
            strcat(write_buf, " ");
            strcat(write_buf, command->args[1]);
            write(fp, write_buf, strlen(write_buf) + 1);
        }
        close(fp);
    }
}


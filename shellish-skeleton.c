#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
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
int show_prompt() {
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
int parse_command(char *buf, struct command_t *command) {
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
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
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
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
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
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
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
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

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
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

if (strcmp(command->name, "cut") == 0) {
    char delimiter = '\t';
    char *fields_str = NULL;

    for (int i = 1; i < command->arg_count; i++) {
        if (command->args[i] == NULL) continue;

        if (strncmp(command->args[i], "-d", 2) == 0) {
            if (strlen(command->args[i]) > 2) {
                delimiter = command->args[i][2];
            } else if (i + 1 < command->arg_count && command->args[i+1] != NULL) {
                delimiter = command->args[++i][0];
            }
        } else if (strncmp(command->args[i], "-f", 2) == 0) {
            if (strlen(command->args[i]) > 2) {
                fields_str = command->args[i] + 2;
            } else if (i + 1 < command->arg_count && command->args[i+1] != NULL) {
                fields_str = command->args[++i];
            }
        }
    }

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
 
        char *tokens[256];
        int token_count = 0;
        char *temp_line = strdup(line);
        if (temp_line == NULL) continue;

        char delim_str[2] = {delimiter, '\0'};
        char *token = strtok(temp_line, delim_str);

        while (token != NULL && token_count < 256) {
            tokens[token_count++] = token;
            token = strtok(NULL, delim_str);
        }

        if (fields_str != NULL && token_count > 0) {
            char *f_copy = strdup(fields_str);
            if (f_copy) {
                char *f_token = strtok(f_copy, ",");
                int first = 1;

                while (f_token != NULL) {
                    int field_idx = atoi(f_token) - 1;
                    if (field_idx >= 0 && field_idx < token_count) {
                        if (!first) printf("%c", delimiter);
                        printf("%s", tokens[field_idx]);
                        first = 0;
                    }
                    f_token = strtok(NULL, ",");
                }
                printf("\n");
                free(f_copy);
            }
        }
        fflush(stdout);
        free(temp_line);
    }
    return SUCCESS;
  }

if (strcmp(command->name, "chatroom") == 0) {
      if (command->arg_count < 3) {
          printf("Kullanım: chatroom <oda_adı> <kullanıcı_adı>\n");
          return SUCCESS;
      }

      char *room_name = command->args[1];
      char *user_name = command->args[2];
      char room_dir[256];
      char my_fifo[512];

      snprintf(room_dir, sizeof(room_dir), "/tmp/chatroom-%s", room_name);
      mkdir(room_dir, 0777);
      snprintf(my_fifo, sizeof(my_fifo), "%s/%s", room_dir, user_name);
      mkfifo(my_fifo, 0666);

      printf("[Chatroom '%s' odasına '%s' olarak katıldınız. Çıkmak için '\\q' yazın.]\n", room_name, user_name);

      pid_t chat_pid = fork();

      if (chat_pid == 0) {
          int fd_read = open(my_fifo, O_RDWR);
          if (fd_read < 0) exit(1);

          char msg_buffer[1024];
          while (1) {
              int bytes_read = read(fd_read, msg_buffer, sizeof(msg_buffer) - 1);
              if (bytes_read > 0) {
                  msg_buffer[bytes_read] = '\0';
                  printf("\r%s\n", msg_buffer);
                  printf("shellish$ ");
                  fflush(stdout);
              }
          }
      } else {
          char input[1024];
          char send_buffer[2048];
          DIR *d;
          struct dirent *dir;

          while (1) {
              if (fgets(input, sizeof(input), stdin) == NULL) break;
              input[strcspn(input, "\n")] = 0;

              if (strcmp(input, "\\q") == 0) {
                  break;
              }
              if (strlen(input) == 0) continue;

              snprintf(send_buffer, sizeof(send_buffer), "[%s]: %s", user_name, input);

              d = opendir(room_dir);
              if (d) {
                  while ((dir = readdir(d)) != NULL) {
                      if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 || strcmp(dir->d_name, user_name) == 0) {
                          continue;
                      }

                      char other_fifo[512];
                      snprintf(other_fifo, sizeof(other_fifo), "%s/%s", room_dir, dir->d_name);

                      int fd_write = open(other_fifo, O_WRONLY | O_NONBLOCK);
                      if (fd_write >= 0) {
                          write(fd_write, send_buffer, strlen(send_buffer));
                          close(fd_write);
                      }
                  }
                  closedir(d);
              }
          }

          printf("[Odadan ayrıldınız.]\n");
          kill(chat_pid, SIGTERM);
          waitpid(chat_pid, NULL, 0);
          unlink(my_fifo);
      }
      return SUCCESS;
  }

  if (command->next != NULL) {
     int fd[2];
     if (pipe(fd) == -1) {
	perror("Pipe creation failed");
	return EXIT;
     }

     pid_t pid_left = fork();
     if (pid_left == 0) {
	dup2(fd[1], STDOUT_FILENO);
	close(fd[0]);
	close(fd[1]);

	command->next = NULL;
	process_command(command);
	exit(0);
     }

     pid_t pid_right = fork();
     if (pid_right == 0) {
	dup2(fd[0], STDIN_FILENO);
	close(fd[1]);
	close(fd[0]);

	process_command(command->next);
	exit(0);
     }

     close(fd[0]);
     close(fd[1]);

     if (!command->background) {
	waitpid(pid_left, NULL, 0);
	waitpid(pid_right, NULL, 0);
     }
     return SUCCESS;
}

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    //execvp(command->name, command->args); // exec+args+path

    if (command->redirects[0]) {
	char *file = command->redirects[0];
        while (*file == ' ') file++;

	int len = strlen(file);
        while (len > 0 && (file[len - 1] == ' ' || file[len - 1] == '\t' || file[len - 1] == '\n' || file[len - 1] == '\r')) {
	    file[--len] = '\0';
        }

	int fd_in = open(file, O_RDONLY);
	if (fd_in < 0) {
	    perror("Input redirection error");
	    exit(1);
	}
	dup2(fd_in, STDIN_FILENO);
	close(fd_in);
    }

    if (command->redirects[1]) {
	char *file = command->redirects[1];
        while (*file == ' ') file++;

        int len = strlen(file);
        while (len > 0 && (file[len - 1] == ' ' || file[len - 1] == '\t' || file[len - 1] == '\n' || file[len - 1] == '\r')) {
            file[--len] = '\0';
        }

	int fd_out = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_out < 0) {
	    perror("Output redirection error");
	    exit(1);
	}
	dup2(fd_out, STDOUT_FILENO);
	close(fd_out);
    }

    if (command->redirects[2]) {
	char *file = command->redirects[2];
        while (*file == ' ') file++;

	int len = strlen(file);
        while (len > 0 && (file[len - 1] == ' ' || file[len - 1] == '\t' || file[len - 1] == '\n' || file[len - 1] == '\r')) {
            file[--len] = '\0';
        }

	int fd_append = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd_append < 0) {
	    perror("Append redirection error");
	    exit(1);
	}
	dup2(fd_append, STDOUT_FILENO);
	close(fd_append);
    }

    char *path_env = getenv("PATH");
    char *path_copy = strdup(path_env);
    char *token = strtok(path_copy, ":");
    char full_path[1024];
    bool found = false;

    if (command->name[0] == '/' || (command->name[0] == '.' && command->name[1] == '/')) {
	if (access(command->name, X_OK) == 0) {
	    execv(command->name, command->args);
	    found = true;
	}
    } else {
	while (token != NULL) {
	    snprintf(full_path, sizeof(full_path), "%s/%s", token, command->name);
	    if (access(full_path, X_OK) == 0) {
		found = true;
		execv(full_path, command->args);
		break;
	    }
	    token = strtok(NULL, ":");
	}
    }
    free(path_copy);

    if (!found) {
	printf("-%s: %s: command not found\n", sysname, command->name);
    }
    exit(127);
  } else {
    // TODO: implement background processes here
    if (command->background) {
	printf("[Process running in background, PID: %d]\n", pid);
    } else {
	waitpid(pid, NULL, 0); // wait for child process to finish
    }
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
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

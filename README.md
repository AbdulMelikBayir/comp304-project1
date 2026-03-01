# comp304-project1
# Shell-ish: A Simple Linux Shell Implementation
**COMP304 - Operating Systems | Project 1**

## Project Overview
This project is a custom Linux shell called `shell-ish` implemented in C. It supports basic command execution, I/O redirection, piping, and several built-in features including a messaging chatroom.

## Features Implemented

### 1. Command Execution & Path Resolving
- Instead of using `execvp`, this shell manually resolves command paths using the `PATH` environment variable.
- Supports execution using `execv` by searching through directories like `/bin`, `/usr/bin`, etc.

### 2. I/O Redirection
- **Standard Output (`>`):** Redirects output to a file (truncates existing).
- **Append Output (`>>`):** Appends output to the end of a file.
- **Standard Input (`<`):** Reads input from a file.

### 3. Piping (`|`)
- Supports connecting multiple commands via pipes (e.g., `ls | grep .c | wc -l`).
- Implemented using a recursive approach to handle multiple pipe segments.

### 4. Built-in Commands
- **`cd`**: Changes the current working directory.
- **`exit`**: Gracefully terminates the shell.
- **`cut`**: A custom implementation of the Unix `cut` command.
  - Supports `-d` (delimiter) and `-f` (fields).
  - Example: `cat /etc/passwd | cut -d : -f1,6`
- **`chatroom <room> <user>`**: A real-time messaging system using **Named Pipes (FIFO)**.
  - Users in the same room can send and receive messages simultaneously.
  - Type `\q` to leave the chat.
- **`count_items` (Custom Command)**: Analyzes the current directory.
  - Displays counts for regular files, directories, and hidden items.

### 5. Background Processes
- Supports running commands in the background using the `&` symbol at the end of a command.

## How to Compile and Run
1. Open your terminal in the project directory.
2. Compile the source code:
   ```bash
   gcc -o shell-ish shellish-skeleton.c

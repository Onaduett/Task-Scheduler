# Task Scheduler

A multi-threaded client-server task scheduling system written in C++ that allows users to schedule, manage, and execute shell commands at specified times.

## Repository

```bash
git clone https://github.com/Onaduett/Task-Scheduler.git
cd Task-Scheduler
```

## Features

- **Remote Task Scheduling**: Schedule shell commands to run at specific times
- **Multi-threaded Server**: Handles multiple client connections simultaneously
- **Persistent Storage**: Tasks are saved to disk and survive server restarts
- **Password Authentication**: Secure access with session-based authentication
- **Task Management**: Add, list, modify, delete, and monitor tasks
- **Automatic Execution**: Background scheduler thread executes tasks at their scheduled times
- **Comprehensive Logging**: All operations are logged with timestamps
- **Task Status Tracking**: Monitor task states (PENDING, RUNNING, COMPLETED, FAILED)

## Architecture

### Server (`server.cpp`)
- Listens on port 8080 for incoming client connections
- Maintains a task queue with persistent storage (`tasks.dat`)
- Runs a background scheduler thread that checks and executes tasks every 5 seconds
- Manages authenticated sessions per client IP
- Logs all activities to `scheduler.log`

### Client (`client.cpp`)
- Interactive command-line interface
- Password-protected authentication
- Full task management capabilities
- Real-time communication with server

## Requirements

- Linux/Unix-based operating system
- G++ compiler with C++11 support or later
- POSIX threads library

## Compilation

Compile both server and client:

```bash
g++ -std=c++11 -pthread server.cpp -o server
g++ -std=c++11 client.cpp -o client
```

## Usage

### Starting the Server

```bash
./server
```

The server will:
- Start listening on port 8080
- Load existing tasks from `tasks.dat` (if available)
- Begin the scheduler thread
- Create/append to `scheduler.log`

**Default password**: `admin123`

### Running the Client

```bash
./client
```

You'll be prompted to enter the password. After successful authentication, you can:

1. **Add Task** - Schedule a new command
   - Format: `HH:MM` (24-hour time)
   - Example: Schedule `ls -la` to run at 14:30

2. **List Tasks** - View all scheduled tasks with their details

3. **Server Status** - View statistics (total, pending, running, completed, failed tasks)

4. **Delete Task** - Remove a task by ID

5. **Task Info** - View detailed information about a specific task

6. **Modify Task** - Update task time and/or command (only for non-executed tasks)

## Command Protocol

The client-server communication uses a simple text-based protocol:

- `AUTH <password>` - Authenticate with the server
- `ADD <HH:MM> <command>` - Add a new task
- `LIST` - List all tasks
- `STATUS` - Get server status
- `DELETE <id>` - Delete a task
- `INFO <id>` - Get task details
- `MODIFY <id> <new_time> <new_command>` - Modify a task (use `-` to keep existing value)

## File Structure

```
Task-Scheduler/
├── server.cpp          # Server implementation
├── client.cpp          # Client implementation
├── README.md           # This file
├── tasks.dat           # Task database (created at runtime)
├── scheduler.log       # Server log file (created at runtime)
└── task_output_*.log   # Individual task output files (created when tasks execute)
```

## Task Scheduling Logic

- Tasks are scheduled using 24-hour time format (HH:MM)
- If the scheduled time has already passed today, the task is scheduled for the next day
- The scheduler checks for due tasks every 5 seconds
- Task output (stdout/stderr) is redirected to `task_output_<id>.log`

## Security Considerations

⚠️ **Important Security Notes**:

- The default password is hardcoded (`admin123`). Change it in `server.cpp` before deployment
- Authentication is IP-based and persists for the server session
- No encryption is used for network communication
- The server executes shell commands with the permissions of the user running it
- This is intended for educational purposes or trusted local networks only

## Example Session

```
Server: 127.0.0.1:8080

Password: ********

[✓] Authentication successful!

==========================================
  TASK SCHEDULER CLIENT
==========================================
[✓] Authenticated

1. Add task
2. List tasks
3. Server status
4. Delete task
5. Task info
6. Modify task
0. Exit

Choice: 1

Time (HH:MM): 15:30
Command: echo "Hello, World!" > greeting.txt

OK: Task #1 added
```

## Limitations

- Tasks are scheduled for the current or next day only (no multi-day scheduling)
- No recurring task support
- Session management is basic (IP-based, no session tokens)
- Single server instance (no clustering or load balancing)





## License

This project is open source and available for educational purposes. 

## Contributing

Contributions are welcome! Feel free to:
- Report bugs
- Suggest new features
- Submit pull requests

## Author

Written by Daulet Yerkinov & Olesksandr Razumov as University project semester 3
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iomanip>
#include <algorithm>
#include <csignal>

using namespace std;

#define PORT 8080
#define MAX_CLIENTS 10
#define TASKS_FILE "tasks.dat"
#define LOG_FILE "scheduler.log"
#define SECURE_LOG_FILE "secure_scheduler.log"
#define SERVER_PASSWORD "admin123"

struct Task {
    int id;
    string command;
    string schedule_time;
    string status;
    bool executed;
    time_t scheduled_timestamp;
    time_t created_at;
};

vector<Task> tasks;
mutex tasks_mutex;
mutex log_mutex;
int task_counter = 1;
ofstream log_file;
ofstream secure_log;

string encrypt_simple(const string& text) {
    string encrypted = text;
    for (size_t i = 0; i < encrypted.length(); i++) {
        encrypted[i] = encrypted[i] ^ 0x5A;
    }
    return encrypted;
}

string decrypt_simple(const string& text) {
    return encrypt_simple(text);
}

void log_message(const string& message, const string& level = "INFO") {
    lock_guard<mutex> lock(log_mutex);
    
    time_t now = time(0);
    char dt[100];
    strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    string log_entry = "[" + string(dt) + "] [" + level + "] " + message;
    
    cout << log_entry << endl;
    
    if (log_file.is_open()) {
        log_file << log_entry << endl;
        log_file.flush();
    }
    
    if (secure_log.is_open()) {
        string encrypted = encrypt_simple(log_entry);
        secure_log << encrypted << endl;
        secure_log.flush();
    }
}

// Вызывать только когда tasks_mutex УЖЕ захвачен вызывающим кодом
void save_tasks_to_file_locked() {
    ofstream file(TASKS_FILE);
    if (!file.is_open()) {
        log_message("Failed to open tasks file for writing", "ERROR");
        return;
    }
    
    file << tasks.size() << endl;
    
    for (const auto& task : tasks) {
        file << task.id << "|"
             << task.command << "|"
             << task.schedule_time << "|"
             << task.status << "|"
             << task.executed << "|"
             << task.scheduled_timestamp << "|"
             << task.created_at << endl;
    }
    
    file.close();
    log_message("Tasks saved to file: " + to_string(tasks.size()) + " tasks", "DEBUG");
}

// Версия с захватом мьютекса — для вызова вне залоченных блоков (например, при завершении)
void save_tasks_to_file() {
    lock_guard<mutex> lock(tasks_mutex);
    save_tasks_to_file_locked();
}

void load_tasks_from_file() {
    ifstream file(TASKS_FILE);
    if (!file.is_open()) {
        log_message("No existing tasks file found, starting fresh", "INFO");
        return;
    }
    
    int count;
    file >> count;
    file.ignore();
    
    lock_guard<mutex> lock(tasks_mutex);
    tasks.clear();
    
    for (int i = 0; i < count; i++) {
        Task task;
        string line;
        getline(file, line);
        
        stringstream ss(line);
        string field;
        
        getline(ss, field, '|'); task.id = stoi(field);
        getline(ss, task.command, '|');
        getline(ss, task.schedule_time, '|');
        getline(ss, task.status, '|');
        getline(ss, field, '|'); task.executed = (field == "1");
        getline(ss, field, '|'); task.scheduled_timestamp = stol(field);
        getline(ss, field, '|'); task.created_at = stol(field);
        
        tasks.push_back(task);
        
        if (task.id >= task_counter) {
            task_counter = task.id + 1;
        }
    }
    
    file.close();
    log_message("Loaded " + to_string(tasks.size()) + " tasks from file", "INFO");
}

time_t parse_time(const string& time_str) {
    struct tm tm_schedule = {};
    time_t now = time(0);
    localtime_r(&now, &tm_schedule);
    
    int hour, minute;
    if (sscanf(time_str.c_str(), "%d:%d", &hour, &minute) != 2) {
        return 0;
    }
    
    tm_schedule.tm_hour = hour;
    tm_schedule.tm_min = minute;
    tm_schedule.tm_sec = 0;
    
    time_t scheduled = mktime(&tm_schedule);
    
    if (scheduled < now) {
        scheduled += 86400;
    }
    
    return scheduled;
}

void execute_task(Task& task) {
    log_message("Executing task #" + to_string(task.id) + ": " + task.command, "INFO");
    task.status = "RUNNING";
    
    string full_command = task.command + " >> task_output.log 2>&1";
    int result = system(full_command.c_str());
    
    if (result == 0) {
        log_message("Task #" + to_string(task.id) + " completed successfully", "INFO");
        task.status = "COMPLETED";
    } else {
        log_message("Task #" + to_string(task.id) + " failed with code " + to_string(result), "ERROR");
        task.status = "FAILED";
    }
    
    task.executed = true;
    save_tasks_to_file_locked();
}

void scheduler_thread() {
    log_message("Scheduler thread started", "INFO");
    
    while (true) {
        time_t now = time(0);
        
        {
            lock_guard<mutex> lock(tasks_mutex);
            
            for (auto& task : tasks) {
                if (!task.executed && task.scheduled_timestamp <= now) {
                    execute_task(task);
                }
            }
        }
        
        this_thread::sleep_for(chrono::seconds(5));
    }
}

string handle_client_request(const string& request, const string& client_ip) {
    log_message("Processing request from " + client_ip + ": " + request, "DEBUG");
    
    stringstream ss(request);
    string action;
    ss >> action;
    
    log_message("Action: " + action, "DEBUG");
    
    if (action == "AUTH") {
        string password;
        ss >> password;
        
        if (password == SERVER_PASSWORD) {
            log_message("Client " + client_ip + " authenticated successfully", "INFO");
            return "OK: Authenticated";
        } else {
            log_message("Client " + client_ip + " authentication failed", "WARNING");
            return "ERROR: Authentication failed";
        }
        
    }
    
    // Все остальные команды требуют пароль в начале запроса
    string password;
    ss >> password;
    
    if (password != SERVER_PASSWORD) {
        log_message("Client " + client_ip + " unauthorized access attempt: " + action, "WARNING");
        return "ERROR: Authentication required. Please authenticate first.";
    }
    
    if (action == "ADD") {
        string time_str, command;
        ss >> time_str;
        
        if (ss.peek() == ' ') ss.ignore();
        getline(ss, command);
        
        log_message("Parsing - Time: " + time_str + ", Command: " + command, "DEBUG");
        
        if (command.empty()) {
            log_message("Empty command detected", "WARNING");
            return "ERROR: Command cannot be empty\n";
        }
        
        log_message("Step 1: About to parse time", "DEBUG");
        time_t scheduled_time = parse_time(time_str);
        if (scheduled_time == 0) {
            log_message("Invalid time format: " + time_str, "WARNING");
            return "ERROR: Invalid time format. Use HH:MM\n";
        }
        
        log_message("Step 2: Creating task object", "DEBUG");
        Task new_task;
        new_task.id = task_counter++;
        new_task.command = command;
        new_task.schedule_time = time_str;
        new_task.status = "PENDING";
        new_task.executed = false;
        new_task.scheduled_timestamp = scheduled_time;
        new_task.created_at = time(0);
        
        log_message("Step 3: Adding to task list", "DEBUG");
        {
            lock_guard<mutex> lock(tasks_mutex);
            tasks.push_back(new_task);
        }
        
        log_message("Step 4: Saving to file", "DEBUG");
        try {
            save_tasks_to_file_locked();
            log_message("Step 5: File saved successfully", "DEBUG");
        } catch (...) {
            log_message("ERROR in save_tasks_to_file", "ERROR");
        }
        
        log_message("Task #" + to_string(new_task.id) + " added successfully", "INFO");
        
        string response = "OK: Task #" + to_string(new_task.id) + " scheduled for " + time_str + "\n";
        log_message("Step 6: Returning response", "DEBUG");
        return response;
        
    }
    
    if (action == "DELETE") {
        int task_id;
        ss >> task_id;
        
        lock_guard<mutex> lock(tasks_mutex);
        
        auto it = find_if(tasks.begin(), tasks.end(), 
                         [task_id](const Task& t) { return t.id == task_id; });
        
        if (it == tasks.end()) {
            return "ERROR: Task #" + to_string(task_id) + " not found";
        }
        
        tasks.erase(it);
        save_tasks_to_file_locked();
        
        log_message("Task #" + to_string(task_id) + " deleted by " + client_ip, "INFO");
        return "OK: Task #" + to_string(task_id) + " deleted";
        
    }
    
    if (action == "MODIFY") {
        int task_id;
        string new_time, new_command;
        ss >> task_id >> new_time;
        getline(ss, new_command);
        
        if (!new_command.empty() && new_command[0] == ' ') {
            new_command = new_command.substr(1);
        }
        
        lock_guard<mutex> lock(tasks_mutex);
        
        auto it = find_if(tasks.begin(), tasks.end(),
                         [task_id](const Task& t) { return t.id == task_id; });
        
        if (it == tasks.end()) {
            return "ERROR: Task #" + to_string(task_id) + " not found";
        }
        
        if (it->executed) {
            return "ERROR: Cannot modify executed task";
        }
        
        if (!new_time.empty()) {
            time_t new_scheduled = parse_time(new_time);
            if (new_scheduled != 0) {
                it->schedule_time = new_time;
                it->scheduled_timestamp = new_scheduled;
            }
        }
        
        if (!new_command.empty()) {
            it->command = new_command;
        }
        
        save_tasks_to_file_locked();
        
        log_message("Task #" + to_string(task_id) + " modified by " + client_ip, "INFO");
        return "OK: Task #" + to_string(task_id) + " modified";
        
    }
    
    if (action == "LIST") {
        lock_guard<mutex> lock(tasks_mutex);
        
        stringstream response;
        response << "=== TASK LIST ===\n";
        response << "Total tasks: " << tasks.size() << "\n";
        
        if (tasks.empty()) {
            response << "\nNo tasks scheduled.\n";
        } else {
            response << "\n";
            for (const auto& task : tasks) {
                response << "ID: " << task.id << "\n"
                         << "Command: " << task.command << "\n"
                         << "Schedule: " << task.schedule_time << "\n"
                         << "Status: " << task.status << "\n"
                         << "---\n";
            }
        }
        
        return response.str();
        
    }
    
    if (action == "STATUS") {
        lock_guard<mutex> lock(tasks_mutex);
        
        int pending = 0, running = 0, completed = 0, failed = 0;
        
        for (const auto& task : tasks) {
            if (task.status == "PENDING") pending++;
            else if (task.status == "RUNNING") running++;
            else if (task.status == "COMPLETED") completed++;
            else if (task.status == "FAILED") failed++;
        }
        
        stringstream response;
        response << "=== SERVER STATUS ===\n"
                 << "Total tasks: " << tasks.size() << "\n"
                 << "Pending: " << pending << "\n"
                 << "Running: " << running << "\n"
                 << "Completed: " << completed << "\n"
                 << "Failed: " << failed << "\n";
        
        return response.str();
        
    }
    
    if (action == "INFO") {
        int task_id;
        ss >> task_id;
        
        lock_guard<mutex> lock(tasks_mutex);
        
        auto it = find_if(tasks.begin(), tasks.end(),
                         [task_id](const Task& t) { return t.id == task_id; });
        
        if (it == tasks.end()) {
            return "ERROR: Task #" + to_string(task_id) + " not found";
        }
        
        char created[100], scheduled[100];
        strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S", localtime(&it->created_at));
        strftime(scheduled, sizeof(scheduled), "%Y-%m-%d %H:%M:%S", localtime(&it->scheduled_timestamp));
        
        stringstream response;
        response << "=== TASK #" << it->id << " INFO ===\n"
                 << "Command: " << it->command << "\n"
                 << "Schedule Time: " << it->schedule_time << "\n"
                 << "Scheduled For: " << scheduled << "\n"
                 << "Created At: " << created << "\n"
                 << "Status: " << it->status << "\n"
                 << "Executed: " << (it->executed ? "Yes" : "No") << "\n";
        
        return response.str();
        
    }
    
    // Неизвестная команда
    log_message("Unknown command from " + client_ip + ": " + action, "WARNING");
    return "ERROR: Unknown command\nAvailable: AUTH, ADD, DELETE, MODIFY, LIST, STATUS, INFO";
}

void handle_client(int client_socket, const string& client_ip) {
    char buffer[4096] = {0};
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        string request(buffer);
        
        log_message("Client request: " + request, "DEBUG");
        
        string response = handle_client_request(request, client_ip);
        
        int total_sent = 0;
        int len = response.length();
        
        while (total_sent < len) {
            int sent = send(client_socket, response.c_str() + total_sent, len - total_sent, 0);
            if (sent < 0) break;
            total_sent += sent;
        }
        
        log_message("Sent " + to_string(total_sent) + " bytes", "DEBUG");
    }
    
    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);
}

void cleanup_and_exit(int sig) {
    log_message("Server shutting down...", "INFO");
    
    save_tasks_to_file(); // захватывает мьютекс сам
    
    if (log_file.is_open()) {
        log_file.close();
    }
    
    if (secure_log.is_open()) {
        secure_log.close();
    }
    
    cout << "\nServer stopped gracefully." << endl;
    exit(0);
}

int main() {
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);
    
    cout << "========================================" << endl;
    cout << "  DISTRIBUTED TASK SCHEDULER SERVER" << endl;
    cout << "========================================" << endl;
    
    log_file.open(LOG_FILE, ios::app);
    if (!log_file.is_open()) {
        cerr << "ERROR: Cannot open log file" << endl;
        return -1;
    }
    
    secure_log.open(SECURE_LOG_FILE, ios::app);
    if (!secure_log.is_open()) {
        cerr << "WARNING: Cannot open secure log file" << endl;
    }
    
    log_message("========== SERVER STARTING ==========", "INFO");
    log_message("Log file: " + string(LOG_FILE), "INFO");
    log_message("Secure log: " + string(SECURE_LOG_FILE), "INFO");
    log_message("Tasks file: " + string(TASKS_FILE), "INFO");
    
    load_tasks_from_file();
    
    thread scheduler(scheduler_thread);
    scheduler.detach();
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        log_message("Socket creation failed", "ERROR");
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (::bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        log_message("Bind failed on port " + to_string(PORT), "ERROR");
        return -1;
    }
    
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        log_message("Listen failed", "ERROR");
        return -1;
    }
    
    log_message("Server listening on port " + to_string(PORT), "INFO");
    log_message("Password: " + string(SERVER_PASSWORD), "DEBUG");
    cout << "\n[✓] Server is ready and waiting for clients..." << endl;
    cout << "[✓] Press Ctrl+C to stop server\n" << endl;
    
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            log_message("Accept failed", "ERROR");
            continue;
        }
        
        string client_ip = inet_ntoa(client_addr.sin_addr);
        log_message("Client connected from " + client_ip, "INFO");
        
        thread client_thread(handle_client, client_socket, client_ip);
        client_thread.detach();
    }
    
    close(server_fd);
    return 0;
}
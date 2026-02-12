#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <map>
#include <csignal>

using namespace std;

#define PORT 8080
#define PASSWORD "admin123"

struct Task {
    int id;
    string command, schedule_time, status;
    bool executed;
    time_t scheduled_timestamp, created_at;
};

vector<Task> tasks;
int task_counter = 1;
ofstream log_file;
mutex tasks_mutex, session_mutex, log_mutex;
bool server_running = true;
map<string, bool> authenticated_sessions;

void log_message(const string& msg) {
    lock_guard<mutex> lock(log_mutex);
    time_t now = time(0);
    char dt[100];
    strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", localtime(&now));
    string entry = "[" + string(dt) + "] " + msg;
    cout << entry << endl;
    if (log_file.is_open()) {
        log_file << entry << endl;
        log_file.flush();
    }
}

void save_tasks() {
    ofstream file("tasks.dat");
    if (!file.is_open()) return;
    file << tasks.size() << endl;
    for (const auto& t : tasks) {
        file << t.id << "|" << t.command << "|" << t.schedule_time << "|" 
             << t.status << "|" << t.executed << "|" << t.scheduled_timestamp 
             << "|" << t.created_at << endl;
    }
    file.close();
}

void load_tasks() {
    ifstream file("tasks.dat");
    if (!file.is_open()) {
        log_message("No existing tasks");
        return;
    }
    int count;
    file >> count;
    file.ignore();
    tasks.clear();
    for (int i = 0; i < count; i++) {
        Task t;
        string line, field;
        getline(file, line);
        stringstream ss(line);
        getline(ss, field, '|'); t.id = stoi(field);
        getline(ss, t.command, '|');
        getline(ss, t.schedule_time, '|');
        getline(ss, t.status, '|');
        getline(ss, field, '|'); t.executed = (field == "1");
        getline(ss, field, '|'); t.scheduled_timestamp = stol(field);
        getline(ss, field, '|'); t.created_at = stol(field);
        tasks.push_back(t);
        if (t.id >= task_counter) task_counter = t.id + 1;
    }
    file.close();
    log_message("Loaded " + to_string(tasks.size()) + " tasks");
}

time_t parse_time(const string& time_str) {
    struct tm tm_schedule = {};
    time_t now = time(0);
    localtime_r(&now, &tm_schedule);
    int hour, minute;
    if (sscanf(time_str.c_str(), "%d:%d", &hour, &minute) != 2) return 0;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return 0;
    tm_schedule.tm_hour = hour;
    tm_schedule.tm_min = minute;
    tm_schedule.tm_sec = 0;
    time_t scheduled = mktime(&tm_schedule);
    if (scheduled < now) scheduled += 86400;
    return scheduled;
}

void execute_task(Task& task) {
    log_message("Executing #" + to_string(task.id) + ": " + task.command);
    task.status = "RUNNING";
    string cmd = task.command + " > task_output_" + to_string(task.id) + ".log 2>&1";
    int result = system(cmd.c_str());
    task.status = (result == 0) ? "COMPLETED" : "FAILED";
    task.executed = true;
    log_message("Task #" + to_string(task.id) + " " + task.status);
}

void scheduler_thread() {
    log_message("Scheduler started");
    while (server_running) {
        time_t now = time(0);
        {
            lock_guard<mutex> lock(tasks_mutex);
            for (auto& t : tasks) {
                if (t.status == "PENDING" && now >= t.scheduled_timestamp && !t.executed) {
                    execute_task(t);
                    save_tasks();
                }
            }
        }
        this_thread::sleep_for(chrono::seconds(5));
    }
    log_message("Scheduler stopped");
}

string handle_request(const string& request, const string& client_ip) {
    stringstream ss(request);
    string action;
    ss >> action;
    log_message("Request from " + client_ip + ": " + action);
    
    if (action == "AUTH") {
        string password;
        ss >> password;
        if (password == PASSWORD) {
            lock_guard<mutex> lock(session_mutex);
            authenticated_sessions[client_ip] = true;
            log_message("Auth OK: " + client_ip);
            return "OK: Authenticated\n";
        }
        log_message("Auth FAIL: " + client_ip);
        return "ERROR: Invalid password\n";
    }
    
    {
        lock_guard<mutex> lock(session_mutex);
        if (!authenticated_sessions[client_ip]) 
            return "ERROR: Not authenticated\n";
    }
    
    if (action == "ADD") {
        string time_str, command;
        ss >> time_str;
        if (ss.peek() == ' ') ss.ignore();
        getline(ss, command);
        if (command.empty()) return "ERROR: Command empty\n";
        time_t scheduled_time = parse_time(time_str);
        if (scheduled_time == 0) return "ERROR: Invalid time (use HH:MM)\n";
        
        lock_guard<mutex> lock(tasks_mutex);
        Task t = {task_counter++, command, time_str, "PENDING", false, scheduled_time, time(0)};
        tasks.push_back(t);
        save_tasks();
        log_message("Task added: #" + to_string(t.id));
        return "OK: Task #" + to_string(t.id) + " added\n";
    }
    
    if (action == "LIST") {
        lock_guard<mutex> lock(tasks_mutex);
        stringstream res;
        res << "TASKS: " << tasks.size() << "\n";
        for (const auto& t : tasks) {
            res << "ID:" << t.id << "|CMD:" << t.command << "|TIME:" 
                << t.schedule_time << "|STATUS:" << t.status << "\n";
        }
        res << "END\n";
        return res.str();
    }
    
    if (action == "DELETE") {
        int id;
        ss >> id;
        lock_guard<mutex> lock(tasks_mutex);
        auto it = find_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.id == id; });
        if (it == tasks.end()) return "ERROR: Task not found\n";
        tasks.erase(it);
        save_tasks();
        log_message("Task deleted: #" + to_string(id));
        return "OK: Task deleted\n";
    }
    
    if (action == "MODIFY") {
        int id;
        string new_time, new_cmd;
        ss >> id >> new_time;
        if (ss.peek() == ' ') ss.ignore();
        getline(ss, new_cmd);
        
        lock_guard<mutex> lock(tasks_mutex);
        auto it = find_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.id == id; });
        if (it == tasks.end()) return "ERROR: Task not found\n";
        if (it->executed) return "ERROR: Cannot modify executed task\n";
        
        if (!new_time.empty() && new_time != "-") {
            time_t ts = parse_time(new_time);
            if (ts == 0) return "ERROR: Invalid time\n";
            it->schedule_time = new_time;
            it->scheduled_timestamp = ts;
        }
        if (!new_cmd.empty() && new_cmd != "-") it->command = new_cmd;
        save_tasks();
        log_message("Task modified: #" + to_string(id));
        return "OK: Task modified\n";
    }
    
    if (action == "STATUS") {
        lock_guard<mutex> lock(tasks_mutex);
        int pending = 0, completed = 0, failed = 0, running = 0;
        for (const auto& t : tasks) {
            if (t.status == "PENDING") pending++;
            else if (t.status == "COMPLETED") completed++;
            else if (t.status == "FAILED") failed++;
            else if (t.status == "RUNNING") running++;
        }
        stringstream res;
        res << "STATUS:\nTotal:" << tasks.size() << "\nPending:" << pending 
            << "\nRunning:" << running << "\nCompleted:" << completed 
            << "\nFailed:" << failed << "\nEND\n";
        return res.str();
    }
    
    if (action == "INFO") {
        int id;
        ss >> id;
        lock_guard<mutex> lock(tasks_mutex);
        auto it = find_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.id == id; });
        if (it == tasks.end()) return "ERROR: Task not found\n";
        stringstream res;
        res << "TASK INFO:\nID:" << it->id << "\nCommand:" << it->command 
            << "\nSchedule:" << it->schedule_time << "\nStatus:" << it->status 
            << "\nExecuted:" << (it->executed ? "Yes" : "No") 
            << "\nCreated:" << ctime(&it->created_at) << "END\n";
        return res.str();
    }
    
    return "ERROR: Unknown command\n";
}

void handle_client(int sock, const string& ip) {
    char buffer[4096] = {0};
    int bytes = read(sock, buffer, sizeof(buffer) - 1);
    string response;
    if (bytes > 0) {
        buffer[bytes] = '\0';
        string request(buffer);
        while (!request.empty() && (request.back() == '\n' || request.back() == '\r'))
            request.pop_back();
        response = handle_request(request, ip);
    } else {
        response = "ERROR: No data\n";
    }
    send(sock, response.c_str(), response.length(), 0);
    close(sock);
}

void signal_handler(int sig) {
    cout << "\nShutting down...\n";
    server_running = false;
    {
        lock_guard<mutex> lock(tasks_mutex);
        save_tasks();
    }
    log_message("Server shutdown");
    if (log_file.is_open()) log_file.close();
    exit(0);
}

int main() {
    cout << "==========================================\n"
         << "  TASK SCHEDULER SERVER\n"
         << "==========================================\n";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    log_file.open("scheduler.log", ios::app);
    if (!log_file.is_open()) {
        cerr << "ERROR: Cannot open log\n";
        return 1;
    }
    
    log_message("Server starting");
    load_tasks();
    
    thread scheduler(scheduler_thread);
    scheduler.detach();
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        log_message("ERROR: Socket failed");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_message("ERROR: Bind failed");
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        log_message("ERROR: Listen failed");
        return 1;
    }
    
    log_message("Listening on port " + to_string(PORT));
    cout << "[✓] Server ready (password: " << PASSWORD << ")\n"
         << "[✓] Scheduler running\n"
         << "[✓] Multi-threaded\n"
         << "[✓] Press Ctrl+C to stop\n\n";
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int sock = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (sock < 0) {
            if (server_running) log_message("ERROR: Accept failed");
            continue;
        }
        string ip = inet_ntoa(client_addr.sin_addr);
        log_message("Client: " + ip);
        thread(handle_client, sock, ip).detach();
    }
    
    close(server_fd);
    log_file.close();
    return 0;
}
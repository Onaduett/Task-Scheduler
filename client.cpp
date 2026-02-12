#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define PORT 8080
#define SERVER_IP "127.0.0.1"

bool is_authenticated = false;
string client_ip = "127.0.0.1";

string send_request(const string& request) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return "ERROR: Cannot create socket\n";
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sock);
        return "ERROR: Invalid address\n";
    }
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return "ERROR: Connection failed. Is server running?\n";
    }
    
    // Send request
    send(sock, request.c_str(), request.length(), 0);
    
    // Read response
    string result = "";
    char buffer[4096];
    int bytes;
    
    while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        result += buffer;
    }
    
    close(sock);
    
    if (result.empty()) {
        return "ERROR: No response from server\n";
    }
    
    return result;
}

void display_menu() {
    cout << "\n==========================================\n";
    cout << "  TASK SCHEDULER CLIENT\n";
    cout << "==========================================\n";
    if (is_authenticated) {
        cout << "[✓] Authenticated\n\n";
    } else {
        cout << "[!] Not authenticated\n\n";
    }
    
    cout << "1. Add task\n";
    cout << "2. List tasks\n";
    cout << "3. Server status\n";
    cout << "4. Delete task\n";
    cout << "5. Task info\n";
    cout << "6. Modify task\n";
    cout << "0. Exit\n";
    cout << "\nChoice: ";
}

int main() {
    cout << "\n==========================================\n";
    cout << "  TASK SCHEDULER CLIENT\n";
    cout << "==========================================\n";
    cout << "Server: " << SERVER_IP << ":" << PORT << "\n\n";
    
    // Password prompt at startup
    cout << "Password: ";
    string password;
    getline(cin, password);
    
    if (password.empty()) {
        cout << "\nERROR: Password cannot be empty\n";
        return 1;
    }
    
    // Try to authenticate with server
    string auth_request = "AUTH " + password;
    string auth_response = send_request(auth_request);
    
    if (auth_response.find("ERROR: Connection failed") != string::npos) {
        cout << "\n" << auth_response;
        cout << "Please make sure the server is running.\n";
        return 1;
    }
    
    if (auth_response.find("OK: Authenticated") != string::npos) {
        is_authenticated = true;
        cout << "\n[✓] Authentication successful!\n";
    } else {
        cout << "\nERROR: Invalid password. Access denied.\n";
        return 1;
    }
    
    // Main menu loop
    while (true) {
        display_menu();
        
        int choice;
        cin >> choice;
        cin.ignore();
        
        if (choice == 0) {
            cout << "\nGoodbye!\n";
            break;
        }
        
        string request, response;
        
        if (choice == 1) {
            // Add task
            cout << "\nTime (HH:MM): ";
            string time;
            getline(cin, time);
            
            cout << "Command: ";
            string cmd;
            getline(cin, cmd);
            
            request = "ADD " + time + " " + cmd;
            response = send_request(request);
            cout << "\n" << response;
            
        } else if (choice == 2) {
            // List tasks
            request = "LIST";
            response = send_request(request);
            
            cout << "\n==========================================\n";
            cout << "  TASK LIST\n";
            cout << "==========================================\n";
            
            if (response.find("TASKS: 0") != string::npos) {
                cout << "No tasks scheduled.\n";
            } else if (response.find("ERROR") == 0) {
                cout << response;
            } else {
                string line;
                size_t pos = 0;
                int task_num = 0;
                
                while ((pos = response.find('\n')) != string::npos) {
                    line = response.substr(0, pos);
                    
                    if (line.find("ID:") == 0) {
                        task_num++;
                        // Parse task line
                        size_t id_pos = line.find("ID:") + 3;
                        size_t cmd_pos = line.find("CMD:") + 4;
                        size_t time_pos = line.find("TIME:") + 5;
                        size_t status_pos = line.find("STATUS:") + 7;
                        
                        string id = line.substr(id_pos, line.find("|", id_pos) - id_pos);
                        string cmd = line.substr(cmd_pos, line.find("|", cmd_pos) - cmd_pos);
                        string time = line.substr(time_pos, line.find("|", time_pos) - time_pos);
                        string status = line.substr(status_pos);
                        
                        cout << "Task #" << id << "\n";
                        cout << "  Command: " << cmd << "\n";
                        cout << "  Time: " << time << "\n";
                        cout << "  Status: " << status << "\n";
                        cout << "---\n";
                    }
                    
                    response.erase(0, pos + 1);
                }
            }
            
        } else if (choice == 3) {
            // Server status
            request = "STATUS";
            response = send_request(request);
            cout << "\n" << response;
            
        } else if (choice == 4) {
            // Delete task
            cout << "\nTask ID: ";
            int id;
            cin >> id;
            cin.ignore();
            
            request = "DELETE " + to_string(id);
            response = send_request(request);
            cout << "\n" << response;
            
        } else if (choice == 5) {
            // Task info
            cout << "\nTask ID: ";
            int id;
            cin >> id;
            cin.ignore();
            
            request = "INFO " + to_string(id);
            response = send_request(request);
            cout << "\n" << response;
            
        } else if (choice == 6) {
            // Modify task
            cout << "\nTask ID: ";
            int id;
            cin >> id;
            cin.ignore();
            
            cout << "New time (or - to keep): ";
            string time;
            getline(cin, time);
            
            cout << "New command (or - to keep): ";
            string cmd;
            getline(cin, cmd);
            
            request = "MODIFY " + to_string(id) + " " + time + " " + cmd;
            response = send_request(request);
            cout << "\n" << response;
            
        } else {
            cout << "\nInvalid choice!\n";
        }
        
        cout << "\nPress Enter to continue...";
        cin.get();
    }
    
    return 0;
}
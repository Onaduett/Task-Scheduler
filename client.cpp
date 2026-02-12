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
#define CONNECT_TIMEOUT_SEC 5

// Состояние авторизации
static bool is_authenticated = false;
static string stored_password = "";

// Устанавливает таймаут на сокет (в секундах)
static bool set_socket_timeout(int sock, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return false;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return false;
    return true;
}

// Отправляет запрос, возвращает ответ сервера или строку "ERROR: ..."
string send_request(const string& request) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return "ERROR: Cannot create socket\n";
    }

    // Таймаут на подключение и I/O — не зависаем навечно
    set_socket_timeout(sock, CONNECT_TIMEOUT_SEC);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sock);
        return "ERROR: Invalid server address\n";
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return "ERROR: Connection failed. Is server running?\n";
    }

    // Отправляем запрос целиком, учитывая частичную отправку
    size_t total_sent = 0;
    while (total_sent < request.size()) {
        ssize_t sent = send(sock, request.c_str() + total_sent,
                            request.size() - total_sent, 0);
        if (sent < 0) {
            close(sock);
            return "ERROR: Failed to send request\n";
        }
        total_sent += static_cast<size_t>(sent);
    }

    // Читаем ответ блокирующим recv до тех пор, пока сервер не закроет соединение.
    // Сервер делает shutdown+close сразу после отправки, поэтому recv вернёт 0 — это EOF.
    // Никакого sleep не нужно.
    string result;
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        result += buffer;
    }
    // bytes == 0  → сервер закрыл соединение (норма)
    // bytes  < 0  → ошибка или таймаут
    if (bytes < 0 && result.empty()) {
        close(sock);
        return "ERROR: No response from server (timeout?)\n";
    }

    close(sock);
    return result;
}

// Сбрасывает флаги ошибок cin и очищает буфер ввода
static void clear_cin() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

int main() {
    cout << "\n========================================" << endl;
    cout << "  TASK SCHEDULER CLIENT" << endl;
    cout << "========================================\n" << endl;

    while (true) {
        cout << "1. Authenticate"   << endl;
        cout << "2. Add task"       << endl;
        cout << "3. List tasks"     << endl;
        cout << "4. Server status"  << endl;
        cout << "5. Delete task"    << endl;
        cout << "6. Task info"      << endl;
        cout << "7. Modify task"    << endl;
        cout << "0. Exit"           << endl;
        cout << "\nChoice: ";

        int choice;
        if (!(cin >> choice)) {
            // Некорректный ввод — очищаем и повторяем
            clear_cin();
            cout << "Invalid input, please enter a number.\n\n";
            continue;
        }
        cin.ignore(); // убираем '\n' после числа

        if (choice == 0) {
            cout << "Goodbye!\n";
            break;
        }

        string request, response;

        if (choice == 1) {
            cout << "\nPassword: ";
            string pass;
            getline(cin, pass);

            request  = "AUTH " + pass;
            response = send_request(request);
            cout << "\n" << response << endl;
            
            // Сохраняем пароль при успешной авторизации
            if (response.find("OK:") == 0) {
                is_authenticated = true;
                stored_password = pass;
            } else {
                is_authenticated = false;
                stored_password = "";
            }

        } else if (choice == 2) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            cout << "\nTime (HH:MM): ";
            string time_str;
            getline(cin, time_str);

            // Валидация формата HH:MM на стороне клиента
            int hh = -1, mm = -1;
            if (sscanf(time_str.c_str(), "%d:%d", &hh, &mm) != 2
                    || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
                cout << "\nERROR: Time must be in HH:MM format (00:00 – 23:59)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }

            cout << "Command: ";
            string cmd;
            getline(cin, cmd);

            if (cmd.empty()) {
                cout << "\nERROR: Command cannot be empty\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }

            // Запрещаем символы переноса строки — защита от инъекции команд
            if (cmd.find('\n') != string::npos || cmd.find('\r') != string::npos) {
                cout << "\nERROR: Command must not contain newline characters\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }

            request  = "ADD " + stored_password + " " + time_str + " " + cmd;
            response = send_request(request);
            cout << "\n" << response << endl;

        } else if (choice == 3) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            request  = "LIST " + stored_password;
            response = send_request(request);
            cout << "\n" << response << endl;

        } else if (choice == 4) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            request  = "STATUS " + stored_password;
            response = send_request(request);
            cout << "\n" << response << endl;

        } else if (choice == 5) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            cout << "\nTask ID: ";
            int id;
            if (!(cin >> id) || id <= 0) {
                clear_cin();
                cout << "\nERROR: Task ID must be a positive integer\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            cin.ignore();

            request  = "DELETE " + stored_password + " " + to_string(id);
            response = send_request(request);
            cout << "\n" << response << endl;

        } else if (choice == 6) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            cout << "\nTask ID: ";
            int id;
            if (!(cin >> id) || id <= 0) {
                clear_cin();
                cout << "\nERROR: Task ID must be a positive integer\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            cin.ignore();

            request  = "INFO " + stored_password + " " + to_string(id);
            response = send_request(request);
            cout << "\n" << response << endl;

        } else if (choice == 7) {
            // Проверка авторизации
            if (!is_authenticated) {
                cout << "\nERROR: You must authenticate first (option 1)\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            
            cout << "\nTask ID: ";
            int id;
            if (!(cin >> id) || id <= 0) {
                clear_cin();
                cout << "\nERROR: Task ID must be a positive integer\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }
            cin.ignore();

            cout << "New time (HH:MM, or leave blank to keep): ";
            string new_time;
            getline(cin, new_time);

            cout << "New command (or leave blank to keep): ";
            string new_cmd;
            getline(cin, new_cmd);

            if (new_cmd.find('\n') != string::npos || new_cmd.find('\r') != string::npos) {
                cout << "\nERROR: Command must not contain newline characters\n" << endl;
                cout << "\nPress Enter...";
                cin.get();
                cout << "\n";
                continue;
            }

            request  = "MODIFY " + stored_password + " " + to_string(id) + " " + new_time + " " + new_cmd;
            response = send_request(request);
            cout << "\n" << response << endl;

        } else {
            cout << "Invalid choice!\n";
        }

        cout << "\nPress Enter...";
        cin.get();
        cout << "\n";
    }

    return 0;
}
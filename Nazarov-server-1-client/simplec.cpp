#include "pch.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// Преобразует 64 - битное число value из хостового порядка байт в сетевой (big - endian).
uint64_t my_htonll(uint64_t value) {
    static const int num = 1;
    if (*(char*)&num == 1) {
        return (((uint64_t)htonl((uint32_t)(value & 0xFFFFFFFFULL))) << 32) |
            htonl((uint32_t)(value >> 32));
    }
    else {
        return value;
    }
}

// Преобразует 64 - битное число value из сетевого порядка байт (big - endian) в хостовый.
uint64_t my_ntohll(uint64_t value) {
    static const int num = 1;
    if (*(char*)&num == 1) {
        return (((uint64_t)ntohl((uint32_t)(value & 0xFFFFFFFFULL))) << 32) |
            ntohl((uint32_t)(value >> 32));
    }
    else {
        return value;
    }
}

// Также преобразует 64 - битное число из сетевого порядка в хостовый, 
// но использует другой способ определения порядка байт
uint64_t my_ntohll2(uint64_t netlonglong) {
    static const int num = 42;
    if (*(const char*)&num == num) {
        // Локальная машина little-endian
        uint64_t hostlonglong = ((uint64_t)ntohl(netlonglong & 0xFFFFFFFF) << 32) |
            ntohl(netlonglong >> 32);
        return hostlonglong;
    }
    else {
        // big-endian
        return netlonglong;
    }
}

// Инициализация Winsock API
void initWSA() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cerr << "WSAStartup failed\n";
        exit(1);
    }
}

// Очистка ресурсов Winsock
void cleanupWSA() {
    WSACleanup();
}

int main() {
    // Инициализация Winsock
    initWSA();

    // Создаем TCP-сокет
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "Socket creation failed\n";
        cleanupWSA();
        return 1;
    }

    string server_ip;
    int server_port;

    // Ввод IP-адреса и порта сервера с проверкой правильности
    while (true) {
        cout << "Enter server IP-address: ";
        getline(cin, server_ip);
        cout << "Enter server port: ";
        cin >> server_port;
        cin.ignore();

        if (server_ip.empty() || server_port <= 0 || server_port > 65535) {
            // Обработка некорректного ввода
            cout << "Uncorrectly enter, try again.\n";
        }
        else {
            break;
        }
    }

    // Заполняем структуру sockaddr_in для подключения
    sockaddr_in server_addr = { 0 };
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        // Обработка неудачного подключения
        cerr << "Couldn't connect to the server\n";
        closesocket(sock);
        cleanupWSA();
        return 1;
    }

    cout << "Connected successfully!\n";

    // Основной цикл меню
    while (true) {
        cout << "\nMenu:\n1. Transfer file\n2. Download file\n3. Exit\nSelect item: ";
        int choice;
        cin >> choice;
        cin.ignore();

        if (choice == 1) {
            // Передача файла (команда "UPLOAD")
            // Отправляем команду на сервер
            send(sock, "UPLOAD\n", 7, 0);

            string filename;
            cout << "Enter the file name to transfer: ";
            getline(cin, filename);

            // Открываем файл для чтения в бинарном режиме
            ifstream file(filename, ios::binary | ios::ate);
            if (!file) {
                cerr << "Failed to open file\n";
                continue; // Возврат к меню
            }

            // Получаем размер файла
            uint64_t filesize = file.tellg();
            file.seekg(0);

            // Отправляем размер файла в сетевом порядке
            uint64_t size_net = htonll(filesize);
            send(sock, (char*)&size_net, sizeof(size_net), 0);

            // Отправляем имя файла с символом новой строки
            string fname = filename + "\n";
            send(sock, fname.c_str(), fname.size(), 0);

            // Отправляем содержимое файла по частям
            char buffer[4096];
            size_t total_sent = 0;
            while (!file.eof()) {
                file.read(buffer, sizeof(buffer));
                streamsize s = file.gcount();
                if (s > 0) {
                    int sended = send(sock, buffer, s, 0);
                    // Подсчет отправленных байт
                    total_sent += sended;
                }
            }
            cout << "File sent successfully. Total bytes: " << total_sent << "\n";

        }
        else if (choice == 2) {
            // Запрос файла на скачивание (команда "DOWNLOAD")
            // Отправляем команду на сервер
            send(sock, "DOWNLOAD\n", 9, 0);

            string filename;
            cout << "Enter the name of the file to download: ";
            getline(cin, filename);

            // Отправляем имя файла и символ новой строки
            send(sock, filename.c_str(), filename.size(), 0);
            send(sock, "\n", 1, 0);

            // Получаем размер файла от сервера
            uint64_t fileSizeNet;
            int total_recv = 0;
            while (total_recv < sizeof(fileSizeNet)) {
                int s = recv(sock, ((char*)&fileSizeNet) + total_recv, sizeof(fileSizeNet) - total_recv, 0);
                if (s <= 0) {
                    cerr << "Error receiving file size or connection closed.\n";
                    closesocket(sock);
                    return 1;
                }
                total_recv += s;
            }

            // Преобразуем размер файла из сетевого порядка в хостовый
            uint64_t fileSize = my_ntohll(fileSizeNet);

            // Создаем новый файл для записи
            ofstream outputFile("downloaded_file", ios::binary);
            if (!outputFile) {
                cerr << "Failed to open output file\n";
                closesocket(sock);
                return 1;
            }

            // Получаем содержимое файла
            uint64_t remaining = fileSize;
            char buffer[4096];

            while (remaining > 0) {
                int to_read = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
                int received = recv(sock, buffer, to_read, 0);
                if (received <= 0) {
                    cerr << "Error receiving file data\n";
                    break;
                }
                // Записываем полученные данные в файл
                outputFile.write(buffer, received);
                // Обновляем оставшийся размер
                remaining -= received;
            }

            cout << "File downloaded successfully.\n";

            outputFile.close();

        }
        else if (choice == 3) {
            // Отправляем команду завершения ("EXIT") и выходим из цикла
            send(sock, "EXIT\n", 5, 0);
            break;
        }
        else {
            // Обработка некорректного ввода
            cout << "Incorrect choice, try again.\n";
        }
    }

    // Закрываем сокет и очищаем ресурсы Winsock
    closesocket(sock);
    cleanupWSA();
    return 0;
}
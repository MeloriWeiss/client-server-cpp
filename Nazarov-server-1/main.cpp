#include "pch.h"
#include <iostream>
#include <fstream>
#include <string>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// Преобразует 64 - битное число из сетевого порядка байт (big - endian) в порядок байт,
// используемый на хосте (обычно little - endian или big - endian, в зависимости от системы).
uint64_t my_ntohll(uint64_t value) {
    static const int num = 42;
    if (*(const char*)&num == num) {
        // little - endian: меняем порядок байт
        return ((uint64_t)ntohl((uint32_t)(value >> 32)) | ((uint64_t)ntohl((uint32_t)value) << 32));
    }
    else {
        // big - endian: ничего не делаем
        return value;
    }
}

// Обратная операция : преобразует 64 - битное число из хостового порядка байт в сетевой(big - endian)
uint64_t my_htonll(uint64_t host64) {
    // Проверка порядка байт системы
    static const int num = 1;
    if (*(char*)&num == 1) {
        // little - endian: меняем байты местами
        return ((uint64_t)htonl(host64 & 0xFFFFFFFF) << 32) | htonl(host64 >> 32);
    }
    else {
        // big - endian: уже в сетевом порядке
        return host64;
    }
}

// Читает из сокета по одному байту, пока не встретит символ новой строки '\n',
// и возвращает считанную строку без этого символа.
string readLineFromSocket(SOCKET sock) {
    string line;
    char ch;
    while (true) {
        int s = recv(sock, &ch, 1, 0);
        if (s <= 0) {
            // Ошибка или соединение закрыто
            // Возвращаем пустую строку, чтобы определить закрытие
            return "";
        }
        if (ch == '\n') {
            break;
        }
        line += ch;
    }
    return line;
}

int main() {
    // Инициализация Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Создание сокета для прослушивания входящих соединений
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed" << endl;
        WSACleanup();
        return 1;
    }

    // Настройка адреса сервера: любой IP-адрес на порту 12345
    sockaddr_in serverAddr = { 0 };
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Привязка сокета к адресу и порту
    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Socket binding failed" << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Начинаем слушать входящие соединения (максимум 1 очередь)
    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        cerr << "Listening mode failed" << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Waiting for client..." << endl;

    // Ожидание входящего соединения (accept)
    sockaddr_in clientAddr;
    int clientSize = sizeof(clientAddr);
    SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientSize);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Failed to accept connection" << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Client connected." << endl;

    // Основной цикл обработки команд от клиента
    while (true) {
        // Получение команды от клиента (например, "DOWNLOAD" или "UPLOAD")
        string commandStr = readLineFromSocket(clientSocket);
        if (commandStr.empty()) {
            cerr << "Connection closed or error.\n";

            // прерываемся при ошибке или закрытии соединения
            break;
        }

        // Удаляем переносы строк из команды
        commandStr.erase(remove(commandStr.begin(), commandStr.end(), '\r'), commandStr.end());
        commandStr.erase(remove(commandStr.begin(), commandStr.end(), '\n'), commandStr.end());

        // Обработка команды скачивания файла
        if (commandStr == "DOWNLOAD") {
            // Получение имени файла от клиента
            string filename = readLineFromSocket(clientSocket);

            filename.erase(remove(filename.begin(), filename.end(), '\r'), filename.end());

            // Открытие файла для чтения в бинарном режиме
            ifstream inputFile(filename, ios::binary | ios::ate);
            if (!inputFile) {
                // Если файл не найден или не открыт, отправляем сообщение об ошибке
                string errorMsg = "ERROR: File not found\n";
                send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);

                // Ждём следующую команду
                continue;
            }

            // Получение размера файла
            uint64_t fileSize = inputFile.tellg();
            inputFile.seekg(0, ios::beg);

            // Отправка размера файла в сетевом порядке байт
            uint64_t fileSizeNet = my_htonll(fileSize);
            send(clientSocket, (char*)&fileSizeNet, sizeof(fileSizeNet), 0);

            // Отправка содержимого файла чанками
            char buffer[4096];
            while (!inputFile.eof()) {
                inputFile.read(buffer, sizeof(buffer));
                streamsize bytesRead = inputFile.gcount();
                send(clientSocket, buffer, static_cast<int>(bytesRead), 0);
            }
            inputFile.close();

            cout << "File successfully downloaded";
        }
        // Обработка команды загрузки файла
        else if (commandStr == "UPLOAD") {
            // Получение размера файла (8 байт)
            uint64_t file_size_net;
            int total_recv = 0;
            while (total_recv < sizeof(file_size_net)) {
                int s = recv(clientSocket, ((char*)&file_size_net) + total_recv, sizeof(file_size_net) - total_recv, 0);
                if (s <= 0) {
                    cerr << "Error receiving file size.\n";
                    break;
                }
                total_recv += s;
            }

            // Вывод полученных байтов
            unsigned char* bytes = (unsigned char*)&file_size_net;
            cout << "Received file size bytes: ";
            for (int i = 0; i < 8; ++i) {
                cout << hex << (int)bytes[i] << " ";
            }
            cout << dec << endl;

            // Преобразование из сетевого порядка в хостовой
            uint64_t filesize = my_ntohll(file_size_net);
            cout << "Parsed file size: " << filesize << endl;

            // Получение имени файла (читаем строку, разделённую переносом строки)
            char buffer1[1024];
            int recv_size = recv(clientSocket, buffer1, sizeof(buffer1) - 1, 0);
            if (recv_size > 0) {
                // завершаем строку
                buffer1[recv_size] = '\0';
                string filename(buffer1);
            }

            // Создание файла для записи полученных данных
            ofstream outFile("received_test.bin", ios::binary);
            if (!outFile.is_open()) {
                cerr << "Failed to open output file." << endl;
                closesocket(clientSocket);
                closesocket(listenSocket);
                WSACleanup();
                return 1;
            }

            // Цикл приема данных файла
            uint64_t totalReceivedData = 0;
            const size_t bufferSize = 4096;
            vector<char> buffer(bufferSize);

            cout << "[LOG] Total received data: " << totalReceivedData << " / " << filesize << endl;

            while (totalReceivedData < filesize) {
                cout << "[LOG] Loop start, totalReceivedData = " << totalReceivedData << ", filesize = " << filesize << endl;

                size_t toRead = (size_t)min<uint64_t>(bufferSize, filesize - totalReceivedData);
                // Используем MSG_PEEK для предварительного просмотра данных
                int received = recv(clientSocket, buffer.data(), (int)toRead, MSG_PEEK);

                cout << "[LOG] recv returned: " << received << endl;

                if (received == SOCKET_ERROR || received == 0) {
                    cerr << "Error receiving data or connection closed." << endl;
                    break;
                }
                // Записываем полученные данные в файл
                outFile.write(buffer.data(), received);
                totalReceivedData += received;

                cout << "[LOG] totalReceivedData after recv: " << totalReceivedData << endl;
            }

            outFile.close();

            cout << "[LOG] Total received data: " << totalReceivedData << " / " << filesize << endl;
            if (totalReceivedData == filesize) {
                cout << "File received successfully." << endl;
            }
            else {
                cerr << "Received less data than expected." << endl;
            }
        }
        else {
            // Обрабатываем неизвестную команду
            string errMsg = "ERROR: Unknown command\n";
            send(clientSocket, errMsg.c_str(), errMsg.size(), 0);
        }
    }

    // Завершение работы: закрытие сокетов и очистка Winsock
    closesocket(clientSocket);
    closesocket(listenSocket);
    // Очистка ресурсов Winsock
    WSACleanup();

    return 0;
}
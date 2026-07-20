#include "AppComm.h"

#ifdef NATIVE_TESTING

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <queue>

static int server_fd = -1;
static int client_socket = -1;
static std::queue<std::string> message_queue;

void AppComm::init() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        return;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9876); // Port 9876 for mock

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return;
    }

    // Set server socket to non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);
    std::cout << "Native TCP Mock listening on port 9876..." << std::endl;
}

void AppComm::poll() {
    if (server_fd < 0) return;

    if (client_socket < 0) {
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_socket >= 0) {
            std::cout << "Client connected to TCP Mock" << std::endl;
            fcntl(client_socket, F_SETFL, O_NONBLOCK);
        }
    } else {
        char buffer[1024] = {0};
        int valread = read(client_socket, buffer, 1024);
        if (valread > 0) {
            std::cout << "Received from app: " << buffer << std::endl;
            message_queue.push(std::string(buffer, valread));
            // Echo back or send acknowledgment
            std::string ack = "ACK: " + std::string(buffer, valread);
            sendData(ack);
        } else if (valread == 0) {
            std::cout << "Client disconnected" << std::endl;
            close(client_socket);
            client_socket = -1;
        }
    }
}

void AppComm::sendData(const std::string& data) {
    if (client_socket >= 0) {
        send(client_socket, data.c_str(), data.length(), 0);
    }
}

bool AppComm::hasData() {
    return !message_queue.empty();
}

std::string AppComm::getNextMessage() {
    if (message_queue.empty()) return "";
    std::string msg = message_queue.front();
    message_queue.pop();
    return msg;
}

#else
// Real ESP32 implementation (BLE) - empty for now
#include <Arduino.h>

void AppComm::init() {
    Serial.println("AppComm init (BLE not yet implemented)");
}

void AppComm::poll() {
    // BLE poll logic
}

void AppComm::sendData(const std::string& data) {
    // BLE send logic
}

bool AppComm::hasData() {
    return false;
}

std::string AppComm::getNextMessage() {
    return "";
}

#endif

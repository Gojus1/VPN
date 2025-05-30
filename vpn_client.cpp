
#define _WIN32_WINNT 0x0601

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "13.41.72.149"
#define PORT 5000
#define BUFSIZE 4096

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in server {};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    std::cout << "Connecting to VPN proxy at " << SERVER_IP << "...\n";
    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0) {
        std::cerr << "Connection failed.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to proxy server. Sending request...\n";

    const char* http_request =
        "GET /ip HTTP/1.1\r\n"
        "Host: example.com\r\n\r\n"
        "Connection: close\r\n\r\n";

    send(sock, http_request, strlen(http_request), 0);

    char buffer[BUFSIZE];
    int len;
    std::string response;
    
    while ((len = recv(sock, buffer, BUFSIZE - 1, 0)) > 0) {
        buffer[len] = '\0';
        response += buffer;
    }
    
    std::cout << "Full Response:\n" << response << std::endl;
    

    closesocket(sock);
    WSACleanup();
    return 0;
}

#include <vector>
#include <climits>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>

#define PORT 5000
#define BUFSIZE 8192

std::map<std::string, std::string> routing_table;
std::mutex route_mutex;
bool toggle = false;

std::string resolve_host(const std::string& host) {
    std::lock_guard<std::mutex> lock(route_mutex);
    if (routing_table.count(host)) {
        std::cout << "[ROUTING] static routing for " << host << "   ->   " << routing_table[host] << "\n";
        return routing_table[host];
    }
    hostent* server = gethostbyname(host.c_str());
    if (!server) return "";
    return inet_ntoa(*(struct in_addr*)server->h_addr);
}

void handle_client(int client_fd) {
    char buffer[BUFSIZE];
    std::string request;
    int len;

    while (true) {
        len = recv(client_fd, buffer, BUFSIZE - 1, 0);
        if (len <= 0) return;
        buffer[len] = '\0';
        request += buffer;
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    size_t host_start = request.find("Host: ");
    if (host_start == std::string::npos) return;
    host_start += 6;
    size_t host_end = request.find("\r\n", host_start);
    std::string host = request.substr(host_start, host_end - host_start);

    std::string dest_ip = resolve_host(host);
    if (dest_ip.empty()) {
        std::cerr << "ERROR" << host << "\n";
        close(client_fd);
        return;
    }

    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    inet_pton(AF_INET, dest_ip.c_str(), &dest.sin_addr);

    int forward_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(forward_fd, (sockaddr*)&dest, sizeof(dest)) < 0) {
        std::cerr << "ERROR CONNECT" << dest_ip << "\n";
        close(client_fd);
        return;
    }

    send(forward_fd, request.c_str(), request.size(), 0);

    while ((len = recv(forward_fd, buffer, BUFSIZE, 0)) > 0) {
        send(client_fd, buffer, len, 0);
    }

    close(forward_fd);
    close(client_fd);
}

void simulate_simple_rip() {
    struct Neighbor {
        std::string ip;
        int cost;
    };

    std::map<std::string, std::vector<Neighbor>> topology = {
        {"httpbin.org", {{"50.16.79.29", 1}, {"3.229.250.103", 2}}},
        {"example.com", {{"96.7.128.175", 1}, {"23.192.228.80", 3}}},
        {"openai.com",  {{"104.18.33.45", 1}, {"172.64.154.211", 2}}}
    };

    int tick = 0;

    while (true) {
        std::lock_guard<std::mutex> lock(route_mutex);

        std::cout << "[ROUTING] New route at " << tick << "...\n";

        for (const auto& entry : topology) {
            const std::string& host = entry.first;
            const auto& neighbors = entry.second;

            std::string best_ip;
            int best_cost = INT_MAX;

            for (const auto& neighbor : neighbors) {
                int dynamic_cost = neighbor.cost + (rand() % 2);
                if (dynamic_cost < best_cost) {
                    best_cost = dynamic_cost;
                    best_ip = neighbor.ip;
                }
            }

            routing_table[host] = best_ip;
            std::cout << "ROUTING] Best route to " << host << "  ->   " << best_ip
                      << " (cost " << best_cost << ")\n";
        }

        tick++;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}


int main() {
    std::thread(simulate_simple_rip).detach();
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);
    std::cout << "[PROXY] VPN + RIP simulator (port: " << PORT << ")\n";

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            if (fork() == 0) {
                close(server_fd);
                handle_client(client_fd);
                exit(0);
            } else {
                close(client_fd);
            }
        }
    }
    return 0;
}

//External server vpn connect, with rip routing simulation
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
#include <sstream>

#define PORT 5000
#define BUFSIZE 8192
#define RIP_UPDATE_INTERVAL 10

std::map<std::string, std::string> routing_table;
std::mutex route_mutex;

struct RouteEntry {
    int cost;
    std::string via_ip;
};

class Router {
public:
    std::string name;
    int udp_port;
    std::map<std::string, RouteEntry> table;
    std::map<std::string, int> direct_hosts;
    std::vector<int> neighbors;

    Router(std::string name, int udp_port) : name(std::move(name)), udp_port(udp_port) {}

    void add_direct_host(const std::string& hostname, const std::string& ip, int cost) {
        table[hostname] = {cost, ip};
        direct_hosts[hostname] = cost;
    }

    void add_neighbor(int neighbor_port) {
        neighbors.push_back(neighbor_port);
    }

    void send_updates() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        for (int port : neighbors) {
            addr.sin_port = htons(port);
            std::ostringstream oss;
            for (const auto& entry : table) {
                oss << entry.first << " " << entry.second.via_ip << " " << entry.second.cost << "\n";
            }
            std::string msg = oss.str();
            sendto(sock, msg.c_str(), msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
        }

        close(sock);
    }

    void receive_updates() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(udp_port);
        bind(sock, (sockaddr*)&addr, sizeof(addr));

        char buffer[1024];
        sockaddr_in sender{};
        socklen_t sender_len = sizeof(sender);

        while (true) {
            int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&sender, &sender_len);
            if (len <= 0) continue;
            buffer[len] = '\0';
            std::istringstream iss(buffer);
            std::string line;
            while (std::getline(iss, line)) {
                std::istringstream liness(line);
                std::string host, via_ip;
                int cost;
                liness >> host >> via_ip >> cost;
                int new_cost = cost + 1;
                if (!table.count(host) || new_cost < table[host].cost) {
                    table[host] = {new_cost, via_ip};
                }
            }
        }
    }

    void run() {
        std::thread([this]() {
            receive_updates();
        }).detach();

        while (true) {
            send_updates();

            std::cout << "[ROUTER " << name << "] Routing table:\n";
            for (const auto& entry : table) {
                std::cout << "  " << entry.first << " -> " << entry.second.via_ip
                          << " (cost " << entry.second.cost << ")\n";
            }

            if (name == "A") {
                std::lock_guard<std::mutex> lock(route_mutex);
                for (const auto& entry : table) {
                    routing_table[entry.first] = entry.second.via_ip;
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(RIP_UPDATE_INTERVAL));
        }
    }
};


std::string resolve_host(const std::string& host) {
    std::lock_guard<std::mutex> lock(route_mutex);
    if (routing_table.count(host)) {
        std::cout << "[ROUTING] resolved " << host << " -> " << routing_table[host] << "\n";
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
        std::cerr << "[ERROR] Could not resolve " << host << "\n";
        close(client_fd);
        return;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    inet_pton(AF_INET, dest_ip.c_str(), &dest.sin_addr);

    int forward_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(forward_fd, (sockaddr*)&dest, sizeof(dest)) < 0) {
        std::cerr << "[ERROR] Could not connect to " << dest_ip << "\n";
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


int main() {
    Router A("A", 5001);
    Router B("B", 5002);
    Router C("C", 5003);

    A.add_direct_host("httpbin.org", "50.16.79.29", 1);
    B.add_direct_host("example.com", "96.7.128.175", 1);
    C.add_direct_host("amazon.com", "104.18.33.45", 1);

    A.add_neighbor(5002);
    B.add_neighbor(5001);
    B.add_neighbor(5003);
    C.add_neighbor(5002);

    std::thread(&Router::run, &A).detach();
    std::thread(&Router::run, &B).detach();
    std::thread(&Router::run, &C).detach();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);
    std::cout << "[PROXY] VPN + RIP Routing on port " << PORT << "\n";

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

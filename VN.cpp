#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <vector>
#include <map>
using namespace std;
#define BUFF_SIZE 4096
// #define DEST_IP "10.17.44.176"
// #define DEST_PORT 5000

class VirtualNode {
private:
    int id;
    int udp_sock_id;
    int tcp_sock_id;
    map<int, pair<string, string>> ip_address_mapping;
    map<int, vector<pair<int, int>>> adj_list;
    vector<pair<string, string>> ip_port_vns;

public:
    VirtualNode(int id, const vector<pair<string, string>>& ip_port_vns)
        : id(id), ip_port_vns(ip_port_vns) {
        udp_sock_id = socket(PF_INET, SOCK_DGRAM, 0);
        tcp_sock_id = socket(PF_INET, SOCK_STREAM, 0);
        if (udp_sock_id < 0 || tcp_sock_id < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
    }

    ~VirtualNode() {
        close(udp_sock_id);
        close(tcp_sock_id);
    }

    int getUdpSockId() const { return udp_sock_id; }
    int getTcpSockId() const { return tcp_sock_id; }

    void connectToOracle(const string& dest_ip, uint16_t dest_port) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(dest_port);
        dest_addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
        memset(&(dest_addr.sin_zero), '\0', 8);

        if (connect(tcp_sock_id, (const sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
            perror("Connection to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }

        string msg = ip_port_vns[id].first + " " + ip_port_vns[id].second;
        const char* msg_to_send = msg.c_str();
        if (send(tcp_sock_id, msg_to_send, strlen(msg_to_send), 0) < 0) {
            perror("Sending message to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }
    }

    void handleUdpMessage(fd_set& read_fds) {
        if (FD_ISSET(udp_sock_id, &read_fds)) {
            char msg_from_neigh[BUFF_SIZE];
            int received_bytes = recv(udp_sock_id, msg_from_neigh, BUFF_SIZE, 0);
            if (received_bytes < 0) {
                perror("Error receiving UDP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
            parse_msg_neigh(msg_from_neigh, ip_address_mapping, adj_list);

            for (auto& neigh : adj_list[id]) {
                int neigh_id = neigh.first;
                if (neigh_id != msg_from_neigh[0] - 'A') {
                    string info_msg = string(msg_from_neigh, received_bytes);
                    const char* info_msg_to_send = info_msg.c_str();
                    struct sockaddr_in dest_addr_vn;
                    dest_addr_vn.sin_family = AF_INET;
                    dest_addr_vn.sin_port = htons(stoi(ip_address_mapping[neigh_id].second));
                    dest_addr_vn.sin_addr.s_addr = inet_addr(ip_address_mapping[neigh_id].first.c_str());
                    memset(&(dest_addr_vn.sin_zero), '\0', 8);
                    if (sendto(udp_sock_id, info_msg_to_send, strlen(info_msg_to_send), 0,
                               (const sockaddr*)&dest_addr_vn, sizeof(dest_addr_vn)) < 0) {
                        perror("Error sending UDP message");
                        close(udp_sock_id);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }

    void handleTcpMessage(fd_set& read_fds) {
        if (FD_ISSET(tcp_sock_id, &read_fds)) {
            char msg_from_on[BUFF_SIZE];
            int received_bytes = recv(tcp_sock_id, msg_from_on, BUFF_SIZE, 0);
            if (received_bytes < 0) {
                perror("Error receiving TCP message");
                close(tcp_sock_id);
                exit(EXIT_FAILURE);
            }
            parse_msg_on(msg_from_on, ip_address_mapping, adj_list);

            for (auto& neigh : adj_list[id]) {
                int neigh_id = neigh.first;
                string info_msg = string(msg_from_on, received_bytes);
                const char* info_msg_to_send = info_msg.c_str();
                struct sockaddr_in dest_addr_vn;
                dest_addr_vn.sin_family = AF_INET;
                dest_addr_vn.sin_port = htons(stoi(ip_address_mapping[neigh_id].second));
                dest_addr_vn.sin_addr.s_addr = inet_addr(ip_address_mapping[neigh_id].first.c_str());
                memset(&(dest_addr_vn.sin_zero), '\0', 8);
                if (sendto(udp_sock_id, info_msg_to_send, strlen(info_msg_to_send), 0,
                           (const sockaddr*)&dest_addr_vn, sizeof(dest_addr_vn)) < 0) {
                    perror("Error sending TCP message");
                    close(udp_sock_id);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    void sendPeriodicLSP() {
        string lsp_msg = create_lsp_msg(id, ip_address_mapping, adj_list);
        const char* lsp_msg_to_send = lsp_msg.c_str();
        for (auto& neigh : adj_list[id]) {
            int neigh_id = neigh.first;
            struct sockaddr_in dest_addr_vn;
            dest_addr_vn.sin_family = AF_INET;
            dest_addr_vn.sin_port = htons(stoi(ip_address_mapping[neigh_id].second));
            dest_addr_vn.sin_addr.s_addr = inet_addr(ip_address_mapping[neigh_id].first.c_str());
            memset(&(dest_addr_vn.sin_zero), '\0', 8);
            if (sendto(udp_sock_id, lsp_msg_to_send, strlen(lsp_msg_to_send), 0,
                       (const sockaddr*)&dest_addr_vn, sizeof(dest_addr_vn)) < 0) {
                perror("Error sending LSP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
        }
    }

    void displayAdjList() const {
        cout << "Adjacency list for virtual node " << char('A' + id) << ":" << endl;
        for (const auto& entry : adj_list) {
            int node = entry.first;
            cout << "  Node " << char('A' + node) << " -> ";
            for (const auto& neighbor : entry.second) {
                cout << "(" << char('A' + neighbor.first) << ", " << neighbor.second << ") ";
            }
            cout << endl;
        }
    }
};
int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <DEST_IP> <DEST_PORT>" << endl;
        return EXIT_FAILURE;
    }

    string DEST_IP = argv[1];
    uint16_t DEST_PORT = stoi(argv[2]);

    vector<pair<string, string>> ip_port_vns = {
        {"10.0.0.1", "8080"},
        {"10.0.0.2", "9090"},
        {"10.0.0.3", "7070"}
    };

    vector<VirtualNode> virtualNodes;
    for (int i = 0; i < ip_port_vns.size(); ++i) {
        virtualNodes.emplace_back(i, ip_port_vns);
    }

    for (auto& vn : virtualNodes) {
        vn.connectToOracle(DEST_IP, DEST_PORT);
    }

    fd_set read_fds;
    int max_fd = 0;
    for (const auto& vn : virtualNodes) {
        max_fd = max(max_fd, max(vn.getUdpSockId(), vn.getTcpSockId()));
    }

    timeval t;
    while (true) {
        FD_ZERO(&read_fds);
        for (const auto& vn : virtualNodes) {
            FD_SET(vn.getUdpSockId(), &read_fds);
            FD_SET(vn.getTcpSockId(), &read_fds);
        }

        t.tv_sec = 20;
        t.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &t);
        if (activity < 0) {
            perror("select error");
            return EXIT_FAILURE;
        } else if (activity == 0) {
            cerr << "Timeout occurred, no activity detected." << endl;
            continue;
        }

        for (auto& vn : virtualNodes) {
            vn.handleUdpMessage(read_fds);
            vn.handleTcpMessage(read_fds);
        }

        // Periodically send LSP messages
        static time_t last_lsp_time = time(nullptr);
        time_t current_time = time(nullptr);

        if (difftime(current_time, last_lsp_time) >= 15) {
            for (auto& vn : virtualNodes) {
                vn.sendPeriodicLSP();
            }

            // Display the state of the adjacency list for each virtual node
            for (const auto& vn : virtualNodes) {
                vn.displayAdjList();
            }

            last_lsp_time = current_time;
        }
    }

    return 0;
}

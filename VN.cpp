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
#include <set>
#include <limits.h>
using namespace std;

#define BUFF_SIZE 4096

// ANSI color macros
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

class VirtualNode
{
private:
    int id;
    int udp_sock_id;
    int tcp_sock_id;
    uint32_t seqNum = 0;
    map<int, vector<pair<int, int>>> adj_list;
    map<int, pair<uint32_t, uint16_t>> ip_port_mapping; // node_id -> (ip, port)
    map<int, uint32_t> latest_seq_num; // origin_id -> latest seq num
    pair<uint32_t, uint16_t> ip_port_vns; // (ip, port)

public:
    VirtualNode(uint32_t ip)
    {
        udp_sock_id = socket(PF_INET, SOCK_DGRAM, 0);
        tcp_sock_id = socket(PF_INET, SOCK_STREAM, 0);

        if (udp_sock_id < 0 || tcp_sock_id < 0)
        {
            cerr << RED << "Socket creation failed" << RESET << endl;
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in local_addr;
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = 0;
        local_addr.sin_addr.s_addr = ip;
        memset(&(local_addr.sin_zero), '\0', 8);

        if (bind(udp_sock_id, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
        {
            cerr << RED << "UDP bind failed" << RESET << endl;
            perror("UDP bind failed");
            close(udp_sock_id);
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in tmp_addr;
        socklen_t addr_len = sizeof(tmp_addr);
        if (getsockname(udp_sock_id, (struct sockaddr *)&tmp_addr, &addr_len) < 0)
        {
            perror("getsockname failed");
            close(udp_sock_id);
            exit(EXIT_FAILURE);
        }
        ip_port_vns = {ip, ntohs(tmp_addr.sin_port)};
    }

    ~VirtualNode()
    {
        close(udp_sock_id);
        close(tcp_sock_id);
    }

    int getUdpSockId() const { return udp_sock_id; }
    int getTcpSockId() const { return tcp_sock_id; }
    uint32_t getIp() const { return ip_port_vns.first; }
    uint16_t getPort() const { return ip_port_vns.second; }

    void connectToOracle(string &dest_ip, uint16_t dest_port)
    {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(dest_port);
        dest_addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
        memset(&(dest_addr.sin_zero), '\0', 8);

        if (connect(tcp_sock_id, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
        {
            cerr << RED << "Connection to oracle failed" << RESET << endl;
            perror("Connection to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }

        //msg format
        //[4 bytes ip][2 bytes port]
        byte msg[6];
        uint32_t ip = ip_port_vns.first;
        memcpy(msg, &ip, 4);
        uint16_t port = htons(ip_port_vns.second);
        memcpy(msg + 4, &port, 2);

        if (send(tcp_sock_id, msg, 6, 0) < 0)
        {
            cerr << RED << "Sending message to oracle failed" << RESET << endl;
            perror("Sending message to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }

        cout << GREEN << "[Connected] Sent registration message to Oracle." << RESET << endl;
    }

    int create_lsp_msg(byte lsp_msg[])
    {
        //msg format
        // [origin id (1 byte)][sender id (1 byte)][seq num (4 bytes)][length (2 bytes)][(neighbor id (1 byte), cost (4 bytes)) * N]
        int index = 0;
        lsp_msg[index++] = (byte)('A' + id);
        lsp_msg[index++] = (byte)('A' + id);
        uint32_t seq = htonl(seqNum++);
        memcpy(lsp_msg + index, &seq, 4);
        index += 4;
        uint16_t length = htons(adj_list[id].size() * 5);
        memcpy(lsp_msg + index, &length, 2);
        index += 2;
        for (auto &neigh : adj_list[id])
        {
            int neigh_id = neigh.first;
            lsp_msg[index++] = (byte)('A' + neigh_id);
            uint32_t c = htonl(neigh.second);
            memcpy(lsp_msg + index, &c, 4);
            index += 4;
        }
        return index;
    }

    pair<int, pair<int, int>> parse_msg_neigh(byte msg_from_neigh[], int received_bytes, int start_index)
    {
        int index = start_index;
        int origin_id = ((char)msg_from_neigh[index]) - 'A';
        index++;
        int sender_id = ((char)msg_from_neigh[index]) - 'A';
        index++;
        uint32_t seq_num = 0;
        for (int i = 0; i < 4; i++)
            seq_num = (seq_num << 8) | (unsigned char)msg_from_neigh[index + i];
        index += 4;

        if (latest_seq_num.count(origin_id) && seq_num <= latest_seq_num[origin_id])
        {
            cout << YELLOW << "[Warning] Virtual Node " << char('A' + id)
                 << " received outdated LSP from Node "
                 << char('A' + sender_id) << RESET << endl;
            return {-1, {-1, -1}};
        }

        latest_seq_num[origin_id] = seq_num;
        uint16_t length = ((unsigned char)msg_from_neigh[index] << 8) | (unsigned char)msg_from_neigh[index + 1];
        index += 2;
        int num_neighbors = length / 5;

        cout << CYAN << "Virtual Node " << char('A' + id)
             << " received LSP from Node " << char('A' + sender_id)
             << ": Origin " << char('A' + origin_id)
             << ", SeqNum " << seq_num <<" Neighbors ["<< RESET;

        adj_list[origin_id].clear();
        for (int i = 0; i < num_neighbors; ++i)
        {
            if (index + 5 > received_bytes)
            {
                cout << RED <<"Incomplete neighbor info in LSP message." <<RESET<< endl;
                exit(EXIT_FAILURE);
            }
            int neigh_id = ((char)msg_from_neigh[index]) - 'A';
            index++;
            uint32_t cost = 0;
            for (int j = 0; j < 4; j++)
                cost = (cost << 8) | (unsigned char)msg_from_neigh[index + j];
            index += 4;
            adj_list[origin_id].push_back({neigh_id, cost});
            cout <<CYAN<< "(" << char('A' + neigh_id) << ", " << cost << "),"<<RESET;
        }
        cout <<CYAN<< "]" << RESET <<endl;
        return {index, {origin_id, sender_id}};
    }

    void handleUdpMessage(fd_set &read_fds)
    {
        if (FD_ISSET(udp_sock_id, &read_fds))
        {
            byte msg_from_neigh[BUFF_SIZE];
            struct sockaddr_in sender_addr;
            socklen_t addr_len = sizeof(sender_addr);
            int received_bytes = recvfrom(udp_sock_id, msg_from_neigh, BUFF_SIZE, 0,
                                          (struct sockaddr *)&sender_addr, &addr_len);
            if (received_bytes < 0)
            {
                perror("Error receiving UDP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }

            auto r = parse_msg_neigh(msg_from_neigh, received_bytes, 0);
            if (r.first == -1)
                return;

            int sender_id = r.second.second;
            int origin_id = r.second.first;
            msg_from_neigh[1] = (byte)((char)(id + 'A'));
            for (auto neigh : adj_list[id])
            {
                if (neigh.first != sender_id && neigh.first != origin_id)
                {
                    struct sockaddr_in dest_addr;
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(ip_port_mapping[neigh.first].second);
                    dest_addr.sin_addr.s_addr = ip_port_mapping[neigh.first].first;
                    memset(&(dest_addr.sin_zero), '\0', 8);
                    if (sendto(udp_sock_id, msg_from_neigh, r.first, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
                    {
                        perror("udp send error");
                        exit(EXIT_FAILURE);
                    }
                    cout << GREEN << "[UDP] Forwarded LSP to " << char('A' + neigh.first) << RESET << endl;
                }
            }
        }
    }

    void handleTcpMessage(fd_set &read_fds)
    {
        if (FD_ISSET(tcp_sock_id, &read_fds))
        {
            byte msg_from_on[BUFF_SIZE];
            int received_bytes = recv(tcp_sock_id, msg_from_on, BUFF_SIZE, 0);
            if (received_bytes == 0)
            {
                cerr << RED << "Server closed connection" << RESET << endl;
                close(tcp_sock_id);
                exit(EXIT_FAILURE);
            }
            else if (received_bytes < 0)
            {
                perror("Error receiving TCP message");
                close(tcp_sock_id);
                exit(EXIT_FAILURE);
            }
            else
            {
                cout << CYAN << "[Oracle] Received neighbor info." << RESET << endl;
                parse_msg_on(msg_from_on);
                sleep(5);
                sendLSP(); //send initial LSP after getting neighbors from oracle
            }
        }
    }

    void sendLSP()
    {
        byte lsp_msg[BUFF_SIZE];
        int lsp_msg_len = create_lsp_msg(lsp_msg);
        for (auto &neigh : adj_list[id])
        {
            struct sockaddr_in dest_addr_vn;
            dest_addr_vn.sin_family = AF_INET;
            dest_addr_vn.sin_port = htons(ip_port_mapping[neigh.first].second);
            dest_addr_vn.sin_addr.s_addr = ip_port_mapping[neigh.first].first;
            memset(&(dest_addr_vn.sin_zero), '\0', 8);
            int sent_bytes = sendto(udp_sock_id, lsp_msg, lsp_msg_len, 0, (const sockaddr *)&dest_addr_vn, sizeof(dest_addr_vn));
            if (sent_bytes < 0)
            {
                perror("Error sending LSP");
                exit(EXIT_FAILURE);
            }
            cout << GREEN << "[UDP] Sent LSP to " << char('A' + neigh.first)
                 << " (" << sent_bytes << " bytes)" << RESET << endl;
        }
    }

    void parse_msg_on(byte msg_from_on[])
    {
        int index = 0;
        vector<pair<int, int>> tmp_vec;
        while (index < BUFF_SIZE)
        {
            //msg format
            // [node id (1 byte)][ip (4 bytes)][port (2 bytes)][cost (4 bytes)]
            int node_id = ((char)msg_from_on[index]) - 'A';
            index++;
            uint32_t ip = 0;
            for (int i = 0; i < 4; i++)
                ip = (ip << 8) | (unsigned char)msg_from_on[index + i];
            ip = htonl(ip);
            index += 4;
            uint16_t port = ((unsigned char)msg_from_on[index] << 8) | (unsigned char)msg_from_on[index + 1];
            index += 2;
            int cost = 0;
            for (int i = 0; i < 4; i++)
                cost = (cost << 8) | (unsigned char)msg_from_on[index + i];
            index += 4;
            if (cost == 0)
            {
                this->id = node_id;
                adj_list[this->id] = tmp_vec;
                break;
            }
            ip_port_mapping[node_id] = {ip, port}; //store ip and port of each VN
            tmp_vec.push_back({node_id, cost}); // tmp vector to store neighbors
        }

        cout << CYAN << "Virtual Node " << char('A' + this->id)
             << " initialized with neighbors: " << RESET;
        for (const auto &neigh : tmp_vec)
            cout << GREEN << "(" << char('A' + neigh.first) << ", " << neigh.second << ") " << RESET;
        cout << endl;
    }

    void displayAdjList() const
    {
        cout << BLUE << "Adjacency list for virtual node " << char('A' + id) << ":" << RESET << endl;
        for (const auto &entry : adj_list)
        {
            cout << "  Node " << char('A' + entry.first) << " -> ";
            for (const auto &neighbor : entry.second)
                cout << MAGENTA << "(" << char('A' + neighbor.first) << ", " << neighbor.second << ") " << RESET;
            cout << endl;
        }
    }

    void applyDijktras()
    {
        set<pair<int, int>> s;
        map<int, int> dist;
        for (auto &entry : adj_list)
            dist[entry.first] = INT_MAX;
        dist[id] = 0;
        s.insert({0, id});
        while (!s.empty())
        {
            auto it = s.begin();
            int node = it->second;
            s.erase(it);
            for (auto &neigh : adj_list[node])
            {
                int neigh_id = neigh.first;
                int cost = neigh.second;
                if (dist[node] + cost < dist[neigh_id])
                {
                    auto it2 = s.find({dist[neigh_id], neigh_id});
                    if (it2 != s.end())
                        s.erase(it2);
                    dist[neigh_id] = dist[node] + cost;
                    s.insert({dist[neigh_id], neigh_id});
                }
            }
        }
        cout << MAGENTA << "Dijkstra result from node " << char('A' + id) << RESET << endl;
        for (auto &entry : dist)
            cout << MAGENTA << "Node " << char('A' + entry.first) << " Distance " << entry.second << RESET << endl;
    }
};

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << YELLOW << "Usage: " << argv[0] << " <DEST_IP (Oracle Node)> <VN_IP>" << RESET << endl;
        return EXIT_FAILURE;
    }

    string DEST_IP = argv[1];
    uint16_t DEST_PORT = 5000;
    string VN_IP = argv[2];
    uint32_t ip_ = inet_addr(VN_IP.c_str());
    VirtualNode *vn = new VirtualNode(ip_);

    cout << GREEN << "Created Virtual Node with IP "
         << vn->getIp() << " and Port " << vn->getPort() << RESET << endl;

    vn->connectToOracle(DEST_IP, DEST_PORT);

    time_t last_time = time(nullptr);

    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(vn->getTcpSockId(), &read_fds);
        FD_SET(vn->getUdpSockId(), &read_fds);

        int max_fd = max(vn->getTcpSockId(), vn->getUdpSockId());
        timeval t;
        t.tv_sec = 20;
        t.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &t);
        if (activity < 0)
        {
            perror("select error");
            return EXIT_FAILURE;
        }
        else if (activity == 0)
        {
            cout << YELLOW << "[Timeout] No activity detected." << RESET << endl;
            vn->displayAdjList();
            vn->applyDijktras();
        }
        else
        {
            if (FD_ISSET(vn->getTcpSockId(), &read_fds))
            {
                cout << CYAN << "TCP event detected." << RESET << endl;
                vn->handleTcpMessage(read_fds);
            }
            if (FD_ISSET(vn->getUdpSockId(), &read_fds))
            {
                cout << CYAN << "UDP event detected." << RESET << endl;
                vn->handleUdpMessage(read_fds);
            }
        }

        time_t current_time = time(nullptr);
        if (current_time - last_time > 15)
        {
            cout << BLUE << "Sending periodic LSP..." << RESET << endl;
            last_time = current_time;
            vn->displayAdjList();
            vn->sendLSP();
            cout << GREEN << "Sent periodic LSP." << RESET << endl;
        }
    }
    return 0;
}

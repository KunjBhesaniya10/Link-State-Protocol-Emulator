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
// #define DEST_IP "10.17.44.176"
// #define DEST_PORT 5000

class VirtualNode
{
public:
    int id;
    int udp_sock_id;
    int tcp_sock_id;
    uint32_t seqNum = 0;
    map<int, vector<pair<int, int>>> adj_list;
    map<int, pair<uint32_t, uint16_t>> ip_port_mapping;
    map<int, uint32_t> latest_seq_num; // to track latest seq num from each origin

    pair<uint32_t, uint16_t> ip_port_vns;

public:
    VirtualNode(uint32_t ip)
    {
        udp_sock_id = socket(PF_INET, SOCK_DGRAM, 0);
        tcp_sock_id = socket(PF_INET, SOCK_STREAM, 0);
        cout << id << " tcp " << tcp_sock_id << " udp " << udp_sock_id << endl;
        if (udp_sock_id < 0 || tcp_sock_id < 0)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
        // Bind UDP socket
        struct sockaddr_in local_addr;
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = 0;
        local_addr.sin_addr.s_addr = ip; // Listen on all available network interfaces

        memset(&(local_addr.sin_zero), '\0', 8);
        if (bind(udp_sock_id, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
        {
            perror("UDP bind failed");
            close(udp_sock_id);
            exit(EXIT_FAILURE);
        }
        // Get the assigned port number
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

    void connectToOracle(string &dest_ip, uint16_t dest_port)
    {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(dest_port);
        dest_addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
        memset(&(dest_addr.sin_zero), '\0', 8);
        // cout<<tcp_sock_id<<endl;
        if (connect(tcp_sock_id, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
        {
            perror("Connection to oracle failed hi");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }
        // cout<<"HI"<<endl;
        byte msg[6];
        // first 4 bytes are ip
        cout << "ip " << ip_port_vns.first << " port " << ip_port_vns.second << endl;
        uint32_t ip = ip_port_vns.first;
        memcpy(msg, &ip, 4);
        // cout << (int)(msg[0]) << endl;
        // next 2 bytes are port
        uint16_t port = htons(ip_port_vns.second);
        memcpy(msg + 4, &port, 2);

        if (send(tcp_sock_id, msg, 6, 0) < 0)
        {
            perror("Sending message to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }
    }
    int create_lsp_msg(byte lsp_msg[])
    {
        // [origin id (1 byte)][sender id (1 byte)][sequence number (4 bytes)][length (2 bytes)]
        //[(neighbor id (1 byte), cost (4 bytes)) * number of neighbors]
        // length is in bytes of the neighbor list part
        int index = 0;
        lsp_msg[index++] = (byte)('A' + id); // origin id
        lsp_msg[index++] = (byte)('A' + id); // sender id
        uint32_t seq = htonl(seqNum++);      // sequence number
        memcpy(lsp_msg + index, &seq, 4);
        index += 4;
        // length field
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

        return index; // returns the length of message
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
        {
            seq_num = (seq_num << 8) | (unsigned char)msg_from_neigh[index + i];
        }
        index += 4;
        if (latest_seq_num.count(origin_id) && seq_num <= latest_seq_num[origin_id])
        {
            cout << "Virtual Node " << char('A' + id) << " received outdated/seen LSP from Node "
                 << char('A' + sender_id) << ": Origin " << char('A' + origin_id)
                 << ", SeqNum " << seq_num << ". Discarding." << endl;
            return {-1, {-1, -1}}; // Discard outdated or already processed message
        }
        latest_seq_num[origin_id] = seq_num;
        uint16_t length = ((unsigned char)msg_from_neigh[index] << 8) | (unsigned char)msg_from_neigh[index + 1];
        index += 2;
        int num_neighbors = length / 5;
        cout << "Virtual Node " << char('A' + id) << " received LSP from Node "
             << char('A' + sender_id) << ": Origin " << char('A' + origin_id)
             << ", SeqNum " << seq_num << ", Neighbors [";

        adj_list[origin_id].clear(); // clear old neighbor info
        for (int i = 0; i < num_neighbors; ++i)
        {
            if (index + 5 > received_bytes)
            {
                cout << "Incomplete neighbor info in LSP message." << endl;
                exit(EXIT_FAILURE);
            }
            int neigh_id = ((char)msg_from_neigh[index]) - 'A';
            index++;
            uint32_t cost = 0;
            for (int j = 0; j < 4; j++)
            {
                cost = (cost << 8) | (unsigned char)msg_from_neigh[index + j];
            }
            index += 4;
            adj_list[origin_id].push_back({neigh_id, cost});
            cout << "(" << char('A' + neigh_id) << ", " << cost << ") ";
        }
        cout << "]" << endl;
        cout << "origin_id = " << origin_id << " sender_id = " << sender_id << endl;
        return {index, {origin_id, sender_id}};
    }

    void handleUdpMessage(fd_set &read_fds)
    {
        if (FD_ISSET(udp_sock_id, &read_fds))
        {
            byte msg_from_neigh[BUFF_SIZE];
            int index = 0;
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
            auto r = parse_msg_neigh(msg_from_neigh, received_bytes, index);
            index = r.first;
            int sender_id = r.second.second;
            int origin_id = r.second.first;
            if (index == -1)
                return; // outdated message, discard

            if (received_bytes < 0)
            {
                perror("Error receiving UDP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
            // forward to all neighbors except the sender and the originator
            // sender id on index 1
            msg_from_neigh[1] = (byte)((char)(id + 'A'));
            cout << "Entered udp" << endl;
            for (auto neigh : adj_list[id])
            {
                if (neigh.first != sender_id && neigh.first != origin_id)
                { // add the above condition
                    uint32_t neigh_ip = ip_port_mapping[neigh.first].first;
                    uint16_t neigh_port = ip_port_mapping[neigh.first].second;
                    cout << "Forwarding to " << char('A' + neigh.first) << " ip " << neigh_ip << " port " << neigh_port << endl;
                    struct sockaddr_in dest_addr;
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(neigh_port);
                    dest_addr.sin_addr.s_addr = neigh_ip;
                    memset(&(dest_addr.sin_zero), '\0', 8);
                    if (sendto(udp_sock_id, msg_from_neigh, index, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0)
                    {
                        perror("udp send error Line 179");
                        exit(EXIT_FAILURE);
                    }
                    cout << "UDP msg sent" << endl;
                }
            }
            cout << "Exited Udp" << endl;
        }
        else
        {
            cout << "Udp unouched" << endl;
        }
    }

    void handleTcpMessage(fd_set &read_fds)
    {
        if (FD_ISSET(tcp_sock_id, &read_fds))
        {
            byte msg_from_on[BUFF_SIZE];
            int received_bytes = recv(tcp_sock_id, msg_from_on, BUFF_SIZE, 0);
            if (received_bytes == 0) // server closed
            {
                perror("Server Closed");
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
                parse_msg_on(msg_from_on);

                sleep(5);

                sendLSP(); // send initial LSP after getting neighbors from oracle}
            }
        }
    }

    void sendLSP()
    {
        byte lsp_msg[BUFF_SIZE];
        int lsp_msg_len = create_lsp_msg(lsp_msg);
        cout << "Size of adj " << adj_list[id].size() << endl;
        for (auto &neigh : adj_list[id])
        {
            int neigh_id = neigh.first;
            struct sockaddr_in dest_addr_vn;
            dest_addr_vn.sin_family = AF_INET;
            dest_addr_vn.sin_port = htons(ip_port_mapping[neigh_id].second);
            dest_addr_vn.sin_addr.s_addr = ip_port_mapping[neigh_id].first;
            memset(&(dest_addr_vn.sin_zero), '\0', 8);
            cout << "Sending to " << char('A' + neigh_id) << " ip " << ip_port_mapping[neigh_id].first << " port " << ip_port_mapping[neigh_id].second << endl;
            int sent_bytes = 0;
            if ((sent_bytes = sendto(udp_sock_id, lsp_msg, lsp_msg_len, 0,
                                     (const sockaddr *)&dest_addr_vn, sizeof(dest_addr_vn))) < 0)
            {
                perror("Error sending LSP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
            cout << "Sent" << " " << sent_bytes << endl;
        }
    }

    void parse_msg_on(byte msg_from_on[])
    {
        int index = 0;
        vector<pair<int, int>> tmp_vec;
        while (index < BUFF_SIZE)
        {
            int node_id = ((char)msg_from_on[index]) - 'A';
            index++;
            // next 4 bytes are ip
            uint32_t ip = 0;
            for (int i = 0; i < 4; i++)
            {
                ip = (ip << 8) | (unsigned char)msg_from_on[index + i];
            }
            ip = htonl(ip);
            // cout<<"Modified ip line 288"<<ip<<endl;
            index += 4;
            // next 2 bytes are port
            uint16_t port = ((unsigned char)msg_from_on[index] << 8) | (unsigned char)msg_from_on[index + 1];
            index += 2;
            // next 4 bytes is cost
            int cost = 0;
            for (int i = 0; i < 4; i++)
            {
                cost = (cost << 8) | (unsigned char)msg_from_on[index + i];
            }
            index += 4;
            if (cost == 0)
            {
                this->id = node_id;
                adj_list[this->id] = tmp_vec;
                break;
            }
            ip_port_mapping[node_id] = {ip, port};
            tmp_vec.push_back({node_id, cost});
        }
        cout << "Virtual Node " << char('A' + this->id) << " initialized with neighbors: ";
        for (const auto &neigh : tmp_vec)
        {
            cout << "(" << char('A' + neigh.first) << ", " << neigh.second << ") ";
        }
        cout << endl;
    }

    void displayAdjList() const
    {
        cout << "Adjacency list for virtual node " << char('A' + id) << ":" << endl;
        for (const auto &entry : adj_list)
        {
            int node = entry.first;
            cout << "  Node " << char('A' + node) << " -> ";
            for (const auto &neighbor : entry.second)
            {
                cout << "(" << char('A' + neighbor.first) << ", " << neighbor.second << ") ";
            }
            cout << endl;
        }
    }

    void applyDijktras()
    {
        set<pair<int, int>> s; // cost, node id
        map<int, int> dist;
        for (auto &entry : adj_list)
        {
            dist[entry.first] = INT_MAX;
        }
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
                    {
                        s.erase(it2);
                    }
                    dist[neigh_id] = dist[node] + cost;
                    s.insert({dist[neigh_id], neigh_id});
                }
            }
        }
        cout << "Dijkstra result from node " << char('A' + id) << endl;
        for (auto &entry : dist)
        {
            cout << "Node " << char('A' + entry.first) << " Distance " << entry.second << endl;
        }
    }
};
int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cout << "Usage: " << argv[0] << " <DEST_IP (Oracle Node)>" << endl;
        return EXIT_FAILURE;
    }
    string DEST_IP = argv[1];
    uint16_t DEST_PORT = 8080;

    uint32_t ip_ = inet_addr(DEST_IP.c_str());
    VirtualNode *vn = new VirtualNode(ip_);
    cout << "Created Virtual Node " << " with IP "
         << vn->ip_port_vns.first << " and Port " << vn->ip_port_vns.second << endl;

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
        t.tv_sec = 2;
        t.tv_usec = 0;
        // cout<<"max fd "<<max_fd+1<<endl;
        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &t);
        if (activity < 0)
        {
            perror("select error");
            return EXIT_FAILURE;
        }
        else if (activity == 0)
        {
            cout << "Timeout occurred, no activity detected." << endl;
            vn->displayAdjList();
            vn->applyDijktras();
            // continue;
        }
        else
        {

            if (FD_ISSET(vn->getTcpSockId(), &read_fds))
            {
                cout << "Tcp is set" << endl;
                vn->handleTcpMessage(read_fds);
            }

            if (FD_ISSET(vn->getUdpSockId(), &read_fds))
            {
                cout << "Udp is set" << endl;
                vn->handleUdpMessage(read_fds);
            }
        }
        time_t current_time = time(nullptr);
        if (current_time - last_time > 5)
        {
            cerr << "Sending periodic LSP" << endl;
            last_time = current_time;
            vn->sendLSP();
            cerr << "Sent periodic LSP" << endl;
        }
    }
    return 0;
}

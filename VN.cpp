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
    VirtualNode(int id, pair<uint32_t, uint16_t>& ip_port_vns)
        : id(id), ip_port_vns(ip_port_vns) {
        udp_sock_id = socket(PF_INET, SOCK_DGRAM, 0);
        tcp_sock_id = socket(PF_INET, SOCK_STREAM, 0);
        cout<<id<<" tcp "<<tcp_sock_id<<" udp "<<udp_sock_id<<endl;
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
        // cout<<tcp_sock_id<<endl;
        if (connect(tcp_sock_id, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
            perror("Connection to oracle failed hi");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }
        // cout<<"HI"<<endl;
        byte msg[6];
        // first 4 bytes are ip
        cerr << "ip "<< ip_port_vns.first << " port " << ip_port_vns.second << endl;
        uint32_t ip = ip_port_vns.first;
        memcpy(msg, &ip, 4);
        cerr << (int)(msg[0]) << endl;
        // next 2 bytes are port
        uint16_t port = htons(ip_port_vns.second);
        memcpy(msg + 4, &port, 2);

        if (send(tcp_sock_id, msg,6, 0) < 0) {
            perror("Sending message to oracle failed");
            close(tcp_sock_id);
            exit(EXIT_FAILURE);
        }
    }
    int create_lsp_msg(byte lsp_msg[]){
        // [origin id (1 byte)][sender id (1 byte)][sequence number (4 bytes)][length (2 bytes)]
        //[(neighbor id (1 byte), cost (4 bytes)) * number of neighbors]
        // length is in bytes of the neighbor list part
        int index =0;
        lsp_msg[index++] = (byte)('A' + id);  // origin id
        lsp_msg[index++] = (byte) ('A' + id); // sender id
        uint32_t seq = htonl(seqNum++);  // sequence number
        memcpy(lsp_msg + index, &seq, 4);
        index += 4;
        // length field
        uint16_t length = htons(adj_list[id].size() * 5);
        memcpy(lsp_msg + index, &length, 2);
        index += 2;
        for(auto& neigh : adj_list[id]){
            int neigh_id = neigh.first;
            lsp_msg[index++] = (byte)('A' + neigh_id);
            uint32_t c = htonl(neigh.second);
            memcpy(lsp_msg + index, &c, 4);
            index += 4;
        }
        
        return index; // return length of message
        
    }

    pair<int,pair<int,int>> parse_msg_neigh(byte msg_from_neigh[], int received_bytes, int start_index) {
        int index = start_index;
        int origin_id = ((char)msg_from_neigh[index]) - 'A';
        index++;
        int sender_id = ((char)msg_from_neigh[index]) - 'A';
        index++;
        uint32_t seq_num = 0;
        for(int i=0;i<4;i++){
            seq_num = (seq_num << 8) | (unsigned char)msg_from_neigh[index+i];
        }
        index+=4;
        if(seq_num <= latest_seq_num[origin_id]){
            cerr << "Virtual Node " << char('A' + id) << " received outdated LSP from Node " 
                 << char('A' + sender_id) << ": Origin " << char('A' + origin_id) 
                 << ", SeqNum " << seq_num << ". Discarding." << endl;
            return {-1,{-1,-1}}; // discard outdated message
        }
        uint16_t length = ((unsigned char)msg_from_neigh[index] << 8) | (unsigned char)msg_from_neigh[index+1];
        index+=2;
        int num_neighbors = length / 5;
        cerr << "Virtual Node " << char('A' + id) << " received LSP from Node " 
             << char('A' + sender_id) << ": Origin " << char('A' + origin_id) 
             << ", SeqNum " << seq_num << ", Neighbors [";
        
        adj_list[origin_id].clear(); // clear old neighbor info
        for (int i = 0; i < num_neighbors; ++i) {
            if (index + 5 > received_bytes) {
                cerr << "Incomplete neighbor info in LSP message." << endl;
                exit(EXIT_FAILURE);
            }
            int neigh_id = ((char)msg_from_neigh[index]) - 'A';
            index++;
            uint32_t cost = 0;
            for(int j=0;j<4;j++){
                cost = (cost << 8) | (unsigned char)msg_from_neigh[index+j];
            }
            index+=4;
            adj_list[origin_id].push_back({neigh_id, cost});
            cerr << "(" << char('A' + neigh_id) << ", " << cost << ") ";
        }
        cerr << "]" << endl;
        return {index,{origin_id,sender_id}};
    }
    void handleUdpMessage(fd_set& read_fds) {
        if (FD_ISSET(udp_sock_id, &read_fds)) {
            byte msg_from_neigh[BUFF_SIZE];
            int index = 0;
            int received_bytes = recv(udp_sock_id, msg_from_neigh, BUFF_SIZE, 0);
            if(received_bytes <0){
                perror("Error receiving UDP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
            auto r = parse_msg_neigh(msg_from_neigh, received_bytes, index);
            index = r.first;
            int sender_id = r.second.second;
            int origin_id = r.second.first;
            if(index == -1) return; // outdated message, discard
            
            if (received_bytes < 0) {
                perror("Error receiving UDP message");
                close(udp_sock_id);
                exit(EXIT_FAILURE);
            }
            // forward to all neighbors except the sender and the originator
            //sender id on index 1
            msg_from_neigh[1] = (byte) (id + 'A') ;
            cerr<<"Entered udp"<<endl;
            for(auto neigh : adj_list[id]){
                if(neigh.first != sender_id && neigh.first != origin_id){ // add the above condition
                    uint32_t neigh_ip = ip_port_mapping[neigh.first].first;
                    uint16_t neigh_port = ip_port_mapping[neigh.first].second;
                    struct sockaddr_in dest_addr;
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(neigh_port);
                    dest_addr.sin_addr.s_addr = neigh_ip;
                    memset(&(dest_addr.sin_zero), '\0', 8);
                    if(send(udp_sock_id,msg_from_neigh,index,0)<0){
                        perror("udp send error Line 179");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            cerr<<"Exited Udp"<<endl;
            
        }
        else{
            cerr<<"Udp unouched"<<endl;
        }
    }

    void handleTcpMessage(fd_set& read_fds) {
        if (FD_ISSET(tcp_sock_id, &read_fds)) {
            byte msg_from_on[BUFF_SIZE];
            int received_bytes = recv(tcp_sock_id, msg_from_on,BUFF_SIZE, 0);
            if (received_bytes < 0) {
                perror("Error receiving TCP message");
                close(tcp_sock_id);
                exit(EXIT_FAILURE);
            }
            parse_msg_on(msg_from_on);

            byte lsp_msg[BUFF_SIZE];
            int lsp_msg_len = create_lsp_msg(lsp_msg);
            cerr<<"Size of adj "<<adj_list[id].size()<<endl;
            for (auto& neigh : adj_list[id]) {
                int neigh_id = neigh.first;
                struct sockaddr_in dest_addr_vn;
                dest_addr_vn.sin_family = AF_INET;
                dest_addr_vn.sin_port = htons(ip_port_mapping[neigh_id].second);
                dest_addr_vn.sin_addr.s_addr = ip_port_mapping[neigh_id].first;
                memset(&(dest_addr_vn.sin_zero), '\0', 8);
                cerr<<"Sending"<<endl;
                if (sendto(udp_sock_id, lsp_msg, lsp_msg_len, 0,
                           (const sockaddr*)&dest_addr_vn, sizeof(dest_addr_vn)) < 0) {
                    perror("Error sending LSP message");
                    close(udp_sock_id);
                    exit(EXIT_FAILURE);
                }
                cerr<<"Sent"<<endl;
            }
        }
    }

    // void sendPeriodicLSP() {
    //     string lsp_msg = create_lsp_msg(id, ip_port_mapping, adj_list);
    //     const char* lsp_msg_to_send = lsp_msg.c_str();
    //     for (auto& neigh : adj_list[id]) {
    //         int neigh_id = neigh.first;
    //         struct sockaddr_in dest_addr_vn;
    //         dest_addr_vn.sin_family = AF_INET;
    //         dest_addr_vn.sin_port = htons(stoi(ip_port_mapping[neigh_id].second));
    //         dest_addr_vn.sin_addr.s_addr = inet_addr(ip_port_mapping[neigh_id].first.c_str());
    //         memset(&(dest_addr_vn.sin_zero), '\0', 8);
    //         if (sendto(udp_sock_id, lsp_msg_to_send, strlen(lsp_msg_to_send), 0,
    //                    (const sockaddr*)&dest_addr_vn, sizeof(dest_addr_vn)) < 0) {
    //             perror("Error sending LSP message");
    //             close(udp_sock_id);
    //             exit(EXIT_FAILURE);
    //         }
    //     }
    // }
    void parse_msg_on(byte msg_from_on[]){
        int index = 0;
        vector<pair<int,int>>tmp_vec;
        while(index < BUFF_SIZE){
            int node_id = ((char)msg_from_on[index]) - 'A';
            index++;
            // next 4 bytes are ip
            uint32_t ip = 0;
            for(int i=0;i<4;i++){
                ip = (ip << 8) | (unsigned char)msg_from_on[index+i];
            }
            index+=4;
            // next 2 bytes are port
            uint16_t port = ((unsigned char)msg_from_on[index] << 8) | (unsigned char)msg_from_on[index+1];
            index+=2;
            // next 4 bytes is cost
            int cost = 0;
            for(int i=0;i<4;i++){
                cost = (cost << 8) | (unsigned char)msg_from_on[index+i];
            }
            index+=4;
            if(cost == 0){
                this->id = node_id;
                adj_list[this->id]=tmp_vec;
                break;
            }
            cerr << "Virtual Node " << char('A' + this->id) << " received from Oracle: Node " 
                 << char('A' + node_id) << ", IP " << ((ip >> 24) & 0xFF) << "." 
                 << ((ip >> 16) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "." 
                 << (ip & 0xFF) << ", Port " << port << ", Cost " << cost << endl;
            ip_port_mapping[node_id] = {ip, port};
            tmp_vec.push_back({node_id, cost});
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
    // if (argc != 3) {
    //     cerr << "Usage: " << argv[0] << " <DEST_IP> <DEST_PORT>" << endl;
    //     return EXIT_FAILURE;
    // }(socketHandle + 1, &rfds, NULL, NULL, &tv);

    string DEST_IP = "10.51.11.2";
    uint16_t DEST_PORT = 8080;

    // create 26 virtual nodes with their ip and port randomly for testing
    vector<pair<string,string>> ip_port_vns = {
            {"10.51.11.2", "8000"}, {"10.51.11.2", "9090"}, {"10.51.11.2", "3000"},
            {"10.51.11.2", "4500"}, {"10.51.11.2", "1234"}, {"10.51.11.2", "5555"},
            {"10.51.11.2", "8081"}, {"10.51.11.2", "6000"}, {"10.51.11.2", "7000"},
            {"10.51.11.2", "4040"}, {"10.51.11.2", "5050"}, {"10.51.11.2", "2020"},
            {"10.51.11.2", "8082"}, {"10.51.11.2", "9091"}, {"10.51.11.2", "6060"},
            {"10.51.11.2", "7070"}, {"10.51.11.2", "8085"}, {"10.51.11.2", "9095"},
            {"192.168.3.30", "3030"}, {"10.5.5.5", "5055"}, {"172.21.2.6", "6065"},
            {"192.168.8.12", "8088"}, {"10.6.6.6", "9099"}, {"172.22.3.3", "7077"},
            {"192.168.4.44", "4044"}, {"10.7.7.7", "5059"}
        
    };

    vector<VirtualNode*> virtualNodes(26,nullptr);
    for (int i = 0; i < ip_port_vns.size(); ++i) {
        uint32_t ip = inet_addr(ip_port_vns[i].first.c_str());
        uint16_t port = stoi(ip_port_vns[i].second); 
        pair<uint32_t,uint16_t> p = {ip,port};
        VirtualNode *vn1 = new VirtualNode(i,p);
        virtualNodes[i]=vn1;
        cerr << "Created Virtual Node " << char('A' + i) << " with IP " 
             << ip_port_vns[i].first << " and Port " << ip_port_vns[i].second << endl;
    }

    for (auto& vn : virtualNodes) {
        vn->connectToOracle(DEST_IP, DEST_PORT);
    }

    fd_set read_fds;
    int max_fd = 0;
    for (const auto& vn : virtualNodes) {
        max_fd = max(max_fd, max(vn->getUdpSockId(), vn->getTcpSockId()));
    }

    timeval t;
    while (true) {
        FD_ZERO(&read_fds);
        for (const auto& vn : virtualNodes) {
            cerr<<"tcp socket "<<(vn->getTcpSockId())<<" udp socket "<<vn->getUdpSockId()<<endl;
            FD_SET(vn->getTcpSockId(), &read_fds);
            FD_SET(vn->getUdpSockId(), &read_fds);
        }

        t.tv_sec = 20;
        t.tv_usec = 0;  
        cerr<<"max fd "<<max_fd+1<<endl;
        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &t);
        if (activity < 0) {
            perror("select error");
            return EXIT_FAILURE;
        } else if (activity == 0) {
            cerr << "Timeout occurred, no activity detected." << endl;
            continue;
        }

        for (auto& vn : virtualNodes) {
            vn->handleTcpMessage(read_fds);
            vn->handleUdpMessage(read_fds);
        }

        // Periodically send LSP messages
        // static time_t last_lsp_time = time(nullptr);
        // time_t current_time = time(nullptr);

        // if (difftime(current_time, last_lsp_time) >= 15) {
        //     for (auto& vn : virtualNodes) {
        //         vn.sendPeriodicLSP();
        //     }

        //     // Display the state of the adjacency list for each virtual node
        //     for (const auto& vn : virtualNodes) {
        //         vn.displayAdjList();
        //     }

        //     last_lsp_time = current_time;
        // }
    }

    return 0;
}

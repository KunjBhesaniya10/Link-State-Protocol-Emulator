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

int main(int argc, char* argv[]){

    int n = 3;
    //DATA-STRUCTURES
    map<int, pair<string,string>>ip_address_mapping; // 0 - A, 1 - B and so on
    vector<map<int, vector<pair<int,int>>>>adj_list(26); //of adjacency list for each node 
    vector<int>udp_sock_ids(n),tcp_sock_ids(n); //0->A,1->B and so onmapping type


    char* DEST_IP = argv[1];
    uint16_t DEST_PORT = stoi(argv[2]);
    cout<<DEST_IP<<" "<<DEST_PORT<<endl;
    vector<pair<string, string>> ip_port_vns = {
        {"10.0.0.1", "8080"},
        {"10.0.0.2", "9090"},
        {"10.0.0.3", "7070"}
    };
    
    int max_fd;
    
    for(int i=0;i<n;i++){
        int udp_sock_id = socket(PF_INET, SOCK_DGRAM, 0);
        udp_sock_ids[i]=udp_sock_id;
        int tcp_sock_id = socket(PF_INET, SOCK_STREAM, 0);
        tcp_sock_ids[i]=tcp_sock_id;
        max_fd = max(tcp_sock_id,udp_sock_id);
    }
    
    timeval t;
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    dest_addr.sin_addr.s_addr = inet_addr((char*)DEST_IP);
    memset(&(dest_addr.sin_zero),'\0',8);

    cout<<"Connecting.."<<endl;
    
    for(int i=0;i<n;i++){
        cout<<"Hello "<<i<<endl;
        if(connect(tcp_sock_ids[i],(const sockaddr*)&dest_addr,sizeof(dest_addr)) < 0){
            cout<<"connection error for socket with id: "<<i<<endl;
            close(tcp_sock_ids[i]);
            exit(EXIT_FAILURE);
        }

        
        string msg = ip_port_vns[i].first + " " + ip_port_vns[i].second;
        const char* msg_to_send = msg.c_str();
        int sent_bytes = send(tcp_sock_ids[i],msg_to_send,strlen(msg_to_send),0);
        if(sent_bytes<0){
            cout<<"sending error for socket with id: "<<i<<endl;
            close(tcp_sock_ids[i]);
            exit(EXIT_FAILURE);
        }
    }
    cout<<"Yeey"<<endl;
    fd_set read_fds;
    while(1){
        FD_ZERO(&read_fds);
        for(int i=0;i<26;i++){
            FD_SET(udp_sock_ids[i],&read_fds);
            FD_SET(tcp_sock_ids[i],&read_fds);
        }
        cout<<"Entered loop"<<endl;
        t.tv_sec = 20;
        int activity =  select(max_fd+1, &read_fds, NULL, NULL, &t);
        if(activity < 0){
            perror("select error");
            for(int i=0;i<26;i++){
                close(udp_sock_ids[i]);
                close(tcp_sock_ids[i]);
            }
            exit(1);
        }
        else if (activity == 0){
            perror("No activity so timeout");
            for(int i=0;i<26;i++){
                close(udp_sock_ids[i]);
                close(tcp_sock_ids[i]);
            }
            exit(1);
        }
        else{
            for(int i=0;i<26;i++){
                if(FD_ISSET(udp_sock_ids[i], &read_fds)){
                    cout<<"Message available from neighbours for node: "+(i+'A')<<endl;
                    char msg_from_neigh[BUFF_SIZE];
                    //storing it in ip_address_mapping
                    int received_bytes = recv(udp_sock_ids[i], msg_from_neigh, BUFF_SIZE, 0);
                    if(received_bytes < 0){
                        cout<<"[Line 117] connection error for socket with id: "<<i<<endl;
                        perror("Error while receiving messages for socket with id");
                        close(udp_sock_ids[i]);
                        exit(EXIT_FAILURE);
                    }
                    parse_msg_neigh(msg_from_neigh, ip_address_mapping, adj_list[i]);

                    //send this info to other neighbours except the one from which it is received
                    for (auto& neigh : adj_list[i][i]) {
                        int neigh_id = neigh.first;
                        if (neigh_id != msg_from_neigh[0] - 'A') { // Assuming the first character of the message indicates the sender
                            string info_msg = string(msg_from_neigh, received_bytes); // Convert received message to string
                            const char* info_msg_to_send = info_msg.c_str();
                            struct sockaddr_in dest_addr_vn;
                            dest_addr_vn.sin_family = AF_INET;
                            dest_addr_vn.sin_port = htons(stoi(ip_address_mapping[neigh_id].second));
                            dest_addr_vn.sin_addr.s_addr = inet_addr((char*)ip_address_mapping[neigh_id].first.c_str());
                            memset(&(dest_addr_vn.sin_zero),'\0',8);
                            int sent_bytes = sendto(udp_sock_ids[i], info_msg_to_send, strlen(info_msg_to_send), 0, 
                                                    (const sockaddr*)&dest_addr_vn, 
                                                    sizeof(dest_addr_vn));
                            if (sent_bytes < 0) {
                                cout << "[Line 117] sending error for socket with id: " << i << endl;
                                close(udp_sock_ids[i]);
                                exit(EXIT_FAILURE);
                            }
                        }
                    }

                }
                if(FD_ISSET(tcp_sock_ids[i], &read_fds)){
                    cout<<"Message available from oracle node for node: "+(i+'A')<<endl;
                    //storing it in ip_address_mapping

                    char msg_from_on[BUFF_SIZE];
                    int received_bytes = recv(tcp_sock_ids[i], msg_from_on, BUFF_SIZE, 0);
                    if(received_bytes < 0){
                        cout<<"[Line 119] connection error for socket with id: "<<i<<endl;
                        perror("Error while receiving messages for socket with id");
                        close(tcp_sock_ids[i]);
                        exit(EXIT_FAILURE);
                    }
                    parse_msg_on(msg_from_on, ip_address_mapping, adj_list[i]);
                    //send this info to all neighbours
                    
                    for (auto& neigh : adj_list[i][i]) {
                        int neigh_id = neigh.first;
                        string info_msg = string(msg_from_on, received_bytes); // Convert received message to string
                        const char* info_msg_to_send = info_msg.c_str();
                        struct sockaddr_in dest_addr_vn;
                        dest_addr_vn.sin_family = AF_INET;
                        dest_addr_vn.sin_port = htons(stoi(ip_address_mapping[neigh_id].second));
                        dest_addr_vn.sin_addr.s_addr = inet_addr((char*)ip_address_mapping[neigh_id].first.c_str());
                        memset(&(dest_addr_vn.sin_zero),'\0',8);
                        int sent_bytes = sendto(udp_sock_ids[i], info_msg_to_send, strlen(info_msg_to_send), 0, 
                                                (const sockaddr*)&dest_addr_vn, 
                                                sizeof(dest_addr_vn));
                        if (sent_bytes < 0) {
                            cout << "[Line 119] sending error for socket with id: " << i << endl;
                            close(udp_sock_ids[i]);
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            }
        }
        //periodically send LSP msgs to neighbors
        static time_t last_lsp_time = time(NULL);
        time_t current_time = time(NULL);

        if (difftime(current_time, last_lsp_time) >= 15) {
            for (int i = 0; i < 26; i++) {
            string lsp_msg = create_lsp_msg(i, ip_address_mapping, adj_list[i]);
            const char* lsp_msg_to_send = lsp_msg.c_str();
            for (auto neigh : adj_list[i][i]) {
                int neigh_id = neigh.first;
                int sent_bytes = sendto(udp_sock_ids[i], lsp_msg_to_send, strlen(lsp_msg_to_send), 0, 
                            (const sockaddr*)&(ip_address_mapping[neigh_id]), 
                            sizeof(ip_address_mapping[neigh_id]));
                if (sent_bytes < 0) {
                cout << "[Line 142] sending error for socket with id: " << i << endl;
                close(udp_sock_ids[i]);
                exit(EXIT_FAILURE);
                }
            }
            }
            // Display the state of the adjacency list for each virtual node
            for (int i = 0; i < n; i++) {
                cout << "Adjacency list for virtual node " << char('A' + i) << ":" << endl;
                for (const auto& entry : adj_list[i]) {
                    int node = entry.first;
                    cout << "  Node " << char('A' + node) << " -> ";
                    for (const auto& neighbor : entry.second) {
                        cout << "(" << char('A' + neighbor.first) << ", " << neighbor.second << ") ";
                    }
                    cout << endl;
                }
            }
            last_lsp_time = current_time;
        }
    }

    for(int i=0;i<n;i++){
        close(udp_sock_ids[i]);
        close(tcp_sock_ids[i]);
    }

    return 0;

}
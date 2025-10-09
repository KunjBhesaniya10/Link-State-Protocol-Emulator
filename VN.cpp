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
using namespace std;
#define BUFF_SIZE 4096
// #define DEST_IP "10.17.44.176"
// #define DEST_PORT 5000

int main(int argc, char* argv[]){

    char* DEST_IP = argv[1];
    uint16_t DEST_PORT = stoi(argv[2]);
    cout<<DEST_IP<<" "<<DEST_PORT<<endl;
    int n = 3;
    vector<pair<string, string>> ip_port_vns = {
        {"10.0.0.1", "8080"},
        {"10.0.0.2", "9090"},
        {"10.0.0.3", "7070"}
    };
    // for(int i=0;i<n;i++){
    //     cin>>ip_port_vns[i].first>>ip_port_vns[i].second;
    // }
    int max_fd;
    vector<int>udp_sock_ids(n),tcp_sock_ids(n); //0->A,1->B and so on
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
    vector<pair<char,pair<string,string>>>ip_address_mapping;
    for(int i=0;i<n;i++){
        cout<<"Hello "<<i<<endl;
        if(connect(tcp_sock_ids[i],(const sockaddr*)&dest_addr,sizeof(dest_addr)) < 0){
            cout<<"connection error for socket with id: "<<i<<endl;
            close(tcp_sock_ids[i]);
            exit(EXIT_FAILURE);
        }

        // char* msg_from_on[BUFF_SIZE];
        // int received_bytes = recv(tcp_sock_ids[i], msg_from_on, BUFF_SIZE, 0);
        // if(received_bytes < 0){
        //     cout<<"connection error for socket with id: "<<i<<endl;
        //     perror("Error while receiving messages for socket with id");
        //     close(tcp_sock_ids[i]);
        //     exit(EXIT_FAILURE);
        // }   
        
        // parse_msg(msg_from_on, ip_address_mapping);
        // cout<<"msg from oracle node for node with id : " << i << endl;
        // cout<<msg_from_on<<endl;
        
        string msg = ip_port_vns[i].first + " " + ip_port_vns[i].second;
        const char* msg_to_send = msg.c_str();
        // msg_to_send = msg.c_str();
        int sent_bytes = send(tcp_sock_ids[i],msg_to_send,strlen(msg_to_send),0);
        if(sent_bytes<0){
            cout<<"sending error for socket with id: "<<i<<endl;
            close(tcp_sock_ids[i]);
            exit(EXIT_FAILURE);
        }
    }
    // cout<<"Yeey"<<endl;
    // fd_set read_fds;
    // while(1){
    //     FD_ZERO(&read_fds);
    //     for(int i=0;i<26;i++){
    //         FD_SET(udp_sock_ids[i],&read_fds);
    //         FD_SET(tcp_sock_ids[i],&read_fds);
    //     }
    //     t.tv_sec = 20;
    //     int activity =  select(max_fd+1, &read_fds, NULL, NULL, &t);
    //     if(activity < 0){
    //         perror("select error");
    //         for(int i=0;i<26;i++){
    //             close(udp_sock_ids[i]);
    //             close(tcp_sock_ids[i]);
    //         }
    //         exit(1);
    //     }
    //     else if (activity == 0){
    //         perror("No activity so timeout");
    //         for(int i=0;i<26;i++){
    //             close(udp_sock_ids[i]);
    //             close(tcp_sock_ids[i]);
    //         }
    //         exit(1);
    //     }
    //     else{
    //         for(int i=0;i<26;i++){
    //             if(FD_ISSET(udp_sock_ids[i], &read_fds)){
    //                 cout<<"Message available from neighbours for node: "+(i+'A')<<endl;
    //             }
    //             if(FD_ISSET(tcp_sock_ids[i], &read_fds)){
    //                 cout<<"Message available from oracle node for node: "+(i+'A')<<endl;

    //                 //TO-DO
    //                 //send this info to neighbors
    //             }
    //         }
    //     }
    //     //periodically send LSP msgs to neighbors
    // }

    for(int i=0;i<n;i++){
        close(udp_sock_ids[i]);
        close(tcp_sock_ids[i]);
    }

    return 0;

}
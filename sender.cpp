#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define DEST_PORT 8000
#define DEST_IP "192.168.56.134"

using namespace std;

int main() {
    struct sockaddr_in dest_addr;

    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(DEST_IP);
    memset(&(dest_addr.sin_zero), '\0', 8);

    cout << "Connecting..." << endl;
    if (connect(sock_fd, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    const char* msg = "Hello Kunj!!!";
    int len = strlen(msg);
    int bytes_sent = send(sock_fd, msg, len, 0);
    if (bytes_sent < 0) {
        perror("send");
    } else {
        cout << "Sent " << bytes_sent << " bytes." << endl;
    }
    char received_msg[100];
    // cout<<(sizeof(received_msg))<<endl;
    int rec_bytes = recv(sock_fd, received_msg, 100 , 0);

    if (rec_bytes < 0) {
        perror("recv");
    } else if (rec_bytes == 0) {
        cout << "Connection closed by peer." << endl;
    } else {
        received_msg[rec_bytes] = '\0';  // <-- important!
        cout << "Received message: " << received_msg << endl;
        cout << "Received " << rec_bytes <<" bytes"<< endl;
    }
    close(sock_fd);
    return 0;
}

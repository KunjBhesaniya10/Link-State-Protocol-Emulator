#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <algorithm> // For std::max

// Define a constant for our buffer size
#define BUFFER_SIZE 1024

/**
 * @class VirtualNode
 * @brief Represents a single participant in the chat.
 *
 * Each node has its own name, a UDP socket for communication,
 * and its network address information.
 */
class VirtualNode {
private:
    std::string id;
    int socket_fd;
    struct sockaddr_in address;

public:
    // Constructor: Sets up the node's socket and binds it to a specific port.
    VirtualNode(const std::string& nodeId, int port) : id(nodeId) {
        // 1. Create a UDP socket
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        // 2. Set up the address struct for this node
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        // Bind to INADDR_ANY to accept packets from any network interface,
        // which is crucial for localhost communication.
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);

        // 3. Bind the socket to the address and port
        if (bind(socket_fd, (const struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("Socket bind failed");
            exit(EXIT_FAILURE);
        }

        std::cout << "Node '" << id << "' created and listening on port " << port << std::endl;
    }

    // Destructor: Cleans up by closing the socket.
    ~VirtualNode() {
        close(socket_fd);
    }

    // --- Getters ---
    int getSocketFd() const { return socket_fd; }
    const std::string& getId() const { return id; }
    const struct sockaddr_in& getAddress() const { return address; }

    /**
     * @brief Sends a message to a specific destination address.
     * @param message The string message to send.
     * @param dest_addr The destination sockaddr_in struct.
     */
    void sendMessage(const std::string& message, const struct sockaddr_in& dest_addr) const {
        sendto(socket_fd, message.c_str(), message.length(), 0,
               (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
    }
};

int main() {
    const int NUM_NODES = 5;
    const int BASE_PORT = 9000;

    // --- 1. SETUP PHASE: Create all virtual nodes ---
    std::vector<VirtualNode*> nodes;
    std::vector<struct sockaddr_in> all_addresses;
    int max_fd = 0;

    for (int i = 0; i < NUM_NODES; ++i) {
        std::string nodeId = "Node-" + std::to_string(i + 1);
        int port = BASE_PORT + i;
        VirtualNode* node = new VirtualNode(nodeId, port);
        nodes.push_back(node);
        all_addresses.push_back(node->getAddress());

        // Keep track of the highest file descriptor for select()
        if (node->getSocketFd() > max_fd) {
            max_fd = node->getSocketFd();
        }
    }

    std::cout << "\n--- All nodes initialized. Starting communication loop. ---\n" << std::endl;

    // --- 2. MAIN EVENT LOOP: Use select() to manage all nodes ---
    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds); // Clear the descriptor set

        // Add all node sockets to the set
        for (const auto& node : nodes) {
            FD_SET(node->getSocketFd(), &read_fds);
        }

        // Set the timeout for selects(). It must be re-initialized inside the loop
        // because some OSes modify it.
        struct timeval timeout;
        timeout.tv_sec = 5; // 5-second timeout
        timeout.tv_usec = 0;

        // The core of the program: wait for activity on any socket or for a timeout.
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select error");
            exit(EXIT_FAILURE);
        }

        // --- 3. HANDLE EVENTS ---

        // Case A: Timeout occurred
        if (activity == 0) {
            std::cout << "\n[Timeout!] No messages received for 5 seconds." << std::endl;
            // Let a random node send a message to everyone.
            int random_node_index = rand() % NUM_NODES;
            VirtualNode* sender = nodes[random_node_index];
            std::string message = "[" + sender->getId() + " says]: Is anyone there?";

            std::cout << "[Action]: " << sender->getId() << " is broadcasting a message." << std::endl;

            // Broadcast to all *other* nodes
            for (int i = 0; i < NUM_NODES; ++i) {
                if (i != random_node_index) {
                    sender->sendMessage(message, all_addresses[i]);
                }
            }
        }
        // Case B: Activity on one or more sockets
        else {
            // Check which node's socket is ready to be read
            for (int i = 0; i < NUM_NODES; ++i) {
                VirtualNode* receiver = nodes[i];
                if (FD_ISSET(receiver->getSocketFd(), &read_fds)) {
                    char buffer[BUFFER_SIZE];
                    struct sockaddr_in sender_addr;
                    socklen_t sender_len = sizeof(sender_addr);

                    // Receive the message
                    int n = recvfrom(receiver->getSocketFd(), buffer, BUFFER_SIZE, 0,
                                     (struct sockaddr*)&sender_addr, &sender_len);

                    if (n > 0) {
                        buffer[n] = '\0'; // Null-terminate the string
                        std::cout << "[" << receiver->getId() << " received]: " << buffer << std::endl;

                        // As an example of interaction, the receiver will broadcast a reply.
                        std::string reply = "[" + receiver->getId() + " replies]: I got your message!";

                        // Broadcast reply to all *other* nodes
                        for (int j = 0; j < NUM_NODES; ++j) {
                            if (j != i) { // Don't send the reply to yourself
                                receiver->sendMessage(reply, all_addresses[j]);
                            }
                        }
                    }
                }
            }
        }
        std::cout << "----------------------------------------------------" << std::endl;
    }

    // --- Cleanup ---
    for (auto& node : nodes) {
        delete node;
    }

    return 0;
}

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

int main() {
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET -> IPv4, SOCK_STREAM -> TCP, 0 -> default IPv4 + TCP protocol.

    // Set socket option
    int opt = 1; // Enable socket option
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // SO_REUSEADDR -> Allows reuse of local addresses


    // Bind socket
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available network interfaces
    address.sin_port = htons(6380); // Convert port number to network byte order(big -endian)

    bind(server_fd, (sockaddr*)&address, sizeof(address));
    listen(server_fd, 10); // 10 -> Maximum number of pending connections
    std::cout<<"Listening on 6380...\n";

    int client_fd = accept(server_fd, nullptr, nullptr);
    char buff[1024];
    ssize_t n;
    while((n = recv(client_fd, buff, sizeof(buff), 0)) > 0){
        send(client_fd, buff, n, 0); //echo back
    }
    close(client_fd);
    close(server_fd);

    
}
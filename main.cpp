#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>

#include <thread>
#include <mutex>

#include <queue>
#include <condition_variable>
#include <vector>

#include <chrono>

#include <fstream>
#include <atomic>

const std::string SNAPSHOT_FILE = "kvstore.snapshot";

struct Node{
    std::string key;
    std::string value;

    std::chrono::steady_clock::time_point expires_at;
    bool has_expiry = false;

    Node* prev = nullptr;
    Node* next = nullptr;
};

std::unordered_map<std::string, Node*> lru_index; // lru_index[key] points to Node.

Node* head = nullptr;
Node* tail = nullptr;

size_t max_size = 3;

std::mutex store_mutex;

std::queue<int> client_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;

void init_lru(int capacity) {
    max_size = capacity;
    head = new Node();
    tail = new Node();
    head->next = tail;
    tail->prev = head;
}

void add_after_head(Node* node) {
    node->prev = head;
    node->next = head->next;

    head->next->prev = node;
    head->next = node;
}

void remove_node(Node* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->prev = nullptr;
    node->next = nullptr;
}

void move_to_head(Node* node) {
    remove_node(node);
    add_after_head(node);
}

void delete_node(Node* node) {
    remove_node(node);
    lru_index.erase(node->key);
    delete node;
}

void evict_tail() {
    if(tail->prev == head){
        return;
    }
    Node* lru = tail->prev;
    delete_node(lru);
}

void put_node(const std::string& key, const std::string& value, bool has_expiry = false,
              std::chrono::steady_clock::time_point expires_at = {}) {
    auto it = lru_index.find(key);

    if(it != lru_index.end()) {
        Node* node = it->second;
        node->value = value;
        node->has_expiry = has_expiry;
        node->expires_at = expires_at;
        move_to_head(node);
        return;
    }

    if(lru_index.size() >= max_size) {
        evict_tail();
    }

    Node* node = new Node();
    node->key = key;
    node->value = value;
    node->has_expiry = has_expiry;
    node->expires_at = expires_at;

    lru_index[key] = node;
    add_after_head(node);
}

bool is_expired(const Node* node) {
    return node->has_expiry && std::chrono::steady_clock::now() > node->expires_at;
}

void save_snapshot() {
    std::ofstream out(SNAPSHOT_FILE, std::ios::trunc);

    if(!out) {
        std::cerr << "Failed to open snapshot file for writing\n";
        return;
    }

    auto now = std::chrono::steady_clock::now();

    for(auto it = lru_index.begin(); it != lru_index.end(); ++it) {
        Node* node = it->second;

        if(is_expired(node)) {
            continue;
        }

        long long ttl_seconds = -1;

        if(node->has_expiry) {
            ttl_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                node->expires_at - now
            ).count();

            if(ttl_seconds <= 0) {
                continue;
            }
        }

        out << node->key << '\t'
            << ttl_seconds << '\t'
            << node->value << '\n';
    }
}

void load_snapshot() {
    std::ifstream in(SNAPSHOT_FILE);

    if(!in) {
        return; // No snapshot yet. First run is fine.
    }

    std::string line;

    while(std::getline(in, line)) {
        std::istringstream iss(line);

        std::string key;
        std::string ttl_str;
        std::string value;

        if(!std::getline(iss, key, '\t')) {
            continue;
        }

        if(!std::getline(iss, ttl_str, '\t')) {
            continue;
        }

        if(!std::getline(iss, value)) {
            value = "";
        }

        long long ttl_seconds = std::stoll(ttl_str);

        if(ttl_seconds == -1) {
            put_node(key, value, false);
        } else if(ttl_seconds > 0) {
            auto expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
            put_node(key, value, true, expires_at);
        }
    }

    std::cout << "Snapshot loaded from disk.\n";
}

std::string handle_command(std::string line){
    while(!line.empty() && (line.back() == '\r' || line.back() == '\n')){
        line.pop_back();
    }

    std::istringstream iss(line); // extracts space-separated tokens from line
    std::string command, key;
    iss >> command >> key; // extract first two tokens as command and key

    if(command == "SET"){
        std::string value;
        getline(iss, value); // extracts the rest of the line as value

        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }

        put_node(key, value, false);

        return "+OK\n";
    }
    else if(command == "GET"){
        if(lru_index.find(key) != lru_index.end()){
            if(is_expired(lru_index[key])){ // Lazy expiration, removed on next access
                delete_node(lru_index[key]);
                return "(nil)\n";
            }
            move_to_head(lru_index[key]);
            return lru_index[key]->value + "\n";
        }else return "(nil)\n";
    }
    else if(command == "DEL"){
        auto it = lru_index.find(key);
        
        if(it == lru_index.end()){
            return "0\n";
        }

        delete_node(it->second);
        return "1\n";
    }
    else if(command == "EXPIRE"){
        int seconds;
        iss >> seconds;
        auto it = lru_index.find(key);
        if(it == lru_index.end()){
            return "0\n";
        }
        else if(is_expired(it->second)){ //it->second is the Entry object (store[key]).
            delete_node(it->second);
            return "0\n";
        }
        it->second->expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        it->second->has_expiry = true;
        return "1\n";
    }
    else if(command == "TTL"){
        auto it = lru_index.find(key);
        if(it == lru_index.end()){
            return "-2\n";
        }

        if(is_expired(it->second)){
            delete_node(it->second);
            return "-2\n";
        }

        if(!it->second->has_expiry){
            return "-1\n";
        }
        auto now = std::chrono::steady_clock::now();
        auto ttl = std::chrono::duration_cast<std::chrono::seconds>(it->second->expires_at - now).count();

        return std::to_string(ttl) + "\n";
    }
    return "-ERR unknown command\n";
}

void snapshot_loop() {
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::lock_guard<std::mutex> lock(store_mutex);
        save_snapshot();
    }
}

void expiry_loop() { // This creates a background thread that periodically checks for expired keys
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        
        std::lock_guard<std::mutex> lock(store_mutex);
        for(auto it = lru_index.begin(); it != lru_index.end(); ){
            if(is_expired(it->second)){
                remove_node(it->second);
                delete it->second;
                it = lru_index.erase(it);
            }else{
                ++it;
            }
        }
    }
}

void handle_client(int client_fd) {
    char buff[1024];
        ssize_t n;
        while((n = recv(client_fd, buff, sizeof(buff), 0)) > 0){
            std::string response; //Initialised out of lock scope because I/O is not included in the lock scope
            { // Scope for lock
                std::lock_guard<std::mutex> lock(store_mutex);
                response = handle_command(std::string(buff, n));
            }
            write(client_fd, response.c_str(), response.length()); // I/O is not included in the lock scope
        }
        close(client_fd);
}

void worker_loop() {
    while(true) {
        int client_fd;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            queue_cv.wait(lock, []{
                return !client_queue.empty();
            });

            client_fd = client_queue.front();
            client_queue.pop();
        }
        handle_client(client_fd);
    }
}

int main() {
    init_lru(3);

    {
        std::lock_guard<std::mutex> lock(store_mutex);
        load_snapshot();
    }

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET -> IPv4, SOCK_STREAM -> TCP, 0 -> default IPv4 + TCP protocol.

    if(server_fd < 0){
        perror("Failed to create socket");
        return 1;
    }

    // Set socket option
    int opt = 1; // Enable socket option
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){ // SO_REUSEADDR -> Allows reuse of local addresses
        perror("Failed to set socket option");
        return 1;
    }


    // Bind socket
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available network interfaces
    address.sin_port = htons(6380); // Convert port number to network byte order(big -endian)

    if(bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0){ // Bind socket to address
        perror("Failed to bind socket");
        return 1;
    }

    if(listen(server_fd, 10) < 0){ // 10 -> Maximum number of pending connections
        perror("Failed to listen on socket");
        return 1;
    }

    std::cout<<"Listening on 6380...\n";

    std::thread(expiry_loop).detach();

    std::thread(snapshot_loop).detach();
    
    const int workers = 4;
    std::vector<std::thread> worker_threads;
    worker_threads.reserve(workers);

    for(int i = 0; i < workers; i++){
        worker_threads.emplace_back(worker_loop);
    }

    while(true){
        int client_fd = accept(server_fd, nullptr, nullptr);
        if(client_fd < 0){
            perror("Failed to accept connection");
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            client_queue.push(client_fd);
        }
        queue_cv.notify_one();
    }

    close(server_fd);
    std::cout<<"Server stopped.\n";
    return 0;
}
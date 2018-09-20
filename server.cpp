#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <thread>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

#define PORT 9999
#define MAX_FILENAME_SIZE 256

class Server {
    private:
        std::unordered_map<std::string, std::vector<int>> file_to_peer_map;

        void error(const char *msg) {
            perror(msg);
            exit(1);
        }

        void handle_client_request(int socket_fd) {
            int peer_port;
            recv(socket_fd, &peer_port, sizeof(peer_port), 0);

            while (1) {
                char request = '0';
                print_files_map();
                if (recv(socket_fd, &request, sizeof(request), 0) < 0) {
                    printf("client unreachable\n");
                    close(socket_fd);
                    return;
                }
                else {
                    switch (request) {
                        case '1':
                            register_(socket_fd, peer_port);
                            break;
                        case '2':
                            deregister_(socket_fd, peer_port);
                            break;
                        case '3':
                            search(socket_fd);
                            break;
                        default:
                            printf("client unreachable\n");
                            close(socket_fd);
                            return;
                    }
                }
            }
        }

        void register_(int socket_fd, int peer_id) {
            std::cout << "adding file to index" << std::endl;

            char buffer[MAX_FILENAME_SIZE];
            if (recv(socket_fd, buffer, MAX_FILENAME_SIZE, 0) < 0)
                error("ERROR receiving\n");
            
            if(!(std::find(file_to_peer_map[std::string(buffer)].begin(), file_to_peer_map[std::string(buffer)].end(), peer_id) != file_to_peer_map[std::string(buffer)].end()))
                file_to_peer_map[std::string(buffer)].push_back(peer_id);
            else 
                std::cout << "file already indexed" << std::endl;
        }

        void deregister_(int socket_fd, int peer_id) {
            std::cout << "reomving file from index" << std::endl;

            char buffer[MAX_FILENAME_SIZE];
            if (recv(socket_fd, buffer, MAX_FILENAME_SIZE, 0) < 0)
                error("ERROR receiving\n");
 
            file_to_peer_map[std::string(buffer)].erase(std::remove(file_to_peer_map[std::string(buffer)].begin(), file_to_peer_map[std::string(buffer)].end(), peer_id), file_to_peer_map[std::string(buffer)].end());
            if (file_to_peer_map[std::string(buffer)].size() == 0)
                file_to_peer_map.erase(std::string(buffer));
        }

        void search(int socket_fd) {
            std::cout << "requesting to search for a file" << std::endl;

            char buffer[MAX_FILENAME_SIZE];
            if (recv(socket_fd, buffer, sizeof(buffer), 0) < 0)
                error("ERROR receiving\n");
            std::cout << "searching index for file: " << buffer << std::endl;

            std::ostringstream ss;
            if (file_to_peer_map.count(std::string(buffer)) > 0) {
                std::cout << buffer << " found" << std::endl;
                std::string separator;
                for (auto && x : file_to_peer_map[buffer]) {
                    ss << separator << x;
                    separator = ",";
                }
            }

            char res_buffer[4096];
            bzero(res_buffer, 4096);
            strcpy(res_buffer, ss.str().c_str());
            if (send(socket_fd, res_buffer, 4096, 0) < 0)
                printf("client unreachable\n");
        }

        void print_files_map() {
            for (auto const& x : file_to_peer_map) {
                std::cout << x.first << ':';
                for (auto i = x.second.begin(); i != x.second.end(); ++i)
                    std::cout << *i << ',';
                std::cout << std::endl;
            }
        }

    public:
        int server_socket_fd;

        Server() {
            struct sockaddr_in serv_addr;

            bzero((char*)&serv_addr, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_addr.s_addr = INADDR_ANY;
            serv_addr.sin_port = htons(PORT);

            server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (bind(server_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
                error("ERROR on binding\n");
        }

        void run() {
            struct sockaddr_in cli_addr;
            socklen_t cli_len;
            int client_socket_fd;

            cli_len = sizeof(cli_addr);

            while (1) {
                listen(server_socket_fd, 5);

                if ((client_socket_fd = accept(server_socket_fd, (struct sockaddr*)&cli_addr, &cli_len)) < 0)
                    error("ERROR on accept\n");

                printf("server: got connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

                std::thread t(&Server::handle_client_request, this, client_socket_fd);
                t.detach();
            }
        }

        ~Server() {
            close(server_socket_fd);
        }
};


int main() {
    Server server;
    server.run();

    return 0;
}

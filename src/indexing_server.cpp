#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <thread>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <chrono>


#define PORT 9999
#define MAX_FILENAME_SIZE 256
#define MAX_MSG_SIZE 4096


class IndexingServer {
    private:
        std::unordered_map<std::string, std::vector<int>> files_index;

        std::string time_now() {
            std::chrono::high_resolution_clock::duration now = std::chrono::high_resolution_clock::now().time_since_epoch();
            std::chrono::microseconds now_ms = std::chrono::duration_cast<std::chrono::microseconds>(now);
            return std::to_string(now_ms.count());
        }

        void log(std::string type, std::string msg) {
            std::cout << '[' << time_now() << "] [" << type << "] " << msg  << '\n' << std::endl;
        }
        
        void error(std::string type) {
            log(type, "exiting program");
            exit(1);
        }

        void remove_client(int client_socket_fd, int client_id, std::string type) {
            log(type, "closing connection and cleaning up index");
            files_index_cleanup(client_id);
            close(client_socket_fd);
        }

        void handle_client_requests(int client_socket_fd) {
            int client_id;
            if (recv(client_socket_fd, &client_id, sizeof(client_id), 0) < 0) {
                log("client unidentified", "closing connection");
                close(client_socket_fd);
                return;
            }

            char request;
            while (1) {
                request = '0';
                if (recv(client_socket_fd, &request, sizeof(request), 0) < 0) {
                    remove_client(client_socket_fd, client_id, "client unreachable");
                    return;
                }

                switch (request) {
                    case '1':
                        registry(client_socket_fd, client_id);
                        break;
                    case '2':
                        deregistry(client_socket_fd, client_id);
                        break;
                    case '3':
                        search(client_socket_fd, client_id);
                        break;
                    case '0':
                        remove_client(client_socket_fd, client_id, "client disconnected");
                        return;
                    default:
                        remove_client(client_socket_fd, client_id, "unexpected request");
                        return;
                }
            }
        }

        void registry(int client_socket_fd, int client_id) {
            char buffer[MAX_FILENAME_SIZE];
            if (recv(client_socket_fd, buffer, sizeof(buffer), 0) < 0) {
                remove_client(client_socket_fd, client_id, "client unreachable");
                return;
            }
            
            std::string filename = std::string(buffer);
            if(!(std::find(files_index[filename].begin(), files_index[filename].end(), client_id) != files_index[filename].end()))
                files_index[filename].push_back(client_id);
        }

        void deregistry(int client_socket_fd, int client_id) {
            char buffer[MAX_FILENAME_SIZE];
            if (recv(client_socket_fd, buffer, sizeof(buffer), 0) < 0) {
                remove_client(client_socket_fd, client_id, "client unreachable");
                return;
            }
 
            std::string filename = std::string(buffer);
            files_index[filename].erase(std::remove(files_index[filename].begin(), files_index[filename].end(), client_id), files_index[filename].end());
            if (files_index[filename].size() == 0)
                files_index.erase(filename);
        }

        void files_index_cleanup(int client_id) {
            std::unordered_map<std::string, std::vector<int>> tmp_files_index;
            
            for (auto const &file_index : files_index) {
                std::vector<int> tmp_client_ids = file_index.second;
                tmp_client_ids.erase(std::remove(tmp_client_ids.begin(), tmp_client_ids.end(), client_id), tmp_client_ids.end());
                if (tmp_client_ids.size() > 0)
                    tmp_files_index[file_index.first] = tmp_client_ids;
            }
            files_index = tmp_files_index;
        }

        void search(int client_socket_fd, int client_id) {
            char buffer[MAX_FILENAME_SIZE];
            if (recv(client_socket_fd, buffer, sizeof(buffer), 0) < 0) {
                remove_client(client_socket_fd, client_id, "client unreachable");
                return;
            }

            std::string filename = std::string(buffer);
            
            std::ostringstream client_ids;
            if (files_index.count(filename) > 0) {
                std::string delimiter;
                for (auto &&client_id : files_index[filename]) {
                    client_ids << delimiter << client_id;
                    delimiter = ',';
                }
            }

            char buffer_[MAX_MSG_SIZE];
            strcpy(buffer_, client_ids.str().c_str());
            if (send(client_socket_fd, buffer_, sizeof(buffer_), 0) < 0) {
                remove_client(client_socket_fd, client_id, "client unreachable");
                return;
            }
        }

        void print_files_map() {
            for (auto const &file_index : files_index) {
                std::cout << file_index.first << ':';
                std::string delimiter;
                for (auto &&client_id : file_index.second) {
                    std::cout << delimiter << client_id;
                    delimiter = ',';
                }
                std::cout << std::endl;
            }
        }

    public:
        int socket_fd;

        IndexingServer() {
            struct sockaddr_in addr;
            socklen_t addr_size = sizeof(addr);
            bzero((char*)&addr, addr_size);
            
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(PORT);

            socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (bind(socket_fd, (struct sockaddr*)&addr, addr_size) < 0)
                error("failed server binding");
        }

        void run() {
            struct sockaddr_in addr;
            socklen_t addr_size = sizeof(addr);
            int client_socket_fd;

            std::ostringstream client_identity;
            while (1) {
                listen(socket_fd, 5);

                if ((client_socket_fd = accept(socket_fd, (struct sockaddr*)&addr, &addr_size)) < 0) {
                    log("failed client connection", "ignoring connection");
                    continue;
                }

                client_identity << inet_ntoa(addr.sin_addr) << '@' << ntohs(addr.sin_port);
                log("client connection", client_identity.str());
                
                std::thread t(&IndexingServer::handle_client_requests, this, client_socket_fd);
                t.detach();

                client_identity.str("");
                client_identity.clear();
            }
        }

        ~IndexingServer() {
            close(socket_fd);
        }
};


int main() {
    IndexingServer indexing_server;
    indexing_server.run();

    return 0;
}

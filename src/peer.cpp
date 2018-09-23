#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include <thread>
#include <mutex>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>


#define HOST "localhost"
#define INDEXING_SERVER_PORT 9999
#define MAX_FILENAME_SIZE 256
#define MAX_MSG_SIZE 4096
#define MAX_STAT_MSG_SIZE 16


class Peer {
    private:
        std::vector<std::pair<std::string, time_t>> files;
        std::ofstream server_log;
        std::ofstream client_log;

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

        void handle_client_request(int client_socket_fd) {
            retrieve(client_socket_fd);

            close(client_socket_fd);
        }

        void retrieve(int client_socket_fd) {
            char buffer[MAX_FILENAME_SIZE];
            if (recv(client_socket_fd, buffer, MAX_FILENAME_SIZE, 0) < 0) {
                log("client unresponsive", "closing connection");
                return;
            }
            
            std::ostringstream filename;
            filename << std::string(files_directory_path);
            filename << std::string(buffer);
            int fd = open(filename.str().c_str(), O_RDONLY);
            if (fd == -1) {
                if (send(client_socket_fd, "-1", MAX_STAT_MSG_SIZE, 0) < 0)
                    log("client unresponsive", "closing connection");
            }
            else {
                struct stat file_stat;
                if (fstat(fd, &file_stat) < 0) {
                    if (send(client_socket_fd, "-2", MAX_STAT_MSG_SIZE, 0) < 0)
                        log("client unresponsive", "closing connection");
                }
                else {
                    char file_size[MAX_STAT_MSG_SIZE];
                    sprintf(file_size, "%ld", file_stat.st_size);

                    if (send(client_socket_fd, file_size, sizeof(file_size), 0) < 0) {
                        log("client unresponsive", "closing connection");
                        return;
                    }

                    off_t offset = 0;
                    int remaining_size = file_stat.st_size;
                    int sent_size = 0;
                    while (((sent_size = sendfile(client_socket_fd, fd, &offset, MAX_MSG_SIZE)) > 0) && (remaining_size > 0))
                        remaining_size -= sent_size;
                }
            }
            close(fd);
        }

        std::vector<std::pair<std::string, time_t>> get_files() {
            std::vector<std::pair<std::string, time_t>> tmp_files;
            
            if (auto directory = opendir(files_directory_path.c_str())) {
                while (auto file = readdir(directory)) {
                    if (!file->d_name || strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0 || file->d_type == DT_DIR)
                        continue;
                    
                    std::ostringstream file_path;
                    file_path << files_directory_path;
                    file_path << file->d_name;
                    
                    int fd = open(file_path.str().c_str(), O_RDONLY);
                    if (fd == -1) {
                        log("failed file open", "ignoring \"" + file_path.str() + '\"');
                        continue;
                    }
                    
                    struct stat file_stat;
                    if (fstat(fd, &file_stat) < 0) {
                        log("failed file stat", "ignoring \"" + file_path.str() + '\"');
                        continue;
                    }
                    close(fd);

                    time_t modified_time = file_stat.st_mtim.tv_sec;
                    std::pair<std::string, time_t> file_info = std::make_pair(file->d_name, modified_time);
                    if(!(std::find(tmp_files.begin(), tmp_files.end(), file_info) != tmp_files.end()))
                        tmp_files.push_back(file_info);
                }
                closedir(directory);
            }
            else {
                error("invalid directory");
            }
            return tmp_files;
        }

        int connect_server(int server_port, bool index_server=true) {
            struct sockaddr_in addr;
            socklen_t addr_size = sizeof(addr);
            bzero((char *)&addr, addr_size);

            struct hostent *server = gethostbyname(HOST);
            int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            addr.sin_family = AF_INET;
            bcopy((char *)server->h_addr, (char *)&addr.sin_addr.s_addr, server->h_length);
            addr.sin_port = htons(server_port);
            
            if (connect(server_socket_fd, (struct sockaddr *)&addr, addr_size) < 0) {
                if (index_server)
                    error("failed indexing server connection");
                else {
                    return -1;
                }
            }
            
            return server_socket_fd;
        }

        void register_files(int server_socket_fd) {
            int n;
            char buffer[MAX_FILENAME_SIZE];

            while (1) {
                std::vector<std::pair<std::string, time_t>> tmp_files = get_files();
                for(auto&& x: files) {
                    char request = '1';
                    if(!(std::find(tmp_files.begin(), tmp_files.end(), x) != tmp_files.end()))
                        request = '2';
                    
                    if (send(server_socket_fd, &request, sizeof(request), 0) < 0) {
                        log("server unresponsive", "ignoring request");
                    }
                    else {
                        bzero(buffer, MAX_FILENAME_SIZE);
                        strcpy(buffer, x.first.c_str());
                        if (send(server_socket_fd, buffer, sizeof(buffer), 0) < 0)
                            log("server unresponsive", "ignoring request");
                    }
                }
                files = tmp_files;
                sleep(5);
            }
        }
        
        void search_request(int server_socket_fd) {
            std::cout << "filename: ";
            char filename[MAX_FILENAME_SIZE];
            std::cin >> filename;
            if (send(server_socket_fd, "3", sizeof(char), 0) < 0) {
                std::cout << "\nunexpected connection issue\n" << std::endl;
                log("server unresponsive", "ignoring request");
            }
            else {
                if (send(server_socket_fd, filename, sizeof(filename), 0) < 0) {
                    std::cout << "\nunexpected connection issue\n" << std::endl;
                    log("server unresponsive", "ignoring request");
                }
                else {
                    char buffer[MAX_MSG_SIZE];
                    if (recv(server_socket_fd, buffer, sizeof(buffer), 0) < 0) {
                        std::cout << "\nunexpected connection issue\n" << std::endl;
                        log("server unresponsive", "ignoring request");
                    }
                    else if (!buffer[0])
                        std::cout << "\nfile not found\n" << std::endl;
                    else 
                        std::cout << "\npeer(s) with file: " << buffer << '\n' << std::endl;
                }
            }
        }

        std::string resolve_filename(std::string filename, std::string(peer)) {
            std::ostringstream local_filename;
            local_filename << files_directory_path;
            size_t extension_idx = filename.find_last_of('.');
            local_filename << filename.substr(0, extension_idx);
            if ((std::find_if(files.begin(), files.end(), [filename](const std::pair<std::string, int> &element){return element.first == filename;}) != files.end()))
                local_filename << "-origin-" << peer;
            local_filename << filename.substr(extension_idx, filename.size() - extension_idx);

            return local_filename.str();
        }
        
        void retrieve_request(int server_socket_fd) {
            std::cout << "peer: ";
            char peer[6];
            std::cin >> peer;
            if (atoi(peer) == port) {
                std::cout << "\npeer is current client\n" << std::endl;
                return;
            }
            int peer_socket_fd = connect_server(atoi(peer), false);
            if (peer_socket_fd < 0) {
                std::cout << "\npeer is not valid\n" << std::endl;
                log("failed peer server connection", "ignoring request");
                return;
            }
            
            std::cout << "filename: ";
            char filename[MAX_FILENAME_SIZE];
            std::cin >> filename;
            if (send(peer_socket_fd, filename, sizeof(filename), 0) < 0) {
                std::cout << "\nunexpected connection issue\n" << std::endl;
                log("peer unresponsive", "ignoring request");
            }
            else {
                char buffer[MAX_STAT_MSG_SIZE];
                if (recv(peer_socket_fd, buffer, sizeof(buffer), 0) < 0) {
                    std::cout << "\nunexpected connection issue\n" << std::endl;
                    log("peer unresponsive", "ignoring request");
                }
                else {
                    int file_size = atoi(buffer);
                    if (file_size == -1)
                        std::cout << "\nfile does not exist\n" << std::endl;
                    else if (file_size == -2)
                        std::cout << "\ncould not read file stats\n" << std::endl;
                    else {
                        std::string local_filename = resolve_filename(filename, peer);
                        FILE *file = fopen(local_filename.c_str(), "w");
                        if (file == NULL) {
                            std::cout << "\nunable to create new file\n" << std::endl;
                            log("failed file open", "ignoring file");
                        }
                        else {
                            char buffer_[MAX_MSG_SIZE];
                            int remaining_size = file_size;
                            int received_size;
                            while (((received_size = recv(peer_socket_fd, buffer_, sizeof(buffer_), 0)) > 0) && (remaining_size > 0)) {
                                fwrite(buffer_, sizeof(char), received_size, file);
                                remaining_size -= received_size;
                            }
                            fclose(file);
                            std::cout << "\nfile tranferred\n" << std::endl;
                        }
                    }
                }
            }
            close(peer_socket_fd);
        }

    public:
        std::string files_directory_path;
        int port;
        int socket_fd;

        Peer(std::string path) {
            files_directory_path = path;
            if (files_directory_path.back() != '/')
                files_directory_path += '/';
            files = get_files();

            struct sockaddr_in addr;
            socklen_t addr_size = sizeof(addr);
            bzero((char*)&addr, addr_size);
            
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;

            socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (bind(socket_fd, (struct sockaddr*)&addr, addr_size) < 0)
                error("failed to start peer server");
            
            getsockname(socket_fd, (struct sockaddr *)&addr, &addr_size);
            port = ntohs(addr.sin_port);

            std::string log_name_prefix = "logs/peers/" + std::to_string(port);
            server_log.open(log_name_prefix + "_server.log");
            client_log.open(log_name_prefix + "_client.log");
        }
        
        void run_client() {
            int server_socket_fd = connect_server(INDEXING_SERVER_PORT);

            if (send(server_socket_fd, &port, sizeof(port), 0) < 0)
                error("server unreachable");

            std::thread t(&Peer::register_files, this, server_socket_fd);
            t.detach();

            while (1) {
                std::string request;
                std::cout << "request [(s)earch|(r)etrieve|(q)uit]: ";
                std::cin >> request;

                switch (request[0]) {
                    case 's':
                    case 'S': 
                        search_request(server_socket_fd);
                        break;
                    case 'r':
                    case 'R':
                        retrieve_request(server_socket_fd);
                        break;
                    case 'q':
                    case 'Q':
                        close(server_socket_fd);
                        exit(0);
                        break;
                    default:
                        std::cout << "\nunexpected request\n" << std::endl;
                        break;
                }
            }
        }

        void run_server() {
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
                log("client connected", client_identity.str());

                std::thread t(&Peer::handle_client_request, this, client_socket_fd);
                t.detach();

                client_identity.str("");
                client_identity.clear();
            }
        }

        void run() {
            std::thread c_t(&Peer::run_client, this);
            std::thread s_t(&Peer::run_server, this);

            c_t.join();
            s_t.join();
        }

        ~Peer() {
            close(socket_fd);
            server_log.close();
            client_log.close();
        }
};


int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " path" << std::endl;
        exit(0);
    }

    Peer peer(argv[1]);
    peer.run();

    return 0;
}

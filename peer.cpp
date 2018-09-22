#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include <thread>
#include <utility>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>


#define HOST "localhost"
#define INDEXING_SERVER_PORT 9999
#define MAX_FILENAME_SIZE 256
#define MAX_MSG_SIZE 4096


class Peer {
    private:
        std::vector<std::pair<std::string, time_t>> files;

        void error(std::string msg) {
            std::cerr << "[ERROR] " << msg << std::endl;
            exit(1);
        }

        void handle_client_request(int socket_fd) {
            retrieve(socket_fd);

            close(socket_fd);
        }

        void retrieve(int socket_fd) {
            char buffer[MAX_FILENAME_SIZE];
            if (recv(socket_fd, buffer, MAX_FILENAME_SIZE, 0) < 0)
                error("ERROR receiving\n");
            
            std::ostringstream ss;
            ss << std::string(files_directory_path);
            ss << std::string(buffer);
            int fd = open(ss.str().c_str(), O_RDONLY);
            if (fd == -1) {
                int n = send(socket_fd, "-1", 16, 0);
                if (n < 0)
                    printf("client unreachable\n");
            }
            else {
                /* Get file stats */
                struct stat file_stat;
                if (fstat(fd, &file_stat) < 0) {
                    int n = send(socket_fd, "-2", 16, 0);
                    if (n < 0)
                        printf("client unreachable\n");
                }

                char file_size[16];
                sprintf(file_size, "%ld", file_stat.st_size);

                /* Sending file size */
                int n = send(socket_fd, file_size, sizeof(file_size), 0);
                if (n < 0)
                    printf("client unreachable\n");

                off_t offset = 0;
                int remain_data = file_stat.st_size;
                int sent_bytes = 0;
                /* Sending file data */
                while (((sent_bytes = sendfile(socket_fd, fd, &offset, 4096)) > 0) && (remain_data > 0))
                    remain_data -= sent_bytes;
                close(fd);
            }
        }

    public:
        std::string files_directory_path;
        int __port;
        int __socket_fd;

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

            __socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (bind(__socket_fd, (struct sockaddr*)&addr, addr_size) < 0)
                error("failed to start peer server");
            
            getsockname(__socket_fd, (struct sockaddr *)&addr, &addr_size);
            __port = ntohs(addr.sin_port);
        }

        std::vector<std::pair<std::string, time_t>> get_files() {
            std::vector<std::pair<std::string, time_t>> tmp_files;
            
            if (auto dir = opendir(files_directory_path.c_str())) {
                while (auto f = readdir(dir)) {
                    if (!f->d_name || f->d_name[0] == '.' || (f->d_name[0] == '.' && f->d_name[1] == '.'))
                        continue;
                    
                    std::ostringstream f_path;
                    f_path << files_directory_path;
                    f_path << std::string(f->d_name);
                    
                    int fd = open(f_path.str().c_str(), O_RDONLY);
                    if (fd == -1)
                        error("ERROR opening file\n");
                    
                    struct stat file_stat;
                    if (fstat(fd, &file_stat) < 0)
                        error("ERROR fstat\n");
                    close(fd);

                    time_t modified_time = file_stat.st_mtim.tv_sec;
                    std::pair<std::string, time_t> file_info = std::make_pair(f->d_name, modified_time);
                    if(!(std::find(tmp_files.begin(), tmp_files.end(), file_info) != tmp_files.end()))
                        tmp_files.push_back(file_info);
                }
                closedir(dir);
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
                    error("failed to connect to indexing server");
                else {
                    std::cout << "failed server connection: " << inet_ntoa(addr.sin_addr) << '@' << ntohs(addr.sin_port) << '\n' << std::endl;
                    return -1;
                }
            }
            
            return server_socket_fd;
        }

        void register_files(int socket_fd) {
            int n;
            char buffer[MAX_FILENAME_SIZE];

            while (1) {
                std::vector<std::pair<std::string, time_t>> tmp_files = get_files();
                for(auto&& x: files) {
                    char request = '1';
                    if(!(std::find(tmp_files.begin(), tmp_files.end(), x) != tmp_files.end()))
                        request = '2';
                    
                    if (send(socket_fd, &request, sizeof(request), 0) < 0)
                        error("ERROR writing to socket");
                    
                    bzero(buffer, MAX_FILENAME_SIZE);
                    strcpy(buffer, x.first.c_str());
                    if (send(socket_fd, buffer, sizeof(buffer), 0) < 0)
                        error("ERROR writing to socket");
                }
                files = tmp_files;
                sleep(5);
            }
        }
        
        void search_request(int server_socket_fd) {
            std::cout << "filename: ";
            char filename[MAX_FILENAME_SIZE];
            std::cin >> filename;
            if (send(server_socket_fd, "3", sizeof(char), 0) < 0)
                error("ERROR writing to socket");
            if (send(server_socket_fd, filename, sizeof(filename), 0) < 0)
                error("ERROR writing to socket");
            
            char buffer[MAX_MSG_SIZE];
            if (recv(server_socket_fd, buffer, sizeof(buffer), 0) < 0)
                error("ERROR reading from socket");
            else if (!buffer[0])
                std::cout << "file not found" << std::endl;
            else 
                std::cout << "peer(s) with file: " << buffer << std::endl;
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
            if (atoi(peer) == __port) {
                std::cout << "peer is current client: no connection needed" << std::endl;
                return;
            }
            int __socket_fd = connect_server(atoi(peer), false);
            
            std::cout << "filename: ";
            char filename[MAX_FILENAME_SIZE];
            std::cin >> filename;
            if (send(__socket_fd, filename, sizeof(filename), 0) < 0)
                error("ERROR writing to socket");
            
            char buffer[16];
            if (recv(__socket_fd, buffer, sizeof(buffer), 0) < 0)
                error("ERROR receiving\n");
            int file_size = atoi(buffer);
            if (file_size == -1)
                std::cout << "file does not exist: no transfer completed" << std::endl;
            else if (file_size == -2)
                std::cout << "could not read file stats: no tranfer completed" << std::endl;
            else {
                std::string local_filename = resolve_filename(filename, peer);
                FILE *file = fopen(local_filename.c_str(), "w");
                if (file == NULL)
                    error("ERROR opening file");

                char buffer_[MAX_MSG_SIZE];
                int recv_size;
                while (((recv_size = recv(__socket_fd, buffer_, sizeof(buffer_), 0)) > 0) && (file_size > 0)) {
                        fwrite(buffer_, sizeof(char), recv_size, file);
                        file_size -= recv_size;
                }
                fclose(file);
            }
            close(__socket_fd);
        }
        
        void run_client() {
            int server_socket_fd = connect_server(INDEXING_SERVER_PORT);

            if (send(server_socket_fd, &__port, sizeof(__port), 0) < 0)
                error("client unreachable");

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
                        std::cout << "unexpected request" << std::endl;
                        break;
                }
            }
        }

        void run_server() {
            struct sockaddr_in addr;
            socklen_t addr_size = sizeof(addr);
            int client_socket_fd;

            while (1) {
                listen(__socket_fd, 5);

                if ((client_socket_fd = accept(__socket_fd, (struct sockaddr*)&addr, &addr_size)) < 0) {
                    std::cout << "failed client connection: " << inet_ntoa(addr.sin_addr) << '@' << ntohs(addr.sin_port) << '\n' << std::endl;
                    continue;
                }

                std::cout << "client connection: " << inet_ntoa(addr.sin_addr) << '@' << ntohs(addr.sin_port) << '\n' << std::endl;

                std::thread t(&Peer::handle_client_request, this, client_socket_fd);
                t.detach();
            }
        }

        void run() {
            std::thread c_t(&Peer::run_client, this);
            std::thread s_t(&Peer::run_server, this);

            c_t.join();
            s_t.join();
        }

        ~Peer() {
            close(__socket_fd);
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

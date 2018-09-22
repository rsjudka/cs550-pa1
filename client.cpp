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

class Peer {
    private:
        std::vector<std::pair<std::string, time_t>> files;

        void error(const char *msg) {
            perror(msg);
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
            ss << std::string(files_directory);
            ss << std::string(buffer);
            int fd = open(ss.str().c_str(), O_RDONLY);
            if (fd == -1)
                error("ERROR opening file\n");

            /* Get file stats */
            struct stat file_stat;
            if (fstat(fd, &file_stat) < 0)
                error("ERROR fstat\n");

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
            while (((sent_bytes = sendfile(socket_fd, fd, &offset, BUFSIZ)) > 0) && (remain_data > 0))
                remain_data -= sent_bytes;
            close(fd);
        }

    public:
        int peer_port;
        char *files_directory;
        int peer_socket_fd;

        Peer(char *directory) {
            files_directory = directory;

            files = get_files();

            struct sockaddr_in serv_addr;

            bzero((char*)&serv_addr, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_addr.s_addr = INADDR_ANY;

            peer_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

            if (bind(peer_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
                error("ERROR on binding\n");
            
            socklen_t addrlen = sizeof(serv_addr);
            getsockname(peer_socket_fd, (struct sockaddr *)&serv_addr, &addrlen);
            peer_port = ntohs(serv_addr.sin_port);
        }

        std::vector<std::pair<std::string, time_t>> get_files() {
            std::vector<std::pair<std::string, time_t>> tmp_files;
            if (auto dir = opendir(files_directory)) {
                while (auto f = readdir(dir)) {
                    if (!f->d_name || f->d_name[0] == '.' || (f->d_name[0] == '.' && f->d_name[1] == '.'))
                        continue;
                    std::ostringstream ss;
                    ss << std::string(files_directory);
                    ss << std::string(f->d_name);
                    int fd = open(ss.str().c_str(), O_RDONLY);
                    if (fd == -1)
                        error("ERROR opening file\n");
                    struct stat file_stat;
                    if (fstat(fd, &file_stat) < 0)
                        error("ERROR fstat\n");
                    time_t modified_time = file_stat.st_mtim.tv_sec;
                    close(fd);
                    std::pair<std::string, time_t> file_info = std::make_pair(f->d_name, modified_time);
                    if(!(std::find(tmp_files.begin(), tmp_files.end(), file_info) != tmp_files.end()))
                        tmp_files.push_back(file_info);
                }
                closedir(dir);
            }
            return tmp_files;
        }

        int join(int port) {
            int n;
            struct sockaddr_in serv_addr;
            struct hostent *server;

            int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_fd < 0)
                error("ERROR opening socket");
            server = gethostbyname(HOST);
            if (server == NULL) {
                fprintf(stderr, "ERROR, no such host\n");
                exit(0);
            }
            bzero((char *)&serv_addr, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            bcopy((char *)server->h_addr,
                (char *)&serv_addr.sin_addr.s_addr,
                server->h_length);
            serv_addr.sin_port = htons(port);
            if (connect(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
                error("ERROR connecting");
            
            return socket_fd;
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
                    n = send(socket_fd, &request, sizeof(char), 0);
                    if (n < 0)
                        error("ERROR writing to socket");
                    bzero(buffer, MAX_FILENAME_SIZE);
                    strcpy(buffer, x.first.c_str());
                    n = send(socket_fd, buffer, MAX_FILENAME_SIZE, 0);
                    if (n < 0)
                        error("ERROR writing to socket");
                }
                files = tmp_files;
                sleep(5);
            }
        }
        
        void run_client() {
            int socket_fd = join(INDEXING_SERVER_PORT);
            char buffer[4096];
            int n;

            n = send(socket_fd, &peer_port, sizeof(peer_port), 0);
            if (n < 0)
                error("ERROR writing to socket");

            std::thread t(&Peer::register_files, this, socket_fd);
            t.detach();

            while (1) {
                std::string request;
                std::cout << "request [(s)earch|(r)etrieve|(q)uit]: ";
                std::cin >> request;

                if (request[0] == 's') {
                    std::cout << "filename: ";
                    char filename[MAX_FILENAME_SIZE];
                    std::cin >> filename;
                    n = send(socket_fd, "3", sizeof(char), 0);
                    if (n < 0)
                        error("ERROR writing to socket");
                    n = send(socket_fd, filename, sizeof(filename), 0);
                    if (n < 0)
                        error("ERROR writing to socket");
                    bzero(buffer, 4096);
                    n = recv(socket_fd, buffer, 4096, 0);
                    if (n < 0)
                        error("ERROR reading from socket");
                    else if (!buffer[0])
                        std::cout << "file not found" << std::endl;
                    else 
                        std::cout << "peer(s) with file: " << buffer << std::endl;
                }
                else if (request[0] == 'r') {
                    std::cout << "peer: ";
                    char peer[6];
                    std::cin >> peer;
                    peer[5] = '\0';
                    int peer_socket_fd = join(atoi(peer));
                    std::cout << "filename: ";
                    char filename[MAX_FILENAME_SIZE];
                    std::cin >> filename;
                    n = send(peer_socket_fd, filename, sizeof(filename), 0);
                    if (n < 0)
                        error("ERROR writing to socket");

                    recv(peer_socket_fd, buffer, BUFSIZ, 0);
                    int file_size = atoi(buffer);

                    std::ostringstream ss;
                    ss << std::string(files_directory);
                    std::string fn = std::string(filename);
                    size_t extension_idx = fn.find_last_of(".");
                    ss << fn.substr(0,extension_idx);
                    if ((std::find_if(files.begin(), files.end(), [filename](const std::pair<std::string, int>& element){ return element.first == filename;}) != files.end()))
                        ss << "-origin-" << std::string(peer);
                    ss << fn.substr(extension_idx, fn.size()-extension_idx);
                    FILE *received_file = fopen(ss.str().c_str(), "w");
                    if (received_file == NULL)
                        error("ERROR opening file");

                    int remain_data = file_size;

                    while (((n = recv(peer_socket_fd, buffer, BUFSIZ, 0)) > 0) && (remain_data > 0)) {
                            fwrite(buffer, sizeof(char), n, received_file);
                            remain_data -= n;
                    }
                    fclose(received_file);
                    close(peer_socket_fd);
                }
                else if (request[0] == 'q') {
                    close(socket_fd);
                    exit(0);
                }
                else
                    std::cout << "unexpected request" << std::endl;
            }
        }

        void run_server() {
            struct sockaddr_in cli_addr;
            socklen_t cli_len;
            int client_socket_fd;

            cli_len = sizeof(cli_addr);

            while (1) {
                listen(peer_socket_fd, 5);

                if ((client_socket_fd = accept(peer_socket_fd, (struct sockaddr*)&cli_addr, &cli_len)) < 0)
                    error("ERROR on accept\n");

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
            close(peer_socket_fd);
        }
};


int main(int argc, char *argv[]) {
    Peer peer(argv[1]);
    peer.run();

    return 0;
}

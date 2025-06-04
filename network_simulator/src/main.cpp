#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <queue>
#include <deque>
#include <optional>
#include <functional>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <poll.h>
#include <time.h> 


#include "simdjson.h"
#include "helpers.h"

struct FrameToDeliver {

    uint64_t delivery_timestamp_ns;
    std::vector<uint8_t> data;

    static bool compare(const uint64_t &a, const FrameToDeliver &b) {
        return a < b.delivery_timestamp_ns;
    }

};


int main(int argc, char *argv[]){

    std::cout << "hello from network simulator\n";

    // std::unordered_map<std::string, int> mac_to_tap_fds;
    std::unordered_map<std::string, int> mac_to_vm_index;
    std::vector<int> tap_fds; // VM index to tap fd
    std::vector<int> qmp_sockets; // VM index to QMP socket
    std::vector<uint64_t> virtual_clocks_ns; // VM index to virtual clock ns
    // TODO make indexing of vector<vector>> consistent !!!
    std::vector<std::vector<std::queue<FrameToDeliver>>> inter_vm_queues; // inter_vm_queues[i][j] is the frame queue to VM i from VM j
    std::vector<std::vector<uint32_t>> inter_vm_delays_ms; // inter_vm_delays_ms[i][j] is the delay when sending from vm i to vm j

    // check number of arguments and read config

    if (argc != 3){
        std::cout << "not enough arg " << argc << std::endl;
        return 1;
    }

    std::ifstream config_file(argv[1]);

    std::string config_line;
    while (std::getline(config_file, config_line)){

        std::stringstream ss_line(config_line);
        std::string port_string;
        std::string mac;
        std::string tap_interface;

        ss_line >> port_string;
        ss_line >> mac;
        ss_line >> tap_interface;

        int port = std::stoi(port_string);

        std::cout << port << " " << mac << " " << tap_interface << std::endl;

        // get tap fd

        int tap_fd;
        tap_fd = tun_alloc(tap_interface.c_str());
        if (tap_fd < 0){
            return 1;
        }

        mac_to_vm_index[mac] = tap_fds.size();
        tap_fds.push_back(tap_fd);

        // connect to QMP server

        int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in serverAddress;
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(port);
        serverAddress.sin_addr.s_addr =  inet_addr("127.0.0.1");

        int err = connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
        if ( err != 0) {
            std::cout << "connection with the server failed... " << err << std::endl;
            perror("connect failed");
        }
        else{
            std::cout << "connected to the server..\n";
        }

        std::cout << "socket " << clientSocket << std::endl;

        qmp_sockets.push_back(clientSocket);

        qmp_socket_buffers[clientSocket] = "";
    }

    const int VM_NUMBER = qmp_sockets.size();

    // init inter vm frame queues

    std::cout << "start init inter vm frame queues \n";

    for(size_t i = 0; i < VM_NUMBER; i++){
        std::cout << inter_vm_queues.size() << " bbdsf\n";
        inter_vm_queues.emplace_back();
        std::cout << inter_vm_queues.size();
        for(size_t j = 0; j < VM_NUMBER; j++){
            inter_vm_queues.back().emplace_back();
        }
    }

    // init virtual clocks
    virtual_clocks_ns.resize(VM_NUMBER, 0);

    std::cout << "done init inter vm frame queues \n";

    // init delays from delays config file

    std::cout << "delay file path " << argv[2] << std::endl;

    std::ifstream delays_file(argv[2]);
    std::string delays_line;
    while (std::getline(delays_file, delays_line)) {

        std::cout << "line of delay \n";

        std::vector<std::string> delays_strings = split_string(delays_line, ' ');
        inter_vm_delays_ms.emplace_back();
        for(std::string delay : delays_strings){
            inter_vm_delays_ms.back().push_back( std::stoi(delay) );
            std::cout << std::stoi(delay) << " ";
        }

        std::cout << std::endl;
    }

    std::cout << "end of delay loop " << std::endl;

    const char* NEGOCIATION_MSG = "{ \"execute\": \"qmp_capabilities\" }";
    const char* MSG_STOP = "{ \"execute\": \"stop\"}";
    const char* MSG_CONT = "{ \"execute\": \"cont\"}";
    const char* MSG_QUERY_STATUS = "{ \"execute\": \"query-status\"}";
    const char* MSG_GET_QEMU_CLOCK_VIRTUAL = "{ \"execute\": \"get-virtual-clock-ns\"}";

    // TODO remove once debug finished
    // const char* MSG_EXECUTE_10_s = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 10000 } }";
    // const char* MSG_EXECUTE_1_s = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 1000 } }";
    // const char* MSG_EXECUTE_500_ms = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 500 } }";
    // const char* MSG_EXECUTE_100_ms = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 100 } }";
    // const char* MSG_EXECUTE_10_ms = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 10 } }";
    // const char* MSG_EXECUTE_1_ms = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": 1 } }";

    simdjson::ondemand::parser parser;

    // init QMP connections
    for(int socket : qmp_sockets){
        send(socket, NEGOCIATION_MSG, strlen(NEGOCIATION_MSG), 0);

        read_line(socket); // wait for the cmd response and remove it from the buffer
    }

    // run initial time slice to speed up boot (and not stay stuck in GRUB somehow)
    for(int i = 0; i < VM_NUMBER; i++){
        int socket = qmp_sockets[i];
        std::string execute_msg = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": " + std::to_string( 10000 ) + " } }"; // TODO is ok to run 10 sec bc we are just booting and not acutally simulating anything yet ?
        send(socket, execute_msg.c_str(), strlen(execute_msg.c_str()), 0);

        // TODO clean ?
        std::string response = read_line(socket); // TODO clear buffer ?
        std::cout << "read from qmp socket " << response << std::endl;
        while(response.find("timestamp") == std::string::npos || response.find("STOP") == std::string::npos){
            response = read_line(socket);
            std::cout << "read from qmp socket " << response << std::endl;
        }

        send(socket, MSG_GET_QEMU_CLOCK_VIRTUAL, strlen(MSG_GET_QEMU_CLOCK_VIRTUAL), 0);

        response = read_line(socket);
        while(response.find("return") == std::string::npos || response.find("time") == std::string::npos){
            response = read_line(socket);
        }

        simdjson::padded_string padded_data2(response);
        simdjson::ondemand::document doc2 = parser.iterate(padded_data2);
        uint64_t virtual_end_time_ns = doc2["return"]["time"].get_uint64();

        virtual_clocks_ns[i] = virtual_end_time_ns;
    }


    struct timespec start_total_slice_time;
    start_total_slice_time.tv_sec = 0;
    start_total_slice_time.tv_nsec = 0;

    struct timespec end_total_slice_time;
    end_total_slice_time.tv_sec = 0;
    end_total_slice_time.tv_nsec = 0;

    const int BUFF_SIZE = 4096;
    

    while(true){

        std::cout << "starting loop\n";

        clock_gettime(CLOCK_MONOTONIC, &start_total_slice_time);

        // choose VM to schedule

        for(auto a : virtual_clocks_ns){
            std::cout << a << std::endl;
        }
        auto min_virtual_clock_it = std::min_element(std::begin(virtual_clocks_ns), std::end(virtual_clocks_ns));
        size_t current_vm_index = std::distance(std::begin(virtual_clocks_ns), min_virtual_clock_it);
        uint64_t min_virtual_clock = *min_virtual_clock_it;

        std::vector<uint64_t> durations_to_run_ns;
        for(size_t vm_index = 0; vm_index < VM_NUMBER; vm_index++){
            if(vm_index != current_vm_index || VM_NUMBER == 1){
                uint64_t duration = virtual_clocks_ns[vm_index] + inter_vm_delays_ms[vm_index][current_vm_index]*1e6 - min_virtual_clock;
                durations_to_run_ns.push_back( duration);
                // std::cout << duration << std::endl;
            }
        }



        const uint64_t duration_to_run_ns = *std::min_element(std::begin(durations_to_run_ns), std::end(durations_to_run_ns));
        
        const uint64_t end_of_time_slice_ns = min_virtual_clock + duration_to_run_ns;


        std::cout << "choosed vm to schedule " << current_vm_index << std::endl;
        // merge queues into one
        std::deque<FrameToDeliver> deque_to_deliver;

        bool there_is_a_frame_to_deliver = true;
        while(there_is_a_frame_to_deliver){
            
            std::optional<std::reference_wrapper<std::queue<FrameToDeliver>>> min_queue = std::nullopt;
            for(auto& q : inter_vm_queues[current_vm_index]){
                if(!q.empty()){
                    if(min_queue.has_value()){
                        if(q.front().delivery_timestamp_ns < min_queue.value().get().front().delivery_timestamp_ns){
                            min_queue = std::ref(q);
                        }
                    }
                    else {
                        if(q.front().delivery_timestamp_ns <= end_of_time_slice_ns){
                            min_queue = std::ref(q);
                        }
                    }
                }
            }

            if(min_queue.has_value()){
                // std::cout << "add a frame to deliver with timestamp " << min_queue.value().get().front().delivery_timestamp_ns << std::endl;
                deque_to_deliver.push_back( min_queue.value().get().front() );
                min_queue.value().get().pop();
            } else {
                there_is_a_frame_to_deliver = false;
            }

            
        }
        // run it, set select timeout...

        // std::cout << " queue to deliver size : " << deque_to_deliver.size() << std::endl;
        
        int qmp_socket = qmp_sockets[current_vm_index];
        int tap_fd = tap_fds[current_vm_index];

        
        // ask QMP virtual start time (useless, we can get the precedent time)
        // send(qmp_socket, MSG_GET_QEMU_CLOCK_VIRTUAL, strlen(MSG_GET_QEMU_CLOCK_VIRTUAL), 0);
        // std::string response = read_line(qmp_socket);
        // while(response.find("return") == std::string::npos || response.find("time") == std::string::npos){
        //     response = read_line(qmp_socket);
        // }

        
        // simdjson::padded_string padded_data(response);
        // simdjson::ondemand::document doc = parser.iterate(padded_data);
        // uint64_t virtual_start_time_ns = doc["return"]["time"].get_uint64();

        // get start time (before sending QMP command to be conservative and avoid sending packet once VM has paused, TODO check if this is a good idea)
        struct timespec start_exec_time;
        start_exec_time.tv_sec = 0;
        start_exec_time.tv_nsec = 0;
        clock_gettime(CLOCK_MONOTONIC, &start_exec_time);

        // send QMP time slice QMP cmd
        std::string execute_msg = "{ \"execute\": \"run-time-slice\", \"arguments\": { \"duration-ms\": " + std::to_string( (int)(duration_to_run_ns/1e6) ) + " } }";
        send(qmp_socket, execute_msg.c_str(), strlen(execute_msg.c_str()), 0);

        // std::cout << "sent execute msg\n";

        // listen for packets until getting stop QMP event using select

        bool has_got_stop_event = false;
        while(!has_got_stop_event){

            // create select
            int nfds;
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(qmp_socket, &fds);
            FD_SET(tap_fd, &fds);
            nfds = (qmp_socket > tap_fd ? qmp_socket : tap_fd) + 1;

            //  send dummy cmd to stress QMP to answer if time slice has probably ended (idk why it works)
            struct timespec select_time;
            select_time.tv_sec = 0;
            select_time.tv_nsec = 0;
            clock_gettime(CLOCK_MONOTONIC, &select_time);

            uint64_t select_time_diff_ms = ((select_time.tv_sec - start_exec_time.tv_sec) * 1e9 + (select_time.tv_nsec - start_exec_time.tv_nsec)) / 1e6;
            if (select_time_diff_ms > duration_to_run_ns/1e6){
                send(qmp_socket, MSG_GET_QEMU_CLOCK_VIRTUAL, strlen(MSG_GET_QEMU_CLOCK_VIRTUAL), 0);
            }

            struct timeval tv = {0, 500}; // sec, usec
            nfds = select(nfds, &fds, 0, 0, &tv);
            
            if (FD_ISSET(qmp_socket, &fds)) {

                std::string response = read_line(qmp_socket); // TODO clear buffer ?
                // std::cout << "read from qmp socket " << response << std::endl;
                if(response.find("timestamp") != std::string::npos && response.find("STOP") != std::string::npos){
                    has_got_stop_event = true;
                }
                else{
                    std::cout << "got qmp cmd : " << response << std::endl;
                }

                // if(response.find("timestamp") != std::string::npos && response.find("guest_finished_execution") != std::string::npos){
                //     has_got_stop_event = true;
                // }
            }

            if (FD_ISSET(tap_fd, &fds)) {
                struct timespec packet_time;
                packet_time.tv_sec = 0;
                packet_time.tv_nsec = 0;
                clock_gettime(CLOCK_MONOTONIC, &packet_time);

                uint64_t packet_virtual_time_ns = (packet_time.tv_sec - start_exec_time.tv_sec) * 1e9 + (packet_time.tv_nsec - start_exec_time.tv_nsec) + virtual_clocks_ns[current_vm_index];

                // std::cout << " received packet at time " << (double)packet_virtual_time_ns/1e6 << " ms" << std::endl;
                
                unsigned char recv_buff[BUFF_SIZE]; 
                bzero(recv_buff, BUFF_SIZE); 

                size_t len = read(tap_fd, recv_buff, sizeof(recv_buff));

                if(len >= 6){
                    std::string dest_mac = hexStr(recv_buff, 6);
                    // std::cout << "dest mac / len is " << dest_mac << " " << len << std::endl;
                
                    if (dest_mac == "ff:ff:ff:ff:ff:ff") {
                        // std::cout << "forward packet to " << dest_mac << std::endl;

                        
                        for(size_t i = 0; i < VM_NUMBER; i++){
                            
                            if(i != current_vm_index){
                                FrameToDeliver frame = {
                                    packet_virtual_time_ns + inter_vm_delays_ms[current_vm_index][i]*1e6,
                                    std::vector<uint8_t> (recv_buff, recv_buff + len),

                                };
                            
                                inter_vm_queues[i][current_vm_index].push(frame);
                            }
                        }

                    } 

                    if (mac_to_vm_index.count(dest_mac) > 0) { 
                        // std::cout << "forward packet to " << dest_mac << std::endl;

                        int dest_vm_index = mac_to_vm_index[dest_mac];
                        FrameToDeliver frame = {
                                packet_virtual_time_ns + inter_vm_delays_ms[current_vm_index][dest_vm_index]*1e6,
                                std::vector<uint8_t> (recv_buff, recv_buff + len),

                            };

                            inter_vm_queues[dest_vm_index][current_vm_index].push(frame);
                    }         
                }             
            }

            
            if(!deque_to_deliver.empty()){

                struct timespec current_ts;
                current_ts.tv_sec = 0;
                current_ts.tv_nsec = 0;
                clock_gettime(CLOCK_MONOTONIC, &current_ts);
                uint64_t current_virtual_time_estimation_ns = (current_ts.tv_sec - start_exec_time.tv_sec) * 1e9 + (current_ts.tv_nsec - start_exec_time.tv_nsec) + virtual_clocks_ns[current_vm_index];

                auto first_frame_to_not_send_it = std::upper_bound(deque_to_deliver.begin(), deque_to_deliver.end(), current_virtual_time_estimation_ns, FrameToDeliver::compare);
                
                for (auto it = deque_to_deliver.begin(); it != first_frame_to_not_send_it;  ++it )
                {
                    auto & frame = *it;

                    std::cout << "time is " << current_virtual_time_estimation_ns << ", sending frame with timestamp " << frame.delivery_timestamp_ns << std::endl;
                    if (write(tap_fds[current_vm_index], frame.data.data(), frame.data.size()) < 0){
                        perror("write error");
                    }
                    deque_to_deliver.pop_front();
                }
            }




        }

        // clock_gettime(CLOCK_MONOTONIC, &end_total_slice_time);

        // uint64_t total_slice_time_ns_2 = (end_total_slice_time.tv_sec - start_total_slice_time.tv_sec) * 1e9 + (end_total_slice_time.tv_nsec - start_total_slice_time.tv_nsec);

        // std::cout <<  "got stop signal at took " << (double)total_slice_time_ns_2 / 1e6 << " ms" << endl;

        // get qmp virtual time, compute compensation for next time

        send(qmp_socket, MSG_GET_QEMU_CLOCK_VIRTUAL, strlen(MSG_GET_QEMU_CLOCK_VIRTUAL), 0);

        std::string response = read_line(qmp_socket);
        while(response.find("return") == std::string::npos || response.find("time") == std::string::npos){
            response = read_line(qmp_socket);
        }

        simdjson::padded_string padded_data2(response);
        simdjson::ondemand::document doc2 = parser.iterate(padded_data2);
        uint64_t virtual_end_time_ns = doc2["return"]["time"].get_uint64();
        
        

        clock_gettime(CLOCK_MONOTONIC, &end_total_slice_time);

        uint64_t total_slice_time_ns = (end_total_slice_time.tv_sec - start_total_slice_time.tv_sec) * 1e9 + (end_total_slice_time.tv_nsec - start_total_slice_time.tv_nsec);

        std::cout << " VM virtual time advanced by " << (double)(virtual_end_time_ns - virtual_clocks_ns[current_vm_index])/1e6 << " ms,\\
        total time slice took " << (double)total_slice_time_ns / 1e6 << " ms,\\
        overhead is " << (double)total_slice_time_ns / (double)(virtual_end_time_ns - virtual_clocks_ns[current_vm_index]) * 100.0 - 100.0 << "%" << std::endl;

        virtual_clocks_ns[current_vm_index] = virtual_end_time_ns;
   }

	return 0;
}

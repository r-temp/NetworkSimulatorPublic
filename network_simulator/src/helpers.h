#include <unordered_map>
#include <vector>
#include <string>

std::unordered_map<int, std::string> qmp_socket_buffers; // <fd, data>

// copied from tun2.c
int tun_alloc(const char *dev)
{
	struct ifreq ifr;
	int fd;

	if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
		perror("open");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = IFF_TAP | IFF_NO_PI; // TAP instead of TUN
	if (*dev)
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);

	if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
		perror("ioctl");
		close(fd);
		return -1;
	}

	// strcpy(dev, ifr.ifr_name);
	return fd;
}

std::vector<std::string> split_string(std::string s, char del){

    std::vector<std::string> result;

    std::stringstream ss(s);

    std::string tmp;

   	// Splitting the str string by delimiter
    while (std::getline(ss, tmp, del)){
        result.push_back(tmp);
    }

    return result;
}

void clear_socket_buffer(int socket){
    const int BUFF_SIZE = 1024;
    char recv_buff[BUFF_SIZE]; 
    bzero(recv_buff, BUFF_SIZE); 

    struct pollfd pfd[1];
    pfd[0].fd = socket;
    pfd[0].events = POLLIN;

    int status = poll(pfd, 1, 500);
    while(status > 0){
        read(socket, recv_buff, sizeof(BUFF_SIZE));
        status = poll(pfd, 1, 500);
        // printf("status %d %s \n", status, recv_buff);
    }
}
std::string read_line(int socket){
    const int BUFF_SIZE = 1024;
    char recv_buff[BUFF_SIZE]; 
    bzero(recv_buff, BUFF_SIZE); 
    

    // if line in cache return it and remove it from cache
    // else read until there is a line in cache then remove it from cache and return it
    //
    // TODO debug this mess
    while(qmp_socket_buffers[socket].find("\n") == std::string::npos){
            bzero(recv_buff, BUFF_SIZE);
            read(socket, recv_buff, sizeof(BUFF_SIZE));
            // cout << "recv_buff " << recv_buff << endl;
            qmp_socket_buffers[socket].append(recv_buff);
            // cout << "updated qmp_socket_buffers[socket] " << qmp_socket_buffers[socket] << endl;
    }
    int res = qmp_socket_buffers[socket].find("\n");
    
    // cout << "res " << res << endl;

    std::string response = qmp_socket_buffers[socket].substr(0, res); // TODO check index is correct

    // cout << "response " << response << endl;

    qmp_socket_buffers[socket].erase(0,res+1);

    return response;
}

// from https://codereview.stackexchange.com/questions/78535/converting-array-of-bytes-to-the-hex-string-representation
constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

std::string hexStr(unsigned char *data, int len)
{
  std::string s( len * 3, ' ');
  for (int i = 0; i < len; ++i) {
    s[3 * i]     = hexmap[(data[i] & 0xF0) >> 4];
    s[3 * i + 1] = hexmap[data[i] & 0x0F];
    s[3 * i + 2] = ':';
  }
  s.pop_back();
  return s;
}
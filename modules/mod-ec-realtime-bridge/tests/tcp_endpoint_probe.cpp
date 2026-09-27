#include "TcpEndpointConnector.h"
#include <filesystem>
#include <iostream>

static std::size_t DescriptorCount()
{
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
        std::filesystem::directory_iterator()));
}

int main(int argc, char** argv)
{
    if (argc != 6) return 64;
    std::atomic<bool> running{std::string(argv[4]) == "1"};
    std::string error;
    std::size_t before = DescriptorCount();
    int successes = 0;
    for (int n = 0; n < std::stoi(argv[5]); ++n)
    {
        int fd = ECRealtimeBridge::ConnectResolvedTcp(argv[1], static_cast<uint16_t>(std::stoi(argv[2])),
            static_cast<uint32_t>(std::stoul(argv[3])), running, error);
        if (fd < 0) continue;
        if (!(fcntl(fd, F_GETFL) & O_NONBLOCK) || !(fcntl(fd, F_GETFD) & FD_CLOEXEC)) return 65;
        ++successes;
        close(fd);
    }
    if (DescriptorCount() != before) return 66;
    std::cout << "connections=" << successes << "; fd_leaks=0; last_error=" << error << '\n';
    return successes ? 0 : 2;
}

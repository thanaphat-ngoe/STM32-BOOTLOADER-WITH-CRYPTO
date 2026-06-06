#include "data-link-layer.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>

static int serial_fd = -1;

bool DLL_Open(const char* port) {
    serial_fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (serial_fd == -1) return false;
    fcntl(serial_fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(serial_fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    
    options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    options.c_cflag |= (CLOCAL | CREAD | CS8);     
    
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(INLCR | ICRNL | IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1; // Timeout 100ms
    tcsetattr(serial_fd, TCSANOW, &options);

    int status;
    ioctl(serial_fd, TIOCMGET, &status);
    status |= TIOCM_DTR;
    status |= TIOCM_RTS;
    ioctl(serial_fd, TIOCMSET, &status);
    
    DLL_Sleep_MS(100);
    tcflush(serial_fd, TCIOFLUSH);

    uint8_t discard[128];
    int flags = fcntl(serial_fd, F_GETFL, 0);
    fcntl(serial_fd, F_SETFL, flags | O_NONBLOCK);
    while(read(serial_fd, discard, sizeof(discard)) > 0);
    fcntl(serial_fd, F_SETFL, flags);

    return true;
}

void DLL_Close(void) {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
    }
}

int DLL_Read(uint8_t* buffer, uint32_t size) {
    if (serial_fd == -1) return -1;
    return read(serial_fd, buffer, size);
}

void DLL_Write(const uint8_t* data, uint32_t size) {
    if (serial_fd == -1) return;
    write(serial_fd, data, size);
}

uint64_t DLL_Get_Time_MS(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

void DLL_Sleep_MS(uint32_t ms) {
    usleep(ms * 1000);
}

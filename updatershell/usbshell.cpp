#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "api/usbcmd.hpp"
#include "usbshell.hpp"
#include "usbtransfer.hpp"

extern "C"
{
    #include "api/bootloader.h"
    #include "deviceinfo.h"
    #include "process.h"
}

using namespace std;

#define USB_FEATURE_SHELL 0x23
#define USB_RESULT_SUCCESS 0
#define USB_RESULT_ERROR -1

struct usb_shell_request {
    int cmd;
    char data[0xfff8];
};

struct usb_shell_response {
    int result;
};

void usbshell_loop()
{
    UsbCmd *cmd = new UsbCmd(USB_FEATURE_SHELL);
    UsbTransfer *transfer = new UsbSequenceTransfer(cmd);

    while (1) {
        usb_shell_request request;
        usb_shell_response response;
        transfer->read(&request, sizeof(request));

        if (request.cmd == *(int *) "TEST") {
            response.result = USB_RESULT_SUCCESS;
            transfer->write(&response, sizeof(response));
        } else if (request.cmd == *(int *) "INFO") {
            device_info info;
            int err = get_device_info(&info);
            response.result = !err ? USB_RESULT_SUCCESS : err;
            transfer->write(&response, sizeof(response));

            if (!err) {
                transfer->read(NULL, 0);
                transfer->write(&info, sizeof(info));
            }
        } else if (request.cmd == *(int *) "SHEL") {
            int fd_stdin, fd_stdout;
            const char *args[] = { "sh", "-i", NULL };
            int pid = popen2((char *const *) args, &fd_stdin, &fd_stdout);
            response.result = pid >= 0 ? USB_RESULT_SUCCESS : pid;
            transfer->write(&response, sizeof(response));

            if (pid >= 0)
                usb_transfer_socket(transfer, fd_stdin, fd_stdout);
        } else if (request.cmd == *(int *) "EXEC") {
            int fd_stdout;
            const char *args[] = { "sh", "-c", request.data, NULL };
            int pid = popen2((char *const *) args, NULL, &fd_stdout);
            response.result = pid >= 0 ? USB_RESULT_SUCCESS : pid;
            transfer->write(&response, sizeof(response));

            if (pid >= 0)
                usb_transfer_socket(transfer, 0, fd_stdout);
        } else if (request.cmd == *(int *) "PULL") {
            int fd = open(request.data, O_RDONLY);
            response.result = fd >= 0 ? USB_RESULT_SUCCESS : fd;
            transfer->write(&response, sizeof(response));

            if (fd >= 0)
                usb_transfer_read_fd(transfer, fd);
        } else if (request.cmd == *(int *) "BLDR") {
            int fd = open(BOOTLOADER_DEV, O_RDONLY);
            size_t num_blocks = bootloader_get_num_blocks();
            response.result = num_blocks;
            transfer->write(&response, sizeof(response));

            bootloader_block blocks[num_blocks];
            bootloader_get_blocks(fd, blocks);
            for (size_t i = 0; i < num_blocks; i++) {
                size_t l = bootloader_get_block_size(blocks + i);
                char block_buf[l];
                bootloader_read_block(fd, blocks + i, block_buf);
                usb_transfer_read_buffer(transfer, block_buf, l);
            }
            close(fd);
        } else if (request.cmd == *(int *) "EXIT") {
            response.result = USB_RESULT_SUCCESS;
            transfer->write(&response, sizeof(response));
            break;
        } else {
            response.result = USB_RESULT_ERROR;
            transfer->write(&response, sizeof(response));
        }
    }

    usleep(500e3);
    delete transfer;
    delete cmd;
}

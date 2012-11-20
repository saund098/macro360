#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "libusb.h"

#include <errno.h>

#define SPOOF_TIMEOUT 15
#define USB_REQ_TIMEOUT 1000

#define USB_DIR_IN 0x80
#define USB_DIR_OUT 0

#define REQTYPE_VENDOR (2 << 5)

#define BUFFER_SIZE 4096

#define ITFNUM 2

static int bexit = 0;

#define BAUDRATE B500000
static int fd = -1;

static int spoof = 0;
static int debug = 0;
static int libusb_debug = 0;
static int vid = 0x045e;
static int pid = 0x028e;

static libusb_device_handle *devh = NULL;
static libusb_context* ctx = NULL;

static char* serial_port = NULL;

typedef struct {
	unsigned char bRequestType;
	unsigned char bRequest;
	unsigned short wValue;
	unsigned short wIndex;
	unsigned short wLength;
} control_request_header;

typedef struct {
	control_request_header header;
	unsigned char data[BUFFER_SIZE];
} control_request;

void ex_program(int sig) {
	bexit = 1;
}

void catch_alarm(int sig) {
	bexit = 1;
	printf("spoof ko!\n");
	signal(sig, catch_alarm);
}

void fatal(const char *msg) {
	perror(msg);
	exit(1);
}

/*
 * Opens a usb_dev_handle for the first 360 controller found.
 */
static void usb_init_spoof() {
	if (libusb_init(&ctx)) {
		perror("libusb_init");
	}

	libusb_set_debug(ctx, libusb_debug);

	devh = libusb_open_device_with_vid_pid(ctx, vid, pid);

	if (!devh) {
		fatal("libusb_open_device_with_vid_pid");
	}

	if (libusb_reset_device(devh)) {
		fatal("libusb_reset_device");
	}
	if (libusb_detach_kernel_driver(devh, ITFNUM) < 0) {
		//fatal("libusb_detach_kernel_driver");
	}
	int i;
	for (i = 0; i < 4; ++i) {
		if (libusb_claim_interface(devh, i) < 0) {
			perror("usb_claim_interface");
		}
	}
}

/*
 * Opens ttyUSB0 for reading and writing.
 */
static void serial_port_init() {
	struct termios options;

	fd = open(serial_port, O_RDWR | O_NOCTTY | O_NDELAY/* | O_NONBLOCK*/);

	if (fd < 0) {
		fatal("open");
	}

	tcgetattr(fd, &options);
	cfsetispeed(&options, BAUDRATE);
	cfsetospeed(&options, BAUDRATE);
	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8;
	options.c_oflag &= ~OPOST;
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	options.c_iflag &= ~(IXON | IXOFF | IXANY);
	if (tcsetattr(fd, TCSANOW, &options) < 0) {
		printf("can't set serial port options\n");
		exit(-1);
	}

	tcflush(fd, TCIFLUSH);
}

static void usage() {
	fprintf(stderr, "Usage: usbspoof [-d] [-v vendor_id] [-p product_id] [-s serial_port]\n");
	exit(EXIT_FAILURE);
}

/*
 * Reads command-line arguments.
 */
static void read_args(int argc, char* argv[]) {
	int opt;

	while ((opt = getopt(argc, argv, "dv:p:s:")) != -1) {
		switch (opt) {
		case 'd':
			debug = 1;
			debug = 1;
			break;
		case 'v':
			vid = strtol(optarg, NULL, 16);
			break;
		case 'p':
			pid = strtol(optarg, NULL, 16);
			break;
		case 's':
			serial_port = optarg;
			break;
		default: /* '?' */
			usage();
			break;
		}
	}
}

int main(int argc, char *argv[]) {
	control_request creq;
	unsigned char* p_data = (unsigned char*) &creq.header;
	int ret;
	int i;

	read_args(argc, argv);

	if (!serial_port) {
		fprintf(stderr, "No serial port specified!\n");
		usage();
	}

	(void) signal(SIGINT, ex_program);

	usb_init_spoof();

	serial_port_init();

	signal(SIGALRM, catch_alarm);
	alarm(SPOOF_TIMEOUT);

	while (!bexit) {
		/*
		 * Get data from the serial port.
		 */
		if (read(fd, p_data, sizeof(*p_data)) == sizeof(*p_data)) {
			p_data++;
			/*
			 * Check if the header is complete.
			 */
			if (p_data >= (unsigned char*) &creq.header + sizeof(creq.header)) {
				/*
				 * Buffer overflow protection.
				 */
				if (creq.header.wLength > BUFFER_SIZE) {
					fatal("bad length");
				}

				/*
				 * No more data to wait for.
				 */
				if (creq.header.bRequestType & USB_DIR_IN) {
					if (debug) {
						printf("--> GET\n");
						printf(
								"bRequestType: 0x%02x bRequest: 0x%02x wValue: 0x%04x wIndex: 0x%04x wLength: 0x%04x\n",
								creq.header.bRequestType, creq.header.bRequest,
								creq.header.wValue, creq.header.wIndex,
								creq.header.wLength);
					}

					if (creq.header.wValue == 0x5b17) {
						spoof = 1;
						printf("spoof started\n");
					}

					if (!(creq.header.bRequestType & REQTYPE_VENDOR)) {
						if (debug) {
							printf("--> standard requests are not forwarded\n");
							printf("\n");
						}
						p_data = (unsigned char*) &creq.header;
						continue;
					}

					/*
					 * Forward the request to the 360 controller.
					 */
					ret = libusb_control_transfer(devh,
							creq.header.bRequestType, creq.header.bRequest,
							creq.header.wValue, creq.header.wIndex, creq.data,
							creq.header.wLength, USB_REQ_TIMEOUT);

					if (ret < 0) {
						printf(
								"libusb_control_transfer failed with error: %d\n",
								ret);
					} else {
						if (debug) {
							printf("read from controller: %d data: {", ret);
							for (i = 0; i < ret; ++i) {
								printf("0x%02hhx,", creq.data[i]);
							}
							printf("}\n");
						}

						unsigned char length[2];
						length[0] = ret & 0xFF;
						length[1] = ret >> 8;

						/*
						 * Forward the length and the data to the serial port.
						 */
						if (write(fd, &length, sizeof(length)) < (ssize_t)sizeof(length)
								|| write(fd, creq.data, ret) < ret) {
							perror("write");
						}
					}

					p_data = (unsigned char*) &creq.header;

					if (debug) {
						printf("\n");
					}
				}
				/*
				 * Check if data has to be waited for.
				 */
				else {
					if (debug) {
						printf("--> SET\n");
						printf(
								"bRequestType: 0x%02x bRequest: 0x%02x wValue: 0x%04x wIndex: 0x%04x wLength: 0x%04x\n",
								creq.header.bRequestType, creq.header.bRequest,
								creq.header.wValue, creq.header.wIndex,
								creq.header.wLength);
					}

					if (creq.header.wValue == 0x001e) {
						spoof = 1;
						printf("spoof successful\n");
						break;
					}

					ret = read(fd, p_data, creq.header.wLength);
					if (ret < creq.header.wLength) {
						printf("\nread error!! expected: %d received: %d\n\n",
								creq.header.wLength, ret);
					}

					if (ret) {
						if (debug) {
							for (i = 0; i < ret; ++i) {
								printf(" 0x%02x ", creq.data[i]);
							}
							printf("\n");
							printf(" data:");

							for (i = 0; i < ret; ++i) {
								printf(" 0x%02x", creq.data[i]);
							}
							printf("\n");
						}
					}

					if (!(creq.header.bRequestType & REQTYPE_VENDOR)) {
						if (debug) {
							printf("--> standard requests are not forwarded\n");
							printf("\n");
						}
						p_data = (unsigned char*) &creq.header;
						continue;
					}

					/*
					 * Forward the request to the 360 controller.
					 * No need to forward anything back to the serial port.
					 */
					ret = libusb_control_transfer(devh,
							creq.header.bRequestType, creq.header.bRequest,
							creq.header.wValue, creq.header.wIndex, creq.data,
							ret, USB_REQ_TIMEOUT);

					if (ret < 0) {
						printf(
								"libusb_control_transfer failed with error: %d\n",
								ret);
					}

					p_data = (unsigned char*) &creq.header;

					if (debug) {
						printf("\n");
					}
				}
			}
		}
	}
	printf("exiting\n");
	close(fd);
	libusb_close(devh);
	libusb_exit(ctx);
	if (bexit) {
		return -1;
	}
	return 0;
}

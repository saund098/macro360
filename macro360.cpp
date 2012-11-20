#include <err.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <sys/time.h>
#include <map>

#define BAUDRATE B500000

#define UP		0
#define DOWN	1
#define LEFT	2
#define RIGHT	3
#define START	4
#define BACK	5
#define LPRESS	6
#define RPRESS	7
#define LB		8
#define RB		9
#define XBOX	10
#define A		12
#define B		13
#define X		14
#define Y		15

typedef void (*macro)();

typedef struct {
	uint8_t type;
	uint8_t size;
	uint16_t buttons;
	uint8_t ltrigger;
	uint8_t rtrigger;
	uint16_t xaxis;
	uint16_t yaxis;
	uint16_t zaxis;
	uint16_t taxis;
	uint8_t unused[6];
} s_report_360;

static bool bexit = false;
static int fd = -1;
char* serial_port = NULL;
s_report_360 report = { 0x00, 0x14 };

static void usage(const char * name) {
	err(EXIT_FAILURE, "Usage: %s [-s serial_port]\n", name);
}

/*
 * Reads command-line arguments.
 */
static void read_args(int argc, char* argv[]) {
	int opt;

	while ((opt = getopt(argc, argv, "s:m:")) != -1) {
		switch (opt) {
		case 's':
			serial_port = optarg;
			break;
		case 'm':

		default: /* '?' */
			usage(argv[0]);
			break;
		}
	}
}

static void serial_port_init() {
	struct termios options;

	fd = open(serial_port, O_RDWR | O_NOCTTY | O_NDELAY/* | O_NONBLOCK*/);

	if (fd < 0) {
		err(EXIT_FAILURE, strerror(errno));
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

static inline void send_report() {
	if (write(fd, &report, sizeof(report)) < (ssize_t) sizeof(report)) {
		perror("write");
	}
}

static inline int usleep_action(int range, int offset) {
	int t = (rand() % range + offset) * 1000;
	usleep(t);
	return t;
}

static inline int sleep_action(int range, int offset) {
	int t = offset * 1000000 + (rand() % range) * 1000;
	usleep(t);
	return t;
}

#define SLEEP_BUTTON usleep_action(50,50) // 50-100 ms
#define SLEEP_FIRE usleep_action(250,250) // 250-500 ms
#define SLEEP_MISSILES usleep_action(25,75) // 150-175 ms
#define SLEEP_A_LITTLE usleep_action(250,250) // 250-500 ms
#define SLEEP_CHARGE_FIRE usleep_action(250,2000) // 2000-2250 ms
#define SLEEP_XBOX usleep_action(500,5000) // 5000-5500 ms
#define SLEEP_RAIN usleep_action(500,7000) // 7000-7500 ms
#define SLEEP_RESTART usleep_action(100,1250) // 1150-1250 ms
#define SLEEP_A usleep_action(100, 1000)
static inline void hold(int bitNum) {
	report.buttons |= (1 << bitNum);
	send_report();
}

static inline void release(int bitNum) {
	report.buttons &= ~(1 << bitNum);
	send_report();
}

static inline void press(int bitNum) {
	hold(bitNum);
	SLEEP_BUTTON;
	release(bitNum);
}

static inline void power_off() {
	hold(XBOX);
	SLEEP_XBOX;
	press(UP);
	SLEEP_A_LITTLE;
	press(UP);
	SLEEP_A_LITTLE;
	press(A);
}

static inline void fire(uint8_t *trigger) {
	*trigger = 0xFF;
	send_report();
	SLEEP_FIRE;
	*trigger = 0x00;
	send_report();
}

static inline void fire_missiles(uint8_t *trigger) {
	*trigger = 0xFF;
	send_report();
	SLEEP_MISSILES;
	*trigger = 0x00;
	send_report();
}

static inline void charge_fire(uint8_t *trigger) {
	*trigger = 0xFF;
	send_report();
	SLEEP_CHARGE_FIRE;
	*trigger = 0x00;
	send_report();
}

static inline void reload_checkpoint() {
	press(A);
	SLEEP_A_LITTLE;
	press(UP);
	SLEEP_A_LITTLE;
	press(A);
	SLEEP_RESTART;
}

void ex_program(int sig) {
	bexit = true;
	(void) signal(SIGINT, ex_program);
}

void level_macro() {
	for (int j = 0; j < 5; j++) {
		press(A);
		SLEEP_FIRE;
	}
	sleep_action(150, 40);
	press(A);
	sleep_action(150, 5);
	press(B);
	sleep_action(150, 5);
	press(B);
	sleep_action(150, 5);
}

void headshot_macro() {
	reload_checkpoint();
	press(START);
	SLEEP_A_LITTLE;
}

void oni_macro() {
	reload_checkpoint();
	SLEEP_A;
	press(START);
}

void delay_kill_macro() {
	reload_checkpoint();
	SLEEP_A;
	press(START);
}

macro get_macro(const char* macro_name) {
	if (strcmp(macro_name, "headshot_macro") == 0) {
		return headshot_macro;
	} else if (strcmp(macro_name, "level_macro") == 0) {
		return level_macro;
	} else if (strcmp(macro_name, "oni_macro") == 0) {
		return oni_macro;
	} else if (strcmp(macro_name, "delay_kill_macro") == 0) {
		return delay_kill_macro;
	} else {
		return NULL;
	}
}

static inline const char* curr_time() {
	time_t curr = time(NULL);
	char* str = asctime(localtime(&curr));
	char* c = str;
	while (*c != '\0') {
		switch (*c) {
		case '\r':
		case '\n':
		case '\0':
			*c = '\0';
			break;
		default:
			c++;
			break;
		}
	}
	return str;
}

static inline double tv_diff(timeval &t1, timeval &t2) {
	return (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
}

static inline void macro_wrapper(const char* macro_name, int count, bool turn_off) {
	macro macro_function = get_macro(macro_name);
	timeval start, t1, t2, end;

	if (macro_function != NULL) {
		printf("Starting %s at %s\n", macro_name, curr_time());
		gettimeofday(&start, NULL);
		if (strcmp(macro_name, "headshot_macro") == 0 || strcmp(macro_name, "delay_kill_macro") == 0) {
			report.rtrigger = 0xFF;
			send_report();
			SLEEP_FIRE;
			press(DOWN);
		}
		for (int i = 1; (!bexit && i <= count); i++) {
			gettimeofday(&t1, NULL);
			(*macro_function)();
			gettimeofday(&t2, NULL);
			printf("%d of %d -- %.02f seconds\n", i, count, tv_diff(t1, t2));
		}
		if (strcmp(macro_name, "headshot_macro") == 0 || strcmp(macro_name, "delay_kill_macro") == 0) {
			report.rtrigger = 0x00;
			send_report();
		}
		gettimeofday(&end, NULL);
		printf("Completed %s at %s (%.02f seconds)\n", macro_name, curr_time(),
				tv_diff(start, end));
		if(!bexit && turn_off) {
			power_off();
		}
	} else {
		err(EXIT_FAILURE, "%s does not exist", macro_name);
	}
}

int main(int argc, char *argv[]) {
	srand(time(NULL));

	read_args(argc, argv);

	if (!serial_port) {
		usage(argv[0]);
	}
	serial_port_init();

	(void) signal(SIGINT, ex_program);

	macro_wrapper("headshot_macro", 32000-12790, true);

	close(fd);

	printf("Finished!\n");
	return 0;
}

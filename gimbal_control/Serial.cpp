/*
 * Serial.cpp
 *
 *  Created on: Dec 7, 2016
 *      Author: kevin
 */

#include "Serial.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>

Serial::Serial(const char *uart_name_, int baudrate_) {
	initialize_defaults();
	uart_name = uart_name_;
	baudrate = baudrate_;
}

Serial::Serial() {
	initialize_defaults();
}

Serial::~Serial() {
	// destroy mutex
	pthread_mutex_destroy(&lock);
}

void Serial::initialize_defaults() {
	// Initialize attributes
	debug = false;
	fd = -1;
	status = SERIAL_PORT_CLOSED;

	uart_name = (char*) "/dev/ttyUSB0";
	baudrate = 115200;

	timeout.tv_sec = 0;
	timeout.tv_usec = 10000;

	// Start mutex
	int result = pthread_mutex_init(&lock, NULL);
	if (result != 0) {
		printf("\n mutex init failed\n");
		throw 1;
	}
}

// ------------------------------------------------------------------------------
//   Read from Serial
//
//	read_message method receives the data sent from SBGC and validates the received data
//	if the header checksum and the body checksum are correct the methode returns true
//	else the method prints the error
// ------------------------------------------------------------------------------
int Serial::read_message() {

	bool msgReceived = false;
	result = 0;
	// --------------------------------------------------------------------------
	//   READ FROM PORT
	// --------------------------------------------------------------------------

	// this function locks the port during read

	result = _read_port();

	int validate = _validate(result);
	if(!validate){
		result = validate;
	}
	//error handling
	switch(result)
	{
		case 0:
			printf("No Bytes to read available\n");
			return msgReceived;
		case -1:
			printf("The header checksum was incorrect\n");
			return msgReceived;
		case -2:
			printf("The body checksum was incorrect\n");
			return msgReceived;
		case -3:
			printf("The received data was not in the correct format\n");
			return msgReceived;
		default:
			msgReceived = true;
	}

	// Done!
	return msgReceived;
}

// ------------------------------------------------------------------------------
//   Write to Serial
// ------------------------------------------------------------------------------
int Serial::write_message(char buf[]) {

	// use default size length
	int len = DEFAULT_COMMAND_SIZE;
	// Write buffer to serial port, locks port while writing
	int bytesWritten = _write_port(	buf, len);


	return bytesWritten;
}

// ------------------------------------------------------------------------------
//   Open Serial Port
// ------------------------------------------------------------------------------
/**
 * throws EXIT_FAILURE if could not open the port
 */
int Serial::open_serial() {

	// --------------------------------------------------------------------------
	//   OPEN PORT
	// --------------------------------------------------------------------------
	printf("OPEN PORT\n");

	fd = _open_port(uart_name);

	FD_ZERO(&set);
	FD_SET(fd, &set);

	// Check success
	if (fd == -1) {
		printf("failure, could not open port.\n");
		throw EXIT_FAILURE;
	}

	// --------------------------------------------------------------------------
	//   SETUP PORT
	// --------------------------------------------------------------------------
	bool success = _setup_port(baudrate, 8, 1, false, false);

	// --------------------------------------------------------------------------
	//   CHECK STATUS
	// --------------------------------------------------------------------------
	if (!success) {
		printf("failure, could not configure port.\n");
		throw EXIT_FAILURE;
	}
	if (fd <= 0) {
		printf(
				"Connection attempt to port %s with %d baud, 8N1 failed, exiting.\n",
				uart_name, baudrate);
		throw EXIT_FAILURE;
	}

	// --------------------------------------------------------------------------
	//   CONNECTED!
	// --------------------------------------------------------------------------
	printf("Connected to %s with %d baud, 8 data bits, no parity, 1 stop bit (8N1), fd = %d\n",
			uart_name, baudrate, fd);

	status = true;

	return 0;

}

// ------------------------------------------------------------------------------
//   Close Serial Port
// ------------------------------------------------------------------------------
void Serial::close_serial() {
	printf("CLOSE PORT\n");

	int result = close(fd);

	if (result) {
		fprintf(stderr, "WARNING: Error on port close (%i)\n", result);
	}

	status = false;

	printf("\n");

}

// ------------------------------------------------------------------------------
//   Convenience Functions
// ------------------------------------------------------------------------------
int Serial::start() {
	int success = open_serial();
	if(success == 0){
		printf("starting serial port successful");
	}else{
		printf("failed to start serial port");
	}
	return success;
}

void Serial::stop() {
	close_serial();
}

// ------------------------------------------------------------------------------
//   Quit Handler
// ------------------------------------------------------------------------------
void Serial::handle_quit(int sig) {
	try {
		stop();
	} catch (int error) {
		fprintf(stderr, "Warning, could not stop serial port\n");
	}
}

// ------------------------------------------------------------------------------
//   Helper Function - Open Serial Port File Descriptor
// ------------------------------------------------------------------------------
// Where the actual port opening happens, returns file descriptor 'fd'
int Serial::_open_port(const char* port) {
	// Open serial port
	// O_RDWR - Read and write
	// O_NOCTTY - Ignore special chars like CTRL-C
	fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);

	// Check for Errors
	if (fd == -1) {
		/* Could not open the port. */
		return (-1);
	}

	// Finalize
	else {
		fcntl(fd, F_SETFL, 0);
	}

	// Done!
	return fd;
}

// ------------------------------------------------------------------------------
//   Helper Function - Setup Serial Port
// ------------------------------------------------------------------------------
// Sets configuration, flags, and baud rate
bool Serial::_setup_port(int baud, int data_bits, int stop_bits, bool parity,
		bool hardware_control) {
	// Check file descriptor
	if (!isatty(fd)) {
		fprintf(stderr, "\nERROR: file descriptor %d is NOT a serial port\n",
				fd);
		return false;
	}

	// Read file descritor configuration
	struct termios config;
	if (tcgetattr(fd, &config) < 0) {
		fprintf(stderr, "\nERROR: could not read configuration of fd %d\n", fd);
		return false;
	}

	// Input flags - Turn off input processing
	// convert break to null byte, no CR to NL translation,
	// no NL to CR translation, don't mark parity errors or breaks
	// no input parity check, don't strip high bit off,
	// no XON/XOFF software flow control
	config.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK
			| ISTRIP | IXON);

	// Output flags - Turn off output processing
	// no CR to NL translation, no NL to CR-NL translation,
	// no NL to CR translation, no column 0 CR suppression,
	// no Ctrl-D suppression, no fill characters, no case mapping,
	// no local output processing
	config.c_oflag &= ~(OCRNL | ONLCR | ONLRET | ONOCR | OFILL | OPOST);

#ifdef OLCUC
	config.c_oflag &= ~OLCUC;
#endif

#ifdef ONOEOT
	config.c_oflag &= ~ONOEOT;
#endif

	// No line processing:
	// echo off, echo newline off, canonical mode off,
	// extended input processing off, signal chars off
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);

	// Turn off character processing
	// clear current char size mask, no parity checking,
	// no output processing, force 8 bit input
	config.c_cflag &= ~(CSIZE | PARENB);
	config.c_cflag |= CS8;

	// One input byte is enough to return from read()
	// Inter-character timer off
	config.c_cc[VMIN] = 1;
	config.c_cc[VTIME] = 10; // was 0

	// Get the current options for the port
	////struct termios options;
	////tcgetattr(fd, &options);

	// Apply baudrate
	switch (baud) {
	case 1200:
		if (cfsetispeed(&config, B1200) < 0
				|| cfsetospeed(&config, B1200) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;
	case 1800:
		cfsetispeed(&config, B1800);
		cfsetospeed(&config, B1800);
		break;
	case 9600:
		cfsetispeed(&config, B9600);
		cfsetospeed(&config, B9600);
		break;
	case 19200:
		cfsetispeed(&config, B19200);
		cfsetospeed(&config, B19200);
		break;
	case 38400:
		if (cfsetispeed(&config, B38400) < 0
				|| cfsetospeed(&config, B38400) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;
	case 57600:
		if (cfsetispeed(&config, B57600) < 0
				|| cfsetospeed(&config, B57600) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;
	case 115200:
		if (cfsetispeed(&config, B115200) < 0
				|| cfsetospeed(&config, B115200) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;

		// These two non-standard (by the 70'ties ) rates are fully supported on
		// current Debian and Mac OS versions (tested since 2010).
	case 460800:
		if (cfsetispeed(&config, B460800) < 0
				|| cfsetospeed(&config, B460800) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;
	case 921600:
		if (cfsetispeed(&config, B921600) < 0
				|| cfsetospeed(&config, B921600) < 0) {
			fprintf(stderr,
					"\nERROR: Could not set desired baud rate of %d Baud\n",
					baud);
			return false;
		}
		break;
	default:
		fprintf(stderr,
				"ERROR: Desired baud rate %d could not be set, aborting.\n",
				baud);
		return false;

		break;
	}

	// Finally, apply the configuration
	if (tcsetattr(fd, TCSAFLUSH, &config) < 0) {
		fprintf(stderr, "\nERROR: could not set configuration of fd %d\n", fd);
		return false;
	}

	// Done!
	return true;
}

// ------------------------------------------------------------------------------
//   Read Port with Lock
// ------------------------------------------------------------------------------
int Serial::_read_port() {

	char buf[1000];
	int bytesReceived;
	// Lock
	pthread_mutex_lock(&lock);

	//returns the number of received bytes

	rv = select(fd +1, &set, NULL, NULL, &timeout);
	if(rv == -1){
		pthread_mutex_unlock(&lock); //this line of code is used to prevent a mutex deadlock
		return 0;
	}else if(rv == 0){
		pthread_mutex_unlock(&lock);
		return 0;
	}else{
		bytesReceived = read(fd, buf, 100);
	}

	for(uint16_t i = 0; i < 53; i++){
		msg[i] = buf[i];
	}


	// Unlock
	pthread_mutex_unlock(&lock);

	return bytesReceived;
}

// ------------------------------------------------------------------------------
//   Write Port with Lock
// ------------------------------------------------------------------------------
int Serial::_write_port(char *buf, unsigned len) {

	// Lock
	pthread_mutex_lock(&lock);

	// Write packet via serial link
	const int bytesWritten = static_cast<int>(write(fd, buf, len));


	// Wait until all data has been written
	tcdrain(fd);

	// Unlock
	pthread_mutex_unlock(&lock);

	return bytesWritten;
}

void Serial::delay(){
	usleep(2);
}
// ------------------------------------------------------------------------------
//   Validates whether the response has the correct format
// ------------------------------------------------------------------------------
int Serial::_validate(int result) {
	if (msg[0] == 0x3E) {
		int i = 1;
		bool hcs = _header_checksum(msg[1], msg[2]) == msg[3] ? true : false;
		bool bcs = _body_checksum(result);
		if (!hcs) {
			i = -1;
		}
		if (!bcs) {
			i = -2;
		}
		return i;
	} else {
		return -3;
	}
}

// ------------------------------------------------------------------------------
//   Helper function - Calculates the header checksum
// ------------------------------------------------------------------------------
int Serial::_header_checksum(char cmd_id, char data_size){
	int sum = cmd_id + data_size;
	int checksum = sum % 256;
	return checksum;
}

// ------------------------------------------------------------------------------
//   Helper function - Checks the body checksum
//	 if you dont understand this, you should be flipping burgers instead
// ------------------------------------------------------------------------------
int Serial::_body_checksum(int result){
	int lengthOfHeader = sizeof(uint32_t);
	int checksum = msg[2] + lengthOfHeader;
	return checksum == result ? true : false;
}


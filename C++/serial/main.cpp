#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ios>
#include <sstream>

#define INPUT_BUF_SZ 1024

bool run = true;

const char *outFile = NULL, *serialFile = NULL;
bool haveStdout = false, modeHex = false, showTimestamp = false;

//! Helper function to parse command-line arguments.
bool parseArgs(int argc, char **args) {
	int opt;
	bool result = true;
	
	while ((opt = getopt(argc, args, "i:m:o:t")) != -1) {
		switch (opt) {
			case 'i':
				serialFile = optarg;
				break;
			case 'm':
				haveStdout = ('0' != *optarg);
				modeHex = ('2' == *optarg);
				break;
			case 'o':
				outFile = optarg;
				break;
			case 't':
				showTimestamp = true;
				break;
			default:
				result = false;
				break;
		}
	}
	
	if (result) {
		if (!serialFile) {
			printf("Missing '-i' argument.\n");
			result = false;
		}
		
		if (!haveStdout && !outFile) {
			printf("No output is set to console and/or file.\n");
			result = false;
		}
	}
	
	if (!result) {
		printf("Usage:\t%s ", args[0]);
		printf("<-i target serial file> <-m 0=off (default), 1=ascii, 2=hex> [-o raw output file] [-t]\n");
	}
	
	return result;
}

int main(int argc, char **args) {
	bool result = parseArgs(argc, args);
	
	std::ofstream ostrm;
	uint8_t *dataR = NULL;
	int fd = -1;
	
	if (result) {
		dataR = (uint8_t*) malloc(INPUT_BUF_SZ);
		fd = open(serialFile, O_RDWR);
		
		if (!dataR) {
			printf("Error allocating read data buffer.\n");
			result = false;
		}
		if (fd == -1) {
			printf("Error opening serial port for R/W: %s\n", strerror(errno));
			result = false;
		}
	}
	
	if (result && outFile) {
		ostrm.open(outFile, std::ios::out | std::ios::binary);
		if (!ostrm) {
			printf("Error opening raw output file '%s'.", outFile);
			result = false;
		}
	}
	
	if (result) {													//Ctrl-C and 'kill' interrupt handler
		struct sigaction sigProp;
		
		sigemptyset(&sigProp.sa_mask);
		sigProp.sa_handler = [](int code) {
			printf("Got interrupt '%d'.\n", code);
			run = false;
		};
		sigProp.sa_flags = 0;
		
		if (sigaction(SIGINT, &sigProp, nullptr)) {
			printf("Error setting up interrupt handler.\n");
			result = false;
		}
	}
	
	if (result) {
		termios ttycfg;
		
		if (tcgetattr(fd, &ttycfg)) {
			printf("Error getting serial file access config: %s\n", strerror(errno));
			result = false;
		}
		else {
			ttycfg.c_cflag &= ~PARENB;								//no parity
			ttycfg.c_cflag &= ~CSTOPB;								//1 stop bit
			ttycfg.c_cflag &= ~CSIZE;								//clear at set to 8-bit data
			ttycfg.c_cflag |= CS8;
			ttycfg.c_cflag &= ~CRTSCTS;								//no HW flow control
			ttycfg.c_cflag |= (CREAD | CLOCAL);						//ignore signal line
			
			ttycfg.c_lflag &= ~ICANON;								//non-canonical mode
			ttycfg.c_lflag &= ~(ISIG | ECHO | ECHOE | ECHONL);		//disable signal chars
			
			ttycfg.c_iflag &= ~(IXON | IXOFF | IXANY);				//no SW flow control
			ttycfg.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);	//no special RX char handling
			
			ttycfg.c_oflag &= ~(OPOST | ONLCR);						//no special TX char handling
			
			ttycfg.c_cc[VTIME] = 1;									//wait for data with 1sec timeout
			ttycfg.c_cc[VMIN] = 0;
			
			cfsetspeed(&ttycfg, B115200);
			
			if (tcsetattr(fd, TCSANOW, &ttycfg) ) {
				printf("Error setting serial file access config: %s\n", strerror(errno));
				result = false;
			}
		}
	}
	
	if (result) {
		std::ostringstream strm;
		if (modeHex) {
			strm << std::hex << std::uppercase;
		}
		
		timespec delay = {.tv_sec = 0, .tv_nsec = 100'000'000}, ts0, ts1;
		timespec_get(&ts0, TIME_UTC);
		
		while (run) {
			const ssize_t count = read(fd, dataR, INPUT_BUF_SZ);
			
			if (count > 0) {
				for (ssize_t i = 0; i < count; ++i) {
					const uint8_t val = (~dataR[i] & 0x7f);
					
					if (haveStdout) {
						if (modeHex) {
							strm << std::setw(2) << std::setfill('0') << (uint32_t) val << ' ';
						}
						else {
							strm << val;
						}
					}
					
					if (outFile) {
						ostrm << val;
					}
				}
				
				if (haveStdout) {
					if (showTimestamp) {
						timespec_get(&ts1, TIME_UTC);
						const double diffMs = (ts1.tv_sec + ts1.tv_nsec * 1e-9) -
							(ts0.tv_sec + ts0.tv_nsec * 1e-9);
						ts0 = ts1;
						
						if (modeHex) {
							printf("%7.4f: %s\n", diffMs, strm.str().c_str());
						}
						else {
							printf("%7.4f: %s", diffMs, strm.str().c_str());
						}
					}
					else {
						if (modeHex) {
							puts(strm.str().c_str());
						}
						else {
							printf("%s", strm.str().c_str());
						}
					}
					
					strm.str("");
				}
			}
			else if (( count < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
				printf("Error reading serial file: %s\n", strerror(errno));
			}
			else {
				nanosleep(&delay, NULL);
			}
		}
	}
	
	if (dataR) {
		free(dataR);
	}
	
	if (fd != -1) {
		close(fd);
	}
	
	return result ? 0 : -1;
}

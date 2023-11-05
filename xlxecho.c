/*
    XLX Echo
    Copyright (C) 2023 Nuno Silva

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h> 
#include <stdlib.h>
#include <signal.h>
#include <unistd.h> 
#include <string.h> 
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <sys/ioctl.h>

#define RCD_MAX_TIME 240
#define RCD_DELAY 3
#define XLX_PORT 10002
#define BUFSIZE 2048
//#define DEBUG_SEND
//#define DEBUG_RECV

int main(int argc, char **argv)
{
	int sockfd;
	fd_set fds; 
	struct sockaddr_in addr;
	struct sockaddr_in rx;
	struct timeval tv;
	socklen_t l = sizeof(addr);
	int rxlen;
	uint8_t buf[BUFSIZE];
	char peername[8];
	uint16_t streamid = 0;
	const uint8_t header[4] = {0x44,0x53,0x56,0x54};
	uint8_t rcd_hdr[56];
	uint8_t rcd_frms[50*RCD_MAX_TIME][46];
	int rcd_fcnt = 0;
	time_t rcd_endt = 0;

	if(argc != 3){
		fprintf(stderr, "Usage: xlxecho [PeerName] [ListenIP]\n");
		return 0;
	}

	memset(peername, ' ', 8);
	memcpy(peername, argv[1], (strlen(argv[1])<8)?strlen(argv[1]):8);
		
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket\n");
		return 0;
	}

	memset((char *)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(XLX_PORT);
	addr.sin_addr.s_addr = inet_addr(argv[2]);
	if ( bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1 ) {
		fprintf(stderr, "error while binding the socket on port %d\n", XLX_PORT);
		return 0;
	}

	printf("Listening on %s:%d\n", inet_ntoa(addr.sin_addr), XLX_PORT);

	memset((char *)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(XLX_PORT);
	
	while (1) {
		FD_ZERO(&fds);
		FD_SET(sockfd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 100*1000;
		rxlen = 0;
		if (select(sockfd + 1, &fds, NULL, NULL, &tv) > 0) {
			rxlen = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *)&rx, &l);
		}
#ifdef DEBUG_RECV
		if(rxlen){
			fprintf(stderr, "RECV: ");
			for(int i = 0; i < rxlen; ++i)
				fprintf(stderr, "%02x ", buf[i]);
			fprintf(stderr, "\n");
			fflush(stderr);
		}
#endif
		if((rxlen == 39) && (buf[0] == 'L')){ //connect
			buf[0] = 'A';
			memcpy(&buf[1], peername, 8);
			buf[9] = 2;
			buf[10] = 5;
			buf[11] = 0;
			memset(&buf[13], 0, 25);
			buf[38] = 0x00;
			sendto(sockfd, buf, 39, 0, (const struct sockaddr *)&rx, sizeof(rx));
#ifdef DEBUG_SEND
			fprintf(stderr, "SEND: ");
			for(int i = 0; i < 39; ++i)
				fprintf(stderr, "%02x ", buf[i]);
			fprintf(stderr, "\n");
			fflush(stderr);
#endif
		}
		if(rxlen == 9){ //keep-alive
			memcpy(buf, peername, 8);
			buf[8] = 0x00;
			sendto(sockfd, buf, 9, 0, (const struct sockaddr *)&rx, sizeof(rx));
#ifdef DEBUG_SEND
			fprintf(stderr, "SEND: ");
			for(int i = 0; i < 9; ++i)
				fprintf(stderr, "%02x ", buf[i]);
			fprintf(stderr, "\n");
			fflush(stderr);
#endif
		}
		if((rxlen == 56) && (!memcmp(&buf[0], header, 4))) { //dv header
			uint16_t s = (buf[12] << 8) | (buf[13] & 0xff);
			if(s != streamid){
				streamid = s;
				memcpy(&addr, &rx, sizeof(addr));
				memcpy(rcd_hdr, buf, 56);
				rcd_fcnt = 0;
				rcd_endt = 0;
			}
		}
		if((rxlen == 27)||(rxlen == 45)){ //dv frame
			uint16_t s = (buf[12] << 8) | (buf[13] & 0xff);
			if(s == streamid){
				if (rcd_fcnt < 50*RCD_MAX_TIME) {
					rcd_frms[rcd_fcnt][0] = rxlen;
					memcpy(&rcd_frms[rcd_fcnt++][1], buf, rxlen);
				}
				//if ((buf[14] & 0x40) != 0) //last frame
					rcd_endt = time(NULL);
			}
		}
		if (rcd_endt && (time(NULL)-rcd_endt > RCD_DELAY)) { //playback
			rcd_endt = 0;
			streamid = 0;
#ifdef DEBUG_SEND
			fprintf(stderr, "Starting playback of %d frames...\n", rcd_fcnt);
			fflush(stderr);
#endif

			for (int i = 0; i < 5; i++)
				sendto(sockfd, rcd_hdr, 56, 0, (const struct sockaddr *)&addr, sizeof(addr));
			
			struct timespec nanos;
			int64_t nowus, trgus = 0;
			for (int i = 0; i < rcd_fcnt; i++) {
				sendto(sockfd, &rcd_frms[i][1], rcd_frms[i][0], 0, (const struct sockaddr *)&addr, sizeof(addr));

				clock_gettime(CLOCK_MONOTONIC, &nanos);
				nowus = (int64_t)nanos.tv_sec * 1000000 + nanos.tv_nsec / 1000;

				if (abs(trgus - nowus) > 1000000)
					trgus = nowus;
				trgus += 20000;

				if (trgus > nowus) {
					nanos.tv_sec = (trgus - nowus) / 1000000;
					nanos.tv_nsec = (trgus - nowus) % 1000000 * 1000;
					while (nanosleep(&nanos, &nanos) == -1 && errno == EINTR);
				}
			}

#ifdef DEBUG_SEND
			fprintf(stderr, "Finished playback.\n");
			fflush(stderr);
#endif
		}
	}
}

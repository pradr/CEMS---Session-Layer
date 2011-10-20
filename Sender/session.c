#include<openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/sockios.h>
#include "session.h"
#include "transport_l.h"
#include "timer.h"

/* Prints log messages in to a file*/
void log_mesg(char *str, ...) {
        FILE * fp=fopen("error_logs.txt","a+");

        fprintf(fp,"SESSION : ");
        va_list arglist;
        va_start(arglist,str);
        vfprintf(fp,str,arglist);
        //printf(str, arglist);
        va_end(arglist);
        fprintf(fp," \n");

        fclose(fp);
}

/* Returns the minimum of two given values */
uint32_t min_value(uint32_t value1, uint32_t value2) {

	if (value1 < value2) {
		return value1; 
	} else {
		return value2;
	}
}

/* Creates a UDP socket */
int create_connection () {
        int socket_t;

        socket_t = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        struct timeval timeout;      
        timeout.tv_sec = 20;
        timeout.tv_usec = 0;

	/* Receive on the socket is socket is generally blocking. To make it unblocking set the socket options to timeout on the receive call. */
        if (setsockopt (socket_t, SOL_SOCKET , SO_RCVTIMEO, (char *)&timeout,sizeof(timeout)) < 0){
                log_mesg("setsockopt error\n");
        }

        if (socket_t == -1) {
                log_mesg("Socket create failed.") ;
        }
	
	return socket_t;
}

/* Build a hello request packet for the session */
void build_pkt_hello_request(session_t *session, char *packet) {
	hello_pkt hpkt_t = {0};

	hpkt_t.type = PKT_TYPE_HELLO_REQUEST;
	hpkt_t.session_id = session->session_id;
        hpkt_t.port = 0;
        bzero(hpkt_t.host_name, sizeof(hpkt_t.host_name));
        memcpy(&hpkt_t.host_name, session->host_name, session->host_name_len);

        log_mesg("Building Hello for session %d\n", session->session_id);
	memcpy(packet, &hpkt_t, PKT_SIZE_HELLO);
}

/* Build a handshake packet */
void build_pkt_handshake (session_t *session, char *packet) {
	handshake_pkt hspkt_t;

	hspkt_t.type = PKT_TYPE_HANDSHAKE;
	hspkt_t.session_id = session->session_id;
	hspkt_t.no_of_packets = session->no_of_packets;
        bzero(hspkt_t.file_name, sizeof(hspkt_t.file_name));
        memcpy(hspkt_t.file_name, session->file_name, sizeof(hspkt_t.file_name));

        log_mesg("Building Handshake for session %d\n", session->session_id);
	memcpy(packet, &hspkt_t, PKT_SIZE_HANDSHAKE);
}
	
/* Build a data packet */
void build_pkt_data (session_t *session, char *data, char *packet) {
	data_pkt dpkt_t;

        dpkt_t.type = PKT_TYPE_DATA;
        dpkt_t.session_id = session->session_id;
	dpkt_t.length = min_value(MAX_FRAME_SIZE, session->no_of_bytes);
	dpkt_t.seq_num = session->next_frame_number;
	data += session->next_frame_number * MAX_FRAME_SIZE;
        memcpy(&dpkt_t.data, data, dpkt_t.length); 
	
        log_mesg("Buildinf DATA packet Type : %d Session : %d Length %d Seq_num %d Data pointer %d\n", dpkt_t.type, dpkt_t.session_id,
                                            dpkt_t.length, dpkt_t.seq_num, session->next_frame_number * MAX_FRAME_SIZE);
        memcpy(packet, &dpkt_t, PKT_SIZE_DATA);
}

/* Build a finish packet */
void build_pkt_finish (session_t *session, char *packet) {
        fin_pkt fpkt_t;

        fpkt_t.type = PKT_TYPE_FINISH;
        fpkt_t.session_id = session->session_id;
        fpkt_t.port = session->port;
        
        log_mesg("Building Finish for session %d\n", session->session_id);
	memcpy(packet, &fpkt_t, PKT_SIZE_FINISH);
}

/* Timeout function. 
 * 
 * Called repeatedly if the ack is not received to  resend the packet */
void session_packet_timeout(void *ptr) {
        timer_data *data;
	int error;
        data = (timer_data *) ptr;
	while(1) {	
                /* Hardcoded value for timer. We should change this */
		sleep(3);
                transport_intf_flap();
		log_mesg("Timeout going to send data on socket => %d size => %d\n", data->session->socket, data->packet_len);
	        transport_send_packet(data->session->socket, data->packet, data->packet_len, &error, &data->session->address, data->session->address_len);
	}
}


/* This function takes care of delivering the packet to the peer. 
 * 1. Sends a packet.
 * 2. Start a timer to resend the packet.
 * 3. Return the timer to the caller. If the received packet is proper, the timer is destroyed. 
 */

task session_send_packet (session_t *session, char *send_packet, int send_packet_len, int *error) {
	
	task task_t;

        /* Send the packet , Error handling should be taken care*/
	transport_send_packet(session->socket, send_packet, send_packet_len, error, &session->address, session->address_len);

	/* Create the timer */
	task_t.data = malloc(sizeof(timer_data *));
        task_t.data->session = session;
        task_t.data->packet = send_packet;
        task_t.data->packet_len = send_packet_len;
        task_t.timer_t = create_new_timer (&session_packet_timeout, task_t.data);
	
        return task_t;
};	


int session_receive_packet (session_t *session, char *recv_packet, int *recv_packet_len, int *error) {
	
        struct sockaddr_in address;
        int address_len = sizeof(address);
        int result;

	/* Receive a packet */
        result = transport_recv_packet(session->socket, recv_packet, recv_packet_len, error, &address, &address_len);

        /* Interesting check 
         *
         * We might have returned because of the socket timeout. 
         * Check if we have wirelss connection. If we have it and not receiving the connection,
         * then the receiver might have crashed. We need to restart the session from the scratch.
         *
         * Let us tell this to the caller. They will take care of it
         */

        if (result == -1) {
                if (check_wireless_connectivity(session)) {
                        return WIRELESS_DOWN;
                }
                return SERVER_CRASH;
        }

        return PROPER_PACKET;
}

/*
 * 1. Send a hello request packet.
 * 2. Once the response is received, update the port number.
 */
int learn_new_port(session_t *session) {

        /* Holders for the hello request and the response packets */
	char hello_pkt_req[PKT_SIZE_HELLO];
	char hello_pkt_resp[PKT_SIZE_HELLO];
	hello_pkt *hpkt_t;
        
        /* Parameters for the transport calls */
        int recv_packet_len;
	int error;

        /* To hold the timer from the session send */
        task task_t;

        /* To hold the return value from the transport receive */
        int ret = 0;
	
        /* Build the hello request packet */
        build_pkt_hello_request(session, hello_pkt_req);

	/* Send a hello request until a response packet is received */
        task_t = session_send_packet(session, hello_pkt_req, PKT_SIZE_HELLO, &error);

        while (1) {
        
                ret = session_receive_packet(session, hello_pkt_resp, &recv_packet_len, &error);
        
                /* Check the received packet */
                if (ret == PROPER_PACKET) {
                        hpkt_t = (hello_pkt *) hello_pkt_resp;
                        if (hpkt_t->type == PKT_TYPE_HELLO_RESPONSE) { 

                                log_mesg("MY SESSION : %d SERVER SESSION %d\n", session->session_id, hpkt_t->session_id);
                                
                                /* Expected packet. Update ourselves with the new port number given by the peer */
                                session->port = hpkt_t->port;
                                delete_timer(task_t.timer_t);
                                break;
                        } else {
                                /* Not a response packet */
                                continue;
                        }
                } else if (ret == WIRELESS_DOWN) {
                        log_mesg("Wireless is down.. I am going to receive again \n");
                        continue;
                } else if (ret == SERVER_CRASH) {
                        delete_timer(task_t.timer_t);
                        return -1;
                }
        }
}

/* Build the data structure for the current session */
void session_build_parameters (char *ip, char *host_name, session_t *session) {

        /* Assign the host name */
        session->host_name = host_name;
        session->host_name_len = strlen (session->host_name);

	 /* Assign the current session id */
        session->session_id = SESSION_ID;
        log_mesg("Building %s %d\n", session->host_name, session->session_id);
         
        /* Initially we will send a hello packet to the common port number and the given IP */
        session->port = COMMON_CONNECT_PORT;

        /* Create the connection */
        session->socket = create_connection();
        memset((char *) &session->address, 0, sizeof(session->address));
        session->address.sin_family = AF_INET;
        session->address.sin_addr.s_addr = inet_addr(ip);
        session->address.sin_port = htons(session->port);
        session->address_len = sizeof(session->address);
} 



/* Send handshake packet and wait for response */
int session_handshake(session_t *session) {
	
        /* Holders for the handshake request and the response packets */
	char hs_pkt[PKT_SIZE_HANDSHAKE];
        char hs_ack[PKT_SIZE_HANDSHAKE];
        handshake_pkt *hspkt_t;

        int recv_packet_len;
        int error;

        task task_t;
        int ret = 0;

        /* Build the handshake request packet */
        build_pkt_handshake(session, hs_pkt);

        /* Send a handshake packet until a response packet is received */
        task_t = session_send_packet(session, hs_pkt, PKT_SIZE_HANDSHAKE, &error);

        while (1) {
                ret = session_receive_packet(session, hs_ack, &recv_packet_len, &error);

                if (ret == PROPER_PACKET) {
                        /* Check the received packet */
                        hspkt_t = (handshake_pkt *) hs_ack;
                        if (hspkt_t->type == PKT_TYPE_HANDSHAKE_ACK) {
                                delete_timer(task_t.timer_t);
                                break;
                                //session->no_of_packets -= session->next_frame_number;
                        } else {
                                /* Not a response packet. Send the request again */
                                continue;
                        }
                } else if (ret == WIRELESS_DOWN) {
                        continue;
                } else if (ret == SERVER_CRASH) {
                        delete_timer(task_t.timer_t);
                        return -1;
                }
        }

        return 1;
}

/* Used to build and send any type of packet as a part of a session */
int  session_send_data(session_t *session, char *data) {

        /* Holders for the Data send and the ack packets */
	char d_pkt[PKT_SIZE_DATA];
        char d_ack[PKT_SIZE_DATA_ACK];
	data_ack_pkt *d_ack_t;

        int recv_packet_len;
        int error;

        task task_t;
        int ret = 0;

	while (session->no_of_packets) {

		build_pkt_data(session, data, d_pkt);

                /* Send the data packet. Size = header + data */
                task_t = session_send_packet(session, d_pkt, 11 + min_value(MAX_FRAME_SIZE, session->no_of_bytes),  &error);
	
                while (1) {
                        ret = session_receive_packet(session, d_ack, &recv_packet_len, &error);

                        if (ret == PROPER_PACKET) {
                                d_ack_t = (data_ack_pkt *) d_ack;

                                /* Check of we got the correct packet type and the expected sequence sequence number */
                                if (d_ack_t->type == PKT_TYPE_DATA_ACK && 
                                                d_ack_t->exp_frame_number == session->next_frame_number + 1) {

                                        log_mesg("Expected ACK %d\n", d_ack_t->exp_frame_number);
                                
                                        /* One packet sent properly */
                                        session->no_of_packets--;
                                        session->no_of_bytes-=MAX_FRAME_SIZE;

                                        /* Update the sequence number */
                                        session->next_frame_number++;

                                        delete_timer(task_t.timer_t);
                                        break;

                                } else {
                                        continue;
                                }
                        } else if (ret == WIRELESS_DOWN) {
                                continue;
                        } else if (ret == SERVER_CRASH) {
                                delete_timer(task_t.timer_t);
                                return -1;
                        }
                }
	}

        return 0;
}

/* Once data transfer is over send the finish packet to the common port at the server */
int session_finish(session_t *session) {

        /* Holders for the Finish send and the ack packets */
        char f_pkt[PKT_SIZE_FINISH];
        char f_ack[PKT_SIZE_FINISH];
        fin_pkt *fpkt_t;

        int recv_packet_len;
        int error;

        int ret = 0;
        task task_t;


        /* Build the finish packet packet */
        build_pkt_finish(session, f_pkt);

        task_t = session_send_packet(session, f_pkt, PKT_SIZE_FINISH, &error);

        if (ret == -1) {
                printf("Error in Finish send\n");
                return -1;
        }

        while (1) {

                ret = session_receive_packet(session, f_ack, &recv_packet_len, &error);
                /* Check the received packet */
                if (ret == PROPER_PACKET) {
                        fpkt_t = (fin_pkt *) f_ack;
                        if (fpkt_t->type == PKT_TYPE_FINISH_ACK) {
                                delete_timer(task_t.timer_t);
                                break;
                        } else {
                                /* Not a finish packet. Send the packet again */
                                continue;
                        }
                } else if (ret == WIRELESS_DOWN) {
                        continue;
                } else if (ret == SERVER_CRASH) {
                        delete_timer(task_t.timer_t);
                        return -1;
                }
        }

        return 1;
}

/* 
 * 1. Handshake
 * 2. Send the data
 * 3. Finish the connection
 *
 * Return 0 on SUCCESS
 *
 * Return -1 on FAILURE
 */
int session_handle(session_t *session, char *data) {

        int ret;
        
        ret = session_handshake(session);
        if (ret == -1) {
                return -1;
        }

	ret = session_send_data(session, data);
        if (ret == -1) {
                return -1;
        }

        ret = session_finish(session);
        if (ret == -1) {
                return -1;
        }

        return 0;
}

/*
 * 1.Learn the port number through which the session will send the data.
 * 2.Spawn a new thread to send the data and give the control back to the above layer
 */ 
void *session_transfer(void *ptr) {

	/* Extract the data from the above layer */
	session_data *s_data;
	s_data = (session_data *) ptr;


        /* Variables to loop through all the files in a folder */
        DIR *dir;
        FILE *fp;
        char file_name[100] = {0}; // To store each file name.
        char *data; // To store each files data.
        int file_length = 0; // each file length.
        struct dirent *ent;
        int count = 0;// Numbe of files in the folder.

        /* variables to check if we have data for 30 minutes */
        int timeout = 0;
        int file_null = 0;

        /* For executing shell commands */
        char command[100] = {0};

        /* Variable to check if the file was sent properly */
        int file_sent;

        while (1) {

                /* Allocate the session */ 
                session_t session;
                                                
                /* Fill the session structure */
                session_build_parameters(s_data->ip, s_data->host_name, &session);

                /* Send the hello packets to get the port number */
                int ret = 0;
                ret = learn_new_port(&session);

                if (ret == -1) {
                        printf("No port assigned.\n");
                        continue;
                }

                /* Change the port number of the address since the new port will be used for further communication */
                session.address.sin_port = htons(session.port);

                log_mesg("SESSION connected to Port %d\n", session.port);

                while (1) {

                        //sleep(SESSION_SLEEP); // Sleep for 5 mins.
                        dir = opendir(s_data->host_name);

                        /* Find the number of files in the current directory */
                        count = 0;
                        if (dir != NULL) {
                                /* Loop all the files and directories within directory */
                                while ((ent = readdir (dir)) != NULL) {
                                        if (ent->d_name[0] == '.') {
                                        } else {
                                                count ++;
                                        }
                                }
                        }
                        closedir(dir);

                        /* No files */
                        if (count == 0) {
                                /* Increment this value, if we reach 6, we did not receive file fo the past 30 mins */
                                timeout += 1;
                                if (timeout == 6) {
                                        /* No data for 30 minutes. Send error message */

                                        fp = fopen("ERROR.txt", "r");
                                        printf("Sending ERROR\n");
                                                                
                                        memcpy(session.file_name, "ERROR.txt", sizeof(session.file_name));

                                        if (fp == -1) {
                                                log_mesg("File Not found \n");
                                                continue;
                                        }

                                        fseek(fp, 0, SEEK_END); // seek to end of file
                                        file_length = ftell(fp); // get current file pointer
                                        fseek(fp, 0, SEEK_SET); // seek back to beginning of file

                                        data = (char *) malloc(file_length);
                                        file_length = fread(data, sizeof(char), file_length, fp);

                                        session.no_of_bytes = file_length;
                                        session.total_no_of_bytes = session.no_of_bytes;

                                        /* Calculate the number of data packets in the session */
                                        session.no_of_packets = file_length/MAX_FRAME_SIZE;

                                        /* One more packet is required if some more bytes are left */
                                        if (file_length % MAX_FRAME_SIZE) {
                                                session.no_of_packets++;
                                        }

                                        /* Initialize the sequence number for the data packets*/
                                        session.next_frame_number = 0;

                                        /* Handle session */
                                        session_handle(&session, data);

                                        fclose(fp);
                                        free(data);
                                        timeout = 0;
                                }
                        }

                        if (count > 0) {

                                /* Okay, we have files. Reset the Timeout counter */
                                timeout = 0;

                                /* Loop through the directory and send all the files */
                                dir = opendir (s_data->host_name);

                                if (dir != NULL) {
                                        while ((ent = readdir (dir)) != NULL) {
                                                if (ent->d_name[0] == '.') {
                                                        /* Skip the hidden files */
                                                } else {
                                                        /* BASE */
                                                        file_sent = 0;

                                                        /* Open the file and read it */
                                                        sprintf(file_name, "%s/%s", s_data->host_name, ent->d_name);
                                                        fp = fopen(file_name, "r");

                                                        if (fp == -1) {
                                                                log_mesg("File Not found \n");
                                                                continue;
                                                        }

                                                        log_mesg("Sending File %s\n", file_name);

                                                        /* FInd the length of the file */
                                                        fseek(fp, 0, SEEK_END); // seek to end of file
                                                        file_length = ftell(fp); // get current file pointer
                                                        fseek(fp, 0, SEEK_SET); // seek back to beginning of file

                                           
                                                        if (file_length > 0) {

                                                                /* Read the data */
                                                                data = (char *) malloc(file_length);
                                                                file_length = fread(data, sizeof(char), file_length, fp);

                                                                /* Save the file_properties in the session, so that we can send this
                                                                 * information in the handshake packet */

                                                                bzero(session.file_name, sizeof(session.file_name));
                                                                memcpy(session.file_name, ent->d_name, sizeof(session.file_name));
                                                                session.no_of_bytes = file_length;
                                                                session.total_no_of_bytes = session.no_of_bytes;
                                
                                                                /* Calculate the number of data packets in the session */
                                                                session.no_of_packets = file_length/MAX_FRAME_SIZE;

                                                                /* One more packet is required if some more bytes are left */
                                                                if (file_length % MAX_FRAME_SIZE) {
                                                                        session.no_of_packets++;
                                                                }

                                                                /* Initialize the sequence number for the data packets*/
                                                                session.next_frame_number = 0;

                                                                /* Handle session */
                                                                file_sent = session_handle(&session, data);
                                                                printf("Back\n");
                                                                free(data);
                                                        }

                                                        fclose(fp);

                                                        if (file_sent == -1) {
                                                                printf("Error sending file\n");
                                                                exit(0);
                                                                break;
                                                        }
                                                        printf("Sending file %s Over\n", file_name);
                                                        sprintf(command, "mv -f %s processed/.", file_name);
                                                        system(command);
                                                        bzero(file_name, sizeof(file_name));
                                                   }
                                        }
                                        closedir(dir);
                                }
                        }

                        /* Get back to square 1 */
                        if (file_sent == -1) {
                                break;
                        }
                }
        }
}

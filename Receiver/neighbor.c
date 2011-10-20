#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include "neighbor.h"
#include "session.h"

/* List to maintain info about the peers */
struct PEER *peer_list = (struct PEER *) NULL;

/* Returns all neigbor information maintained in the structure given the port number*/
int session_get_neighbor_port(int port) {

	struct PEER *local_peer_list = peer_list;
        
	while (local_peer_list) {
		if (local_peer_list->port == port) {
                        return 1;
                }
                local_peer_list = (struct PEER *) local_peer_list->next;
        }

        return 0;
}

/* Returns all neigbor information maintained in the structure */
int session_get_neighbor_addr(struct sockaddr_in addr, uint32_t session_id, char *host_name) {

        struct PEER *local_peer_list = peer_list;

	while (local_peer_list) {
                if (!memcmp(local_peer_list->host_name, host_name, strlen(host_name))) {
                        return local_peer_list->port;
		}
		local_peer_list = (struct PEER *) local_peer_list->next;
	}
	return 0;
}


uint32_t session_get_neighbor_session_addr(struct sockaddr_in addr, uint32_t session_id, char *host_name) {

        struct PEER *local_peer_list = peer_list;
    
        while (local_peer_list) {
                if (!memcmp(local_peer_list->host_name, host_name, strlen(host_name))) {
                        return local_peer_list->session_id;
                }   
                local_peer_list = (struct PEER *) local_peer_list->next;
        }   
        return 0;
}

/* Assign a new port to each of the clients
 * This also reuses ports. It scans the entire list and assigns the first free port.
*/
int session_get_port() {

	int port = LISTEN_PORT_NUMBER + 1;

	while (1) {
		if (session_get_neighbor_port(port)) {
			port++;
			continue;
		} else {
			return port;
		}
	}
}


/* Adds a new session of a client as a neighbor in the neighbor list */
void session_add_neighbor(session_t *session) {
		
	struct PEER *local_peer_list;
	/* Allocate the node */
	struct PEER *peer_node;

	local_peer_list = peer_list;

	peer_node = (struct PEER *)malloc(sizeof(struct PEER));
	peer_node->peer_address = session->address;
	peer_node->session_id = session->session_id;
	peer_node->port = session->port;
	peer_node->thread_id = session->thread_id;
	peer_node->socket = session->socket;
        peer_node->seq_num = 0;
        memcpy(peer_node->host_name, session->host_name, strlen(session->host_name));
	peer_node->next = NULL;

	if (peer_list == NULL) {
		/* First node */
		peer_list = peer_node;
	} else {
		while (local_peer_list->next != NULL) {
			local_peer_list = (struct PEER *) local_peer_list->next;
		}
		local_peer_list->next = peer_node;
	}

}

/* Deletes a client's session from the neighbor list once all data has been transferred */
void session_del_neighbor(int port) {
        struct PEER *local_peer_list = (struct PEER *) peer_list, *prev = (struct PEER *) peer_list;

	if (peer_list == NULL) {
		return;
	}
	
        if (local_peer_list->port == port) {
		peer_list = peer_list->next;
		/* Close the socket */	
		sleep(1);
		close(local_peer_list->socket);
		pthread_cancel(local_peer_list->thread_id);
		free (local_peer_list);
		return;
	}
	
	prev = local_peer_list;
        local_peer_list = (struct PEER *) local_peer_list->next;
	        	
	
	while (local_peer_list) {
                if (local_peer_list->port == port) {
			prev->next = local_peer_list->next;
			/* Close the socket */	
			close(local_peer_list->socket);
			pthread_cancel(local_peer_list->thread_id);
			free(local_peer_list);
			return;
		}
		prev = local_peer_list;
		local_peer_list = (struct PEER *) local_peer_list->next;
	}
}

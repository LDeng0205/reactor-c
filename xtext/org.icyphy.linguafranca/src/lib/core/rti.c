/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
 * @author Soroush Bateni
 *
 * @section LICENSE
Copyright (c) 2020, The University of California at Berkeley.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * @section DESCRIPTION
 * Runtime infrastructure for distributed Lingua Franca programs.
 *
 * This implementation creates one thread per federate so as to be able
 * to take advantage of multiple cores. It may be more efficient, however,
 * to use select() instead to read from the multiple socket connections
 * to each federate.
 *
 * This implementation sends messages in little endian order
 * because Intel, RISC V, and Arm processors are little endian.
 * This is not what is normally considered "network order",
 * but we control both ends, and hence, for commonly used
 * processors, this will be more efficient since it won't have
 * to swap bytes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>      // Defines perror(), errno
#include <sys/socket.h>
#include <sys/types.h>  // Provides select() function to read from multiple sockets.
#include <netinet/in.h> // Defines struct sockaddr_in
#include <arpa/inet.h>  // inet_ntop & inet_pton
#include <unistd.h>     // Defines read(), write(), and close()
#include <netdb.h>      // Defines gethostbyname().
#include <strings.h>    // Defines bzero().
#include <assert.h>
#include <sys/wait.h>   // Defines wait() for process to change state.
#include "util.c"   // Defines network functions.
#include "net_util.c"   // Defines network functions.
#include "rti.h"        // Defines TIMESTAMP. Includes <pthread.h> and "reactor.h".
#include "tag.c"        // Time-related types and functions.

// The main mutex lock.
pthread_mutex_t rti_mutex = PTHREAD_MUTEX_INITIALIZER;

// Condition variable used to signal receipt of all proposed start times.
pthread_cond_t received_start_times = PTHREAD_COND_INITIALIZER;

// Condition variable used to signal that a start time has been sent to a federate.
pthread_cond_t sent_start_time = PTHREAD_COND_INITIALIZER;

// RTI's decided stop time for federates
instant_t max_stop_time = NEVER;

// The federates.
federate_t federates[NUMBER_OF_FEDERATES];

// Maximum start time seen so far from the federates.
instant_t max_start_time = 0LL;

// Number of federates that have proposed start times.
int num_feds_proposed_start = 0;

// Number of federates handling stop
int num_feds_handling_stop = 0;

// Boolean indicating that all federates have exited.
volatile bool all_federates_exited = false;

/**
 * The ID of the federation that this RTI will supervise.
 * This should be overridden with a command-line -i option to ensure
 * that each federate only joins its assigned federation.
 */
char* federation_id = "Unidentified Federation";

/************* TCP server information *************/
/** The final port number that the TCP socket server ends up using. */
ushort final_port_TCP = -1;

/** The TCP socket descriptor for the socket server. */
int socket_descriptor_TCP = -1;

/************* UDP server information *************/
/** The final port number that the UDP socket server ends up using. */
ushort final_port_UDP = -1;

/** The UDP socket descriptor for the socket server. */
int socket_descriptor_UDP = USHRT_MAX;

#ifdef _LF_CLOCK_SYNC
pthread_t clock_thread; // Thread performing PTP clock sync sessions periodically.
#endif // _LF_CLOCK_SYNC

/**
 * Mark a federate requesting stop.
 * 
 * If the number of federates handling stop reaches the
 * NUM_OF_FEDERATES, broadcast STOP_GRANTED to every federate.
 * 
 * This function assumes the rti_mutex is already locked.
 * 
 * @param fed The federate that has requested a stop or has suddenly
 *  stopped (disconnected).
 */
void _lf_rti_mark_federate_requesting_stop(federate_t* fed);

/** 
 * Create a server and enable listening for socket connections.
 * 
 * @note This function is similar to create_server(...) in 
 * federate.c. However, it contains logs that are specific
 * to the RTI.
 * 
 * @param port The port number to use.
 * @param socket_type The type of the socket for the server (TCP or UDP).
 * @return The socket descriptor on which to accept connections.
 */
int create_server(int specified_port, ushort port, socket_type_t socket_type) {
    // Timeout time for the communications of the server
    struct timeval timeout_time = {.tv_sec = TCP_TIMEOUT_TIME / BILLION, .tv_usec = (TCP_TIMEOUT_TIME % BILLION) / 1000};
    // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
    int socket_descriptor = -1;
    if (socket_type == TCP) {
        socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    } else if (socket_type == UDP) {
        socket_descriptor = socket(AF_INET, SOCK_DGRAM, 0);
        // Set the appropriate timeout time
        timeout_time = (struct timeval){.tv_sec = UDP_TIMEOUT_TIME / BILLION, .tv_usec = (UDP_TIMEOUT_TIME % BILLION) / 1000};        
    }
    if (socket_descriptor < 0) {
        error_print_and_exit("Failed to create RTI socket.");
    }

    // Set the option for this socket to reuse the same address
    int true_variable = 1; // setsockopt() requires a reference to the value assigned to an option
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &true_variable, sizeof(int)) < 0) {
        error_print("RTI failed to set SO_REUSEADDR option on the socket: %s.", strerror(errno));
    }
    // Set the timeout on the socket so that read and write operations don't block for too long
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_time, sizeof(timeout_time)) < 0) {
        error_print("RTI failed to set SO_RCVTIMEO option on the socket: %s.", strerror(errno));
    }
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_time, sizeof(timeout_time)) < 0) {
        error_print("RTI failed to set SO_SNDTIMEO option on the socket: %s.", strerror(errno));
    }

    /*
     * The following used to permit reuse of a port that an RTI has previously
     * used that has not been released. We no longer do this, but instead
     * increment the port number until an available port is found.

    // SO_REUSEPORT (since Linux 3.9)
    //       Permits multiple AF_INET or AF_INET6 sockets to be bound to an
    //       identical socket address.  This option must be set on each
    //       socket (including the first socket) prior to calling bind(2)
    //       on the socket.  To prevent port hijacking, all of the
    //       processes binding to the same address must have the same
    //       effective UID.  This option can be employed with both TCP and
    //       UDP sockets.

    int reuse = 1;
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, 
            (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    #ifdef SO_REUSEPORT
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEPORT, 
            (const char*)&reuse, sizeof(reuse)) < 0)  {
        perror("setsockopt(SO_REUSEPORT) failed");
    }
    #endif
    */

    // Server file descriptor.
    struct sockaddr_in server_fd;
    // Zero out the server address structure.
    bzero((char *) &server_fd, sizeof(server_fd));

    server_fd.sin_family = AF_INET;            // IPv4
    server_fd.sin_addr.s_addr = INADDR_ANY;    // All interfaces, 0.0.0.0.
    // Convert the port number from host byte order to network byte order.
    server_fd.sin_port = htons(port);

    int result = bind(
            socket_descriptor,
            (struct sockaddr *) &server_fd,
            sizeof(server_fd));

    // If the binding fails with this port and no particular port was specified
    // in the LF program, then try the next few ports in sequence.
    while (result != 0
            && specified_port == 0
            && port >= STARTING_PORT
            && port <= STARTING_PORT + PORT_RANGE_LIMIT) {
        info_print("RTI failed to get port %d. Trying %d.", port, port + 1);
        port++;
        server_fd.sin_port = htons(port);
        result = bind(
                socket_descriptor,
                (struct sockaddr *) &server_fd,
                sizeof(server_fd));
    }
    if (result != 0) {
        if (specified_port == 0) {
            error_print_and_exit("Failed to bind the RTI socket. Cannot find a usable port. Consider increasing PORT_RANGE_LIMIT in rti.h.");
        } else {
            error_print_and_exit("Failed to bind the RTI socket. Specified port is not available. Consider leaving the port unspecified");
        }
    }
    char* type = "TCP";
    if (socket_type == UDP) {
        type = "UDP";
    }
    info_print("RTI for federation %s started using port %d for %s.", federation_id, port, type);

    if (socket_type == TCP) {
        final_port_TCP = port;
        // Enable listening for socket connections.
        // The second argument is the maximum number of queued socket requests,
        // which according to the Mac man page is limited to 128.
        listen(socket_descriptor, 128);
    } else if (socket_type == UDP) {
        final_port_UDP = port;
        // No need to listen on the UDP socket
    }

    return socket_descriptor;
}

/** Handle a timed message being received from a federate via the RTI.
 *  @param sending_socket The identifier for the sending socket.
 *  @param buffer The buffer to read into (the first byte is already there).
 */
void handle_timed_message(int sending_socket, unsigned char* buffer) {
    int header_size = 1 + sizeof(ushort) + sizeof(ushort) + sizeof(int) + sizeof(instant_t) + sizeof(microstep_t);
    // Read the header, minus the first byte which is already there.
    read_from_socket_errexit(sending_socket, header_size - 1, &(buffer[1]), "RTI failed to read the timed message header from remote federate.");
    // Extract the header information.
    unsigned short port_id;
    unsigned short federate_id;
    unsigned int length;
    // Extract information from the header.
    extract_header(&(buffer[1]), &port_id, &federate_id, &length);

    unsigned int total_bytes_to_read = length + header_size;
    unsigned int bytes_to_read = length;
    // Prevent a buffer overflow.
    if (bytes_to_read + header_size > FED_COM_BUFFER_SIZE) {
        bytes_to_read = FED_COM_BUFFER_SIZE - header_size;
    }

    read_from_socket_errexit(sending_socket, bytes_to_read, &(buffer[header_size]),
                     "RTI failed to read timed message from federate %d.", federate_id);
    int bytes_read = bytes_to_read + header_size;
    DEBUG_PRINT("Message received by RTI: %s.", buffer + header_size);

    // Need to acquire the mutex lock to ensure that the thread handling
    // messages coming from the socket connected to the destination does not
    // issue a TAG before this message has been forwarded.
    pthread_mutex_lock(&rti_mutex);

    // If the destination federate is no longer connected, issue a warning
    // and return.
    if (federates[federate_id].state == NOT_CONNECTED) {
        pthread_mutex_unlock(&rti_mutex);
        warning_print("RTI: Destination federate %d is no longer connected. Dropping message.",
                federate_id);
        return;
    }

    // Forward the message or message chunk.
    int destination_socket = federates[federate_id].socket;

    DEBUG_PRINT("RTI forwarding message to port %d of federate %d of length %d.", port_id, federate_id, length);
    // Need to make sure that the destination federate's thread has already
    // sent the starting TIMESTAMP message.
    while (federates[federate_id].state == NOT_CONNECTED) {
        // Need to wait here.
        pthread_cond_wait(&sent_start_time, &rti_mutex);
    }
    write_to_socket_errexit(destination_socket, bytes_read, buffer,
            "RTI failed to forward message to federate %d.", federate_id);

    // The message length may be longer than the buffer,
    // in which case we have to handle it in chunks.
    int total_bytes_read = bytes_read;
    while (total_bytes_read < total_bytes_to_read) {
        DEBUG_PRINT("Forwarding message in chunks.");
        bytes_to_read = total_bytes_to_read - total_bytes_read;
        if (bytes_to_read > FED_COM_BUFFER_SIZE) {
            bytes_to_read = FED_COM_BUFFER_SIZE;
        }
        read_from_socket_errexit(sending_socket, bytes_to_read, buffer,
                "RTI failed to read message chunks.");
        total_bytes_read += bytes_to_read;

        write_to_socket_errexit(destination_socket, bytes_to_read, buffer,
                "RTI failed to send message chunks.");
    }
    pthread_mutex_unlock(&rti_mutex);
}

/** 
 * Send a tag advance grant (TAG) message to the specified socket
 *  with the specified time and microstep.
 */
void send_tag_advance_grant(federate_t* fed, tag_t tag) {
    int message_length = 1 + sizeof(instant_t) + sizeof(microstep_t);
    unsigned char buffer[message_length];
    buffer[0] = TIME_ADVANCE_GRANT;
    encode_ll(tag.time, &(buffer[1]));
    encode_int(tag.microstep, &(buffer[1 + sizeof(instant_t)]));
    if (fed->state == NOT_CONNECTED) {
        return;
    }
    // This function is called in send_tag_advance_if_appropriate(), which is a long
    // function. During this call, the socket might close, causing the following write_to_socket
    // to fail. Consider a failure here a soft failure and update the federate's status.
    int bytes_read = write_to_socket(fed->socket, message_length, buffer);
    if (bytes_read < message_length) {
        error_print("RTI failed to send time advance grant to federate %d.", fed->id);
        if (bytes_read < 0) {
            fed->state = NOT_CONNECTED;
            _lf_rti_mark_federate_requesting_stop(fed);
        }
    }
    DEBUG_PRINT("RTI sent to federate %d the TAG (%lld, %u).", fed->id, tag.time - start_time, tag.microstep);
}

/** 
 * Find the earliest time at which the specified federate may
 * experience its next event. This is the least next event time
 * of the specified federate and (transitively) upstream federates
 * (with delays of the connections added). For upstream federates,
 * we assume (conservatively) that federate upstream of those
 * may also send an event. If any upstream federate has not sent
 * a next event message, this will return the completion time
 * of the federate (which may be NEVER, if the federate has not
 * yet completed a logical time).
 * @param fed The federate.
 * @param candidate A candidate tag (for the first invocation,
 *  this should be fed->next_event).
 * @param visited An array of booleans indicating which federates
 *  have been visited (for the first invocation, this should be
 *  an array of falses of size NUMBER_OF_FEDERATES).
 */
tag_t transitive_next_event(federate_t* fed, tag_t candidate, bool visited[]) {
    if (visited[fed->id] || fed->state == NOT_CONNECTED) {
        // DEBUG_PRINT("Federate %d not connected to RTI.", fed->id);
        // Federate has stopped executing or we have visited it before.
        // No point in checking upstream federates.
        return candidate;
    }

    visited[fed->id] = true;
    tag_t result = fed->next_event;
    if (compare_tags(candidate, result) < 0) {
        result = candidate;
    }
    // Check upstream federates to see whether any of them might send
    // an event that would result in an earlier next event.
    for (int i = 0; i < fed->num_upstream; i++) {
        tag_t upstream_result = transitive_next_event(
                &federates[fed->upstream[i]], result, visited);
        if (upstream_result.time != NEVER && fed->upstream_delay[i] > 0) {
            upstream_result.time += fed->upstream_delay[i];
            upstream_result.microstep = 0;
        }
        if (compare_tags(upstream_result, result) < 0) {
            result = upstream_result;
        }
    }
    if (result.time == NEVER) {
        result = fed->completed;
    }
    return result;
}

/** 
 * Determine whether the specified reactor is eligible for a tag advance grant,
 * and, if so, send it one. This first compares the next event tag of the
 * federate to the completion tag of all its upstream federates (plus the
 * delay on the connection to those federates). It finds the minimum of all
 * these tags, with a caveat. If the candidate for minimum has a sufficiently
 * large next event tag that we can be sure it will provide no event before
 * the next smallest minimum, then that candidate is ignored and the next
 * smallest minimum determines the tag. If the resulting tag to advance
 * to does not move time forward for the federate, then no tag advance
 * grant message is sent to the federate.
 *
 * This should be called whenever an upstream federate either registers
 * a next event tag or completes a logical tag.
 *
 * This function assumes that the caller holds the mutex lock.
 *
 * @return True if the TAG message is sent and false otherwise.
 */
bool send_tag_advance_if_appropriate(federate_t* fed) {
    // Determine whether to send a time advance grant to the downstream reactor.
    // The first candidate is its next event time. But we may need to advance
    // it to a lesser time.

    tag_t candidate_tag_advance = fed->next_event;

    // Look at its upstream federates (including this one).
    for (int j = 0; j < fed->num_upstream; j++) {
        // First, find the minimum completed time or time of the
        // next event of all federates upstream from this one.
        federate_t* upstream = &federates[fed->upstream[j]];
        // There may be a delay on the connection. Add that to the candidate.
        interval_t delay = fed->upstream_delay[j];
        tag_t upstream_completion_tag = upstream->completed;
        upstream_completion_tag.time += delay;
        if (delay > 0) {
            // If a positive delay is given, then the event will be processed
            // at microstep 0 in a future time
            upstream_completion_tag.microstep = 0;
        }
        // Preserve NEVER.
        if (upstream->completed.time == NEVER) {
            upstream_completion_tag.time = NEVER;
            upstream_completion_tag.microstep = 0;
        }
        // If the completion tag of the upstream federate
        // is less than the candidate tag, then we will need to use that
        // completion tag unless the (transitive) next_event tag of the
        // upstream federate (plus the delay) is larger than the
        // current candidate completion tag.
        if (compare_tags(upstream_completion_tag, candidate_tag_advance) < 0) {
            // To handle cycles, need to create a boolean array to keep
            // track of which upstream federates have been visited.
            bool visited[NUMBER_OF_FEDERATES];
            for (int i = 0; i < NUMBER_OF_FEDERATES; i++) {
                visited[i] = false;
            }

            // Find the (transitive) next event tag upstream.
            tag_t upstream_next_event = transitive_next_event(
                    upstream, upstream->next_event, visited);
            DEBUG_PRINT("RTI: Upstream next event: (%lld, %u). Upstream completion time: (%lld, %u). Candidate time: (%lld, %u).",
                    upstream_next_event.time - start_time,
                    upstream_next_event.microstep,
                    upstream_completion_tag.time - start_time,
                    upstream_completion_tag.microstep,
                    candidate_tag_advance.time - start_time,
                    candidate_tag_advance.microstep);
            if (upstream_next_event.time != NEVER && delay > 0) {
                upstream_next_event.time += delay;
                upstream_next_event.microstep = 0;
            }
            // If the upstream federate has disconnected,
            // it will not produce further events. Thus,
            // the next assignment will be unnecessary.
            if (upstream->state != NOT_CONNECTED) {
                if (compare_tags(upstream_next_event, candidate_tag_advance) <= 0) {
                    // Cannot advance the federate to the upstream
                    // next event time because that event has presumably not yet
                    // been produced.
                    candidate_tag_advance = upstream_completion_tag;
                }
            }
        }
    }

    // If the resulting time will advance time
    // in the federate, send it a time advance grant.
    if (compare_tags(candidate_tag_advance, fed->completed) > 0) {
        DEBUG_PRINT("Sending TAG to fed %d of (%lld, %u) because it is greater than the completed (%lld, %u).", fed->id, candidate_tag_advance.time - start_time, candidate_tag_advance.microstep, fed->completed.time - start_time, fed->completed.microstep);
        send_tag_advance_grant(fed, candidate_tag_advance);
        return true;
    } else {
        DEBUG_PRINT("Not sending TAG to fed %d of %lld because it is not greater than the completed %lld.", fed->id, candidate_tag_advance.time - start_time, fed->completed.time - start_time);
    }
    return false;
}

/**
 * Handle a logical tag complete (LTC) message.
 * @param fed The federate that has completed a logical tag.
 */
void handle_logical_time_complete(federate_t* fed) {
    unsigned char buffer[sizeof(instant_t) + sizeof(microstep_t)];
    read_from_socket_errexit(fed->socket, sizeof(instant_t) + sizeof(microstep_t), buffer, "RTI failed to read the content of the logical tag complete from federate %d.", fed->id);

    // Acquire a mutex lock to ensure that this state does change while a
    // message is in transport or being used to determine a TAG.
    pthread_mutex_lock(&rti_mutex);

    fed->completed.time = extract_ll(buffer);
    fed->completed.microstep = extract_int(&(buffer[sizeof(instant_t)]));

    DEBUG_PRINT("RTI received from federate %d the logical tag complete (%lld, %u).",
                fed->id, fed->completed.time - start_time, fed->completed.microstep);

    // Check downstream federates to see whether they should now be granted a TAG.
    for (int i = 0; i < fed->num_downstream; i++) {
        send_tag_advance_if_appropriate(&federates[fed->downstream[i]]);
    }
    pthread_mutex_unlock(&rti_mutex);
}

/**
 * Handle a next event tag (NET) message.
 * @param fed The federate sending a NET message.
 */
void handle_next_event_time(federate_t* fed) {
    unsigned char buffer[sizeof(instant_t) + sizeof(microstep_t)];
    read_from_socket_errexit(fed->socket, sizeof(instant_t) + sizeof(microstep_t), buffer, "RTI failed to read the content of the next event tag from federate %d.", fed->id);

    // Acquire a mutex lock to ensure that this state does change while a
    // message is in transport or being used to determine a TAG.
    pthread_mutex_lock(&rti_mutex);

    fed->next_event.time = extract_ll(buffer);
    fed->next_event.microstep = extract_int(&(buffer[sizeof(instant_t)]));
    DEBUG_PRINT("RTI received from federate %d the NET (%lld, %u).", fed->id, fed->next_event.time - start_time, fed->next_event.microstep);

    // Check to see whether we can reply now with a time advance grant.
    // If the federate has no upstream federates, then it does not wait for
    // nor expect a reply. It just proceeds to advance time.
    if (fed->num_upstream > 0) {
        send_tag_advance_if_appropriate(fed);
    }
    pthread_mutex_unlock(&rti_mutex);
}

/**
 * Broadcast a message to all federates.
 * 
 * If a federate is disconnected, this function will skip over it.
 * 
 * This function assumes that the mutex lock is already acquired by the caller.
 * 
 * @param buffer The buffer containing the message
 * @param size_of_message The size of the message
 */
void _lf_rti_broadcast_message_to_federates_already_locked(unsigned char* buffer, size_t size_of_message) {
    // Iterate over federates and send each the message.
    for (int i = 0; i < NUMBER_OF_FEDERATES; i++) {
        if (federates[i].state == NOT_CONNECTED) {
            continue;
        }
        write_to_socket_errexit(federates[i].socket, size_of_message, buffer,
                "RTI failed to broadcast message to federate %d.", federates[i].id);
    }
}

/////////////////// STOP functions ////////////////////
/**
 * Boolean used to prevent the RTI from sending the
 * STOP_GRANTED message multiple times.
 */
bool _lf_rti_stop_granted_already_sent_to_federates = false;

/**
 * Once the RTI has seen proposed times from all connected federates,
 * it will broadcast a STOP_GRANTED carrying the max_stop_time.
 * 
 * This function assumes the caller holds the rti_mutex lock.
 */
void _lf_rti_broadcast_stop_time_to_federates_already_locked() {
    if (_lf_rti_stop_granted_already_sent_to_federates == true) {
        return;
    }
    // Reply with a stop granted to all federates
    unsigned char outgoing_buffer[1 + sizeof(instant_t)];
    outgoing_buffer[0] = STOP_GRANTED;
    encode_ll(max_stop_time, &(outgoing_buffer[1]));
    _lf_rti_broadcast_message_to_federates_already_locked(outgoing_buffer, 1 + sizeof(instant_t));
    DEBUG_PRINT("RTI broadcasted to federates STOP_GRANTED with time %lld.", max_stop_time);
    _lf_rti_stop_granted_already_sent_to_federates = true;
}

/**
 * Mark a federate requesting stop.
 * 
 * If the number of federates handling stop reaches the
 * NUM_OF_FEDERATES, broadcast STOP_GRANTED to every federate.
 * 
 * This function assumes the rti_mutex is already locked.
 * 
 * @param fed The federate that has requested a stop or has suddenly
 *  stopped (disconnected).
 */
void _lf_rti_mark_federate_requesting_stop(federate_t* fed) {    
    if (!fed->requested_stop) {
        // Assume that the federate
        // has requested stop
        num_feds_handling_stop++;
        fed->requested_stop = true;
    }
    if (num_feds_handling_stop == NUMBER_OF_FEDERATES) {
        // We now have information about the stop time of all
        // federates.
        _lf_rti_broadcast_stop_time_to_federates_already_locked();
    }
}

/**
 * Handle a STOP_REQUEST message.
 * @param fed The federate sending a STOP_REQUEST message.
 */
void handle_stop_request_message(federate_t* fed) {
    DEBUG_PRINT("RTI handling stop_request from federate %d.", fed->id);
    unsigned char buffer[sizeof(instant_t)];
    read_from_socket_errexit(fed->socket, sizeof(instant_t), buffer, "RTI failed to read the stop message timestamp from federate %d.", fed->id);

    // Acquire a mutex lock to ensure that this state does change while a
    // message is in transport or being used to determine a TAG.
    pthread_mutex_lock(&rti_mutex);    
    // Extract the proposed stop time for the federate
    instant_t stop_time = extract_ll(buffer);
    
    if (stop_time > max_stop_time) {
        max_stop_time = stop_time;
    }

    // Check if we have already received a stop_time
    // from this federate
    if (federates[fed->id].requested_stop) {
        // Ignore this request
        pthread_mutex_unlock(&rti_mutex);  
        return;
    }
    // If this federate has not already asked
    // for a stop, add it to the tally.
    _lf_rti_mark_federate_requesting_stop(fed);

    if (num_feds_handling_stop == NUMBER_OF_FEDERATES) {
        // We now have information about the stop time of all
        // federates. This is extremely unlikely, but it can occur
        // all federates call request_stop() at the same tag.
        pthread_mutex_unlock(&rti_mutex);    
        return;
    }
    DEBUG_PRINT("RTI received from federate %d a STOP_REQUEST message with time %lld.", fed->id, stop_time);
    unsigned char stop_request_buffer[1 + sizeof(instant_t)];
    stop_request_buffer[0] = STOP_REQUEST;
    encode_ll(stop_time, &(stop_request_buffer[1]));
    
    // Iterate over federates and send each the STOP_REQUEST message
    // if we do not have a stop_time already for them.
    for (int i = 0; i < NUMBER_OF_FEDERATES; i++) {
        if (federates[i].id != fed->id && federates[i].requested_stop == false) {
            if (federates[i].state == NOT_CONNECTED) {
                _lf_rti_mark_federate_requesting_stop(&federates[i]);
                continue;
            }
            write_to_socket_errexit(federates[i].socket, 1 + sizeof(instant_t), stop_request_buffer,
                    "RTI failed to broadcast message to federate %d.", federates[i].id);
        }
    }
    DEBUG_PRINT("RTI broadcasted to federates STOP_REQUEST with time %lld.", stop_time);
    pthread_mutex_unlock(&rti_mutex);
}

/** 
 * Handle a STOP_REQUEST_REPLY message.
 * @param fed The federate replying the STOP_REQUEST
 */
void handle_stop_request_reply(federate_t* fed) {
    unsigned char buffer_stop_time[sizeof(instant_t)];
    read_from_socket_errexit(fed->socket, sizeof(instant_t), buffer_stop_time, "RTI failed to read the reply to STOP_REQUEST message from federate %d.", fed->id);
    instant_t federate_stop_time = extract_ll(buffer_stop_time);
    DEBUG_PRINT("RTI received from federate %d STOP time %lld.", fed->id, federate_stop_time);

    // Acquire the mutex lock so that we can change the state of the RTI
    pthread_mutex_lock(&rti_mutex);
    // If the federate has not requested stop before,
    // count the reply
    if (federate_stop_time > max_stop_time) {
        max_stop_time = federate_stop_time;
    }
    _lf_rti_mark_federate_requesting_stop(fed);
    pthread_mutex_unlock(&rti_mutex);
}
//////////////////////////////////////////////////

/** 
 * Handle address query messages.
 * This function reads the body of a ADDRESS_QUERY (@see rti.h) message
 * which is the requested destination federate ID and replies with the stored
 * port value for the socket server of that federate. The port values
 * are initialized to -1. If no ADDRESS_AD message has been received from
 * the destination federate, the RTI will simply reply with -1 for the port.
 * The sending federate is responsible for checking back with the RTI after a 
 * period of time. @see connect_to_federate() in federate.c.
 * @param fed_id The federate sending a ADDRESS_QUERY message.
 */
void handle_address_query(ushort fed_id) {
    // Use buffer both for reading and constructing the reply.
    // The length is what is needed for the reply.
    unsigned char buffer[sizeof(int)];
    int bytes_read = read_from_socket(federates[fed_id].socket, sizeof(ushort), (unsigned char*)buffer);
    if (bytes_read == 0) {
        error_print_and_exit("Failed to read address query.");
    }
    ushort remote_fed_id = extract_ushort(buffer);
    
    // debug_print("Received address query from %d for %d.\n", fed_id, remote_fed_id);

    assert(federates[remote_fed_id].server_port < 65536);
    // NOTE: server_port initializes to -1, which means the RTI does not know
    // the port number because it has not yet received an ADDRESS_AD message
    // from this federate. It will respond by sending -1.

    // Encode the port number.
    encode_int(federates[remote_fed_id].server_port, (unsigned char*)buffer);
    // Send the port number (which could be -1).
    write_to_socket_errexit(federates[fed_id].socket, sizeof(int), (unsigned char*)buffer,
                        "Failed to write port number to socket of federate %d.", fed_id);

    // Send the server IP address to federate.
    write_to_socket_errexit(federates[fed_id].socket, sizeof(federates[remote_fed_id].server_ip_addr),
                        (unsigned char *)&federates[remote_fed_id].server_ip_addr,
                        "Failed to write ip address to socket of federate %d.", fed_id);

    if (federates[remote_fed_id].server_port != -1) {
        DEBUG_PRINT("Replied to address query from federate %d with address %s:%d.",
                fed_id, federates[remote_fed_id].server_hostname, federates[remote_fed_id].server_port);
    }
}

/**
 * Handle address advertisement messages (@see ADDRESS_AD in rti.h).
 * The federate is expected to send its server port number as the next
 * byte. The RTI will keep a record of this number in the .server_port
 * field of the federates[federate_id] array of structs.
 * 
 * The server_hostname and server_ip_addr fields are assigned
 * in connect_to_federates() upon accepting the socket
 * from the remote federate.
 * 
 * @param federate_id The id of the remote federate that is
 *  sending the address advertisement.
 */
void handle_address_ad(ushort federate_id) {   
    DEBUG_PRINT("Received address advertisement from federate %d.", federate_id);
    // Read the port number of the federate that can be used for physical
    // connections to other federates
    int server_port = -1;
    unsigned char buffer[sizeof(int)];
    int bytes_read = read_from_socket(federates[federate_id].socket, sizeof(int), (unsigned char *)buffer);

    if (bytes_read < sizeof(int)) {
        DEBUG_PRINT("Error reading port data from federate %d.", federates[federate_id].id);
        // Leave the server port at -1, which mean "I don't know".
        return;
    }

    server_port = extract_int(buffer);
    
    pthread_mutex_lock(&rti_mutex);
    federates[federate_id].server_port = server_port;
    pthread_mutex_unlock(&rti_mutex);

    DEBUG_PRINT("Got physical connection server address %s:%d from federate %d.\n", federates[federate_id].server_hostname, federates[federate_id].server_port, federates[federate_id].id);
}

/**
 * A function to handle timestamp messages.
 * This function assumes the caller does not hold the mutex.
 */
void handle_timestamp(federate_t *my_fed) {
    unsigned char buffer[8];
    // Read bytes from the socket. We need 8 bytes.
    int bytes_read = read_from_socket(my_fed->socket, sizeof(long long), (unsigned char*)&buffer);
    if (bytes_read < 1) {
        error_print("ERROR reading timestamp from federate %d.\n", my_fed->id);
    }


    instant_t timestamp = swap_bytes_if_big_endian_ll(*((long long *)(&buffer)));
    DEBUG_PRINT("RTI received timestamp message: %lld.", timestamp);

    pthread_mutex_lock(&rti_mutex);
    num_feds_proposed_start++;
    if (timestamp > max_start_time) {
        max_start_time = timestamp;
    }
    if (num_feds_proposed_start == NUMBER_OF_FEDERATES) {
        // All federates have proposed a start time.
        pthread_cond_broadcast(&received_start_times);
    } else {
        // Some federates have not yet proposed a start time.
        // wait for a notification.
        while (num_feds_proposed_start < NUMBER_OF_FEDERATES) {
            // FIXME: Should have a timeout here?
            pthread_cond_wait(&received_start_times, &rti_mutex);
        }
    }

    // Send back to the federate the maximum time plus an offset.
    // Start by sending a timestamp marker.
    unsigned char message_marker = TIMESTAMP;
    int bytes_written = write_to_socket(my_fed->socket, 1, &message_marker);
    if (bytes_written < 1) {
        error_print("ERROR sending timestamp to federate %d.", my_fed->id);
    }

    // Send the timestamp.
    // Add an offset to this start time to get everyone starting together.
    start_time = max_start_time + DELAY_START;
    long long message = swap_bytes_if_big_endian_ll(start_time);
    bytes_written = write_to_socket(my_fed->socket, sizeof(long long), (unsigned char *)(&message));
    if (bytes_written < 1) {
        error_print("ERROR sending starting time to federate %d.", my_fed->id);
    }

    // Update state for the federate to indicate that the TIMESTAMP
    // message has been sent. That TIMESTAMP message grants time advance to
    // the federate to the start time.
    my_fed->state = GRANTED;
    pthread_cond_broadcast(&sent_start_time);
    pthread_mutex_unlock(&rti_mutex);
    DEBUG_PRINT("RTI sent start time %lld to federate %d.", start_time, my_fed->id);
}

/**
 * Take a snapshot of the physical clock time and send
 * it to federate fed_id.
 * 
 * This version assumes the caller holds the mutex lock.
 * 
 * @param message_type The type of the clock sync message (see rti.h).
 * @param fed The federate to send the physical time to.
 * @param socket_type The socket type (TCP or UDP).
 */
void _lf_rti_send_physical_clock(unsigned char message_type, federate_t* fed, socket_type_t socket_type) {
    if (fed->state == NOT_CONNECTED) {
        warning_print("Clock sync: RTI failed to send physical time to federate %d. Socket not connected.\n",
                fed->id);
        return;
    }
    unsigned char buffer[sizeof(instant_t) + 1];
    buffer[0] = message_type;
    instant_t current_physical_time = get_physical_time();
    encode_ll(current_physical_time, &(buffer[1]));
    
    // Send the message
    if (socket_type == UDP) {
        // FIXME: UDP_addr is never initialized.
        DEBUG_PRINT("Clock sync: RTI sending UDP message type %u.", buffer[0]);
        int bytes_written = sendto(socket_descriptor_UDP, buffer, 1 + sizeof(instant_t), 0,
                                (struct sockaddr*)&fed->UDP_addr, sizeof(fed->UDP_addr));
        if (bytes_written < sizeof(instant_t) + 1) {
            warning_print("Clock sync: RTI failed to send physical time to federate %d: %s\n",
                        fed->id,
                        strerror(errno));
            return;
        }
    } else if (socket_type == TCP) {
        DEBUG_PRINT("Clock sync:  RTI sending TCP message type %u.", buffer[0]);
        write_to_socket_errexit(fed->socket, 1 + sizeof(instant_t), buffer,
                        "Clock sync: RTI failed to send physical time to federate %d: %s.",
                        fed->id,
                        strerror(errno));
    }
    DEBUG_PRINT("Clock sync: RTI sent PHYSICAL_TIME_SYNC_MESSAGE with timestamp %lld to federate %d.",
                 current_physical_time,
                 fed->id);
}

/**
 * Handle clock synchronization T3 messages from federates.
 * These will come in on the TCP channel during initialization
 * and on the UDP channel subsequently. In both cases, this
 * function will reply with a T4 message. If the channel is
 * the UDP channel, then it will follow the T4 message
 * immediately with a "coded probe" message, which will be
 * used by the federate to decide whether to discard this
 * clock synchronization round.
 *
 * @param my_fed The sending federate.
 * @param socket_type The RTI's socket type used for the communication (TCP or UDP)
 */
void handle_physical_clock_sync_message(federate_t* my_fed, socket_type_t socket_type) {
    // Lock the mutex to prevent interference between sending the two
    // coded probe messages.
    pthread_mutex_lock(&rti_mutex);
    // Reply with a T4 type message
    _lf_rti_send_physical_clock(PHYSICAL_CLOCK_SYNC_MESSAGE_T4, my_fed, socket_type);
    // Send the corresponding coded probe immediately after,
    // but only if this is a UDP channel.
    if (socket_type == UDP) {
        _lf_rti_send_physical_clock(PHYSICAL_CLOCK_SYNC_MESSAGE_T4_CODED_PROBE, my_fed, socket_type);
    }
    pthread_mutex_unlock(&rti_mutex);
}

/**
 * A (quasi-)periodic thread that performs clock synchronization with each
 * federate. It starts by waiting a time given by CLOCK_SYNCHRONIZATION_T1_PERIOD_NS
 * and then iterates over the federates, performing a complete clock synchronization
 * interaction with each federate before proceeding to the next federate.
 * The interaction starts with this RTI sending a snapshot of its physical clock
 * to the federate (message T1). It then waits for a reply and then sends another
 * snapshot of its physical clock (message T4).  It then follows that T4 message
 * with a coded probe message that the federate can use to discard the session if
 * the network is congested.
 */
void* clock_synchronization_thread(void* noargs) {

    // Initiate a clock synchronization every CLOCK_SYNCHRONIZATION_T1_PERIOD_NS
    struct timespec sleep_time = {(time_t) CLOCK_SYNCHRONIZATION_T1_PERIOD_NS / BILLION,
                                  CLOCK_SYNCHRONIZATION_T1_PERIOD_NS % BILLION};
    struct timespec remaining_time;

    bool any_federates_connected = true;
    while (any_federates_connected) {
        // Sleep
        nanosleep(&sleep_time, &remaining_time); // Can be interrupted
        any_federates_connected = false;
        for (int fed = 0; fed < NUMBER_OF_FEDERATES; fed++) {
            if (federates[fed].state == NOT_CONNECTED) {
                _lf_rti_mark_federate_requesting_stop(&federates[fed]);
                continue;
            } else if (!federates[fed].clock_synchronization_enabled) {
                continue;
            }
            // FIXME: This shouldn't be set to true until after comm has occurred successfully.
            any_federates_connected = true;
            // Send the RTI's current physical time to the federate
            // Send on UDP.
            DEBUG_PRINT("RTI sending T1 message to initiate clock sync round.");
            _lf_rti_send_physical_clock(PHYSICAL_CLOCK_SYNC_MESSAGE_T1, &federates[fed], UDP);

            // Listen for reply message, which should be T3.
            size_t message_size = 1 + sizeof(int);
            unsigned char buffer[message_size];
            // Maximum number of messages that we discard before giving up on this cycle.
            // If the T3 message from this federate does not arrive and we keep receiving
            // other message, then give up on this federate and move to the next federate.
            int remaining_attempts = 5;
            while (remaining_attempts > 0) {
                remaining_attempts--;
                int bytes_read = read_from_socket(socket_descriptor_UDP, message_size, buffer);
                // If any errors occur, either discard the message or the clock sync round.
                if (bytes_read == message_size) {
                    if (buffer[0] == PHYSICAL_CLOCK_SYNC_MESSAGE_T3) {
                        int fed_id = extract_int(&(buffer[1]));
                        // Check that this message came from the correct federate.
                        if (fed_id != federates[fed].id) {
                            // Message is from the wrong federate. Discard the message.
                            warning_print("Clock sync: Received T3 message from federate %d, "
                                    "but expected one from %d. Discarding message.",
                                    fed_id, federates[fed].id);
                            continue;
                        }
                        DEBUG_PRINT("Clock sync: RTI received T3 message from federate %d.", fed_id);
                        handle_physical_clock_sync_message(&federates[fed_id], UDP);
                        remaining_attempts = 0;
                    } else {
                        // The message is not a T3 message. Discard the message and
                        // continue waiting for the T3 message. This is possibly a message
                        // from a previous cycle that was discarded.
                        warning_print("Clock sync: Unexpected UDP message %u. Expected %u from federate %d. "
                                "Discarding message.",
                                buffer[0],
                                PHYSICAL_CLOCK_SYNC_MESSAGE_T3,
                                federates[fed].id);
                        continue;
                    }
                } else {
                    warning_print("Clock sync: Read from UDP socket failed: %s. "
                            "Skipping clock sync round for federate %d.",
                            strerror(errno),
                            federates[fed].id);
                    remaining_attempts = -1;
                }
            }
        }
    }
    return NULL;
}

/**
 * A function to handle messages labeled
 * as RESIGN sent by a federate. This 
 * message is sent at the time of termination
 * after all shutdown events are processed
 * on the federate. This function assumes
 * that the caller does not hold the mutex
 * lock.
 * 
 * @note At this point, the RTI might have
 * outgoing messages to the federate. This
 * function thus first performs a shutdown
 * on the socket which sends an EOF. It then
 * waits for the remote socket to be closed
 * before closing the socket itself.
 * 
 * @param my_fed The federate sending a RESIGN message.
 **/
void handle_federate_resign(federate_t *my_fed) {
    //Nothing more to do. Close the socket and exit.
    unsigned char temporary_read_buffer[1];
    pthread_mutex_lock(&rti_mutex);
    my_fed->state = NOT_CONNECTED;
    _lf_rti_mark_federate_requesting_stop(my_fed);
    my_fed->next_event.time = NEVER;
    my_fed->next_event.microstep = 0;
    close(my_fed->socket); //  from unistd.h
    pthread_mutex_unlock(&rti_mutex);
    info_print("Federate %d has resigned.", my_fed->id);
}

/** 
 * Thread handling TCP communication with a federate.
 * @param fed A pointer to the federate's struct that has the
 *  socket descriptor for the federate.
 */
void* federate_thread_TCP(void* fed) {
    federate_t* my_fed = (federate_t*)fed;

    // Buffer for incoming messages.
    // This does not constrain the message size because messages
    // are forwarded piece by piece.
    unsigned char buffer[FED_COM_BUFFER_SIZE];

    // Listen for messages from the federate.
    while (1) {
        // Read no more than one byte to get the message type.
        int bytes_read = read_from_socket(my_fed->socket, 1, buffer);
        if (bytes_read < 1) {
            // Socket is closed
            warning_print("RTI socket to federate %d is closed. Exiting the thread.\n", my_fed->id);
            my_fed->state = NOT_CONNECTED;
            my_fed->socket = -1;
            _lf_rti_mark_federate_requesting_stop(my_fed);
            break;
        }
        switch(buffer[0]) {
            case TIMESTAMP:
                DEBUG_PRINT("RTI handling TIMESTAMP message.");
                handle_timestamp(my_fed);
                break;
            case ADDRESS_QUERY:
                // debug_print("Handling ADDRESS_QUERY message.\n");
                handle_address_query(my_fed->id);
                break;
            case ADDRESS_AD:
                DEBUG_PRINT("RTI handling ADDRESS_AD message.");
                handle_address_ad(my_fed->id);
                break;
            case TIMED_MESSAGE:
                DEBUG_PRINT("RTI handling timed message.");
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_timed_message(my_fed->socket, buffer);
                break;
            case RESIGN:
                DEBUG_PRINT("RTI handling resign.");
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_federate_resign(my_fed);
                return NULL;
                break;
            case NEXT_EVENT_TIME:            
                DEBUG_PRINT("RTI handling next event time.");
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_next_event_time(my_fed);
                break;
            case LOGICAL_TIME_COMPLETE:            
                DEBUG_PRINT("RTI handling logical time completion.");
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_logical_time_complete(my_fed);
                break;
            case STOP_REQUEST:
                DEBUG_PRINT("RTI handling stop request from federate %d.", my_fed->id);
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_stop_request_message(my_fed);
                break;
            case STOP_REQUEST_REPLY:
                if (my_fed->state == NOT_CONNECTED) {
                    pthread_mutex_lock(&rti_mutex);
                    _lf_rti_mark_federate_requesting_stop(my_fed);
                    pthread_mutex_unlock(&rti_mutex);
                    return NULL;
                }
                handle_stop_request_reply(my_fed);
                break;
            default:
                error_print("RTI received from federate %d an unrecognized TCP message type: %u.", my_fed->id, buffer[0]);
        }
    }

    // Nothing more to do. Close the socket and exit.
    close(my_fed->socket); //  from unistd.h

    return NULL;
}

/** Wait for one incoming connection request from each federate,
 *  and upon receiving it, create a thread to communicate with
 *  that federate. Return when all federates have connected.
 *  @param socket_descriptor The socket on which to accept connections.
 */
void connect_to_federates(int socket_descriptor) {
    for (int i = 0; i < NUMBER_OF_FEDERATES; i++) {
        // Wait for an incoming connection request.
        struct sockaddr client_fd;
        uint32_t client_length = sizeof(client_fd);
        int socket_id = accept(socket_descriptor_TCP, &client_fd, &client_length);
        if (socket_id < 0) {
            error_print_and_exit("RTI failed to accept the socket.");
        }

        // The first message from the federate should contain its ID and the federation ID.
        // Buffer for message ID, federate ID, and federation ID length.
        int length = sizeof(ushort) + 2; // This should be 4.
        unsigned char buffer[length];

        // Read bytes from the socket. We need 4 bytes.
        read_from_socket_errexit(socket_id, length, buffer, "RTI failed to read from accepted socket.");
        // debug_print("read %d bytes.\n", bytes_read);

        // If any error occurs, this will be set to non-zero.
        unsigned char error_code = 0;

        ushort fed_id;

        // First byte received is the message ID.
        if (buffer[0] != FED_ID) {
            if(buffer[0] == P2P_SENDING_FED_ID || buffer[0] == P2P_TIMED_MESSAGE) {
                // If the connection is a peer-to-peer connection between two
                // federates, reject the connection with the WRONG_SERVER error.
                error_code = WRONG_SERVER;
            } else {
                error_code = UNEXPECTED_MESSAGE;
            }
            error_print("RTI expected a FED_ID message. Got %u (see rti.h).", buffer[0]);
        } else {
            // Received federate ID.
            fed_id = extract_ushort(buffer + 1);
            DEBUG_PRINT("RTI received federate ID: %d.", fed_id);

            // Read the federation ID.  First read the length, which is one byte.
            size_t federation_id_length = (size_t)buffer[sizeof(ushort) + 1];
            char federation_id_received[federation_id_length + 1]; // One extra for null terminator.
            // Next read the actual federation ID.
            read_from_socket_errexit(socket_id, federation_id_length,
                                (unsigned char*)federation_id_received,
                                "RTI failed to read federation id from federate %d.", fed_id);

            // Terminate the string with a null.
            federation_id_received[federation_id_length] = 0;

            DEBUG_PRINT("RTI received federation ID: %s.", federation_id_received);

            // Compare the received federation ID to mine.
            if (strncmp(federation_id, federation_id_received, federation_id_length) != 0) {
                // Federation IDs do not match. Send back a REJECT message.
                error_print("WARNING: Federate from another federation %s attempted to connect to RTI in federation %s.\n",
                        federation_id_received,
                        federation_id);
                error_code = FEDERATION_ID_DOES_NOT_MATCH;
            } else {
                if (fed_id >= NUMBER_OF_FEDERATES) {
                    // Federate ID is out of range.
                    error_print("RTI received federate ID %d, which is out of range.", fed_id);
                    error_code = FEDERATE_ID_OUT_OF_RANGE;
                } else {
                    if (federates[fed_id].state != NOT_CONNECTED) {
                        error_print("RTI received duplicate federate ID: %d.", fed_id);
                        error_code = FEDERATE_ID_IN_USE;
                    }
                }
            }
        }

        // If the FED_ID message was not exactly right, respond with a REJECT,
        // close the socket, and continue waiting for federates to join.
        if (error_code != 0) {
            DEBUG_PRINT("RTI sending REJECT.");
            unsigned char response[2];
            response[0] = REJECT;
            response[1] = error_code;
            // Ignore errors on this response.
            write_to_socket_errexit(socket_id, 2, response, "RTI failed to write REJECT message on the socket.");
            // Close the socket.
            close(socket_id);
            // Invalid federate. Try again.
            i--;
        } else {
            // The FED_ID message matches.
            // Assign the address information for federate
            // The IP address is stored here as an in_addr struct (in .server_ip_addr) that can be useful
            // to create sockets and can be efficiently sent over the network. If VERBOSE is defined
            // in the target LF program, the IP address is also stored in a human readable format
            // (stored in .server_hostname) that can be useful for log messages.
            // First, convert the sockaddr structure into a sockaddr_in that contains an internet address.
            struct sockaddr_in* pV4_addr = (struct sockaddr_in*)&client_fd;
            // Then extract the internet address (which is in IPv4 format) and assign it as the federate's socket server
            federates[fed_id].server_ip_addr = pV4_addr->sin_addr;

#ifdef VERBOSE
            // Then create the human readable format and copy that into
            // the .server_hostname field of the federate.
            char str[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, &federates[fed_id].server_ip_addr, str, INET_ADDRSTRLEN );
            strncpy (federates[fed_id].server_hostname, str, INET_ADDRSTRLEN);

            DEBUG_PRINT("RTI got address %s from federate %d.", federates[fed_id].server_hostname, fed_id);
#endif
            federates[fed_id].socket = socket_id;

            // Set the federate's state as pending
            // because it is waiting for the start time to be
            // sent by the RTI before beginning its execution
            federates[fed_id].state = PENDING;

            DEBUG_PRINT("RTI responding with ACK to federate %d.", fed_id);
            // Send an ACK message and the server's UDP port number
            unsigned char ack_message = ACK;
            write_to_socket_errexit(socket_id, 1, &ack_message,
                    "RTI failed to write ACK message to federate %d.", fed_id);
            // Next, read the UDP_PORT message from the federate regardless of the status of
            // clock synchronization.
            DEBUG_PRINT("RTI waiting for UDP_PORT from federate %d.", fed_id);
            unsigned char response[1 + sizeof(ushort)];
            read_from_socket_errexit(socket_id, 1 + sizeof(ushort) , response,
                    "RTI failed to read UDP_PORT message from federate %d.", fed_id);
            if (response[0] != UDP_PORT) {
                error_print("RTI was expecting a UDP_PORT message from federate %d. Got %u instead. "
                        "Clock sync disabled.",
                                     fed_id, response[0]);
                federates[fed_id].clock_synchronization_enabled = false;
            }
#ifdef _LF_CLOCK_SYNC
            else {
                ushort federate_UDP_port_number = extract_ushort(&(response[1]));
                if (federate_UDP_port_number > 0) {
                    // Initialize the UDP_addr field of the federate struct
                    federates[fed_id].UDP_addr.sin_family = AF_INET;
                    federates[fed_id].UDP_addr.sin_port = htons(federate_UDP_port_number);
                    federates[fed_id].UDP_addr.sin_addr = federates[fed_id].server_ip_addr;

                    // Perform the initialization clock synchronization with the federate.
                    // Send the required number of messages for clock synchronization
                    for (int i=0; i < CLOCK_SYNCHRONIZATION_T4_MESSAGES_PER_INTERVAL; i++) {
                        // Send the RTI's current physical time T1 to the federate.
                        _lf_rti_send_physical_clock(PHYSICAL_CLOCK_SYNC_MESSAGE_T1, &federates[fed_id], TCP);

                        // Listen for reply message, which should be T3.
                        size_t message_size = 1 + sizeof(int);
                        unsigned char buffer[message_size];
                        read_from_socket_errexit(socket_id, message_size, buffer,
                                "Socket to federate %d unexpectedly closed.", fed_id);
                        if (buffer[0] == PHYSICAL_CLOCK_SYNC_MESSAGE_T3) {
                            int fed_id = extract_int(&(buffer[1]));
                            assert(fed_id > -1);
                            assert(fed_id < 65536);
                            DEBUG_PRINT("RTI received T3 clock sync message from federate %d.", fed_id);
                            handle_physical_clock_sync_message(&federates[fed_id], TCP);
                        } else {
                            error_print_and_exit("Unexpected message %u from federate %d.", buffer[0], fed_id);
                        }
                    }
                    DEBUG_PRINT("RTI finished clock synchronization with federate %d.", fed_id);
                } else {
                    federates[fed_id].clock_synchronization_enabled = false;
                    warning_print("RTI: Clock synchronization has been disabled for federate %d.", fed_id);
                }
            }
#else // No clock synchronization
            // Clock synchronization is universally disabled via the clock-sync target parameter
            // (#define _LF_CLOCK_SYNC was not generated for the RTI).
            // Note that the federates are still going to send a UDP_PORT message but with a payload (port) of 0..
            federates[fed_id].clock_synchronization_enabled = false;
#endif
        }

        // Create a thread to communicate with the federate.
        // This has to be done after clock synchronization is finished
        // or that thread may end up attetmpting to handle incoming clock
        // synchronization messages.
        pthread_create(&(federates[fed_id].thread_id), NULL, federate_thread_TCP, &(federates[fed_id]));
    }
    // All federates have connected.
    DEBUG_PRINT("All federates have connected to RTI.");

#ifdef _LF_CLOCK_SYNC
    // Create the thread that performs periodic PTP clock synchronization sessions
    // over the UDP channel, but only if the UDP channel is open.
    if (final_port_UDP != USHRT_MAX) {
        pthread_create(&clock_thread, NULL, clock_synchronization_thread, NULL);
    }
#endif // _LF_CLOCK_SYNC
}

/**
 * Thread to respond to new connections, which could be federates of other
 * federations who are attempting to join the wrong federation.
 * @param nothing Nothing needed here.
 */
void* respond_to_erroneous_connections(void* nothing) {
    while (true) {
        // Wait for an incoming connection request.
        struct sockaddr client_fd;
        uint32_t client_length = sizeof(client_fd);
        int socket_id = accept(socket_descriptor_TCP, &client_fd, &client_length);
        if (socket_id < 0) return NULL;

        if (all_federates_exited) {
            return NULL;
        }

        error_print("RTI received an unexpected connection request. Federation is running.");
        unsigned char response[2];
        response[0] = REJECT;
        response[1] = FEDERATION_ID_DOES_NOT_MATCH;
        // Ignore errors on this response.
        write_to_socket_errexit(socket_id, 2, response,
                 "RTI failed to write FEDERATION_ID_DOES_NOT_MATCH to erroneous incoming connection.");
        // Close the socket.
        close(socket_id);
    }
    return NULL;
}

/** Initialize the federate with the specified ID.
 *  @param id The federate ID.
 */
void initialize_federate(int id) {
    federates[id].id = id;
    federates[id].socket = -1;      // No socket.
    federates[id].clock_synchronization_enabled = true;
    federates[id].completed.time = NEVER;
    federates[id].completed.microstep = 0u;
    federates[id].next_event.time = NEVER;
    federates[id].next_event.microstep = 0u;
    federates[id].state = NOT_CONNECTED;
    federates[id].upstream = NULL;
    federates[id].upstream_delay = NULL;
    federates[id].num_upstream = 0;
    federates[id].downstream = NULL;
    federates[id].num_downstream = 0;
    federates[id].mode = REALTIME;    
    strncpy(federates[id].server_hostname ,"localhost", INET_ADDRSTRLEN);
    federates[id].server_ip_addr.s_addr = 0;
    federates[id].server_port = -1;
    federates[id].requested_stop = false;
}

/**
 *
 */
void initialize_clock() {
    // Initialize logical time to match physical clock.
    struct timespec actualStartTime;
    clock_gettime(_LF_CLOCK, &actualStartTime);
    physical_start_time = actualStartTime.tv_sec * BILLION + actualStartTime.tv_nsec;
    
    // Set the epoch offset to zero (see tag.h)
    _lf_epoch_offset = 0LL;
    if (_LF_CLOCK != CLOCK_REALTIME) {
        struct timespec real_time_start;
        clock_gettime(CLOCK_REALTIME, &real_time_start);
        instant_t real_time_start_ns = real_time_start.tv_sec * BILLION + real_time_start.tv_nsec;
        // If the clock is not CLOCK_REALTIME, find the necessary epoch offset
        _lf_epoch_offset = real_time_start_ns - physical_start_time;
        DEBUG_PRINT("Setting epoch offset to %lld.", _lf_epoch_offset);
    }
}

/** 
 * Start the socket server for the runtime infrastructure (RTI) and
 * return the socket descriptor.
 * @param num_feds Number of federates.
 * @param port The port on which to listen for socket connections, or
 *  0 to use the default port range.
 */
int start_rti_server(ushort port) {
    int specified_port = port;
    if (port == 0) {
        // Use the default starting port.
        port = STARTING_PORT;
    }
    initialize_clock();
    // Create the TCP socket server
    socket_descriptor_TCP = create_server(specified_port, port, TCP);
    info_print("RTI: Listening for federates.");
    // Create the UDP socket server
    // Try to get the final_port_TCP + 1 port
    socket_descriptor_UDP = create_server(specified_port, final_port_TCP + 1, UDP);
    return socket_descriptor_TCP;
}

/** 
 * Start the runtime infrastructure (RTI) interaction with the federates
 * and wait for the federates to exit.
 * @param socket_descriptor The socket descriptor returned by start_rti_server().
 */
void wait_for_federates(int socket_descriptor) {
    // Wait for connections from federates and create a thread for each.
    connect_to_federates(socket_descriptor);

    // All federates have connected.
    info_print("RTI: All expected federates have connected. Starting execution.");

    // Unfortunately, the socket server will continue to accept connections.
    // In case some other federation's federates are trying to join the wrong
    // federation, need to respond. Start a separate thread to do that.

    pthread_t responder_thread;
    pthread_create(&responder_thread, NULL, respond_to_erroneous_connections, NULL);

    // Wait for federate threads to exit.
    void* thread_exit_status;
    for (int i = 0; i < NUMBER_OF_FEDERATES; i++) {
        pthread_join(federates[i].thread_id, &thread_exit_status);
        info_print("RTI: Federate %d thread exited.", federates[i].id);
    }

    // NOTE: Apparently, closing the socket will not necessarily
    // cause the respond_to_erroneous_connections accept() call to return,
    // so instead, we connect here so that it can check the all_federates_exited
    // variable.
    all_federates_exited = true;

    // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
    int tmp_socket = socket(AF_INET , SOCK_STREAM , 0);
    // If creating the socket fails, assume the thread has already exited.
    if (tmp_socket >= 0) {
        struct hostent *server = gethostbyname("localhost");
        if (server != NULL) {
            // Server file descriptor.
            struct sockaddr_in server_fd;
            // Zero out the server_fd struct.
            bzero((char *) &server_fd, sizeof(server_fd));
            // Set up the server_fd fields.
            server_fd.sin_family = AF_INET;    // IPv4
            bcopy((char *)server->h_addr,
                 (char *)&server_fd.sin_addr.s_addr,
                 server->h_length);
            // Convert the port number from host byte order to network byte order.
            server_fd.sin_port = htons(final_port_TCP);
            connect(
                tmp_socket,
                (struct sockaddr *)&server_fd,
                sizeof(server_fd));
            close(tmp_socket);
        }
    }

    // NOTE: In all common TCP/IP stacks, there is a time period,
    // typically between 30 and 120 seconds, called the TIME_WAIT period,
    // before the port is released after this close. This is because
    // the OS is preventing another program from accidentally receiving
    // duplicated packets intended for this program.
    close(socket_descriptor);
    close(socket_descriptor_UDP);
}

/**
 * Print a usage message.
 */
void usage(int argc, char* argv[]) {
    printf("\nCommand-line arguments: \n\n");
    printf("  -i, --id <n>\n");
    printf("   The ID of the federation that this RTI will control.\n\n");

    printf("Command given:\n");
    for (int i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n\n");
}

/**
 * Process the command-line arguments. If the command line arguments are not
 * understood, then print a usage message and return 0. Otherwise, return 1.
 * @return 1 if the arguments processed successfully, 0 otherwise.
 */
int process_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
       if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--id") == 0) {
           if (argc < i + 2) {
               fprintf(stderr, "Error: --id needs a string argument.\n");
               usage(argc, argv);
               return 0;
           }
           i++;
           printf("Federation ID at RTI: %s\n", argv[i]);
           federation_id = argv[i++];
       } else {
           fprintf(stderr, "Error: Unrecognized command-line argument: %s\n", argv[i]);
           usage(argc, argv);
           return 0;
       }
    }
    return 1;
}

/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
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
 * Utility functions for a federate in a federated execution.
 * The main entry point is synchronize_with_other_federates().
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>      // Defined perror(), errno
#include <sys/socket.h>
#include <netinet/in.h> // Defines struct sockaddr_in
#include <unistd.h>     // Defines read(), write(), and close()
#include <netdb.h>      // Defines gethostbyname().
#include <strings.h>    // Defines bzero().
#include <pthread.h>
#include <assert.h>
#include "util.c"       // Defines error() and swap_bytes_if_big_endian().
#include "rti.h"        // Defines TIMESTAMP.
#include "reactor.h"    // Defines instant_t.

// Error messages.
char* ERROR_SENDING_HEADER = "ERROR sending header information to federate via RTI";
char* ERROR_SENDING_MESSAGE = "ERROR sending message to federate via RTI";
char* ERROR_UNRECOGNIZED_MESSAGE_TYPE = "ERROR Received from RTI an unrecognized message type";

/** The ID of this federate as assigned when synchronize_with_other_federates()
 *  is called.
 */
ushort __my_fed_id = 0;

/** The socket descriptor for this federate to communicate with the RTI.
 *  This is set by connect_to_rti(), which must be called before other
 *  functions are called.
 */
int rti_socket = -1;

/**
 * A dynamically allocated array that holds the socket descriptor for
 * physical connections to each federate. The index will be the federate
 * ID. This is initialized at strutup and is set by connect_to_federate().
 */
int *federate_sockets;

/**
 * A socket descriptor for the socket server of the federate.
 * This is assigned in __initialize_trigger_objects()
 */
int server_socket;

/**
 * The port used to listen for messages from other federates.
 */
int server_port = -1;

/** 
 *  Thread that listens for inputs from other federates.
 *  This always calls schedule.
 */
void* listen_to_federates(void *args);

/** Create a server and enable listening for socket connections.
 *  @param port The port number to use.
 *  @param my_ID The federate ID of the host. Used for logging purposes.
 *  @return The socket descriptor on which to accept connections.
 */
int create_server(int specified_port,
        int port,
        int my_ID) {
    if (port == 0) {
        // Use the default starting port.
        port = STARTING_PORT;
    }
    // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
    int socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_descriptor < 0)
    {
        fprintf(stderr, "ERROR on creating socket server for federate %d", my_ID);
        exit(1);
    }

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
        printf("Federate %d failed to get port %d. Trying %d\n", my_ID, port, port + 1);
        port++;
        server_fd.sin_port = htons(port);
        result = bind(
                socket_descriptor,
                (struct sockaddr *) &server_fd,
                sizeof(server_fd));
    }
    if (result != 0) {
        if (specified_port == 0) {
            fprintf(stderr,
                    "ERROR on binding the socket for federate %d. Cannot find a usable port. Consider increasing PORT_RANGE_LIMIT in federate.c",
                    my_ID);
            exit(1);
        } else {
            fprintf(stderr,
            "ERROR on binding socket for federate %d. Specified port is not available. Consider leaving the port unspecified",
            my_ID);
            exit(1);
        }
    }
    printf("Server for federate %d started using port %d.\n", my_ID, port);

    // Enable listening for socket connections.
    // The second argument is the maximum number of queued socket requests,
    // which according to the Mac man page is limited to 128.
    listen(socket_descriptor, 128);

    // Start a thread to listen for incoming messages from other federates.
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, listen_to_federates, &socket_descriptor);

    // Set the global server port
    server_port = port;
    
    // Send the server port number to the RTI
    unsigned char buffer[sizeof(int) + 1];
    buffer[0] = ADDRESSAD;
    encode_int(server_port, &(buffer[1]));
    write_to_socket(rti_socket, sizeof(int) + 1, (unsigned char*)buffer);
    printf("Federate %d sent port %d to RTI.\n", __my_fed_id, server_port);

    return socket_descriptor;
}

/** Send the specified message directly to the specified port in the
 *  specified federate. The port should be an
 *  input port of a reactor in the destination federate.
 *  This version is used for physical connections and does not include
 *  the timestamp in the message.
 *  The caller can reuse or free the memory after this returns.
 *  This function assumes the caller does not hold the mutex lock,
 *  which it acquires to perform the send.
 *  @param port The ID of the destination port.
 *  @param federate The ID of the destination federate.
 *  @param length The message length.
 *  @param message The message.
 */
void send_directly(unsigned int port, unsigned int federate, size_t length, unsigned char* message) {
    assert(port < 65536);
    assert(federate < 65536);
    unsigned char buffer[9];
    buffer[0] = MESSAGE;
    // NOTE: Send messages little endian, not big endian.
    buffer[1] = port & 0xff;
    buffer[2] = port & 0xff00;
    buffer[3] = federate & 0xff;
    buffer[4] = federate & 0xff00;
    buffer[5] = length & 0xff;
    buffer[6] = length & 0xff00;
    buffer[7] = length & 0xff0000;
    buffer[8] = length & 0xff000000;

    // Use a mutex lock to prevent multiple threads from simulatenously sending.
    // printf("DEBUG: Federate %d pthread_mutex_lock send_via_rti\n", __my_fed_id);
    pthread_mutex_lock(&mutex);
    // printf("DEBUG: Federate %d pthread_mutex_locked\n", __my_fed_id);
    write_to_socket(federate_sockets[federate], 9, buffer);
    write_to_socket(federate_sockets[federate], length, message);
    // printf("DEBUG: Federate %d pthread_mutex_unlock\n", __my_fed_id);
    pthread_mutex_unlock(&mutex);
}

/** Send the specified message to the specified port in the
 *  specified federate via the RTI. The port should be an
 *  input port of a reactor in the destination federate.
 *  This version does not include the timestamp in the message.
 *  The caller can reuse or free the memory after this returns.
 *  This function assumes the caller does not hold the mutex lock,
 *  which it acquires to perform the send.
 *  @param port The ID of the destination port.
 *  @param federate The ID of the destination federate.
 *  @param length The message length.
 *  @param message The message.
 */
void send_via_rti(unsigned int port, unsigned int federate, size_t length, unsigned char* message) {
    assert(port < 65536);
    assert(federate < 65536);
    unsigned char buffer[9];
    buffer[0] = MESSAGE;
    // NOTE: Send messages little endian, not big endian.
    buffer[1] = port & 0xff;
    buffer[2] = port & 0xff00;
    buffer[3] = federate & 0xff;
    buffer[4] = federate & 0xff00;
    buffer[5] = length & 0xff;
    buffer[6] = length & 0xff00;
    buffer[7] = length & 0xff0000;
    buffer[8] = length & 0xff000000;

    // Use a mutex lock to prevent multiple threads from simulatenously sending.
    // printf("DEBUG: Federate %d pthread_mutex_lock send_via_rti\n", __my_fed_id);
    pthread_mutex_lock(&mutex);
    // printf("DEBUG: Federate %d pthread_mutex_locked\n", __my_fed_id);
    write_to_socket(rti_socket, 9, buffer);
    write_to_socket(rti_socket, length, message);
    // printf("DEBUG: Federate %d pthread_mutex_unlock\n", __my_fed_id);
    pthread_mutex_unlock(&mutex);
}

/** Send the specified timestamped message to the specified port in the
 *  specified federate via the RTI. The port should be an
 *  input port of a reactor in the destination federate.
 *  This version does include the timestamp in the message.
 *  The caller can reuse or free the memory after this returns.
 *  This method assumes that the caller does not hold the mutex lock,
 *  which it acquires to perform the send.
 *  @param port The ID of the destination port.
 *  @param federate The ID of the destination federate.
 *  @param length The message length.
 *  @param message The message.
 */
void send_via_rti_timed(unsigned int port, unsigned int federate, size_t length, unsigned char* message) {
    assert(port < 65536);
    assert(federate < 65536);
    unsigned char buffer[17];
    // First byte identifies this as a timed message.
    buffer[0] = TIMED_MESSAGE;
    // Next two bytes identify the destination port.
    // NOTE: Send messages little endian, not big endian.
    encode_ushort(port, &(buffer[1]));

    // Next two bytes identify the destination federate.
    encode_ushort(federate, &(buffer[3]));

    // The next four bytes are the message length.
    encode_int(length, &(buffer[5]));

    // Next 8 bytes are the timestamp.
    instant_t current_time = get_logical_time();

    encode_ll(current_time, &(buffer[9]));
    // printf("DEBUG: Federate %d sending message with timestamp %lld.\n", __my_fed_id, current_time - start_time);

    // Use a mutex lock to prevent multiple threads from simultaneously sending.
    // printf("DEBUG: Federate %d pthread_mutex_lock send_via_rti_timed\n", __my_fed_id);
    pthread_mutex_lock(&mutex);
    // printf("DEBUG: Federate %d pthread_mutex_locked\n", __my_fed_id);
    write_to_socket(rti_socket, 17, buffer);
    write_to_socket(rti_socket, length, message);
    // printf("DEBUG: Federate %d pthread_mutex_unlock\n", __my_fed_id);
    pthread_mutex_unlock(&mutex);
}

/** Send a time to the RTI.
 *  This is not synchronized.
 *  It assumes the caller is.
 *  @param type The message type (NEXT_EVENT_TIME or LOGICAL_TIME_COMPLETE).
 *  @param time The time of this federate's next event.
 */
void send_time(unsigned char type, instant_t time) {
    unsigned char buffer[9];
    buffer[0] = type;
    encode_ll(time, &(buffer[1]));
    write_to_socket(rti_socket, 9, buffer);
}

/** Send a STOP message to the RTI, which will then broadcast
 *  the message to all federates.
 *  This function assumes the caller holds the mutex lock.
 */
void __broadcast_stop() {
    printf("Federate %d requesting a whole program stop.\n", __my_fed_id);
    send_time(STOP, current_time);
}

void connect_to_federates(int expected_number_of_federates)
{
    int received_federates = 0;
    while (received_federates < expected_number_of_federates) {
        // Wait for an incoming connection request.
        struct sockaddr client_fd;
        uint32_t client_length = sizeof(client_fd);
        int socket_id = accept(server_socket, &client_fd, &client_length);
        if (socket_id < 0) return;

        ushort remote_fed_id;
        unsigned char bf[2];
        read_from_socket(server_socket, 2, (unsigned char*)&bf);
        remote_fed_id = extract_ushort((unsigned char*)&bf);
        received_federates++;
    }
}

/**
 * Connect to the federate at the specified host and port and return
 * the socket descriptor for the connection. If this fails, the
 * program exits. If it succeeds, it sets element [id] of the federate_sockets
 * global array to refer to the socket for communicating with the federate.
 * 
 * This function assumes that the port is correctly given by the RTI.
 * @param id The ID of the remote federate.
 * @param hostname A hostname, such as "localhost".
 */
void connect_to_federate(ushort id, char* hostname) {
    int result = -1;
    int count_retries = 0;

    // Ask the RTI for port number of the remote federate
    unsigned char buffer[3];
    buffer[0] = ADDRESSQUERY;
    // NOTE: Sending messages in little endian.
    encode_ushort(id, &(buffer[1]));

    printf("DEBUG: sending address query for federate %d.\n", id);

    write_to_socket(rti_socket, sizeof(ushort) + 1, buffer);

    // Read RIT's response
    read_from_socket(rti_socket, sizeof(ushort), buffer);
    
    // FIXME: converting signed to unsigned
    ushort port = extract_ushort(buffer);
    assert(id < 65536);
    assert(port > 0);

    bool failure_message = false;
    while (result < 0) {
        // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
        federate_sockets[id] = socket(AF_INET , SOCK_STREAM , 0);
        if (federate_sockets[id] < 0) {
            fprintf(stderr, "ERROR on federate %d creating socket to federate %d", __my_fed_id, id);
            exit(1);
        }

        struct hostent *server = gethostbyname(hostname);
        if (server == NULL) {
            fprintf(stderr,"ERROR, no such host for federate %d: %s\n", id, hostname);
            exit(1);
        }
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
        server_fd.sin_port = htons(port);
        result = connect(
            federate_sockets[id],
            (struct sockaddr *)&server_fd,
            sizeof(server_fd));
        
        if (result != 0) {
            if (!failure_message) {
                printf("Federate %d failed to connect to federate %d on port %d.", __my_fed_id, id, port);
                failure_message = true;
            }
        }
        if (failure_message) {
            printf("\n");
            failure_message = false;
        }
        // Try again after some time.
        if (result < 0) {
            count_retries++;
            if (count_retries > CONNECT_NUM_RETRIES) {
                fprintf(stderr, "Federate %d failed to connect to federate %d after %d retries. Giving up.\n",
                        __my_fed_id, id, CONNECT_NUM_RETRIES);
                exit(2);
            }
            printf("Federate %d could not connect to federate %d at %s. Will try again every %d seconds.\n",
                    __my_fed_id, id, hostname, CONNECT_RETRY_INTERVAL);
            // Wait CONNECT_RETRY_INTERVAL seconds.
            struct timespec wait_time = {(time_t)CONNECT_RETRY_INTERVAL, 0L};
            struct timespec remaining_time;
            if (nanosleep(&wait_time, &remaining_time) != 0) {
                // Sleep was interrupted.
                continue;
            }
        } else {
            printf("Federate %d: connected to federate %d at %s, port %d.\n", __my_fed_id, id, hostname, port);
        }
    }

    unsigned char bf[2];
    encode_ushort(__my_fed_id, (unsigned char*)&bf);
    write_to_socket(federate_sockets[id], sizeof(ushort), (unsigned char*)&bf);
}


/**
 * Connect to the RTI at the specified host and port and return
 * the socket descriptor for the connection. If this fails, the
 *  program exits. If it succeeds, it sets the rti_socket global
 *  variable to refer to the socket for communicating with the RTI.
 *  @param id The assigned ID of the federate.
 *  @param hostname A hostname, such as "localhost".
 *  @param port A port number.
 */
void connect_to_rti(ushort id, char* hostname, int port) {
    // Repeatedly try to connect, one attempt every 2 seconds, until
    // either the program is killed, the sleep is interrupted,
    // or the connection succeeds.
    // If the specified port is 0, set it instead to the start of the
    // port range.
    bool specific_port_given = true;
    if (port == 0) {
        port = STARTING_PORT;
        specific_port_given = false;
    }
    int result = -1;
    int count_retries = 0;

    bool failure_message = false;
    while (result < 0) {
        // Create an IPv4 socket for TCP (not UDP) communication over IP (0).
        rti_socket = socket(AF_INET , SOCK_STREAM , 0);
        if (rti_socket < 0) error("ERROR on federate creating socket to RTI");

        struct hostent *server = gethostbyname(hostname);
        if (server == NULL) {
            fprintf(stderr,"ERROR, no such host for RTI: %s\n", hostname);
            exit(1);
        }
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
        server_fd.sin_port = htons(port);
        result = connect(
            rti_socket,
            (struct sockaddr *)&server_fd,
            sizeof(server_fd));
        // If this failed, try more ports, unless a specific port was given.
        if (result != 0
                && !specific_port_given
                && port >= STARTING_PORT
                && port <= STARTING_PORT + PORT_RANGE_LIMIT
        ) {
            if (!failure_message) {
                printf("Federate %d failed to connect to RTI on port %d. Trying %d", __my_fed_id, port, port + 1);
                failure_message = true;
            } else {
                printf(", %d", port);
            }
            port++;
            continue;
        }
        if (failure_message) {
            printf("\n");
            failure_message = false;
        }
        // If this still failed, try again with the original port after some time.
        if (result < 0) {
            if (!specific_port_given && port == STARTING_PORT + PORT_RANGE_LIMIT + 1) {
                port = STARTING_PORT;
            }
            count_retries++;
            if (count_retries > CONNECT_NUM_RETRIES) {
                fprintf(stderr, "Federate %d failed to connect to the RTI after %d retries. Giving up.\n",
                        __my_fed_id, CONNECT_NUM_RETRIES);
                exit(2);
            }
            printf("Federate %d could not connect to RTI at %s. Will try again every %d seconds.\n",
                    __my_fed_id, hostname, CONNECT_RETRY_INTERVAL);
            // Wait CONNECT_RETRY_INTERVAL seconds.
            struct timespec wait_time = {(time_t)CONNECT_RETRY_INTERVAL, 0L};
            struct timespec remaining_time;
            if (nanosleep(&wait_time, &remaining_time) != 0) {
                // Sleep was interrupted.
                continue;
            }
        } else {
            // Have connected to an RTI, but not sure it's the right RTI.
            // Send a FED_ID message and wait for a reply.
            printf("Federate %d: connected to RTI at %s, port %d.\n", __my_fed_id, hostname, port);

            // Notify the RTI of the ID of this federate and its federation.
            unsigned char buffer[4];

            // Send the message type first.
            buffer[0] = FED_ID;
            // Next send the federate ID.
            encode_ushort(id, &buffer[1]);
            // Next send the federation ID length.
            // The federation ID is limited to 255 bytes.
            size_t federation_id_length = strnlen(federation_id, 255);
            buffer[3] = federation_id_length & 0xff;

            int bytes_written = write(rti_socket, buffer, 4);
            // FIXME: Retry rather than exit.
            if (bytes_written < 0) error("ERROR sending federate ID to RTI");

            // Next send the federation ID itself.
            bytes_written = write(rti_socket, federation_id, federation_id_length);
            // FIXME: Retry rather than exit.
            if (bytes_written < 0) error("ERROR sending federation ID to RTI");

            // Wait for a response.
            unsigned char response;
            int bytes_read = read(rti_socket, &response, 1);
            if (response == REJECT) {
                // Read one more byte to determine the cause of rejection.
                unsigned char cause;
                bytes_read = read(rti_socket, &cause, 1);
                if (cause == FEDERATION_ID_DOES_NOT_MATCH) {
                    printf("Federate %d connected to the wrong RTI on port %d. Trying %d", __my_fed_id, port, port + 1);
                    port++;
                    result = -1;
                    continue;
                }
                fprintf(stderr, "RTI Rejected FED_ID message with response (see rti.h): %d. Federate quits.\n", cause);
                exit(1);
            }
        }
    }
}

/** Send the specified timestamp to the RTI and wait for a response.
 *  The specified timestamp should be current physical time of the
 *  federate, and the response will be the designated start time for
 *  the federate. This proceedure blocks until the response is
 *  received from the RTI.
 *  @param my_physical_time The physical time at this federate.
 *  @return The designated start time for the federate.
 */
instant_t get_start_time_from_rti(instant_t my_physical_time) {
    // Send the timestamp marker first.
    unsigned char message_marker = TIMESTAMP;
    int bytes_written = write(rti_socket, &message_marker, 1);
    // FIXME: Retry rather than exit.
    if (bytes_written < 0) error("ERROR sending message ID to RTI");

    // Send the timestamp.
    long long message = swap_bytes_if_big_endian_ll(my_physical_time);

    bytes_written = write(rti_socket, (void*)(&message), sizeof(long long));
    if (bytes_written < 0) error("ERROR sending message to RTI");

    // Get a reply.
    // Buffer for message ID plus timestamp.
    int buffer_length = sizeof(long long) + 1;
    unsigned char buffer[buffer_length];

    // Read bytes from the socket. We need 9 bytes.
    read_from_socket(rti_socket, 9, &(buffer[0]));
    // printf("DEBUG: federate read 9 bytes.\n");

    // First byte received is the message ID.
    if (buffer[0] != TIMESTAMP) {
        fprintf(stderr,
                "ERROR: Federate expected a TIMESTAMP message from the RTI. Got %u (see rti.h).\n",
                buffer[0]);
        exit(1);
    }

    instant_t timestamp = swap_bytes_if_big_endian_ll(*((long long*)(&(buffer[1]))));
    printf("Federate %d: starting timestamp is: %lld\n", __my_fed_id, timestamp);

    return timestamp;
}

/**
 * Placeholder for a generated function that returns a pointer to the
 * trigger_t struct for the action corresponding to the specified port ID.
 * @param port_id The port ID.
 * @return A pointer to a trigger_t struct or null if the ID is out of range.
 */
trigger_t* __action_for_port(int port_id);

/** 
 * Handle a message being received from a remote federate via the RTI or
 * directly via a physical connection.
 * This version is for messages carrying no timestamp.
 * @param socket The socket to read from.
 * @param buffer The buffer to read.
 */
void handle_message(int socket, unsigned char* buffer) {
    // Read the header.
    read_from_socket(socket, 8, buffer);
    // Extract the header information.
    unsigned short port_id;
    unsigned short federate_id;
    unsigned int length;
    extract_header(buffer, &port_id, &federate_id, &length);
    // printf("DEBUG: Federate %d receiving message to port %d of length %d.\n", federate_id, port_id, length);

    // Read the payload.
    // Allocate memory for the message contents.
    unsigned char* message_contents = (unsigned char*)malloc(length);
    read_from_socket(socket, length, message_contents);
    // printf("DEBUG: Message received by federate: %s.\n", message_contents);

    schedule_value(__action_for_port(port_id), 0LL, message_contents, length);
    // printf("DEBUG: Called schedule\n");
}

/** Version of schedule_value() identical to that in reactor_common.c
 *  except that it does not acquire the mutex lock.
 *  @param action The action or timer to be triggered.
 *  @param extra_delay Extra offset of the event release.
 *  @param value Dynamically allocated memory containing the value to send.
 *  @param length The length of the array, if it is an array, or 1 for a
 *   scalar and 0 for no payload.
 *  @return A handle to the event, or 0 if no event was scheduled, or -1 for error.
 */
handle_t schedule_value_already_locked(
        trigger_t* trigger, interval_t extra_delay, void* value, int length) {
    token_t* token = create_token(trigger->element_size);
    token->value = value;
    token->length = length;
    int return_value = __schedule(trigger, extra_delay, token);
    // Notify the main thread in case it is waiting for physical time to elapse.
    // printf("DEBUG: Federate %d pthread_cond_broadcast(&event_q_changed)\n", __my_fed_id);
    pthread_cond_broadcast(&event_q_changed);
    return return_value;
}

/** Handle a timestamped message being received from a remote federate via the RTI.
 *  This will read the timestamp, which is appended to the header,
 *  and calculate an offset to pass to the schedule function.
 *  This function assumes the caller does not hold the mutex lock,
 *  which it acquires to call schedule.
 *  @param buffer The buffer to read.
 */
void handle_timed_message(unsigned char* buffer) {
    // Read the header.
    read_from_socket(rti_socket, 16, buffer);
    // Extract the header information.
    unsigned short port_id;
    unsigned short federate_id;
    unsigned int length;
    extract_header(buffer, &port_id, &federate_id, &length);
    // printf("DEBUG: Federate receiving message to port %d to federate %d of length %d.\n", port_id, federate_id, length);

    // Read the timestamp.
    instant_t timestamp = extract_ll(buffer + 8);
    // printf("DEBUG: Message timestamp: %lld.\n", timestamp - start_time);

    // Read the payload.
    // Allocate memory for the message contents.
    unsigned char* message_contents = (unsigned char*)malloc(length);
    read_from_socket(rti_socket, length, message_contents);
    // printf("DEBUG: Message received by federate: %s.\n", message_contents);

    // Acquire the one mutex lock to prevent logical time from advancing
    // between the time we get logical time and the time we call schedule().
    // printf("DEBUG: Federate %d pthread_mutex_lock handle_timed_message\n", __my_fed_id);
    pthread_mutex_lock(&mutex);
    // printf("DEBUG: Federate %d pthread_mutex_locked\n", __my_fed_id);

    interval_t delay = timestamp - get_logical_time();
    // NOTE: Cannot call schedule_value(), which is what we really want to call,
    // because pthreads it too incredibly stupid and deadlocks trying to acquire
    // a lock that the calling thread already holds.
    schedule_value_already_locked(__action_for_port(port_id), delay, message_contents, length);
    // printf("DEBUG: Called schedule with delay %lld.\n", delay);

    // printf("DEBUG: Federate %d pthread_mutex_unlock\n", __my_fed_id);
    pthread_mutex_unlock(&mutex);
}

/** Most recent TIME_ADVANCE_GRANT received from the RTI, or NEVER if none
 *  has been received.
 *  This is used to communicate between the listen_to_rti thread and the
 *  main federate thread.
 */
volatile instant_t __tag = NEVER;

/** Indicator of whether a NET has been sent to the RTI and no TAG
 *  yet received in reply.
 */
volatile bool __tag_pending = false;

/** Handle a time advance grant (TAG) message from the RTI.
 *  This function assumes the caller does not hold the mutex lock,
 *  which it acquires to interact with the main thread, which may
 *  be waiting for a TAG (this broadcasts a condition signal).
 */
void handle_time_advance_grant() {
    union {
        long long ull;
        unsigned char c[sizeof(long long)];
    } result;
    read_from_socket(rti_socket, sizeof(long long), (unsigned char*)&result.c);

    // printf("DEBUG: Federate %d pthread_mutex_lock handle_time_advance_grant\n", __my_fed_id);
    pthread_mutex_lock(&mutex);
    // printf("DEBUG: Federate %d pthread_mutex_locked\n", __my_fed_id);
    __tag = swap_bytes_if_big_endian_ll(result.ull);
    __tag_pending = false;
    // printf("DEBUG: Federate %d received TAG %lld.\n", __my_fed_id, __tag - start_time);
    // Notify everything that is blocked.
    pthread_cond_broadcast(&event_q_changed);
    // printf("DEBUG: Federate %d pthread_mutex_unlock\n", __my_fed_id);
    pthread_mutex_unlock(&mutex);
}

/** Handle a STOP message from the RTI.
 *  NOTE: The stop time is ignored. This federate will stop as soon
 *  as possible.
 *  FIXME: It should be possible to at least handle the situation
 *  where the specified stop time is larger than current time.
 *  This would require implementing a shutdown action.
 *  @param buffer A pointer to the bytes specifying the stop time.
 */
void handle_incoming_stop_message() {
    union {
        long long ull;
        unsigned char c[sizeof(long long)];
    } time;
    read_from_socket(rti_socket, sizeof(long long), (unsigned char*)&time.c);

    // Acquire a mutex lock to ensure that this state does change while a
    // message is transport or being used to determine a TAG.
    pthread_mutex_lock(&mutex);

    instant_t stop_time = swap_bytes_if_big_endian_ll(time.ull);
    // printf("DEBUG: Federate %d received from RTI a STOP request with time %lld.\n", FED_ID, stop_time - start_time);
    stop_requested = true;
    pthread_cond_broadcast(&event_q_changed);

    pthread_mutex_unlock(&mutex);
}

/** 
 *  Thread that listens for inputs from other federates.
 *  This always calls schedule.
 */
void* listen_to_federates(void *args) {
    // Buffer for incoming messages.
    // This does not constrain the message size
    // because the message will be put into malloc'd memory.
    unsigned char buffer[BUFFER_SIZE];

    // Listen for messages from the federate.
    while (1) {
        // Read one byte to get the message type.
        read_from_socket(server_socket, 1, buffer);
        switch(buffer[0]) {
        case MESSAGE:
            handle_message(server_socket, buffer + 1);
            break;
        default:
            // printf("DEBUG: Erroneous message type: %d\n", buffer[0]);
            error(ERROR_UNRECOGNIZED_MESSAGE_TYPE);
        }
    }
    return NULL;
}

/** Thread that listens for inputs from the RTI.
 *  When a physical message arrives, this calls schedule.
 */
void* listen_to_rti(void* args) {
    // Buffer for incoming messages.
    // This does not constrain the message size
    // because the message will be put into malloc'd memory.
    unsigned char buffer[BUFFER_SIZE];

    // Listen for messages from the federate.
    while (1) {
        // Read one byte to get the message type.
        read_from_socket(rti_socket, 1, buffer);
        switch(buffer[0]) {
        case MESSAGE:
            handle_message(rti_socket, buffer + 1);
            break;
        case TIMED_MESSAGE:
            handle_timed_message(buffer + 1);
            break;
        case TIME_ADVANCE_GRANT:
            handle_time_advance_grant();
            break;
        case STOP:
            handle_incoming_stop_message();
            break;
        default:
            // printf("DEBUG: Erroneous message type: %d\n", buffer[0]);
            error(ERROR_UNRECOGNIZED_MESSAGE_TYPE);
        }
    }
    return NULL;
}

/** Synchronize the start with other federates via the RTI.
 *  This initiates a connection with the RTI, then
 *  sends the current logical time to the RTI and waits for the
 *  RTI to respond with a specified time.
 *  It starts a thread to listen for messages from the RTI.
 *  It then waits for physical time to match the specified time,
 *  sets current logical time to the time returned by the RTI,
 *  and then returns. If --fast was specified, then this does
 *  not wait for physical time to match the logical start time
 *  returned by the RTI.
 */
void synchronize_with_other_federates() {
                
    // printf("DEBUG: Federate synchronizing with other federates.\n");

    // Reset the start time to the coordinated start time for all federates.
    current_time = get_start_time_from_rti(get_physical_time());

    start_time = current_time;

    if (duration >= 0LL) {
        // A duration has been specified. Recalculate the stop time.
        stop_time = current_time + duration;
    }

    // Start a thread to listen for incoming messages from the RTI.
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, listen_to_rti, NULL);

    // If --fast was not specified, wait until physical time matches
    // or exceeds the start time.
    wait_until(current_time);
    // printf("DEBUG: Done waiting for start time %lld.\n", current_time);
    // printf("DEBUG: Physical time is ahead of current time by %lld.\n", get_physical_time() - current_time);

    // Reinitialize the physical start time to match the current physical time.
    // This will be different on each federate. If --fast was given, it could
    // be very different.
    physical_start_time = get_physical_time();
}

/** Indicator of whether this federate has upstream federates.
 *  The default value of false may be overridden in __initialize_trigger_objects.
 */
bool __fed_has_upstream = false;

/** Indicator of whether this federate has downstream federates.
 *  The default value of false may be overridden in __initialize_trigger_objects.
 */
bool __fed_has_downstream = false;

/** Send a logical time complete (LTC) message to the RTI
 *  if there are downstream federates. Otherwise, do nothing.
 *  This function assumes the caller holds the mutex lock.
 */
void __logical_time_complete(instant_t time) {
    if (__fed_has_downstream) {
        send_time(LOGICAL_TIME_COMPLETE, time);
    }
}

/** If this federate depends on upstream federates or sends data to downstream
 *  federates, then notify the RTI of the next event on the event queue.
 *  If there are upstream federates, then this will block until either the
 *  RTI grants the advance to the requested time or the wait for the response
 *  from the RTI is interrupted by a change in the event queue (e.g., a
 *  physical action triggered).  This returns either the specified time or
 *  a lesser time when it is safe to advance logical time to the returned time.
 *  The returned time may be less than the specified time if there are upstream
 *  federates and either the RTI responds with a lesser time or
 *  the wait for a response from the RTI is interrupted by a
 *  change in the event queue.
 *  This function assumes the caller holds the mutex lock.
 */
 instant_t __next_event_time(instant_t time) {
     if (!__fed_has_downstream && !__fed_has_upstream) {
         // This federate is not connected (except possibly by physical links)
         // so there is no need for the RTI to get involved.
         return time;
     }

     // FIXME: The returned value t is a promise that, absent inputs from
     // another federate, this federate will not produce events earlier than t.
     // But if there are downstream federates and there is
     // a physical action (not counting receivers from upstream federates),
     // then we can only promise up to current physical time.
     // This will result in this federate busy waiting, looping through this code
     // and notifying the RTI with next_event_time(current_physical_time())
     // repeatedly.

     // If there are upstream federates, then we need to wait for a
     // reply from the RTI.

     // If time advance has already been granted for this time or a larger
     // time, then return immediately.
     if (__tag >= time) {
         return time;
     }

     send_time(NEXT_EVENT_TIME, time);
     // printf("DEBUG: Federate %d sent next event time %lld.\n", __my_fed_id, time - start_time);

     // If there are no upstream federates, return immediately, without
     // waiting for a reply. This federate does not need to wait for
     // any other federate.
     // FIXME: If fast execution is being used, it may be necessary to
     // throttle upstream federates.
     if (!__fed_has_upstream) {
         return time;
     }

     __tag_pending = true;
     while (__tag_pending) {
         // Wait until either something changes on the event queue or
         // the RTI has responded with a TAG.
         // printf("DEBUG: Federate %d pthread_cond_wait\n", __my_fed_id);
         if (pthread_cond_wait(&event_q_changed, &mutex) != 0) {
             fprintf(stderr, "ERROR: pthread_cond_wait errored.\n");
         }
         // printf("DEBUG: Federate %d pthread_cond_wait returned\n", __my_fed_id);

         if (__tag_pending) {
             // The RTI has not replied, so the wait must have been
             // interrupted by activity on the event queue.
             // If there is now an earlier event on the event queue,
             // then we should return with the time of that event.
             event_t* head_event = (event_t*)pqueue_peek(event_q);
             if (head_event != NULL && head_event->time < time) {
                 return head_event->time;
             }
             // If we get here, any activity on the event queue is not relevant.
             // Either the queue is empty or whatever appeared on it
             // has a timestamp greater than this request.
             // Keep waiting for the TAG.
         }
     }
     return __tag;
}

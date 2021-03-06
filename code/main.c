#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "udp.h"
#include <pthread.h>
#include "time.h"
#include <sys/time.h>
#include <sys/times.h>
#include <semaphore.h>

//global variables
static pthread_t get_sender;
static pthread_t receive;
static pthread_t controller;
static pthread_t signal_ack;

static UDPConn* conn=NULL;

char recvBuf[64];
char value_string[20];
float dt=0.003;

//Mutex
static sem_t sem_y;
static sem_t sem_get;

typedef struct UDPpackets {
 char  start[6];
 char  get[4];
 char  set[5];
 char  stop[5];
 char  signal[11];
} UDPpackets;

UDPpackets message;


//variables
float yvalue;
int reference=1;
int size_response_server=0;


void* pthread_get_sender(void *args);
void* pthread_receive(void *args);
void* pthread_signal_ack(void *args);
void* pthread_controller(void *args);
void init_struct(UDPpackets* message);
void init_server( UDPpackets* message);
void get_y_value(char* recvBuf, int size_response_server);


// get the y value from the receive buffer
void get_y_value(char* recvBuf, int size_response_server){

	yvalue= atof(&recvBuf[8]);

}


void* pthread_get_sender(void *args){

	struct timespec deadline;

	deadline.tv_sec = 0;
	deadline.tv_nsec = dt*1000;

 	while(1)
	{
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
		udpconn_send(conn, message.get);
		printf("Send GET signal\n");
 	}

 	return NULL;
}


//Listen for messages from the server
void* pthread_receive(void *args){

	while(1){

		size_response_server=udpconn_receive(conn, recvBuf, sizeof(recvBuf));

		if (recvBuf[0] == 'S'){
		 	sem_post(&sem_get);
		}

		if (recvBuf[0] == 'G') {
			get_y_value(recvBuf, size_response_server);
			sem_post(&sem_y);

		}
	}
}

//Respond to the signal sent from the server
void* pthread_signal_ack(void *args){

	while(1){
		sem_wait(&sem_get);
		udpconn_send(conn, message.signal);
	}

	return NULL;
}

// Pid controller
void* pthread_controller(void *args){

	char pid_message[20];
	float error=0.0, integral=0.0, derivative=0.0, prev_error=0.0;

	while(1) {
	sem_wait(&sem_y);
	error = reference - yvalue;

	integral    += error * dt;
	derivative  = (error - prev_error) / dt;
	prev_error  = error;

	sprintf(value_string, "%f", 1.0*error + 805*integral + 0*derivative);


	strcpy(pid_message, message.set);
	strcat(pid_message, value_string);
	udpconn_send(conn, pid_message);

	}

}

// Init the struct with the messages to sent to the server
void init_struct(UDPpackets* message){
	strcpy( message->start, "START");
	strcpy( message->get, "GET");
	strcpy( message->set, "SET:");
	strcpy( message->stop, "STOP");
	strcpy( message->signal, "SIGNAL_ACK");
}

// Initialize communication with the server
void init_server( UDPpackets* message){


	conn=udpconn_new("10.100.23.147", 9999); // ip is found in the terminal
  memset(recvBuf, 0, sizeof(recvBuf));
  udpconn_send(conn, message->start);

}



int main(void){

	init_struct(&message);

	init_server(&message);


	sem_init(&sem_y, 0, 0);
	sem_init(&sem_get, 0, 0);

	if (pthread_create(&get_sender, NULL, pthread_get_sender, NULL)!= 0){
		printf("Error creating thread. \n");
		return 0;
	}

	if (pthread_create(&receive, NULL, pthread_receive, NULL)!= 0){
		printf("Error creating thread.");
		return 1;
	}


	if (pthread_create(&signal_ack, NULL,pthread_signal_ack, NULL) != 0){
		printf("Error creating thread.");
		return 2;
	}

	if (pthread_create(&controller, NULL, pthread_controller, NULL)!= 0){
		printf("Error creating thread. \n");
		return 0;
	}

	sleep(1);
	reference= 0;
	sleep(1);
	udpconn_send(conn, message.stop);



	return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "string.h"
#include <signal.h>
#include <semaphore.h>
#include <sys/msg.h>

#define PIPE_NAME "my_pipe"
#define LOG_NAME "log.txt"
// #define DEBUG
#define BUFFER_LENGTH 100
#define TIME_NOW_LENGTH 9
#define CONFIG_FILE_NAME "config.txt"
#define FLIGHT_CODE_MAX_LENGTH 15
#define DEBUG
#define UNUSED_SLOT 0
#define WAITING_ORDERS 1

//HEADER FILE
typedef struct config_options {
  int ut;
  int takeoff_dur, takeoff_int;
  int landing_dur, landing_int;
  int max_holding, min_holding;
  int max_takeoffs;
  int max_landings;
}Config_options;

typedef struct stats{
  int num_created_flights;
  int num_landed;
  int avg_wait_land_time;
  int num_takeoffs;
  int avg_wait_takeoff_time;
  int avg_holdings;
  int avg_emergency_holdings;
  int redirected_flights;
  int num_rejected;
}Stats;

typedef struct{
  Stats stats;
  int flight_slots[100];
  //PROBLEMA: RESOLVER ISTO
} mem_structure;
mem_structure* shared_memory;

typedef struct ll_threads* ptr_ll_threads;
typedef struct ll_threads{
  pthread_t this_thread;
  ptr_ll_threads next;
}Ll_threads;

typedef struct ll_departures_to_create* ptr_ll_departures_to_create;
typedef struct ll_departures_to_create{
  char flight_code[FLIGHT_CODE_MAX_LENGTH];
  int init;
  int takeoff;
  ptr_ll_departures_to_create next;
}Ll_departures_to_create;

typedef struct ll_arrivals_to_create* ptr_ll_arrivals_to_create;
typedef struct ll_arrivals_to_create{
  char flight_code[FLIGHT_CODE_MAX_LENGTH];
  int init;
  int eta;
  int fuel;
  ptr_ll_arrivals_to_create next;
}Ll_arrivals_to_create;

//struct for messageQ
typedef struct message_struct{
  long msgtype; //2 for non-urgent 1 for urgent
  char type; //a for arrival d for departure
  int takeoff;
  int fuel;
  int eta;
  int type_rcv;
} message;

typedef struct message_slot{
  long msgtype;
  int slot;
} message_give_slot;

typedef struct queue_to_arrive* ptr_ll_queue_arrive;
typedef struct queue_to_arrive{
  int slot;
  int urgent;
  int fuel;
  int eta;
  ptr_ll_queue_arrive next;
}node_queue_to_arrive;

typedef struct queue_to_departure* ptr_ll_queue_departure;
typedef struct queue_to_departure{
  int slot;
  int takeoff;
  ptr_ll_queue_departure next;
}node_queue_to_departure;

void init_shared_memory();
void read_config();
void sim_manager();
int validate_command(char* command);
int check_only_numbers(char* str);
void get_current_time_to_string(char* return_string);
ptr_ll_threads insert_thread(ptr_ll_threads list, pthread_t new_thread);
void control_tower();
ptr_ll_departures_to_create sorted_insert_departures(ptr_ll_departures_to_create list, ptr_ll_departures_to_create new);
ptr_ll_arrivals_to_create sorted_insert_arrivals(ptr_ll_arrivals_to_create list, ptr_ll_arrivals_to_create new);
void *pipe_worker();
void *time_worker();
void *departure_worker();
void* arrival_worker();
ptr_ll_threads remove_thread_from_ll(ptr_ll_threads list,pthread_t thread_id);
void cleanup();
void fill_message_arrivals(message* msg, Ll_arrivals_to_create flight_info, int type_rcv);
void fill_message_departures(message* msg, Ll_departures_to_create flight_info,int type_rcv);
//CONTROL tower
void* manage_worker();
ptr_ll_queue_arrive sorted_insert_queue_to_arrive(ptr_ll_queue_arrive list, ptr_ll_queue_arrive new);
ptr_ll_queue_departure sorted_insert_queue_to_departure(ptr_ll_queue_departure list, ptr_ll_queue_departure new);

//signals
void sigusr1_handler(int signal);

//GLOBAL VARIABLES
Config_options options;
//linked lists
ptr_ll_threads thread_list = NULL;
ptr_ll_departures_to_create departures_list = NULL;
ptr_ll_arrivals_to_create arrivals_list = NULL;
ptr_ll_queue_arrive queue_to_arrive = NULL;
ptr_ll_queue_departure queue_to_departure = NULL;
int shmid;
int fd;
int msq_id;
// 1 FOR URGENT
// 2 FOR SEND TO CONTROL TOWER
// 3+ for send to each flight


//cona//

// então todos os que queres ignorar metes para dentro de um sigset
// por exemplo sigemptyset (&block_sigs); (isto é paraa esvasiares o set, para teres a certeza que n há lá lixo)
// depois adicionas os sinais que queres ignorar, assim:
// 	sigfillset
// sigremset sigint
// sigrem set sigusr1
// no final de teres adicionado todos os que queres ignorar, fazes:
// pthread_sigmask
// sigprocmask(SIG_BLOCK,&block_sigs, NULL);
// para dizer que os queres bloquear
// depois tens que apanhar o sigint e o sigusr1
// eu costumava fazer assim:
// if(signal(SIGINT,funcao_de_tratamento)==SIG_ERR){
//            funcao_de_erro();
// }

//  sigdelset(&sigusr1.sa_mask, SIGUSR1);
//  sigdelset(&sigusr1.sa_mask, SIGINT);


int time_counter;
int atm_departures = 0;
int atm_arrivals = 0;
FILE* log_fich;
pid_t pid_ct;
//signals
struct sigaction sigusr1;

// semaphores
sem_t* sem_write_log;
sem_t* sem_shared_stats;
sem_t* sem_shared_flight_slots;
sem_t* sem_shared_crtl_c;
pthread_mutex_t mutex_ll_threads = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_time_counter = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_cond = PTHREAD_MUTEX_INITIALIZER;
//PARA CT
pthread_mutex_t mutex_ll_departures_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_landings_queue = PTHREAD_MUTEX_INITIALIZER;
// cond variables
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

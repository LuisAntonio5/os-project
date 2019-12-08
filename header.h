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
// #define DEBUG
#define UNUSED_SLOT 0
#define WAITING_ORDERS -1
#define NO_FUEL -2
#define GO_28L -3 //ATERRAGEM
#define GO_28R -4 //ATERRAGEM
#define GO_01L -5 //DESCOLAGEM
#define GO_01R -6 //DESCOLAGEM
#define REJECTED -7
#define URGENT_REQUEST -8
#define SEM_LOG "SEM_LOG"
#define SEM_SHARED_STATS "SEM_SHARED_STATS"
#define SEM_SHARED_FLIGHT_SLOTS "SEM_SHARED_FLIGHT_SLOT"
#define SEM_TIME_COUNTER "SEM_TIME_COUNTER"
#define SEM_RUNWAY "SEM_RUNWAY"
#define SEM_GO_TIME_SM "SEM_GO_TIME_SM"
#define SEM_GO_TIME_CT "SEM_GO_TIME_CT"
#define SEM_COND "SEM_COND"
#define SEM_CTRL_C "SEM_CTRL_C"

//HOLDING PARA TODOS OS VALORES > 0

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
  float avg_wait_land_time; //TODO:
  int num_takeoffs;
  float avg_wait_takeoff_time; //TODO:
  float avg_holdings; //TODO:
  float avg_emergency_holdings; //TODO:
  int redirected_flights;
  int num_rejected;
}Stats;

typedef struct{
  Stats stats;
  int* flight_slots;
  int time_counter;
  int ctrl_c;
  // int runway[2][4];
  /* ____________________________
    | P1 | P2 | TIMEP1 | TIMEP2 | ARRIVALS -> If(P1 && !TIMEP1{VOO a acontecer em p1} If(!p1 && TimeP1){Segurança}
    | P3 | P4 | TIMEP3 | TIMEP4 | DEPARTURES
    _____________________________ */

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

typedef struct wait_queue_arrivals* ptr_ll_wait_arrivals;
typedef struct wait_queue_arrivals{
  int slot;
  int urgent;
  int fuel;
  int eta;
  ptr_ll_wait_arrivals next;
}node_wait_queue_arrivals;

typedef struct ptr_wait_queue_arrivals* ptr_to_ptr_wait_queue_arrivals;
typedef struct ptr_wait_queue_arrivals{
  //SERVE PARA ORDENAR POR WAITING TIME
  ptr_ll_wait_arrivals arrival;
  ptr_to_ptr_wait_queue_arrivals next;
}node_ptr_wait_queue_arrivals;

typedef struct wait_queue_departures* ptr_ll_wait_departures;
typedef struct wait_queue_departures{
  int slot;
  int takeoff;
  ptr_ll_wait_departures next;
}node_wait_queue_departures;

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
void* notify_worker();
ptr_ll_threads remove_thread_from_ll(ptr_ll_threads list,pthread_t thread_id);
void cleanup();
void zero_runways();
void fill_message_arrivals(message* msg, Ll_arrivals_to_create flight_info, int type_rcv);
void fill_message_departures(message* msg, Ll_departures_to_create flight_info,int type_rcv);
void sigusr1_handler(int signal);
void sigint_handler(int signal);
void destroy_everything();
//CONTROL tower
void* time_worker_ct();
void* manage_worker();
ptr_ll_wait_arrivals sorted_insert_wait_queue_arrivals(ptr_ll_wait_arrivals list, ptr_ll_wait_arrivals new);
ptr_ll_wait_departures sorted_insert_wait_queue_departures(ptr_ll_wait_departures list, ptr_ll_wait_departures new);
ptr_ll_wait_arrivals insert_to_arrive(ptr_ll_wait_arrivals list, ptr_ll_wait_arrivals new);
ptr_ll_wait_departures insert_to_departure(ptr_ll_wait_departures list, ptr_ll_wait_departures new);
int schedule_flights();
void sub_1_times_runways();
void send_holdings();
void verify_arrivals_in_queue();
//GLOBAL VARIABLES
Config_options options;
//linked lists
ptr_ll_threads thread_list = NULL;
ptr_ll_departures_to_create departures_list = NULL;
ptr_ll_arrivals_to_create arrivals_list = NULL;
ptr_ll_wait_arrivals wait_queue_arrivals = NULL;
ptr_ll_wait_departures wait_queue_departures = NULL;
ptr_ll_wait_arrivals arrival_queue = NULL;
ptr_ll_wait_departures departure_queue = NULL;
int shmid;
int fd;
int msq_id;
int shmidSLOTS;
// 1 FOR URGENT
// 2 FOR SEND TO CONTROL TOWER
// 3+ for send to each flight
pid_t pid_ct;
int atm_departures = 0;
int atm_arrivals = 0;
FILE* log_fich;
int runways[2][4];
struct sigaction sa;
int n_urgent_created = 0;
int n_arrivals_created = 0;
// semaphores
sem_t* sem_write_log;
sem_t* sem_shared_stats;
sem_t* sem_shared_flight_slots;
sem_t* sem_shared_crtl_c;
sem_t* sem_shared_runway;
sem_t* sem_shared_time_counter;
//semaphore for time counter sync
sem_t* sem_go_time_sm;
sem_t* sem_go_time_ct;
//semaphore to notify threads from other process
sem_t* sem_cond;
pthread_mutex_t mutex_ll_threads = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_cond = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_cond_cleanup = PTHREAD_MUTEX_INITIALIZER;//FALTA DESTRUIR
pthread_mutex_t mutex_arrivals_created = PTHREAD_MUTEX_INITIALIZER;
//PARA CT
pthread_mutex_t mutex_ll_wait_departures_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_wait_arrivals_queue = PTHREAD_MUTEX_INITIALIZER;
// cond variables
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_cleanup = PTHREAD_COND_INITIALIZER;

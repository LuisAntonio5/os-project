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
#define DEBUG
#define BUFFER_LENGTH 100
#define TIME_NOW_LENGTH 9
#define CONFIG_FILE_NAME "config.txt"
#define FLIGHT_CODE_MAX_LENGTH 15


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
  int* flight_slots;
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

//GLOBAL VARIABLES
Config_options options;
//linked lists
ptr_ll_threads thread_list = NULL;
ptr_ll_departures_to_create departures_list = NULL;
ptr_ll_arrivals_to_create arrivals_list = NULL;
int shmid;
int fd;
int msq_id;
int time_counter;
int atm_departures = 0;
int atm_arrivals = 0;
FILE* log_fich;

// semaphores
sem_t* sem_write_log;
sem_t* sem_shared_stats;
sem_t* sem_shared_flight_slots;
sem_t* sem_manage_flights; // if 0 means that there is no flight to manage
pthread_mutex_t mutex_ll_threads = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_ll_create_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_departures = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_atm_arrivals = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_time_counter = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_cond = PTHREAD_MUTEX_INITIALIZER;
// cond variables
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

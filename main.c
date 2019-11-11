//gcc main.c -lpthread -D_REENTRANT -Wall -o projeto
//ps -ef | grep projeto
//ipcs
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

void sigint(int num){
  #ifdef DEBUG
  ptr_ll_departures_to_create aux = departures_list;
  while (aux != NULL) {
    printf("%s %d\n", aux->flight_code, aux->init);
    aux = aux->next;
  }
  ptr_ll_arrivals_to_create aa = arrivals_list;
  while (aa != NULL) {
    printf("%s %d\n", aa->flight_code, aa->init);
    aa = aa->next;
  }
  printf("\n\n\n");
  ptr_ll_threads bbb = thread_list;
  while (bbb != NULL) {
    printf("%ld \n", bbb->this_thread);
    bbb = bbb->next;
  }
  #endif
  cleanup();
  exit(0);


}

int main(void){
  read_config(&options);

  // while(1){
  //   gettimeofday(&now, NULL);
  //   printf("%ld\n", now.tv_usec);
  //   sleep(1);
  // }

  //create pipe
  unlink(PIPE_NAME);
  if(mkfifo(PIPE_NAME, O_CREAT|0600)<0){
    printf("Error creating pipe.\n");
  }
  #ifdef DEBUG
  printf("Pipe created\n");
  #endif

  //clear log.txt
  log_fich = fopen(LOG_NAME, "w");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  fclose(log_fich);
  #ifdef DEBUG
  printf("log.txt cleared\n");
  #endif


  //CREATE SHARED MEMORY
  shmid = shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT | 0766);
  if(shmid == -1){
    printf("Error creating shared memory\n");
    exit(-1);
  }
  shared_memory = (mem_structure*)shmat(shmid,NULL,0);
  #ifdef DEBUG
  printf("Shared memory created\n");
  #endif
  //CREATE MESSAGE QUEUE
  if((msq_id = msgget(IPC_PRIVATE, IPC_CREAT|0700)) != -1 ){
    #ifdef DEBUG
    printf("MESSAGE QUEUE CREATED\n");
    #endif
  }
  //INIT SHARED MEMORY
  init_shared_memory();
  #ifdef DEBUG
  printf("Shared memory initialized\n");
  #endif
  //semaphore to write on log.txt
  sem_unlink("SEM_LOG");
  sem_write_log = sem_open("SEM_LOG", O_CREAT|O_EXCL, 0700, 1);
  //semaphore to write on shared mem
  sem_unlink("sem_shared_stats");
  sem_shared_stats = sem_open("sem_shared_stats", O_CREAT|O_EXCL, 0700, 1);
  //create control tower process
  if(fork() == 0){
    control_tower();
  }
  else{
    sim_manager();
  }

  return 0;
}

void sim_manager(){
  pthread_t new_thread;
  signal(SIGINT,sigint);
  // create thread to manage pipe input
  if (pthread_create(&new_thread,NULL,pipe_worker, NULL)){
    printf("error creating thread\n");
    exit(-1);
  }

  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = insert_thread(thread_list, new_thread);
  pthread_mutex_unlock(&mutex_ll_threads);

  // create thread to manage time
  if (pthread_create(&new_thread,NULL,time_worker, NULL)){
    printf("error creating thread\n");
    exit(-1);
  }

  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = insert_thread(thread_list, new_thread);
  pthread_mutex_unlock(&mutex_ll_threads);

  // fix este join, funciona só para nao acabar o programa
  pthread_join(new_thread,NULL);
}


//ESCREVER NO LOG E VERIFICAR ERROS
void *pipe_worker(){
  char buffer[BUFFER_LENGTH];
  char time_string[TIME_NOW_LENGTH];

  fd = open(PIPE_NAME,O_RDWR);
  printf("%d\n", options.max_takeoffs);
  while(1){
    read(fd,buffer,sizeof(buffer));
    #ifdef DEBUG
    printf("%s", buffer);
    #endif
    buffer[strlen(buffer)-1] = '\0';
    if(validate_command(buffer)){
      sem_wait(sem_write_log);
      log_fich = fopen(LOG_NAME,"a");
      if(log_fich == NULL){
        printf("Error opening %s", LOG_NAME);
      }
      //GET CURRENT TIME
      get_current_time_to_string(time_string);
      printf("%s NEW COMMAND => %s\n",time_string, buffer);
      fprintf(log_fich, "%s NEW COMMAND => %s\n",time_string,buffer);
      fclose(log_fich);
      sem_post(sem_write_log);
    }
    else{
      //GET CURRENT TIME

      sem_wait(sem_write_log);
      log_fich = fopen(LOG_NAME,"a");
      if(log_fich == NULL){
        printf("Error opening %s", LOG_NAME);
      }
      //GET CURRENT TIME
      get_current_time_to_string(time_string);
      printf("%s WRONG COMMAND => %s\n",time_string, buffer);
      fprintf(log_fich, "%s WRONG COMMAND => %s\n",time_string ,buffer);
      fclose(log_fich);
      sem_post(sem_write_log);
    }

    //clearing buffer after each command -> avoids problems
    memset(buffer,0,sizeof(buffer));


  }

}

void * time_worker(){
  //initialize time counter
  ptr_ll_arrivals_to_create currentArrival;
  ptr_ll_threads aux;
  ptr_ll_departures_to_create currentDepartures;
  pthread_t new_thread;
  time_counter = 0;
  while(1){
    pthread_mutex_lock(&mutex_time_counter);
    time_counter++;
    pthread_mutex_unlock(&mutex_time_counter);
    //printf("%d\n", time_counter);
    //counting time in milliseconds
    usleep(options.ut * 1000);

    if (arrivals_list != NULL && arrivals_list->init == time_counter) {
      //time to create at least one arrival
      currentArrival = arrivals_list;
      printf("a\n");
      while(currentArrival && currentArrival->init == time_counter){
        if (pthread_create(&new_thread,NULL,arrival_worker, currentArrival->flight_code)){
          printf("error creating thread\n");
          exit(-1);
        }

        pthread_mutex_lock(&mutex_ll_create_arrivals);
        pthread_mutex_lock(&mutex_ll_threads);
        thread_list = insert_thread(thread_list, new_thread);
        arrivals_list = currentArrival->next;
        currentArrival = arrivals_list;
        pthread_mutex_unlock(&mutex_ll_threads);
        pthread_mutex_unlock(&mutex_ll_create_arrivals);

        //print da lista das thread_list
        aux= thread_list;
        while(aux){
          printf("%ld\n", aux->this_thread);
          aux=aux->next;
        }
      }
    }

    if (departures_list != NULL && departures_list->init == time_counter) {
      //time to create at least one departure
      currentDepartures = departures_list;
      printf("a\n");
      while(currentDepartures && currentDepartures->init == time_counter){
        if (pthread_create(&new_thread,NULL,departure_worker, currentDepartures->flight_code)){
          printf("error creating thread\n");
          exit(-1);
        }

        pthread_mutex_lock(&mutex_ll_create_departures);
        pthread_mutex_lock(&mutex_ll_threads);
        thread_list = insert_thread(thread_list, new_thread);
        departures_list = currentDepartures->next;
        // free(currentDepartures);
        currentDepartures = departures_list;
        pthread_mutex_unlock(&mutex_ll_threads);
        pthread_mutex_unlock(&mutex_ll_create_departures);

        //print da lista das thread_list
        aux= thread_list;
        while(aux){
          printf("%ld\n", aux->this_thread);
          aux=aux->next;
        }
      }
    }
  }
}

int validate_command(char* command){
  //Validate commands and split command by " " for thread data creation
  int i = 0;
  char* token;
  char aux[BUFFER_LENGTH];
  char** split_command;
  int init_time,takeoff,eta,fuel;
  ptr_ll_departures_to_create new_departure;
  ptr_ll_arrivals_to_create new_arrival;
  strcpy(aux,command);
  split_command = (char**)calloc(1,sizeof(char*));
  token = strtok(aux," ");
  while(token){
    split_command = (char**)realloc(split_command,(i+1)*sizeof(char*));
    split_command[i] = (char*)calloc(strlen(token)+1,sizeof(char));
    strcpy(split_command[i],token);
    token = strtok(NULL, " ");
    i++;
  }
  if(i == 6 && strcmp(split_command[0],"DEPARTURE") == 0 && strcmp(split_command[2],"init:") == 0 && strcmp(split_command[4],"takeoff:") == 0){
    //Validate if everything is numbers in init slot and takeoff slot
    if(!check_only_numbers(split_command[3]) || !check_only_numbers(split_command[5])){
      #ifdef DEBUG
      printf("INVALID COMMAND, EXPECTED INT VALUE\n");
      #endif
      return 0;
    }
    else{
      //Reach here if everything is OK
      init_time = atoi(split_command[3]);
      // todo : validate init time!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      pthread_mutex_lock(&mutex_time_counter);
      if(init_time<time_counter || atm_departures == options.max_takeoffs){
        #ifdef DEBUG
        printf("INVALID COMMAND\n");
        #endif
        return 0;
      }
      pthread_mutex_unlock(&mutex_time_counter);
      takeoff = atoi(split_command[5]);
      new_departure = (ptr_ll_departures_to_create) malloc(sizeof(Ll_departures_to_create));
      strcpy(new_departure->flight_code,split_command[1]);
      new_departure->init = init_time;
      new_departure->takeoff= takeoff;
      new_departure->next = NULL;
      printf("%s %d %d\n", new_departure->flight_code, new_departure->init, new_departure->takeoff);
      pthread_mutex_lock(&mutex_atm_departures);
      atm_departures++;
      pthread_mutex_unlock(&mutex_atm_departures);
      pthread_mutex_lock(&mutex_ll_create_departures);
      departures_list = sorted_insert_departures(departures_list, new_departure);
      pthread_mutex_unlock(&mutex_ll_create_departures);

      return 1;
    }
  }
  else if(i==8 && strcmp(split_command[0],"ARRIVAL") == 0 && strcmp(split_command[2],"init:") == 0 && strcmp(split_command[4],"eta:") == 0 && strcmp(split_command[6],"fuel:") == 0){
    //Validate if everything is numbers in init slot, eta slot and fuel slot
    if(!check_only_numbers(split_command[3]) || !check_only_numbers(split_command[5]) || !check_only_numbers(split_command[7])){
      #ifdef DEBUG
      printf("INVALID COMMAND, EXPECTED INT VALUE\n");
      #endif
      return 0;
    }
    else{
      init_time = atoi(split_command[3]);
      eta = atoi(split_command[5]);
      fuel = atoi(split_command[7]);
      pthread_mutex_lock(&mutex_time_counter);
      if (eta > fuel || init_time<time_counter || atm_arrivals == options.max_landings ) {
        #ifdef DEBUG
        printf("INVALID COMMAND\n");
        #endif
        return 0;
      }
      pthread_mutex_unlock(&mutex_time_counter);
      //Reach here if everything is OK
      new_arrival = (ptr_ll_arrivals_to_create) malloc(sizeof(Ll_arrivals_to_create));
      strcpy(new_arrival->flight_code,split_command[1]);
      new_arrival->init = init_time;
      new_arrival->eta = eta;
      new_arrival->fuel = fuel;
      new_arrival->next = NULL;
      printf("%s %d %d\n", new_arrival->flight_code, new_arrival->init, new_arrival->fuel);

      pthread_mutex_lock(&mutex_atm_arrivals);
      atm_arrivals++;
      pthread_mutex_unlock(&mutex_atm_arrivals);
      pthread_mutex_lock(&mutex_ll_create_arrivals);
      arrivals_list = sorted_insert_arrivals(arrivals_list, new_arrival);
      pthread_mutex_unlock(&mutex_ll_create_arrivals);

      return 1;
    }
  }
  else{
    #ifdef DEBUG
    printf("INVALID COMMAND\n");
    #endif
    return 0;
  }
  return 1;
}

int check_only_numbers(char* str){
  int i;
  for(i=0;i<strlen(str);i++){
    if(str[i] < '0' || str[i] > '9'){
      return 0;
    }
  }
  return 1;
}

void read_config(Config_options* Ptr_options){
  FILE* f;
  char temp[15];
  f = fopen(CONFIG_FILE_NAME,"r");
  if(f==NULL){
    printf("Error opening config.txt\n");
    exit(-1);
  }
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d",&Ptr_options->ut);
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d, %d",&Ptr_options->takeoff_dur,&Ptr_options->takeoff_int);
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d, %d",&Ptr_options->landing_dur,&Ptr_options->landing_int);
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d, %d",&Ptr_options->max_holding,&Ptr_options->min_holding);
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d",&Ptr_options->max_takeoffs);
  fflush(stdin);
  fgets(temp,sizeof(temp),f);
  sscanf(temp,"%d",&Ptr_options->max_landings);
  fclose(f);
}

void get_current_time_to_string(char* return_string){
  // TIME VARIABLES
  struct tm* tm_info_now;
  time_t now_time;
  // clear return_string, avoiding errors
  //memset(return_string,0,sizeof(return_string));
  time(&now_time);
  tm_info_now = localtime(&now_time);
  strftime(return_string, TIME_NOW_LENGTH, "%H:%M:%S", tm_info_now);
}

void init_shared_memory(){
  shared_memory->stats.num_created_flights = 0;
  shared_memory->stats.num_landed = 0;
  shared_memory->stats.avg_wait_land_time = 0;
  shared_memory->stats.num_takeoffs = 0;
  shared_memory->stats.avg_wait_takeoff_time = 0;
  shared_memory->stats.avg_holdings = 0;
  shared_memory->stats.avg_emergency_holdings = 0;
  shared_memory->stats.redirected_flights = 0;
  shared_memory->stats.num_rejected = 0;
  shared_memory->flight_slots = (int*)calloc(options.max_landings + options.max_takeoffs,sizeof(int));
}

ptr_ll_threads insert_thread(ptr_ll_threads list, pthread_t new_thread){
  ptr_ll_threads new = (ptr_ll_threads) malloc(sizeof(Ll_threads));
  new->this_thread = new_thread;
  if (list == NULL) {
    new->next = NULL;
  }
  else{
    new->next = list;
  }
  return new;
}

//MDUAR FICHEIRO
void control_tower(){
  char time_string[TIME_NOW_LENGTH];
  sem_wait(sem_write_log);
  log_fich = fopen(LOG_NAME,"a");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  //GET CURRENT TIME
  get_current_time_to_string(time_string);
  printf("%s Created process: %d and %d\n", time_string, getppid(), getpid());
  fprintf(log_fich, "%s Created process: %d and %d\n", time_string, getppid(), getpid());
  fclose(log_fich);
  sem_post(sem_write_log);
  exit(0);
}

ptr_ll_departures_to_create sorted_insert_departures(ptr_ll_departures_to_create list, ptr_ll_departures_to_create new){
  ptr_ll_departures_to_create current = list, ant = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if (new->init <= current->init) {
        if (ant == NULL) {
          new->next = current;
          return new;
        }
        else{
          ant->next = new;
          new->next = current;
          return list;
        }
      }
      ant = current;
      current = current->next;
    }
    ant->next = new;
    return list;
  }
}

ptr_ll_arrivals_to_create sorted_insert_arrivals(ptr_ll_arrivals_to_create list, ptr_ll_arrivals_to_create new){
  ptr_ll_arrivals_to_create current = list, ant = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if (new->init <= current->init) {
        if (ant == NULL) {
          new->next = current;
          return new;
        }
        else{
          ant->next = new;
          new->next = current;
          return list;
        }
      }
      ant = current;
      current = current->next;
    }
    ant->next = new;
    return list;
  }
}

void* departure_worker(void* ptr_ll_departure){
  Ll_departures_to_create flight_info = *((ptr_ll_departures_to_create)ptr_ll_departure);
  char time_string[TIME_NOW_LENGTH];

  sem_wait(sem_write_log);
  log_fich = fopen(LOG_NAME,"a");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  //GET CURRENT TIME
  get_current_time_to_string(time_string);
  fprintf(log_fich,"%s DEPARTURE %s CREATED\n",time_string,flight_info.flight_code);
  printf("%s DEPARTURE %s CREATED\n",time_string,flight_info.flight_code);
  fclose(log_fich);
  sem_post(sem_write_log);

  // FALTA FAZER CONTROLO DE PISTAS 2 META
  usleep(1000 * flight_info.takeoff);

  sem_wait(sem_write_log);
  log_fich = fopen(LOG_NAME,"a");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  //GET CURRENT TIME
  get_current_time_to_string(time_string);
  fprintf(log_fich,"%s DEPARTURE %s CONCLUDED\n",time_string,flight_info.flight_code);
  printf("DEPARTURE %s CONCLUDED\n",flight_info.flight_code);
  fclose(log_fich);
  sem_post(sem_write_log);

  pthread_mutex_lock(&mutex_atm_departures);
  atm_departures--;
  pthread_mutex_unlock(&mutex_atm_departures);
  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = remove_thread_from_ll(thread_list,pthread_self());
  free(ptr_ll_departure);
  pthread_mutex_unlock(&mutex_ll_threads);

  pthread_exit(NULL);
}

void* arrival_worker(void* ptr_ll_arrival){
  Ll_arrivals_to_create flight_info = *((ptr_ll_arrivals_to_create)ptr_ll_arrival);
  char time_string[TIME_NOW_LENGTH];

  sem_wait(sem_write_log);
  log_fich = fopen(LOG_NAME,"a");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  //GET CURRENT TIME
  get_current_time_to_string(time_string);
  fprintf(log_fich, "%s ARRIVAL %s CREATED\n",time_string,flight_info.flight_code);
  printf("%s ARRIVAL %s CREATED\n",time_string,flight_info.flight_code);
  fclose(log_fich);
  sem_post(sem_write_log);

  // FALTA FAZER CONTROLO DE PISTAS
  usleep(1000 * flight_info.eta);

  sem_wait(sem_write_log);
  log_fich = fopen(LOG_NAME,"a");
  if(log_fich == NULL){
    printf("Error opening %s", LOG_NAME);
  }
  //GET CURRENT TIME
  get_current_time_to_string(time_string);
  fprintf(log_fich,"%s ARRIVAL %s CONCLUDED\n",time_string,flight_info.flight_code);
  printf("ARRIVAL %s CONCLUDED\n",flight_info.flight_code);
  fclose(log_fich);
  sem_post(sem_write_log);

  pthread_mutex_lock(&mutex_atm_arrivals);
  atm_arrivals--;
  pthread_mutex_unlock(&mutex_atm_arrivals);
  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = remove_thread_from_ll(thread_list,pthread_self());
  free(ptr_ll_arrival);
  pthread_mutex_unlock(&mutex_ll_threads);

  pthread_exit(NULL);
}

ptr_ll_threads remove_thread_from_ll(ptr_ll_threads list,pthread_t thread_id){
  ptr_ll_threads current,aux,ant;
  current = list;
  while(current){
    if(current->this_thread == thread_id){
      if(ant==NULL){
        aux = current->next;
        free(current);
        return aux;
      }
      else{
        ant->next = current->next;
        free(current);
        return list;
      }
    }
    else{
      ant = current;
      current = current->next;
    }
  }
  return list;
}

void cleanup(){
  //CLEAN SHARED MEMORY
  shmdt(shared_memory);
	shmctl(shmid, IPC_RMID,NULL);
  //DESTROY SEMAPHROE
  sem_destroy(sem_write_log);
  sem_destroy(sem_shared_stats);
  pthread_mutex_destroy(&mutex_ll_threads);
  pthread_mutex_destroy(&mutex_ll_create_arrivals);
  pthread_mutex_destroy(&mutex_ll_create_departures);
  //CLEAN MSG QUEUE
  msgctl(msq_id, IPC_RMID, NULL);
}

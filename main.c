//gcc main.c -lpthread -D_REENTRANT -Wall -o ex
//ps -ef | grep ex
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

#define PIPE_NAME "my_pipe"
#define LOG_NAME "log.txt"
#define DEBUG
#define BUFFER_LENGTH 100
#define TIME_NOW_LENGTH 9


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
} mem_structure;
mem_structure* shared_memory;

void init_shared_memory();
void read_config();
void sim_manager();
void *pipe_worker();
int validate_command(char* command);
int check_only_numbers(char* str);
void get_current_time_to_string(char* return_string);
//MDUAR FICHEIRO
void control_tower(){
  exit(0);
}

//GLOBAL VARIABLES
//struct timeval now;
Config_options options;
int shmid;
int fd;
FILE* log_fich;

void sigint(int num){
  fclose(log_fich);
  printf("\n");
  exit(0);
}

int main(void){
  read_config(&options);

  // while(1){
  //   gettimeofday(&now, NULL);
  //   printf("%ld\n", now.tv_usec);
  //   sleep(1);
  // }
  signal(SIGINT,sigint);
  //create pipe
  unlink(PIPE_NAME);
  if(mkfifo(PIPE_NAME, O_CREAT|0600)<0){
    printf("Error creating pipe.\n");
  }
  #ifdef DEBUG
  printf("Pipe created\n");
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
  //INIT SHARED MEMORY
  init_shared_memory();
  #ifdef DEBUG
  printf("Shared memory initialized\n");
  #endif
  //create control tower process
  if(fork() == 0){
    control_tower();
  }
  else{
    sim_manager();
  }
  //CLEAN SHARED MEMORY
  shmdt(shared_memory);
	shmctl(shmid, IPC_RMID,NULL);
  return 0;
}

void sim_manager(){
  pthread_t input_thread;
  pthread_create(&input_thread,NULL,pipe_worker,&input_thread);
  pthread_join(input_thread,NULL);
}

//ESCREVER NO LOG E VERIFICAR ERROS
void *pipe_worker(){
  char buffer[BUFFER_LENGTH];
  char time_string[TIME_NOW_LENGTH];

  fd = open(PIPE_NAME,O_RDWR);
  log_fich = fopen(LOG_NAME,"w");
  if(log_fich < 0){
    printf("Error creating %s", LOG_NAME);
  }
  while(1){
    read(fd,buffer,sizeof(buffer));
    #ifdef DEBUG
    printf("%s", buffer);
    #endif
    buffer[strlen(buffer)-1] = '\0';
    if(validate_command(buffer)){
      //GET CURRENT TIME
      get_current_time_to_string(time_string);
      printf("%s NEW COMMAND => %s\n",time_string, buffer);
      fprintf(log_fich, "%s NEW COMMAND => %s\n",time_string,buffer);
    }
    else{
      //GET CURRENT TIME
      get_current_time_to_string(time_string);
      printf("%s WRONG COMMAND => %s\n",time_string, buffer);
      fprintf(log_fich, "%s WRONG COMMAND => %s\n",time_string ,buffer);
    }

    //clearing buffer after each command -> avoids problems
    memset(buffer,0,sizeof(buffer));


  }

}

int validate_command(char* command){
  //Validate commands and split command by " " for thread data creation
  int i = 0;
  char* token;
  char aux[BUFFER_LENGTH];
  char** split_command;
  int init_time,takeoff,eta,fuel;
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
      takeoff = atoi(split_command[5]);
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
      //Reach here if everything is OK
      init_time = atoi(split_command[3]);
      takeoff = atoi(split_command[5]);
      printf("VALIDO\n");
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
  f = fopen("./config.txt","r");
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
}

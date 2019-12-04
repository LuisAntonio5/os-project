//gcc main.c -lpthread -D_REENTRANT -Wall -o projeto
//ps -ef | grep projeto
//ipcs
// echo "ARRIVAL TP437 init: 100 eta: 100 fuel: 1000" > my_pipe
//TRABALHO REALIZADO POR BERNARDO M S PRIOR 2018285108 e POR LUIS ANTONIO LIMA DA SILVA 2018278644
#include "header.h"


// FIM HEADER

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

  sigusr1.sa_flags = 0;
  sigusr1.sa_handler = sigusr1_handler;
  sigemptyset(&sigusr1.sa_mask);
  sigfillset(&sigusr1.sa_mask);
  sigprocmask(SIG_BLOCK, &sigusr1.sa_mask, NULL);
 // pthread_sigmask(SIG_BLOCK, &sigusr1.sa_mask, NULL);
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
  //zero runways array
  zero_runways();
  #ifdef DEBUG
  printf("Shared memory initialized\n");
  #endif
  //semaphore to write on log.txt
  sem_unlink("SEM_LOG");
  sem_write_log = sem_open("SEM_LOG", O_CREAT|O_EXCL, 0700, 1);
  //semaphore to write on shared mem stats
  sem_unlink("sem_shared_stats");
  sem_shared_stats = sem_open("sem_shared_stats", O_CREAT|O_EXCL, 0700, 1);
  //semaphore to write on shared mem slots
  sem_unlink("sem_shared_flight_slots");
  sem_shared_flight_slots = sem_open("sem_shared_flight_slots", O_CREAT|O_EXCL, 0700, 1);
  //semaphore to write on shared time time_counter
  sem_unlink("SEM_TIME_COUNTER");
  sem_shared_time_counter = sem_open("SEM_TIME_COUNTER", O_CREAT|O_EXCL, 0700, 1);
  //semaphore to write on shared runways
  sem_unlink("SEM_RUNWAY");
  sem_shared_runway = sem_open("SEM_RUNWAY", O_CREAT|O_EXCL, 0700, 1);
  //sems for sync time beetwen processes
  sem_unlink("SEM_GO_TIME_SM");
  sem_go_time_sm = sem_open("SEM_GO_TIME_SM", O_CREAT|O_EXCL, 0700, 1);
  sem_unlink("SEM_GO_TIME_CT");
  sem_go_time_ct = sem_open("SEM_GO_TIME_CT", O_CREAT|O_EXCL, 0700, 0);
  //NOTIFY THREAD FROM OTHER PROCESS
  sem_unlink("SEM_COND");
  sem_cond = sem_open("SEM_COND", O_CREAT|O_EXCL, 0700, 0);
  //create control tower process
  if((pid_ct = fork()) == 0){
    control_tower();
  }
  else{
    sim_manager();
  }

  // //sigaction
  // pthread_sigmask(SIG_UNBLOCK, &sigusr1.sa_mask, NULL);
  // sigusr1.sa_flags = 0;
  // sigusr1.sa_handler = sigusr1_handler;
  // sigemptyset(&sigusr1.sa_mask);
  // sigfillset(&sigusr1.sa_mask);
  // sigdelset(&sigusr1.sa_mask, SIGINT);
  // pthread_sigmask(SIG_BLOCK, &sigusr1.sa_mask, NULL);
  //
  // signal(SIGINT,sigint);
  // pause();
  return 0;
}

void sim_manager(){
  pthread_t new_thread;
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

  // create thread to notify
  if (pthread_create(&new_thread,NULL,notify_worker, NULL)){
    printf("error creating thread\n");
    exit(-1);
  }

  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = insert_thread(thread_list, new_thread);
  pthread_mutex_unlock(&mutex_ll_threads);

  // // fix este join, funciona só para nao acabar o programa
  // pthread_join(new_thread,NULL);

  //sigaction
  pthread_sigmask(SIG_UNBLOCK, &sigusr1.sa_mask, NULL);
  sigusr1.sa_flags = 0;
  sigusr1.sa_handler = sigusr1_handler;
  sigemptyset(&sigusr1.sa_mask);
  sigfillset(&sigusr1.sa_mask);
  sigdelset(&sigusr1.sa_mask, SIGINT);
  pthread_sigmask(SIG_BLOCK, &sigusr1.sa_mask, NULL);

  signal(SIGINT,sigint);
  pause();
}


//ESCREVER NO LOG E VERIFICAR ERROS
void *pipe_worker(){
  char buffer[BUFFER_LENGTH];
  char time_string[TIME_NOW_LENGTH];
  int n;
  fd = open(PIPE_NAME,O_RDWR);
  while(1){
    n = read(fd,buffer,sizeof(buffer));
    buffer[n-1] = '\0';
    #ifdef DEBUG
    printf("%s\n", buffer);
    #endif
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
    //memset(buffer,0,sizeof(buffer));


  }

}

void * time_worker(){
  //initialize time counter
  ptr_ll_arrivals_to_create currentArrival;
  ptr_ll_threads aux;
  ptr_ll_departures_to_create currentDepartures;
  pthread_t new_thread;
  sem_wait(sem_shared_time_counter);
  shared_memory->time_counter = 0;
  sem_post(sem_shared_time_counter);
  while(1){
    sem_wait(sem_go_time_sm);

    sem_wait(sem_shared_time_counter);
    shared_memory->time_counter++;
    printf("%d\n", shared_memory->time_counter);
    sem_post(sem_shared_time_counter);
    // printf("%d\n", time_counter);
    //Allow time CT



    if (arrivals_list != NULL && arrivals_list->init == shared_memory->time_counter) {
      //time to create at least one arrival
      currentArrival = arrivals_list;
      printf("a\n");
      while(currentArrival && currentArrival->init == shared_memory->time_counter){
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
        // aux= thread_list;
        // while(aux){
        //   printf("%ld\n", aux->this_thread);
        //   aux=aux->next;
        // }
      }
    }

    if (departures_list != NULL && departures_list->init == shared_memory->time_counter) {
      //time to create at least one departure
      currentDepartures = departures_list;
      printf("a\n");
      while(currentDepartures && currentDepartures->init == shared_memory->time_counter){
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
          aux=aux->next;
        }
      }
    }
    sem_post(sem_go_time_ct);
    //counting time in milliseconds
    usleep(options.ut * 1000);
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
      sem_wait(sem_shared_time_counter);
      if(init_time<shared_memory->time_counter){
        #ifdef DEBUG
        printf("INVALID COMMAND\n");
        #endif
        sem_post(sem_shared_time_counter);
        return 0;
      }
      sem_post(sem_shared_time_counter);
      takeoff = atoi(split_command[5]);
      new_departure = (ptr_ll_departures_to_create) malloc(sizeof(Ll_departures_to_create));
      strcpy(new_departure->flight_code,split_command[1]);
      new_departure->init = init_time;
      new_departure->takeoff= takeoff;
      new_departure->next = NULL;
      #ifdef DEBUG
      printf("%s %d %d\n", new_departure->flight_code, new_departure->init, new_departure->takeoff);
      #endif
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
      sem_wait(sem_shared_time_counter);
      if (eta > fuel || init_time<shared_memory->time_counter) {
        #ifdef DEBUG
        printf("INVALID COMMAND\n");
        #endif
        sem_post(sem_shared_time_counter);
        return 0;
      }
      sem_post(sem_shared_time_counter);
      //Reach here if everything is OK
      new_arrival = (ptr_ll_arrivals_to_create) malloc(sizeof(Ll_arrivals_to_create));
      strcpy(new_arrival->flight_code,split_command[1]);
      new_arrival->init = init_time;
      new_arrival->eta = eta;
      new_arrival->fuel = fuel;
      new_arrival->next = NULL;
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
  // shared_memory->flight_slots = (int*)calloc(options.max_landings + options.max_takeoffs,sizeof(int));
  //shared_memory->flight_slots[i] == 0, espaço nao ocupado
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
  pthread_t new_thread;
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

  //STARTS HERE
  if (pthread_create(&new_thread,NULL,manage_worker, NULL)){
    printf("error creating thread\n");
    exit(-1);
  }
  //CREATE THREAD FOR MESSAGE QUEUE
  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = insert_thread(thread_list, new_thread);
  pthread_mutex_unlock(&mutex_ll_threads);

  if (pthread_create(&new_thread,NULL,time_worker_ct, NULL)){
    printf("error creating thread\n");
    exit(-1);
  }
  //CREATE THREAD FOR TIMER
  pthread_mutex_lock(&mutex_ll_threads);
  thread_list = insert_thread(thread_list, new_thread);
  pthread_mutex_unlock(&mutex_ll_threads);

  //sigaction
  pthread_sigmask(SIG_UNBLOCK, &sigusr1.sa_mask, NULL);
  sigusr1.sa_flags = 0;
  sigusr1.sa_handler = sigusr1_handler;
  sigemptyset(&sigusr1.sa_mask);
  sigfillset(&sigusr1.sa_mask);
  sigdelset(&sigusr1.sa_mask, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sigusr1.sa_mask, NULL);

  while (1) {
    sigaction(SIGUSR1, &sigusr1, NULL);
    pause();
  }
}

void printLista(ptr_ll_wait_arrivals list){
  ptr_ll_wait_arrivals aux = list;
  while(aux){
    printf("LISTA %d %d\n", aux->slot,aux->eta);
    aux = aux->next;
  }
}

void printListaD(ptr_ll_wait_departures list){
  ptr_ll_wait_departures aux = list;
  while(aux){
    printf("LISTA %d %d\n", aux->slot,aux->takeoff);
    aux = aux->next;
  }
}

// void sub_1_takeoff(ptr_ll_wait_departures list){
//   ptr_ll_wait_departures aux = list;
//   while(aux){
//     printf("\t\t%d\n", aux->takeoff);
//     aux->takeoff--;
//     aux = aux->next;
//   }
// }
//
// void sub_1_eta(ptr_ll_wait_arrivals list){
//   ptr_ll_wait_arrivals aux = list;
//   while(aux){
//     printf("\t\t%d\n", aux->eta);
//     aux->eta--;
//     aux->fuel--;
//     if(aux-> fuel == 0){
//       //TODO: ENVIAR SHUTDOWN NO AVIAO
//       #ifdef DEBUG
//       printf("\tFUEL 0\n");
//       #endif
//     }
//     aux = aux->next;
//   }
// }

void* time_worker_ct(){
  int choice = 0;
  int timer = 0;
  ptr_ll_wait_arrivals free_arrival;
  ptr_ll_wait_departures free_departure;
  ptr_ll_wait_arrivals current_wait_arrivals,ant_wait_arrivals = NULL,next_wait_arrivals;
  ptr_ll_wait_departures current_wait_departures,ant_wait_departures = NULL,next_wait_departures;
  ptr_ll_wait_arrivals current_arrival_queue;
  while(1){
    ant_wait_arrivals = NULL;
    ant_wait_departures = NULL;
    next_wait_arrivals = NULL;
    next_wait_departures = NULL;
    current_arrival_queue = arrival_queue;
    sem_wait(sem_go_time_ct);
    sem_wait(sem_shared_time_counter);
    timer = shared_memory->time_counter;
    sem_post(sem_shared_time_counter);
    pthread_mutex_lock(&mutex_ll_wait_arrivals_queue);
    current_wait_arrivals = wait_queue_arrivals;
    pthread_mutex_unlock(&mutex_ll_wait_arrivals_queue);
    pthread_mutex_lock(&mutex_ll_wait_departures_queue);
    current_wait_departures = wait_queue_departures;
    pthread_mutex_unlock(&mutex_ll_wait_departures_queue);

    //DECREMENTA FUEL NA LISTA QUE ESTA A ESPERA PARA ATERRAR
    while(current_arrival_queue){
      current_arrival_queue->fuel--;
      current_arrival_queue = current_arrival_queue->next;
    }

    //TODO: SEMAFORO DESTAs LISTAs LIGADAs
    //CHEGADAS
    pthread_mutex_lock(&mutex_ll_wait_arrivals_queue);
    while(current_wait_arrivals){
      printf("\t\tFUEL %d\n", current_wait_arrivals->fuel);
      if(current_wait_arrivals->fuel == 0){
        #ifdef DEBUG
        printf("Slot: %d Sem FUEL\n",current_wait_arrivals->slot);
        #endif
        //TODO: ENVIAR O VOO PARA O CARALHO
        sem_wait(sem_shared_flight_slots);
        shared_memory->flight_slots[current_wait_arrivals->slot] = NO_FUEL;
        sem_post(sem_shared_flight_slots);
        sem_post(sem_cond);
        //REMOVE NO NA LISTA
        free_arrival = current_wait_arrivals;
        current_wait_arrivals = current_wait_arrivals->next;
        free(free_arrival);
        if(ant_wait_arrivals){
          ant_wait_arrivals->next = current_wait_arrivals;
        }
        else{
          wait_queue_arrivals = current_wait_arrivals;
        }

      }
      else if(current_wait_arrivals->eta == timer){
        //REMOVE NA LISTA WAIT
        if(ant_wait_arrivals){
          ant_wait_arrivals->next = current_wait_arrivals->next;
        }
        else{
          wait_queue_arrivals = current_wait_arrivals->next;
        }
        //ADICIONA A LISTA DE ESPERA PARA ENTRAR
        next_wait_arrivals = current_wait_arrivals->next;
        current_wait_arrivals->next = NULL;
        current_wait_arrivals->fuel--;
        if(current_wait_arrivals->urgent){
          sem_wait(sem_shared_flight_slots);
          shared_memory->flight_slots[current_wait_arrivals->slot] = URGENT_REQUEST;
          sem_post(sem_shared_flight_slots);
          sem_post(sem_cond);
        }
        arrival_queue = insert_to_arrive(arrival_queue,current_wait_arrivals);
        #ifdef DEBUG
        printf("LISTA DOS WAITS\n");
        printLista(wait_queue_arrivals);
        printf("LISTA DOS MANAGE\n");
        printLista(arrival_queue);
        #endif
        current_wait_arrivals = next_wait_arrivals;

      }
      else{
        //DECREMENTA O FUEL DE CADA UM QUE ESTA COM ETA > TIMER
        current_wait_arrivals->fuel--;
        ant_wait_arrivals = current_wait_arrivals;
        current_wait_arrivals = current_wait_arrivals->next;
      }
    }
    pthread_mutex_unlock(&mutex_ll_wait_arrivals_queue);
    //AQUI A LISTA DA QUEUE ARRIVALS ESTA ATUALIZADA, TEMOS TODOS OS VOOS QUE ESPERAM PARA ATERRAR EM arrival_queue
    //SE LEN MAIOR QUE 5 TEMOS QUE MANDAR HOLD PARA OS QUE TÊM MENOR PRIORIDADE
    send_holdings();
    #ifdef DEBUG
    printf("LISTA DOS WAITS\n");
    printLista(wait_queue_arrivals);
    printf("LISTA DOS MANAGE\n");
    printLista(arrival_queue);
    #endif
    //SAIDAS
    pthread_mutex_lock(&mutex_ll_wait_departures_queue);
    while(current_wait_departures){
      if(current_wait_departures->takeoff == timer){
        //REMOVE NA LISTA WAIT
        if(ant_wait_departures){
          ant_wait_departures->next = current_wait_departures->next;
        }
        else{
          wait_queue_departures = current_wait_departures->next;
        }
        // ADICIONA A LISTA DE ESPERA PARA ENTRAR
        // ADICIONA NO FIM, ASSIM ESTA ORDENADA POR > WAITING TIME
        next_wait_departures = current_wait_departures->next;
        current_wait_departures->next = NULL;
        departure_queue = insert_to_departure(departure_queue,current_wait_departures);
        printf("LISTA DOS WAITS\n");
        printListaD(wait_queue_departures);
        printf("LISTA DOS MANAGE\n");
        printListaD(departure_queue);
        current_wait_departures = next_wait_departures;
      }
      else{
        ant_wait_departures = current_wait_departures;
        current_wait_departures = current_wait_departures->next;
      }
    }
    pthread_mutex_unlock(&mutex_ll_wait_departures_queue);

    verify_arrivals_in_queue();

    sub_1_times_runways();

    choice = schedule_flights(departure_queue,arrival_queue);
    //AQUI AS LISTAS DE WAIT E QUEUE ESTAO ATUALIZADAS
    // ALGORITMO DE ESCALONAMENTO
    // 1 PRIORIDADE: EXISTEM URGENTES
    // 2 PRIORIDADE: EXISTE FUEL SUFICIENTE NOS ARRIVALS PARA AGUENTAR UM TAKEOFF
    // 3 PRIORIDADE: COMPARA WAITING TIMES
    // SE IGUAIS OPTA POR ARRIVALS
    #ifdef DEBUG
    printf("\tChoice: %d\n",choice);
    #endif
    if(choice==1){
      //GO ARRIVAL
      if(!runways[0][0] && !runways[0][2] && arrival_queue){
        //GO ARRIVAL PISTA 1
        #ifdef DEBUG
        printf("   A PISTA 1\n");
        #endif
        runways[0][0] = options.landing_dur;
        //ALTERA SLOT
        sem_wait(sem_shared_flight_slots);
        shared_memory->flight_slots[arrival_queue->slot] = GO_28L;
        sem_post(sem_shared_flight_slots);
        //NOTIFY
        sem_post(sem_cond);
        free_arrival = arrival_queue;
        arrival_queue = arrival_queue->next;
        free(free_arrival);
      }
      if(!runways[0][1] && !runways[0][3] && arrival_queue){
        //GO ARRIVAL PISTA 2
        #ifdef DEBUG
        printf("   A PISTA 2\n");
        #endif
        runways[0][1] = options.landing_dur;
        //ALTERA SLOT
        shared_memory->flight_slots[arrival_queue->slot] = GO_28R;
        //NOTiFY
        sem_post(sem_cond);
        free_arrival = arrival_queue;
        arrival_queue = arrival_queue->next;
        free(free_arrival);
      }
    }

    else if(choice == 2){
      //GO DEPARTURE
      if(!runways[1][0] && !runways[1][2] && departure_queue){
        //GO DEPARTURE PISTA 1
        #ifdef DEBUG
        printf("   D PISTA 1\n");
        #endif
        printf("a\n");
        runways[1][0] = options.takeoff_dur;
        sem_wait(sem_shared_flight_slots);
        printf("a\n");
        shared_memory->flight_slots[departure_queue->slot] = GO_01L;
        sem_post(sem_shared_flight_slots);
        printf("a\n");
        sem_post(sem_cond);
        printf("a\n");
        free_departure = departure_queue;
        departure_queue = departure_queue->next;
        printf("a\n");
        free(free_departure);
      }
      if(!runways[1][1] && !runways[1][3] && departure_queue){
        //GO DEPARTURE PISTA 2
        #ifdef DEBUG
        printf("   D PISTA 2\n");
        #endif
        runways[1][1] = options.takeoff_dur;
        sem_wait(sem_shared_flight_slots);
        shared_memory->flight_slots[departure_queue->slot] = GO_01R;
        sem_post(sem_shared_flight_slots);
        sem_post(sem_cond);
        free_departure = departure_queue;
        departure_queue = departure_queue->next;
        free(free_departure);
      }
    }

    //FIM
    sem_post(sem_go_time_sm);
  }
}

void verify_arrivals_in_queue(){
  ptr_ll_wait_arrivals current = arrival_queue, ant = NULL;
  while(current){
    if(current->fuel == 0){
      //NAO TEM FUEL REJEITAR
      sem_wait(sem_shared_flight_slots);
      shared_memory->flight_slots[current->slot] = NO_FUEL;
      sem_post(sem_shared_flight_slots);
      sem_post(sem_cond);
      if(!ant){
        arrival_queue = current->next;
        current = current->next;
      }
      else{
        ant->next = current->next;
        free(current);
        current = ant->next;
      }
    }
    else{
      ant = current;
      current = current->next;
    }
  }
}

void send_holdings(){
  ptr_ll_wait_arrivals current = arrival_queue, ant= NULL;
  int num_node = 0;
  int holding_time = 0;
  while(current){
    if(num_node>4){
      //ENVIAR HOLD
      //ELIMINAR NESTA LL
      ant->next = current->next;
      current->next = NULL;
      //ADCIONAR À WAIT LIST, ONDE ETA > TIME_COUNTER
      sem_wait(sem_shared_time_counter);
      //COLOCAR O TEMPO DO HOLDING
      holding_time = (rand() % (options.max_holding - options.min_holding + 1)) + options.min_holding;
      current->eta = shared_memory->time_counter + holding_time;
      sem_post(sem_shared_time_counter);
      wait_queue_arrivals = sorted_insert_wait_queue_arrivals(wait_queue_arrivals,current);
      sem_wait(sem_shared_flight_slots);
      shared_memory->flight_slots[current->slot] = holding_time;
      sem_post(sem_shared_flight_slots);
      sem_post(sem_cond);
      current = ant->next;
      #ifdef DEBUG
      printf("HOLDING ORDER\n");
      #endif
    }
    else{
      ant = current;
      current = current->next;
    }
    num_node++;
  }
}

void sub_1_times_runways(){
  sem_wait(sem_shared_runway);
  if(runways[0][2]) runways[0][2]--;
  if(runways[0][3]) runways[0][3]--;
  if(runways[1][2]) runways[1][2]--;
  if(runways[1][3]) runways[1][3]--;

  if(runways[0][0]){
    runways[0][0]--;
    if(!runways[0][0]){
      runways[0][2] = options.landing_int;
      pthread_mutex_lock(&mutex_atm_arrivals);
      atm_arrivals--;
      pthread_mutex_unlock(&mutex_atm_arrivals);
    }
  }
  if(runways[0][1]){
    runways[0][1]--;
    if(!runways[0][1]){
      runways[0][3] = options.landing_int;
      pthread_mutex_lock(&mutex_atm_arrivals);
      atm_arrivals--;
      pthread_mutex_unlock(&mutex_atm_arrivals);
    }
  }
  if(runways[1][0]){
    runways[1][0]--;
    if(!runways[1][0]){
      runways[1][2] = options.takeoff_int;
      pthread_mutex_lock(&mutex_atm_departures);
      atm_departures--;
      pthread_mutex_unlock(&mutex_atm_departures);
    }
  }
  if(runways[1][1]){
    runways[1][1]--;
    if(!runways[1][1]){
      runways[1][3] = options.takeoff_int;
      pthread_mutex_lock(&mutex_atm_departures);
      atm_departures--;
      pthread_mutex_unlock(&mutex_atm_departures);
    }
  }
  printf("D PISTA 1: %d TEMPO p1: %d PISTA 2:%d TEMPO p2: %d\n", runways[1][0],runways[1][2],runways[1][1],runways[1][3] );
  printf("A PISTA 1: %d TEMPO p1: %d PISTA 2:%d TEMPO p2: %d\n", runways[0][0],runways[0][2],runways[0][1],runways[0][3] );
  sem_post(sem_shared_runway);
}

ptr_to_ptr_wait_queue_arrivals insert_node_waiting_sort(ptr_to_ptr_wait_queue_arrivals list, ptr_to_ptr_wait_queue_arrivals new){
  ptr_to_ptr_wait_queue_arrivals current = list, ant = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if (new->arrival->eta <= current->arrival->eta) {
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

ptr_to_ptr_wait_queue_arrivals sort_waiting_time_arrivals(ptr_ll_wait_arrivals list){
  ptr_ll_wait_arrivals aux = list;
  ptr_to_ptr_wait_queue_arrivals new;
  ptr_to_ptr_wait_queue_arrivals sorted = NULL;
  if(!list){
    return NULL;
  }
  while(aux){
    new = (ptr_to_ptr_wait_queue_arrivals)malloc(sizeof(node_ptr_wait_queue_arrivals));
    new->arrival = aux;
    new->next = NULL;
    sorted = insert_node_waiting_sort(sorted,new);
    aux = aux->next;
  }
  return sorted;
}

int schedule_flights(){
  ptr_ll_wait_departures aux_departures = departure_queue;
  float avg_waiting_time_arrivals = 0;
  float avg_waiting_time_departures = 0;
  int choice = 0;
  // choice==1 -> go arrivals choice==2 -> go departures
  ptr_to_ptr_wait_queue_arrivals sorted_waiting_time_arrival_queue = NULL;
  ptr_to_ptr_wait_queue_arrivals current_sorted_waiting_time_arrival_queue = NULL;
  if((runways[0][1] && runways[0][0]) ||( runways[1][1] && runways[1][0])){
    //NADA A FAZER
    return 0;
  }

  if(departure_queue && arrival_queue){
    // URGENTES ESTÃO NA CABEÇA SEMPRE, SE A CABEÇA FOR NAO URGENTE NAO EXISTEM URGENTES NA LISTA
    sorted_waiting_time_arrival_queue = sort_waiting_time_arrivals(arrival_queue);
    if(arrival_queue->urgent){
      printf("HA URGENTES\n");
      choice = 1;
    }
    else{
      //NAO HA URGENTES A PRIORIDADE É VER SE EXISTE FUEL SUFICIENTE PARA UM TAKEOFF
      // DUVIDA: O INTERVALO E POR PISTA?

      //ARRVIALS
      if(runways[0][0] && !runways[0][3]){
        //PISTA 1 BLOQUEADA 2 LIVRE
        if(aux_departures->takeoff < sorted_waiting_time_arrival_queue->arrival->eta){
          if(arrival_queue->fuel > runways[0][0] + options.takeoff_dur){
            //NADA A FAZER, ESPERAR POR TAKEOFF
            #ifdef DEBUG
            printf("\t\tESPERAR POR NEXT\n");
            #endif
            return 0;
          }
        }
        // NAO HA FUEL SIGA ARRIVAL NA PISTA 2
        choice = 1;
      }
      else if(runways[0][1] && !runways[0][2]){
        //PISTA 2 BLOQUEADA 1 LIVRE
        if(aux_departures->takeoff < sorted_waiting_time_arrival_queue->arrival->eta){
          if(arrival_queue->fuel > runways[0][1] + options.takeoff_dur){
            //NADA A FAZER, ESPERAR POR TAKEOFF
            #ifdef DEBUG
            printf("\t\tESPERAR POR NEXT\n");
            #endif
            return 0;
          }
        }
        // NAO HA FUEL SIGA ARRIVAL NA PISTA 1
        choice = 1;
      }
        //DEPARTURES
      else if((runways[1][0] && !runways[1][3]) || (runways[1][1] && !runways[1][2])){
        //PISTA 3 OCUPADA 4 LIVRE OU VICE VERSA
        if(sorted_waiting_time_arrival_queue->arrival->eta < aux_departures->takeoff){
          //NADA ACONTECE ESPERA PARA ARRIVAL
          return 0 ;
        }
        else if(runways[0][0]){
          if(arrival_queue->fuel > runways[0][0] && arrival_queue->fuel < options.takeoff_dur)
            return 0 ;
          else choice = 2;
          //SO ESCOLHER ESPERAR SE NAO EXISTIR FUEL PARA OUTRO
        }
        else if(runways[1][0]){
          if(arrival_queue->fuel > runways[1][0] && arrival_queue->fuel < options.takeoff_dur)
            return 0 ;
          else choice = 2;
          //SO ESCOLHER ESPERAR SE NAO EXISTIR FUEL PARA OUTRO
        }
      }
      else{
        //NAO HA VOOS A ACONTECEREM
        if(arrival_queue->fuel > options.takeoff_dur){
          //COMPARAR WAITING TIMES, REDUZIR AO MAXIMO
          // MAX WAITING TIME ESTA NA CABEÇA DO DEPARTURES, NOS ARRIVALS A CABEÇA E O MENOR FUEL
          printf("HA FUEL %d\n", arrival_queue->fuel);
          // ALGORITMO PARA COMPARAR DUAS LL PARA OBTER QUAL TEM MAIOR WAITING TIME
          current_sorted_waiting_time_arrival_queue = sorted_waiting_time_arrival_queue;
          if(current_sorted_waiting_time_arrival_queue->next != NULL){
            avg_waiting_time_arrivals = (current_sorted_waiting_time_arrival_queue->arrival->eta + current_sorted_waiting_time_arrival_queue->next->arrival->eta)/2;
          }
          else{
            avg_waiting_time_arrivals = current_sorted_waiting_time_arrival_queue->arrival->eta;
          }
          if(aux_departures->next != NULL){
            avg_waiting_time_departures = (aux_departures->takeoff + aux_departures->next->takeoff)/2;
          }
          else{
            avg_waiting_time_departures = aux_departures->takeoff;
          }
          printf("\tWAITING TIME ARRIVALS %f\n",avg_waiting_time_arrivals );
          printf("\tWAITING TIME DEPARTURES %f\n",avg_waiting_time_departures);
          if(avg_waiting_time_departures < avg_waiting_time_arrivals){
            choice = 2;
          }
          else{
            choice = 1;
          }
        }
        else{
          choice = 1;
        }
      }
    }
  }
  else if(departure_queue && !arrival_queue){
    if(!runways[0][0] && !runways[0][1])
      choice = 2;
  }
  else if(arrival_queue && !departure_queue){
    if(!runways[1][0] && !runways[1][1])
      choice = 1;
  }
  return choice;
}

ptr_ll_wait_arrivals insert_to_arrive(ptr_ll_wait_arrivals list, ptr_ll_wait_arrivals new){
  ptr_ll_wait_arrivals current = list, ant = NULL;
  new->next = NULL;
  // printf("URGENTE:%d\n", new->urgent);
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if(new->urgent > current->urgent){
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
      else{
        if (new->fuel <= current->fuel) {
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
      }
      ant = current;
      current = current->next;
    }
    ant->next = new;
    return list;
  }
}

ptr_ll_wait_departures insert_to_departure(ptr_ll_wait_departures list, ptr_ll_wait_departures new){
  ptr_ll_wait_departures current = list, ant = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      ant = current;
      current = current->next;
    }
    ant->next = new;
    return list;
  }
}

void* manage_worker(){
  message msg;
  message_give_slot msgSend;
  ptr_ll_wait_arrivals newArrive;
  ptr_ll_wait_departures newDeparture;
  int i = 0;
  int slot;
  while(1){
    msgrcv(msq_id, &msg, sizeof(msg)-sizeof(long), -2, 0);
    #ifdef DEBUG
    printf("MESSAGE READ\n");
    #endif

    while(shared_memory->flight_slots[i] != UNUSED_SLOT){
      i++;
    }
    slot = i;
    sem_wait(sem_shared_flight_slots);
    shared_memory->flight_slots[i] = WAITING_ORDERS;
    sem_post(sem_shared_flight_slots);
    printf("I %d\n", shared_memory->flight_slots[i]);
    msgSend.slot = slot;
    msgSend.msgtype = msg.type_rcv;
    // printf("%ld\n", msgSend.msgtype);
    msgsnd(msq_id,&msgSend,sizeof(msgSend)-sizeof(long),0);
    //ADICIONA NA QUEUE PARA ATERRAR OU DESCOLAR
    if(msg.type == 'a'){
      pthread_mutex_lock(&mutex_atm_arrivals);
      printf("%d\n", atm_arrivals);
      if(options.max_landings <= atm_arrivals){
        pthread_mutex_unlock(&mutex_atm_arrivals);
        sem_wait(sem_shared_flight_slots);
        shared_memory->flight_slots[slot] = REJECTED;
        sem_post(sem_shared_flight_slots);
        sem_wait(sem_shared_stats);
        shared_memory->stats.num_rejected++;
        sem_post(sem_shared_stats);
        sem_post(sem_cond);
      }
      else{
        atm_arrivals++;
        pthread_mutex_unlock(&mutex_atm_arrivals);
        newArrive = (ptr_ll_wait_arrivals)malloc(sizeof(node_wait_queue_arrivals));
        newArrive->slot = slot;
        if(msg.msgtype == 1){
          newArrive->urgent = 1;
        }
        else{
          newArrive->urgent = 0;
        }
        newArrive->next= NULL;
        newArrive->fuel = msg.fuel;
        sem_wait(sem_shared_time_counter);
        newArrive->eta = msg.eta+shared_memory->time_counter;
        sem_post(sem_shared_time_counter);
        pthread_mutex_lock(&mutex_ll_wait_arrivals_queue);
        wait_queue_arrivals = sorted_insert_wait_queue_arrivals(wait_queue_arrivals,newArrive);
        pthread_mutex_unlock(&mutex_ll_wait_arrivals_queue);
      }
    }
    else if(msg.type == 'd'){
      pthread_mutex_lock(&mutex_atm_departures);
      if(options.max_takeoffs <= atm_departures){
        pthread_mutex_unlock(&mutex_atm_departures);
        sem_wait(sem_shared_flight_slots);
        shared_memory->flight_slots[slot] = REJECTED;
        sem_post(sem_shared_flight_slots);
        sem_wait(sem_shared_stats);
        shared_memory->stats.num_rejected++;
        sem_post(sem_shared_stats);
        sem_post(sem_cond);
      }
      else{
        atm_departures++;
        pthread_mutex_unlock(&mutex_atm_departures);
        newDeparture = (ptr_ll_wait_departures)malloc(sizeof(node_wait_queue_departures));
        newDeparture->slot = slot;
        newDeparture->next = NULL;
        sem_wait(sem_shared_time_counter);
        newDeparture->takeoff = msg.takeoff+shared_memory->time_counter;
        sem_post(sem_shared_time_counter);
        pthread_mutex_lock(&mutex_ll_wait_departures_queue);
        wait_queue_departures = sorted_insert_wait_queue_departures(wait_queue_departures,newDeparture);
        pthread_mutex_unlock(&mutex_ll_wait_departures_queue);
      }
    }
  }

}

ptr_ll_wait_arrivals sorted_insert_wait_queue_arrivals(ptr_ll_wait_arrivals list, ptr_ll_wait_arrivals new){
  ptr_ll_wait_arrivals current = list, ant = NULL;
  new->next = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if (new->eta <= current->eta) {
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

ptr_ll_wait_departures sorted_insert_wait_queue_departures(ptr_ll_wait_departures list, ptr_ll_wait_departures new){
  ptr_ll_wait_departures current = list, ant = NULL;
  new->next = NULL;
  if (list == NULL) {
    return new;
  }
  else{
    while (current != NULL) {
      if (new->takeoff <= current->takeoff) {
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

//FIM CT

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
  message msg;
  message_give_slot msgSlot;
  int type_rcv;
  sem_wait(sem_shared_stats);
  shared_memory->stats.num_created_flights++;
  type_rcv = shared_memory->stats.num_created_flights + 2;
  //SE CALHAR MUDAR AQUI O METODO
  sem_post(sem_shared_stats);

  //VERIFICA SE PODE CRIAR
  // pthread_mutex_lock(&mutex_atm_departures);
  // if(options.max_takeoffs <= atm_departures){
  //   sem_wait(sem_write_log);
  //   log_fich = fopen(LOG_NAME,"a");
  //   if(log_fich == NULL){
  //     printf("Error opening %s", LOG_NAME);
  //   }
  //   //GET CURRENT TIME
  //   get_current_time_to_string(time_string);
  //   printf("%s %s REJECTED => MAX TAKEOFFS REACHED\n",flight_info.flight_code,time_string);
  //   fprintf(log_fich,"%s %s REJECTED => MAX TAKEOFFS REACHED\n",flight_info.flight_code,time_string);
  //   fclose(log_fich);
  //   sem_post(sem_write_log);
  //   pthread_mutex_unlock(&mutex_atm_departures);
  //   pthread_exit(NULL);
  // }
  // atm_departures++;
  // pthread_mutex_unlock(&mutex_atm_departures);
  //fill message queue message
  fill_message_departures(&msg,flight_info,type_rcv);

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


  //SEND message
  msgsnd(msq_id, &msg, sizeof(msg)-sizeof(long), 0);

  //wait for slot
  msgrcv(msq_id,&msgSlot,sizeof(msgSlot)-sizeof(long),type_rcv,0);
  printf("PONTEIRO: %d CONTEUDO: %d\n",msgSlot.slot,shared_memory->flight_slots[msgSlot.slot] );

  pthread_mutex_lock(&mutex_cond);
  while(shared_memory->flight_slots[msgSlot.slot] == WAITING_ORDERS){
      pthread_cond_wait(&cond,&mutex_cond);
  }
  pthread_mutex_unlock(&mutex_cond);

  sem_wait(sem_shared_flight_slots);
  if(shared_memory->flight_slots[msgSlot.slot] == REJECTED){
    shared_memory->flight_slots[msgSlot.slot] = UNUSED_SLOT;
    sem_post(sem_shared_flight_slots);
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s REJECTED => MAX TAKEOFFS REACHED\n",time_string,flight_info.flight_code);
    printf("%s %s REJECTED => MAX TAKEOFFS REACHED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
    pthread_exit(NULL);
  }
  sem_post(sem_shared_flight_slots);

  //ESCREVE QUE COMEÇOU
  // printf("SAIUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU %d\n",shared_memory->flight_slots[msgSlot.slot]);
  sem_wait(sem_shared_flight_slots);
  if(shared_memory->flight_slots[msgSlot.slot] == GO_01L){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s DEPARTURE 1L STARTED\n",time_string,flight_info.flight_code);
    printf("%s %s DEPARTURE 1L STARTED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  else if(shared_memory->flight_slots[msgSlot.slot] == GO_01R){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s DEPARTURE 1R STARTED\n",time_string,flight_info.flight_code);
    printf("%s %s DEPARTURE 1R STARTED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  sem_post(sem_shared_flight_slots);

  usleep(options.ut * options.takeoff_dur * 1000);

  //ESCREVE QUE ACABOU
  sem_wait(sem_shared_flight_slots);
  if(shared_memory->flight_slots[msgSlot.slot] == GO_01L){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s DEPARTURE 1L CONCLUDED\n",time_string,flight_info.flight_code);
    printf("%s %s DEPARTURE 1L CONCLUDED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  else if(shared_memory->flight_slots[msgSlot.slot] == GO_01R){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s DEPARTURE 1R CONCLUDED\n",time_string,flight_info.flight_code);
    printf("%s %s DEPARTURE 1R CONCLUDED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  sem_post(sem_shared_flight_slots);

  // pthread_mutex_lock(&mutex_ll_threads);
  // thread_list = remove_thread_from_ll(thread_list,pthread_self());
  // free(ptr_ll_departure);
  // pthread_mutex_unlock(&mutex_ll_threads);
  sem_wait(sem_shared_stats);
  shared_memory->stats.num_takeoffs++;
  sem_post(sem_shared_stats);
  pthread_exit(NULL);
}

void* arrival_worker(void* ptr_ll_arrival){
  Ll_arrivals_to_create flight_info = *((ptr_ll_arrivals_to_create)ptr_ll_arrival);
  char time_string[TIME_NOW_LENGTH];
  message msg;
  message_give_slot msgSlot;
  int type_rcv;
  sem_wait(sem_shared_stats);
  shared_memory->stats.num_created_flights++;
  type_rcv = shared_memory->stats.num_created_flights + 2;
  //SE CALHAR MUDAR AQUI O METODO
  sem_post(sem_shared_stats);

  //VERIFICA MAX
  // pthread_mutex_lock(&mutex_atm_arrivals);
  // if(options.max_takeoffs <= atm_arrivals){
  //   sem_wait(sem_write_log);
  //   log_fich = fopen(LOG_NAME,"a");
  //   if(log_fich == NULL){
  //     printf("Error opening %s", LOG_NAME);
  //   }
  //   //GET CURRENT TIME
  //   get_current_time_to_string(time_string);
  //   printf("%s %s REJECTED => MAX ARRIVALS REACHED\n",flight_info.flight_code,time_string);
  //   fprintf(log_fich,"%s %s REJECTED => MAX ARRIVALS REACHED\n",flight_info.flight_code,time_string);
  //   fclose(log_fich);
  //   sem_post(sem_write_log);
  //   pthread_mutex_unlock(&mutex_atm_arrivals);
  //   pthread_exit(NULL);
  // }
  // atm_arrivals++;
  // pthread_mutex_unlock(&mutex_atm_arrivals);
  //fill message queue message
  fill_message_arrivals(&msg,flight_info,type_rcv);
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

  //SEND MESSAGE
  msgsnd(msq_id, &msg, sizeof(msg)-sizeof(long), 0);
  //wait for slot
  msgrcv(msq_id,&msgSlot,sizeof(msgSlot)-sizeof(long),type_rcv,0);
  // FALTA FAZER CONTROLO DE PISTAS
  pthread_mutex_lock(&mutex_cond);
  while(shared_memory->flight_slots[msgSlot.slot] == WAITING_ORDERS){
      pthread_cond_wait(&cond,&mutex_cond);
      sem_wait(sem_shared_flight_slots);
      if(shared_memory->flight_slots[msgSlot.slot] > 0){
        sem_wait(sem_write_log);
        log_fich = fopen(LOG_NAME,"a");
        if(log_fich == NULL){
          printf("Error opening %s", LOG_NAME);
        }
        //GET CURRENT TIME
        get_current_time_to_string(time_string);
        fprintf(log_fich,"%s ARRIVAL %s HOLDING %d\n",time_string,flight_info.flight_code,shared_memory->flight_slots[msgSlot.slot]);
        printf("%s ARRIVAL %s HOLDING %d\n",time_string,flight_info.flight_code,shared_memory->flight_slots[msgSlot.slot]);
        fclose(log_fich);
        sem_post(sem_write_log);
        shared_memory->flight_slots[msgSlot.slot] = WAITING_ORDERS;
      }
      else if(shared_memory->flight_slots[msgSlot.slot] == URGENT_REQUEST){
        sem_wait(sem_write_log);
        log_fich = fopen(LOG_NAME,"a");
        if(log_fich == NULL){
          printf("Error opening %s", LOG_NAME);
        }
        //GET CURRENT TIME
        get_current_time_to_string(time_string);
        fprintf(log_fich,"%s %s EMERGENCY LANDING REQUESTED\n",time_string,flight_info.flight_code);
        printf("%s %s EMERGENCY LANDING REQUESTED\n",time_string,flight_info.flight_code);
        fclose(log_fich);
        sem_post(sem_write_log);
        shared_memory->flight_slots[msgSlot.slot] = WAITING_ORDERS;
      }
      sem_post(sem_shared_flight_slots);
  }
  pthread_mutex_unlock(&mutex_cond);

  //2 OPÇOES OU NAO TEM FUEL OU E PARA ATERRAR
  sem_wait(sem_shared_flight_slots);
  if(shared_memory->flight_slots[msgSlot.slot] == NO_FUEL){
    shared_memory->flight_slots[msgSlot.slot] = UNUSED_SLOT;
    sem_post(sem_shared_flight_slots);
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n",time_string,flight_info.flight_code);
    printf("%s %s LEAVING TO OTHER AIRPORT => FUEL = 0\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
    sem_wait(sem_shared_stats);
    shared_memory->stats.redirected_flights++;
    sem_post(sem_shared_stats);
    pthread_exit(NULL);
  }
  else if(shared_memory->flight_slots[msgSlot.slot] == REJECTED){
    shared_memory->flight_slots[msgSlot.slot] = UNUSED_SLOT;
    sem_post(sem_shared_flight_slots);
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s REJECTED => MAX ARRIVALS REACHED\n",time_string,flight_info.flight_code);
    printf("%s %s REJECTED => MAX ARRIVALS REACHED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
    pthread_exit(NULL);
  }
  sem_post(sem_shared_flight_slots);

  sem_wait(sem_shared_flight_slots);
  if(shared_memory->flight_slots[msgSlot.slot] == GO_28L){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s ARRIVAL 28L STARTED\n",time_string,flight_info.flight_code);
    printf("%s %s ARRIVAL 28L STARTED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  else if(shared_memory->flight_slots[msgSlot.slot] == GO_28R){
    sem_wait(sem_write_log);
    log_fich = fopen(LOG_NAME,"a");
    if(log_fich == NULL){
      printf("Error opening %s", LOG_NAME);
    }
    //GET CURRENT TIME
    get_current_time_to_string(time_string);
    fprintf(log_fich,"%s %s ARRIVAL 28R STARTED\n",time_string,flight_info.flight_code);
    printf("%s %s ARRIVAL 28R STARTED\n",time_string,flight_info.flight_code);
    fclose(log_fich);
    sem_post(sem_write_log);
  }
  sem_post(sem_shared_flight_slots);
  //ESPERA PARA CONCLUIR ATERRAGEM
  usleep(options.ut * options.landing_dur * 1000);

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

  // //ELEMINA THREAD DA THREADS LL
  // pthread_mutex_lock(&mutex_ll_threads);
  // thread_list = remove_thread_from_ll(thread_list,pthread_self());
  // free(ptr_ll_arrival);
  // pthread_mutex_unlock(&mutex_ll_threads);

  sem_wait(sem_shared_flight_slots);
  shared_memory->flight_slots[msgSlot.slot] = UNUSED_SLOT;
  sem_post(sem_shared_flight_slots);
  sem_wait(sem_shared_stats);
  shared_memory->stats.num_landed++;
  sem_post(sem_shared_stats);
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

void fill_message_arrivals(message* msg, Ll_arrivals_to_create flight_info, int type_rcv){
  msg->type = 'a';
  msg->fuel = flight_info.fuel;
  msg->eta = flight_info.eta;
  msg->takeoff = -1;
  msg->type_rcv = type_rcv;
  if(msg->fuel <= msg->eta + options.landing_dur +4){
    msg->msgtype = 1;
  }
  else{
    msg->msgtype = 2;
  }
}

void fill_message_departures(message* msg, Ll_departures_to_create flight_info, int type_rcv){
  msg->type = 'd';
  msg->fuel = -1;
  msg->eta = -1;
  msg->takeoff = flight_info.takeoff;
  msg->msgtype = 2;
  msg->type_rcv = type_rcv;
}

void* notify_worker(){
  while(1){
    sem_wait(sem_cond);
    #ifdef DEBUG
    printf("NOTIFICA\n");
    #endif
    pthread_mutex_lock(&mutex_cond);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex_cond);
  }
}

void zero_runways(){
  for(int l = 0;l<2;l++){
    for(int c = 0;c<4;c++){
      runways[l][c] = 0;
    }
  }
}

void cleanup(){
  //CLEAN SHARED MEMORY
  shmdt(shared_memory);
	shmctl(shmid, IPC_RMID,NULL);

  kill (pid_ct, SIGKILL);
  //DESTROY SEMAPHROE
  sem_destroy(sem_write_log);
  sem_destroy(sem_shared_stats);
  pthread_mutex_destroy(&mutex_ll_threads);
  pthread_mutex_destroy(&mutex_ll_create_arrivals);
  pthread_mutex_destroy(&mutex_ll_create_departures);
  //CLEAN MSG QUEUE
  msgctl(msq_id, IPC_RMID, NULL);

}

void sigusr1_handler(int signal){
  sem_wait(sem_shared_stats);
  printf("\n\t\t\t[Statistics]\n\n");
  printf("Número total de voos criados: %d\n", shared_memory->stats.num_created_flights);
  printf("Número total de voos que aterraram: %d\n", shared_memory->stats.num_landed);
  printf("Tempo média de espera (para além do ETA) para aterrar: %d\n", shared_memory->stats.avg_wait_land_time);
  printf("Número total de voos que descolaram: %d\n", shared_memory->stats.num_takeoffs);
  printf("Tempo médio de espera para descolar: %d\n", shared_memory->stats.avg_wait_takeoff_time);
  printf("Número médio de manobras de holding por voo de aterragem: %d\n", shared_memory->stats.avg_holdings);
  printf("Número médio de manobras de holding por voo em estado de urgência: %d\n", shared_memory->stats.avg_emergency_holdings);
  printf("Número de voos redirecionados para outro aeroporto: %d\n", shared_memory->stats.redirected_flights);
  printf("Voos rejeitados pela Torre de Controlo: %d\n", shared_memory->stats.num_rejected);
  sem_post(sem_shared_stats);
}

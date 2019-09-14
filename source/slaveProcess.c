#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/constants.h"


#define MAX_PROCESS_LENGTH 200
#define MAX_INPUT_BUFFER 200
#define MAX_SAT_INPUT 2048
#define MAX_PROCESSES_ALLOWED 50

#define EXIT_STATUS 256




int processSat(char * inputBuffer);
void analyseSatResults(char * processInfo, char * buffer, char * fileName);
void readFromStdin(char * inputBuffer, int size);
int selectProcesses(char * inputBuffer, char * filesToProcess[]);
void cleanBuffer(char * buffer, int size);


int main(int argc, char * argv[]){

    char inputBuffer[MAX_INPUT_BUFFER];
    char * filesToProcess[MAX_PROCESSES_ALLOWED];
    int cantP,i;


    //Leemos del standard input e iniciamos el ciclo hasta que el applicacion mande
    //la senial terminate process.
    readFromStdin(inputBuffer, MAX_INPUT_BUFFER);
    while(strncmp(inputBuffer,TERMINATE_MESSAGE,sizeof(TERMINATE_MESSAGE) != 0)){
        
        //Guardamos en un array los files a procesar.
        cantP = selectProcesses(inputBuffer,filesToProcess);

        for(i=0;i<cantP;i++){
            //Procesamos el archivo en el sat,en orden.
            processSat(filesToProcess[i]);
        }
        
        //Limpiamos el buffer y leemos el input que nos manden.
        cleanBuffer(inputBuffer,MAX_INPUT_BUFFER);
        readFromStdin(inputBuffer,MAX_INPUT_BUFFER);        

    }

    //TESTING
    printf("Slave Terminated\n");

    return 0;
}


/*
Creates a slave, procceses and parsed the info of the sat.
Sends the info via stdout to application process.
 */
int processSat(char * inputBuffer){

    int  pid,error, childInfo = 0;
    char * executeCommandArgs[3] = {"minisat",NULL,NULL};
    int pipefd[2]; //Descriptor para el redirigir el output del child.
    char satBuffer[MAX_SAT_INPUT]; 

    
    pipe(pipefd);//Creamos el canal de comunicacion entre el hijo y el padre
    pid = fork();

    if(-1 == pid){
        perror("Error fork en slaveProcess: ");
        return -1;
    }
    //El hijo que ejecutara el minisat
    if(pid == 0){
        
        //Establecemos el IPC con el hijo que va a ejecutar el minisat.
        close(pipefd[READ_END]);
        dup2(pipefd[WRITE_END],STDOUT_FILENO);
        close(pipefd[WRITE_END]);

        //Ejecutamos minisat
        executeCommandArgs[1] = inputBuffer;
        error = execvp(executeCommandArgs[0],executeCommandArgs);
        //Si falla, entonces imprimimos que no pudimos ejecutar el minisat y exiteamos al padre.
        if(error){
            perror("Failed executing minisat in slave:");
            exit(1);
        }

    }
    else{ 

        //Esperamos por el hijo
        wait(&childInfo);

        //Chequeamos si pudimos ejecutar el exevp correctamente.
        if(childInfo == EXIT_STATUS){
            close(pipefd[READ_END]);
            write(STDOUT_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
            return -1;
        }

        //Leemos del hijo.
        close(pipefd[1]); //No necesitamos write end
        while (read(pipefd[READ_END], satBuffer, sizeof(satBuffer)) != 0){}
        
        close(pipefd[READ_END]);// Close pertinente.

        //Guardamos la informacion del proceso en processInfo
        char processInfo[MAX_PROCESS_LENGTH];
        analyseSatResults(processInfo, satBuffer,inputBuffer);

        //Mandamos el resultado al application via stdout
        write(STDOUT_FILENO, processInfo, strlen(processInfo));

    }

    return 0;

}

void analyseSatResults(char * processInfo, char * buffer, char * fileName){

    char * occurPosition;
    char numberOfVariables[10];
    char numberOfClauses[10];
    char cpuTime[15];
    char satisfacible[10];

    occurPosition = strstr(buffer, "Number of variables:");
    if(occurPosition != NULL){
        sscanf(occurPosition, "Number of variables: %s", numberOfVariables);
    }
    else{//Caso raro si no esta el dato.
        perror("Failure finding the number of variables");
        //check that is null terminated
        strncpy(numberOfVariables,NO_INFO,strlen(NO_INFO));
    }

    //Notar que para no reccorrer todo de nuevo arrancamos en occurposition del anterior.
    occurPosition = strstr(buffer, "Number of clauses:");
    if(occurPosition != NULL){
        sscanf(occurPosition, "Number of clauses: %s",numberOfClauses);
    }
    else{ 
        perror("Failure finding the number of clauses");
        strncpy(numberOfClauses,NO_INFO,strlen(NO_INFO));
    }
    
    occurPosition = strstr(buffer, "CPU time");
    if(occurPosition != NULL){
        sscanf(occurPosition, "CPU time : %s ",cpuTime);
    }
    else{
        perror("Failure finding CPU time");
        strncpy(cpuTime,NO_INFO,strlen(NO_INFO));
    }

    //Ahora verificamos si es satisfacible o no.
    occurPosition = strstr(buffer,"UNSATISFIABLE");
    if(occurPosition != NULL){
        strncpy(satisfacible, "UNSAT", strlen("UNSAT"));
    }
    else if(strstr(buffer, "SATISFIABLE")!= NULL){ 
       strncpy(satisfacible, "SAT", strlen("SAT"));
    }
    else{
        perror("Error finding satisfacibility.");
        strncpy(satisfacible,NO_INFO,strlen(NO_INFO));
    }

    
    
    
    //Guardamos la informacion en process info.
    sprintf(processInfo,"%s %s %s %s %s %d\n", fileName, numberOfClauses,numberOfVariables,
    satisfacible,cpuTime,getpid());


}



void readFromStdin(char * inputBuffer, int size){
    //Leemos de la stdin hasta que ingrese una newline.
    char c = 0;
    int cnt = 0;
    while(read(STDIN_FILENO, &c, 1) != 0 && cnt < size - 1){
        if(c == '\n'){
            inputBuffer[cnt] = 0;
            return;
        }
        inputBuffer[cnt++] = c;
    }
    inputBuffer[cnt] = 0; //Por las dudas, hay que asegurar que termine con 0.
    return;
}

/*
    Selects file from the inputBuffer delimited with the FILE_DELIMITER.
    Saves them in the filesToProcess array.
    Returns: quantity of files to be processed
 */
int selectProcesses(char * inputBuffer, char * filesToProcess[]){
    const char * fileDelimiter = FILE_DELIMITER;
    char * token = strtok(inputBuffer,fileDelimiter);
    int cantP = 0;

    /* walk through other tokens */
    while(cantP < MAX_PROCESSES_ALLOWED && token != NULL ) {
        filesToProcess[cantP++] = token;
        token = strtok(NULL, fileDelimiter);
    }

    return cantP;
}

void cleanBuffer(char * buffer, int size){
    int i=0;
    for(;i<size;i++){
        buffer[i] = 0;
    }
}

/**
 * @file proj2.c
 * @author Henrich Sobotovič xsoboth00
 * @date 24/04/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h> 
#include <semaphore.h> 
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>

/**
 * @brief Data shared between the parent and child processes
 */
typedef struct
{
    int line_number;            // Global line counter 
    int vis_in_line;            // Remaining visitors who were not assigned to a cart yet
    int cart_load;              // Number of visitors the dispatcher has assigned to the next cart
    sem_t vis_boarding;         // Each visitor posts this after printing "boarding" 
    sem_t key;                  // Acts as binary mutex, initial value = 1
    sem_t vis_queue;            // Tells how many seats are available in the cart
    sem_t cart_ready;           // Dispatcher posts this to wake up one cart 
    sem_t vis_leaving;          // Each visitor posts this after they leave
    sem_t unboard;              // Cart posts this once per passanger to give each visitor permisson to leave
    sem_t cart_confirmed;       // Cart posts this to tell the dispatcher the cart has departed
    sem_t leaving_station;      // It ensures that only one cart occupies the exit station at a time
} data;

/**
 * @brief Allocates a block of shared memory that is readable and writable 
 * @param size Number of bytes to allocate 
 * @return Pointer to alocated memory or MAP_FAILED or error 
 */
void* create_shared_memory(size_t size) 
{
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

/**
 * @brief Checks if the argument is a valid decimal integer 
 *        It uses strtol so strings like "3.5", "4abc" are rejected 
 * @param str           Input string to validate 
 * @param out_value     Is set to the integer if successful 
 * @return true         if @p str is a decimal integer 
 * @return false        if the input is incorrect  
 */
bool is_integer(char *str, int *out_value) {
    char *last_arg;                         // Points to first char that is not a number
    long val = strtol(str, &last_arg, 10);  // strtol function that convert string to long integer (10 => decimal)
    
    // If the string is empty (str == last_arg) or the input looks something like "4abc" 
    if (str == last_arg || *last_arg != '\0') {
        return false; 
    }
    
    *out_value = (int)val;
    return true;
}

/**
 * @brief Main function where everything interesting happens 
 * @return 0 - all processes finished correctly 
 * @return 1 - error 
 */
int main(int argc, char *argv[]) {

    if (argc == 7)
    {
        FILE* output;
        output = fopen("proj2.out", "w");

        if(output == NULL)
        {
            fprintf(stderr, "Wasn't able to open the output file\n");
            exit(1);
        }
        
        // Parse all arguments into integers 
        int arguments [7] = {0};    // Index 0 is unused to we can intialize it 
        for (int i = 1; i < 7; i++)
        {
            bool is_okay = is_integer(argv[i], &arguments[i]);
            if(!is_okay)
            {
                fprintf(output, "Wrong arguments. %s is not an integer\n", argv[i]);
                fclose(output);
                exit(1);
            }
        }

        int V  = arguments [1]; // Number of carts     
        int N  = arguments [2]; // Number of visitors 
        int K  = arguments [3]; // Cart capacity
        int TV = arguments [4]; // Max ride duration in microseconds 
        int TN = arguments [5]; // Max visitor walk-to-queue time 
        int O  = arguments [6]; // Min gap between carts 

        int to_take = 0;        // How many visitors dispatcher assigns to cart

        setbuf(output, NULL);   // Function to disable buffering so we can print to disk instantly
        
        srand(getpid());        // PID for parent 

        // Range validation 
        if  ((V > 0   && V < 10)     && 
             (N > 0   && N < 10000)  && 
             (K >= 4  && K <= 40)    && 
             (TV >= 0 && TV <= 1000) &&
             (TN >= 0 && TN <= 1000) &&
             (O > 0   && O <= 100))
            {
                // Shared memory and semaphore initialization
                data *shared = create_shared_memory(sizeof(data));

                shared->line_number = 1;    // First action is numbered 1
                shared->vis_in_line = N;    // All visitors start as not assigned

                sem_init(&(shared->key),             1, 1); // Mutex, initial value 1 means unlocked

                // These semaphores start at 0 because nobody is waiting yet
                sem_init(&(shared->vis_queue),       1, 0); 
                sem_init(&(shared->vis_boarding),    1, 0);
                sem_init(&(shared->cart_ready),      1, 0);
                sem_init(&(shared->vis_leaving),     1, 0);
                sem_init(&(shared->unboard),         1, 0);
                sem_init(&(shared->cart_confirmed),  1, 0);

                // Semaphore for exit station, 1 means the station is free 
                sem_init(&(shared->leaving_station), 1, 1);

                pid_t dispatcher = fork();

                if(dispatcher < 0)
                {
                    fprintf(stderr, "Wasn't able to create dispatcher\n");
                    fclose(output);
                    exit(1);
                }
                // Child -> DISPATCHER 
                else if(dispatcher == 0)
                {
                    sem_wait(&(shared->key));
                    fprintf(output, "%d: D: started\n", shared->line_number++);
                    sem_post(&(shared->key));

                    // Main loop where dispatcher keep sending cart while 
                    // there are visitors waiting for the ride
                    while(true)
                    {
                        sem_wait(&(shared->key));

                        // If there are no visitors left, then break and move to shutdown
                        if (shared->vis_in_line <= 0) 
                        {
                            sem_post(&(shared->key));
                            break; 
                        }

                        // Either take take maximum capacity or who is left 
                        if (shared->vis_in_line < K)
                        {
                            to_take = shared->vis_in_line;
                        }
                        else 
                        {
                            to_take = K;
                        }

                        // Decrease the number of visitors waiting and 
                        // publish the number of visitors going for this ride 
                        shared->vis_in_line -= to_take;
                        shared->cart_load    = to_take;

                        fprintf(output, "%d: D: next cart\n", shared->line_number++);
                        sem_post(&(shared->key));

                        // Wake up the next available cart 
                        sem_post(&(shared->cart_ready)); 

                        // Wait for the cart to confirm it has departed before starting the timer
                        sem_wait(&(shared->cart_confirmed));

                        // Sleep for (0, O> microseconds 
                        usleep((rand()%(O)));
                                          
                    }

                    // If all visitors have been dispatched start closing the park
                    sem_wait(&(shared->key));
                    fprintf(output, "%d: D: closing\n", shared->line_number++);
                    sem_post(&(shared->key)); 

                    // Wake up every cart so they won't wait forever
                    for(int j = 0; j < V; j++)
                    {
                        sem_post(&(shared->cart_ready));
                    }

                    fclose(output);
                    exit(0);
                }

                for (int i = 1; i <= N; i++)
                {
                    pid_t visitors = fork();

                    if (visitors < 0)
                    {
                        fprintf(stderr, "Wasn't able to create visitors\n");
                        fclose(output);
                        exit(1);
                    }
                    // child -> VISITOR 
                    else if(visitors == 0)
                    {
                        // Each visitor gets a unique seed (Used XOR and quite large offset)
                        srand(getpid() ^ (i + 1000));

                        sem_wait(&(shared->key));
                        fprintf(output, "%d: N %d: started\n", shared->line_number++, i);
                        sem_post(&(shared->key));

                        // Walking time <0, TN> microseconds
                        if(TN > 0)
                        {
                            usleep((rand() % TN));
                        }

                        
                        sem_wait(&(shared->key));
                        fprintf(output, "%d: N %d: queue\n", shared->line_number++, i);
                        sem_post(&(shared->key));
                        
                        // Wait until the cart opens a seat for the visitor
                        sem_wait(&(shared->vis_queue));

                        sem_wait(&(shared->key));
                        fprintf(output, "%d: N %d: boarding\n", shared->line_number++, i);
                        sem_post(&(shared->key));
                        
                        // Tell cart that one passenger is seated
                        sem_post(&(shared->vis_boarding));
                        
                        // Wait for the permission to unboard at exit station
                        sem_wait(&(shared->unboard));

                        sem_wait(&(shared->key));
                        fprintf(output, "%d: N %d: leaving\n", shared->line_number++, i);
                        sem_post(&(shared->key));
                        
                        // Tell the cart visitor exited 
                        sem_post(&(shared->vis_leaving));
                        
                        fclose(output);
                        exit(0);
                    }
                }

                for (int i = 1; i <= V; i++)
                {
                    pid_t carts = fork();

                    if (carts < 0)
                    {
                        fprintf(stderr, "Wasn't able to create carts\n");
                        fclose(output);
                        exit(1);
                    }
                    // child -> CARTS 
                    else if(carts == 0)
                    {
                        sem_wait(&(shared->key));
                        fprintf(output, "%d: V %d: started\n", shared->line_number++, i);
                        sem_post(&(shared->key));
                        
                        // Each cart has different ride duration
                        srand(getpid() ^ i);

                        // Main cart loop that keeps cycling until the dispatcher signals shutdown
                        while(true)
                        {
                            // Wait in queue for signal
                            sem_wait(&(shared->cart_ready));
                            
                            sem_wait(&(shared->key));
                            
                            // Shutdown condition if there is no more visitors 
                            if (shared->vis_in_line <= 0 && shared->cart_load == 0)
                            {
                                fprintf(output, "%d: V %d: closed\n", shared->line_number++, i);
                                sem_post(&(shared->key));
                                fclose(output);
                                exit(0);
                            }
                            
                            // Get the cargo number that dispatcher prepared
                            int cart_cargo = shared->cart_load;

                            // Reset cart_load so the next cart doesn't re-use 
                            shared->cart_load = 0;

                            fprintf(output, "%d: V %d: boarding started\n", shared->line_number++, i);
                            sem_post(&(shared->key));
                            
                            // Give cart_cargo number of seats so the right number 
                            // of visitors board 
                            for (int j = 0; j < cart_cargo; j++)
                            {
                                sem_post(&(shared->vis_queue));
                            }

                            // Wait until every boarding visitor confirm they are in the cart 
                            for(int j = 0; j < cart_cargo; j++)
                            {
                                sem_wait(&(shared->vis_boarding));
                            }


                            sem_wait(&(shared->key));
                            fprintf(output, "%d: V %d: boarding complete\n", shared->line_number++, i);
                            sem_post(&(shared->key));

                            // Tell the dispatcher the cart has left 
                            sem_post(&(shared->cart_confirmed));
                            
                            // Random duration of the ride (0, TV> microseconds
                            if(TV > 0)
                            {
                                usleep((rand() % TV));
                            }

                            // Get access to the exit station 
                            // If there is another cart, wait 
                            sem_wait(&(shared->leaving_station));
                            
                            sem_wait((&(shared->key))); 
                            fprintf(output, "%d: V %d: leaving started\n", shared->line_number++, i);
                            sem_post(&(shared->key));

                            // Give each passanger permission to unboard
                            for (int j = 0; j < cart_cargo; j++) 
                            {
                                sem_post(&(shared->unboard));
                            }

                            // Wait for every passenger to fully exit
                            for (int j = 0; j < cart_cargo; j++) 
                            {
                                sem_wait(&(shared->vis_leaving));
                            }

                            sem_wait(&(shared->key));
                            fprintf(output, "%d: V %d: leaving complete\n", shared->line_number++, i);
                            
                            // Release the exit station before releasing the mutex so another
                            // cart can enter 
                            sem_post(&(shared->leaving_station));
                            sem_post(&(shared->key));
                        }
                    }
                }

                // Block until every child process has exited
                // wait() returns -1 when no more children exist
                while(wait (NULL) > 0);

                // Destroy every semaphore 
                sem_destroy(&(shared->key));
                sem_destroy(&(shared->cart_confirmed));
                sem_destroy(&(shared->cart_ready));
                sem_destroy(&(shared->unboard));
                sem_destroy(&(shared->vis_boarding));
                sem_destroy(&(shared->vis_leaving));
                sem_destroy(&(shared->vis_queue));
                sem_destroy(&(shared->leaving_station));
                
                // Release the shared memory
                munmap(shared, sizeof(data));
                
                fclose(output);
            }
        else
        {
            fprintf(stderr, "Wrong range for arguments\n");
            fclose(output);
            return 1;
        } 
    }
    else
    {
    fprintf(stderr, "Incorrect number of arguments\n");
    return 1;
    }
    return 0;
}

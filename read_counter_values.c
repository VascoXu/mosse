#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define __NR_set_task_by_pid 436
#define __NR_get_counter_values 437
#define dev "/dev/mosse"
#define N 20

// Stores collection of counter values
struct counter_values {
   long long cpu_cycles_saved;
   long long cpu_instructions_saved;
};

void get_counter_values(pid_t pid) 
{
   syscall(__NR_get_counter_values, pid);
}

void set_task(pid_t pid) 
{
   syscall(__NR_set_task_by_pid, pid);
}

void multiplyMatrix()
{
   int mat1[N][N];
   int mat2[N][N];
   int res[N][N];
   int i, j, k;
   
   // Generate matrices
   for (i = 0; i < N; i++) {
      for (j = 0; j < N; j++) {
         mat1[i][j] = rand() % 10;
         mat2[i][j] = rand() % 10;
      }
   }
   
   // Multiply matrix
   for (i = 0; i < N; i++) {
      for (j = 0; j < N; j++) {
         res[i][j] = 0;
         for (k = 0; k < N; k++) {
            res[i][j] += mat1[i][k]*mat2[k][j];
         }
      }
   }
}

int main()
{
   int i;

   // Get the current pid
   pid_t pid = getpid();

   // Set Task
   set_task(pid);

   // Open file for writing counter values
   FILE *fp = fopen("cpu_cycles_saved.txt", "w+");

   get_counter_values(pid);
   int fd = open(dev, O_RDWR, S_IWUSR | S_IRUSR);
   if (fd < 0) {
      fprintf(stderr, "Unable to read value from user space \n");
   }

   struct counter_values cv;
   for (;;) {
      get_counter_values(pid);
      read(fd, &cv, sizeof(struct counter_values)); 
      for (i = 0; i < 50; i++) multiplyMatrix();
      printf("cpu_cycles_saved: %llu\n", cv.cpu_cycles_saved); 
      printf("cpu_instructions_saved: %llu\n", cv.cpu_instructions_saved); 
      //fputs(cv.cpu_cycles_saved, fp);
      sleep(3);
   }
}

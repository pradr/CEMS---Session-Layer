#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h> 
#include <unistd.h>
#include <stdio.h>

int inputAvailable()  
{
  struct timeval tv;
  fd_set fds;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
  return (FD_ISSET(0, &fds));
}


void main() {

        char session_data[10000] = {0};
        int num_bytes = 0;
        FILE *fp;


        

        char *s;

        while(1) {

                  if (!inputAvailable()) {
                          printf("Waiting for input..\n");
                          sleep(1);
                  }

                        
                  printf("Reading...\n");

                  s = fgets(session_data + num_bytes, 15, stdin);

                  if (s == NULL) {
                          printf("Sleeping\n");
                          sleep(5);
                          continue;
                  }

                  num_bytes = strlen(session_data);
                        
                  printf("----------%d----------\n", num_bytes);
                        printf("%s", session_data);
                        printf("----------------------\n");
                  
                  if (num_bytes >= 85) {
                          fp = fopen("read.txt", "a+");
                          fwrite(session_data, num_bytes, 1, fp);
                          fclose(fp);
                          bzero(session_data, 10000);
                          num_bytes = 0;
                          sleep(1);
                          //break;
                  }
        }
}

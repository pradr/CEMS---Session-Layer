#include <stdio.h>
#include <string.h>
int main() {
        
        int count = 0;
        char str[100] = {0};
        FILE *fp;


        int i = 0;

        int ting  = 0;
        while(1) {

                if (i % 2 == 0) {
                        sprintf(str, "%s-%d\n", "Rambo", i++);
                } else  if (i% 3 ==0) {
                        sprintf(str, "%s-%d\n", "Jack-sparrow", i++);
                } else {
                        sprintf(str, "%s-%d\n", "Optimus-Prime", i++);
                }

                ting += strlen(str);
                fp = fopen("cool.txt", "a+");
                count = fwrite(str, strlen(str), 1, fp);
                count = fwrite(str, strlen(str), 1, stdout);
                fclose(fp);
                sleep(1);
/*
                if (ting >= 10000) {
                        ting = 0;
                        sleep(10);
                }
                */
    }
 
    return 1;
}


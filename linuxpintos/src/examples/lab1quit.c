#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <stdarg.h>

#define FD_TEST_COUNT 128
#define READ_SIZE 50
#define READ_CONSOLE_COUNT 10

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

#define TITLE(x) printf(WHT x RESET)
#define ERROR(x, ...) printf(RED "ERR: " x RESET, ##__VA_ARGS__); halt()
#define SUCCESS(x) printf(GRN x RESET)




int main(void)
{
  halt();

  exit(0);
}

#include <time.h>
#include <fenv.h>
#include <omp.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
// #include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysinfo.h> // for memory check

// #include <pthread.h>

#ifndef __linux__
// #include <windows.h>
#endif

#include <stdbool.h>
#include <inttypes.h>
// #include <stdatomic.h>





#define BREAK_PROGRAM __asm__("int $0x3")

#define PRINFO(format, ...) printf("%s:%d %s:  " format "\n", __FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__);

#define PR(format, ...) printf("%s:%d   " format "\n", __FILE__, __LINE__, __VA_ARGS__);

#define PR1(value) PR("%f", value)
#define PR2(value1, value2) PR("%f %f", value1, value2)


#define LOADER(format, ...)\
printf("%s:%d   " format "\r", __FILE__, __LINE__, __VA_ARGS__);\
fflush(stdout);




#define PERROR(format, ...) {fprintf(stderr, "%s:%d   " format "\n", __FILE__, __LINE__, __VA_ARGS__); exit(EXIT_FAILURE);}

#define PERROR_BREAK(error_message, ...) {fprintf(stderr, "%s:%d\n"error_message, __FILE__, __LINE__, __VA_ARGS__); __asm__("int $0x3");}

#define ERROR_CHECK(condition) {if (__builtin_expect(condition, false)) PERROR(#condition)}


#define MALLOC(size) malloc_checked(size, __FILE__, __LINE__)




static inline void* malloc_checked(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (!ptr) {
        printf("%s:%d Memory allocation failed! Requested size: %zu bytes", file, line, size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}




#define MIN_FREE_MEMORY_MB 500 

// Function to get available memory (Linux)
static inline size_t get_available_memory() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram;  // Available RAM in bytes
    }
    return 0;  // If sysinfo fails, assume no available memory
}

// Safe malloc function with system memory check
static inline void* malloc_safe(size_t size, const char *file, int line) {
    size_t available_memory = get_available_memory();

    if (available_memory == 0) {
        printf("%s:%d Failed to check available memory! Exiting.", file, line);
        exit(EXIT_FAILURE);
    }

    if (available_memory < size + (MIN_FREE_MEMORY_MB * 1024)) {
        printf("%s:%d Not enough RAM available. Needed: %lu bytes, Available: %lu bytes", 
            file, line, (unsigned long)size, (unsigned long)available_memory);
        exit(EXIT_FAILURE);
    }

    void *ptr = malloc(size);
    if (!ptr) {
        printf("%s:%d Memory allocation failed! Requested size: %zu bytes", file, line, size);
        exit(EXIT_FAILURE);
    }

    return ptr;
}

#define MALLOC_SAFE(size) malloc_safe(size, __FILE__, __LINE__)





// verifies if a number is normal, hault the program if the number is not normal
#define verifyIsNormal(number__, variable__, iter__)\
{\
    if (isnan(number__) || isinf(number__) || number__ >= __FLT_MAX__)\
    {\
        printf("\n***\n%s:%d \nat iteration: %d\n%s not normal:\n%f or %e\n***\n", __FILE__, __LINE__, iter__, variable__, number__, number__);\
        __asm__("int $0x3");\
    }\
}\




#define FROM_FILE(array_pointer, file_name)\
{\
    OPEN(fp, file_name, "rb");\
    GET_FILE_SIZE(fp, file_len, file_name);\
    FREAD(array_pointer, 1, file_len, fp);\
    CLOSE(fp);\
}




/*
after a file has been opened, this macro will get the size of the file in bytes.

"file_len" is a variable that will be declared from within this macro.
*/
#define GET_FILE_SIZE(file_pointer, file_len, file_name)\
(void)fseek(file_pointer, 0L, SEEK_END);\
uint32_t file_len = (uint32_t)ftell(file_pointer);\
if (fseek(file_pointer, 0, SEEK_SET) != 0)\
{\
    fprintf(stderr, "%s:%d\nError returning the file pointer back to the beginning of the file: %s.\n%s\n", __FILE__, __LINE__, file_name, strerror(errno));\
    exit(EXIT_FAILURE);\
}




/*
Open a file and check for errors; 
then print them to the console; 
then exit the program if there were errors.
Makes the file pointer in the macro

"file_pointer_name" : FILE pointer
"file_name"         : string
"open_type"         : string; what type of file operation
*/
#define OPEN(file_pointer_name, file_name, open_type) \
FILE *file_pointer_name; \
if ((file_pointer_name = fopen(file_name, open_type)) == NULL) \
{ \
	fprintf(stderr, "In Macro: %s:%d\nError Opening File: %s\n%s\n", __FILE__, __LINE__, file_name, strerror(errno)); \
	exit(EXIT_FAILURE); \
} 


/*
Open a file and check for errors; 
then print them to the console; 
then exit the program if there were errors.

"file_pointer_name" : FILE pointer
"file_name"         : string
"open_type"         : string; what type of file operation
*/
#define OPEN_FP(file_pointer_name, file_name, open_type) \
if ((file_pointer_name = fopen(file_name, open_type)) == NULL) \
{ \
	fprintf(stderr, "In Macro: %s:%d\nError Opening File: %s\n%s\n", __FILE__, __LINE__, file_name, strerror(errno)); \
	exit(EXIT_FAILURE); \
} 


/*
Close a file and check for errors; 
then print them to the console; 
then exit the program if there were errors.

"file_pointer_name": FILE pointer
*/
#define CLOSE(file_pointer_name) \
if (fclose(file_pointer_name) == EOF) \
{ \
	fprintf(stderr, "In Macro: %s:%d\nError Closing File.\n%s\n", __FILE__, __LINE__, strerror(errno)); \
	exit(EXIT_FAILURE); \
}

/*
Writes to a file and checks for errors; 
then print them to the console; 
then exit the program if there were errors.

array: the array to be saved to the file
size: the size of one of the elements of the array
array_size: number of elements in the array
file_pointer: FILE pointer
*/
#define FWRITE(array_pointer, size, array_len, file_pointer) \
if(fwrite(array_pointer, size, array_len, file_pointer) < array_len) \
{ \
    fprintf(stderr, "In Macro: %s:%d\nError Writing To File.\n%s\n", __FILE__, __LINE__, strerror(errno)); \
    exit(EXIT_FAILURE); \
} \

#define FREAD(array_pointer, size, array_len, file_pointer) \
{size_t byes_read = fread(array_pointer, size, array_len, file_pointer); \
if(byes_read < array_len && errno != 0) \
{ \
    fprintf(stderr, "In Macro: %s:%d\nError Reading From File.\n%s\n%d read: %d desired.\n", __FILE__, __LINE__, strerror(errno), (uint32_t)size, (uint32_t)array_len); \
    exit(EXIT_FAILURE); \
}}



#define TO_FILE(array_pointer, file_name)\
{\
    OPEN(__fp__, file_name, "wb");\
    FWRITE(array_pointer, 1, sizeof(array_pointer), __fp__);\
    CLOSE(__fp__);\
}

// runs the system() command and checks the output.
#define SYSTEM(command) \
if (system(command) != 0) \
{ \
    fprintf(stderr, "In Macro: %s:%d\nError Executing Command: %s\n%s\n", __FILE__, __LINE__, command, strerror(errno)); \
    exit(EXIT_FAILURE); \
}


// Allocates memory and checks if the allocation was successful.
#define SAFE_MALLOC(ptr, size) \
{ \
    ptr = malloc(size); \
    if (ptr == NULL) \
    { \
        PR("Error Allocating Memory (%zu bytes): %s\n", size, strerror(errno)); \
        exit(EXIT_FAILURE); \
    } \
}


/*
print any datatype with a new line after it.
*/
#define PRINTLN(x) \
do { \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wformat\"") \
	_Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"") \
    _Generic((x), \
        char:                           printf("%c\n",   x), \
        signed char:                    printf("%hhd\n", x), \
        unsigned char:                  printf("%hhu\n", x), \
        signed short:                   printf("%hd\n",  x), \
        unsigned short:                 printf("%hu\n",  x), \
        signed int:                     printf("%d\n",   x), \
        unsigned int:                   printf("%u\n",   x), \
        long int:                       printf("%ld\n",  x), \
        unsigned long int:              printf("%lu\n",  x), \
        long long int:                  printf("%lld\n", x), \
        unsigned long long int:         printf("%llu\n", x), \
        float:                          printf("%f\n",   x), \
        double:                         printf("%f\n",   x), \
        long double:                    printf("%Lf\n",  x), \
        char *:                         printf("%s\n",   x), \
        default:                        printf("%p\n", (void*)&(x)) \
    ); \
    _Pragma("GCC diagnostic pop") \
} while(0)

/*
print any datatype with a new line after it.
*/
#define PRINT(x) \
do { \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wformat\"") \
	_Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"") \
    _Generic((x), \
        char:                           printf("%c ",   x), \
        signed char:                    printf("%hhd ", x), \
        unsigned char:                  printf("%hhu ", x), \
        signed short:                   printf("%hd ",  x), \
        unsigned short:                 printf("%hu ",  x), \
        signed int:                     printf("%d ",   x), \
        unsigned int:                   printf("%u ",   x), \
        long int:                       printf("%ld ",  x), \
        unsigned long int:              printf("%lu ",  x), \
        long long int:                  printf("%lld ", x), \
        unsigned long long int:         printf("%llu ", x), \
        float:                          printf("%f ",   x), \
        double:                         printf("%f ",   x), \
        long double:                    printf("%Lf ",  x), \
        char *:                         printf("%s ",   x), \
        default:                        printf("%p ", (void*)&(x)) \
    ); \
    _Pragma("GCC diagnostic pop") \
} while(0)



/*
Do Not Call From "C"
Only macros should call this macro because it does not ignore certain formatting errors with "pragma"
*/
#define PRINT_TWO(x, a) \
do { \
    _Generic((x), \
        char:                           printf("%c%s",   x, a), \
        signed char:                    printf("%hhd%s", x, a), \
        unsigned char:                  printf("%hhu%s", x, a), \
        signed short:                   printf("%hd%s",  x, a), \
        unsigned short:                 printf("%hu%s",  x, a), \
        signed int:                     printf("%d%s",   x, a), \
        unsigned int:                   printf("%u%s",   x, a), \
        long int:                       printf("%ld%s",  x, a), \
        unsigned long int:              printf("%lu%s",  x, a), \
        long long int:                  printf("%lld%s", x, a), \
        unsigned long long int:         printf("%llu%s", x, a), \
        float:                          printf("%f%s",   x, a), \
        double:                         printf("%f%s",   x, a), \
        long double:                    printf("%Lf%s",  x, a), \
        char *:                         printf("%s%s",   x, a), \
        default:                        printf("%d%s",   x, a) \
    ); \
} while(0)




/*
print up to a 4d array
dimension: enter 1, 2, 3, 4 as the dimension of the array
the dimensions of the array should be spelled out at compilation time becuase this macro uses that information.
*/

#define PRINT_ARR(values) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wformat\"") \
_Pragma("GCC diagnostic ignored \"-Wsign-conversion\"") \
_Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"") \
	for (unsigned int ITERATOR = 0; ITERATOR < sizeof(values) / sizeof(values[0]); ++ITERATOR) \
		PRINT_TWO(values[ITERATOR], "\n"); \
_Pragma("GCC diagnostic pop")
    

/*
print an array from a start to a stop index.
*/
#define PRINT_SIZE(values, start, stop) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wformat\"") \
_Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"") \
for (unsigned int ITERATOR = start; ITERATOR < stop; ++ITERATOR) \
		PRINT_TWO(values[ITERATOR], "\n"); \
puts("");\
_Pragma("GCC diagnostic pop")
    


// Macros for printing one, two, three, ... arguments.
#define PRINT_1(x     ) PRINT_TWO(x, "\n");
#define PRINT_2(x, ...) PRINT_TWO(x, " " ); PRINT_1(__VA_ARGS__);
#define PRINT_3(x, ...) PRINT_TWO(x, " " ); PRINT_2(__VA_ARGS__);
#define PRINT_4(x, ...) PRINT_TWO(x, " " ); PRINT_3(__VA_ARGS__);
#define PRINT_5(x, ...) PRINT_TWO(x, " " ); PRINT_4(__VA_ARGS__);
#define PRINT_6(x, ...) PRINT_TWO(x, " " ); PRINT_5(__VA_ARGS__);
#define PRINT_7(x, ...) PRINT_TWO(x, " " ); PRINT_6(__VA_ARGS__);
#define PRINT_8(x, ...) PRINT_TWO(x, " " ); PRINT_7(__VA_ARGS__);

#define GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME

/*
can print up to 8 arguments no matter what datatype they are.
*/
#define PRINTS(...) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wformat\"") \
_Pragma("GCC diagnostic ignored \"-Wformat-extra-args\"") \
GET_MACRO(__VA_ARGS__, PRINT_8, PRINT_7, PRINT_6, PRINT_5, PRINT_4, PRINT_3, PRINT_2, PRINT_1)(__VA_ARGS__); \
_Pragma("GCC diagnostic pop")


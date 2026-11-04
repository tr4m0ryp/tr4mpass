#ifndef LOG_H
#define LOG_H

extern bool debug_enabled;

/* LOG macro */
#define ERROR(x, ...)             do { printf("\x1b[31m[%s] ERROR:"x"\x1b[39m\n", __FUNCTION__, ##__VA_ARGS__); } while(0)
#define DEBUGLOG(x, ...)          do { if(debug_enabled) printf("\x1b[34m[%s] "x"\x1b[39m\n", __FUNCTION__, ##__VA_ARGS__); } while(0)
#define LOG_EXPLOIT_NAME(x, ...)  do { printf("** \x1b[31mexploiting with "x"\x1b[39m\n", ##__VA_ARGS__); } while(0)
#define LOG(x, ...)               do { printf("\x1b[32m[%s] "x"\x1b[39m\n", __FUNCTION__, ##__VA_ARGS__); } while(0)
#define LOG_NOFUNC(x, ...)        do { printf("\x1b[32m"x"\x1b[39m\n", ##__VA_ARGS__); } while(0)

#endif

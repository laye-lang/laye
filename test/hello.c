
#define NICE 69
#define STR_(Y) # Y
#define STR(X) STR_(X)
const char* number = STR(NICE);
const char* word = STR_(NICE);

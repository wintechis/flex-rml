#include "custom_io.h"

/////////////////////////////////
///// Platform dependent IO /////
/////////////////////////////////
#include <cstdlib>
#include <iostream>

// Enable debug output
// 0 == off
// 1 == on
#define DEBUG_MODE 0

void logln_debug(const char *str) {
#if DEBUG_MODE == 1
  std::cout << str << std::endl;
#else
  (void)str;
#endif
}

void log_debug(const char *str) {
#if DEBUG_MODE == 1
  std::cout << str;
#else
  (void)str;
#endif
}

void log_debug(int value) {
#if DEBUG_MODE == 1
  std::cout << value;
#else
  (void)value;
#endif
}

void logln_debug(int value) {
#if DEBUG_MODE == 1
  std::cout << value << std::endl;
#else
  (void)value;
#endif
}

/////////////////////////////
/// NON Debug
/////////////////////////////

//// CONST CHAR ////

// Custom log function
// Custom log line function
void logln(const char *str) {
  std::cout << str << std::endl;
}

// Custom log function
void log(const char *str) {
  std::cout << str;
}

//// CONST INT ////

// Overloaded log function for int input
void log(int value) {
  std::cout << value;
}

// Overloaded logln function for int input
void logln(int value) {
  std::cout << value << std::endl;
}

// Throws an error an stops the program
void throw_error(const char *error_text) {
  logln(error_text);

  std::exit(EXIT_FAILURE);
}
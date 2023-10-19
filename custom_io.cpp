#include "custom_io.h"

/////////////////////////////////
///// Platform dependent IO /////
/////////////////////////////////

#ifdef ARDUINO
// Code for Arduino platforms
#include <Arduino.h>
#else
// Code for non-Arduino platforms
#include <cstdlib>
#include <iostream>
#endif

// Custom log line in debug mode function
void logln_debug(const char *str) {
  if (debug_mode_flag) {
#ifdef ARDUINO
    Serial.println(str);
#else
    std::cout << str << std::endl;
#endif
  }
}

// Custom log in debug mode function
void log_debug(const char *str) {
  if (debug_mode_flag) {
#ifdef ARDUINO
    Serial.print(str);
#else
    std::cout << str;
#endif
  }
}

// Overloaded log_debug function for int input
void log_debug(int value) {
  if (debug_mode_flag) {
#ifdef ARDUINO
    Serial.print(value);
#else
    std::cout << value;
#endif
  }
}

// Overloaded logln_debug function for int input
void logln_debug(int value) {
  if (debug_mode_flag) {
#ifdef ARDUINO
    Serial.println(value);
#else
    std::cout << value << std::endl;
#endif
  }
}

/////////////////////////////
/// NON Debug
/////////////////////////////

//// CONST CHAR ////

// Custom log function
// Custom log line function
void logln(const char *str) {
#ifdef ARDUINO
  Serial.println(str);
#else
  std::cout << str << std::endl;
#endif
}

// Custom log function
void log(const char *str) {
#ifdef ARDUINO
  Serial.print(str);
#else
  std::cout << str;
#endif
}

//// CONST INT ////

// Overloaded log function for int input
void log(int value) {
#ifdef ARDUINO
  Serial.print(value);
#else
  std::cout << value;
#endif
}

// Overloaded logln function for int input
void logln(int value) {
#ifdef ARDUINO
  Serial.println(value);
#else
  std::cout << value << std::endl;
#endif
}

// Throws an error an stops the program
void throw_error(const char *error_text) {
  logln(error_text);
#ifdef ARDUINO
  while (1)
    ;
#else
  std::exit(EXIT_FAILURE);
#endif
}
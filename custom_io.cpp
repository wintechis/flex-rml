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

// Enable debug output
// 0 == off
// 1 == on
#define DEBUG_MODE 0

void logln_debug(const char *str) {
#if DEBUG_MODE == 1
#ifdef ARDUINO
  Serial.println(str);
#else
  std::cout << str << std::endl;
#endif
#else
  (void)str;
#endif
}

void log_debug(const char *str) {
#if DEBUG_MODE == 1
#ifdef ARDUINO
  Serial.print(str);
#else
  std::cout << str;
#endif
#else
  (void)str;
#endif
}

void log_debug(int value) {
#if DEBUG_MODE == 1
#ifdef ARDUINO
  Serial.print(value);
#else
  std::cout << value;
#endif
#else
  (void)value;
#endif
}

void logln_debug(int value) {
#if DEBUG_MODE == 1
#ifdef ARDUINO
  Serial.println(value);
#else
  std::cout << value << std::endl;
#endif
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
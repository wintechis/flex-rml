#ifndef JSON_HANDLING_H
#define JSON_HANDLING_H

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stack>
#include <string>

#include "custom_io.h"
#include "Json/ArduinoJson.h"

std::string get_next_element_csv(std::ifstream& file, std::vector<std::string>& json_path_tokens);
void reinit_json(std::ifstream& file);

#endif
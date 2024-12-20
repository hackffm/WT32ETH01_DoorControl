#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class UserDatabase {
private:
    static DynamicJsonDocument userData;  // Static document for storing JSON data
    const char* filename;

public:
    UserDatabase(const char* filename);

    // Function to load user data from JSON file
    bool loadUserData();

    // Function to save the current JSON data back to the file
    bool saveUserData();

    // Function to find a user by their code (e.g. "ab1")
    bool findUser(const char* code, String &name, String &flags);

    // Function to add a new user to the database
    bool addUser(const char* code, const char* name, const char* flags);

    // Function to delete a user from the database
    bool deleteUser(const char* code);
};

extern UserDatabase UserDB;


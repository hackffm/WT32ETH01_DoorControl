#include "userdb.h"
#include "LL_Lib.h"

// Initialize the static userData object
DynamicJsonDocument UserDatabase::userData(1024*32);

// Constructor to initialize the filename
UserDatabase::UserDatabase(const char* filename) : filename(filename) {}

// Function to load user data from JSON file
bool UserDatabase::loadUserData() {
    File file = LittleFS.open(filename, "r");
    if (!file) {
        LL_Log.println("Error opening the file");
        return false;
    }

    DeserializationError error = deserializeJson(userData, file);
    if (error) {
        LL_Log.print("Error parsing the JSON file: ");
        LL_Log.println(error.c_str());
        file.close();
        return false;
    }

    file.close();
    return true;
}

// Function to find a user by their code (e.g. "ab1")
bool UserDatabase::findUser(const char* code, String &name, String &flags) {
    JsonObject users = userData["users"];
    if (!users.containsKey(code)) {
        LL_Log.println("User not found");
        return false;
    }

    JsonObject user = users[code];
    name = user["name"].as<String>();
    flags = user["flags"].as<String>();
    return true;
}

// Function to add a new user to the database
bool UserDatabase::addUser(const char* code, const char* name, const char* flags) {
    JsonObject users = userData["users"];

    // Check if user already exists
    if (users.containsKey(code)) {
        LL_Log.println("User already exists");
        return false;
    }

    // Create a new user object
    JsonObject newUser = users.createNestedObject(code);
    newUser["name"] = name;
    newUser["flags"] = flags;

    LL_Log.println("User added successfully");
    return true;
}

// Function to delete a user from the database
bool UserDatabase::deleteUser(const char* code) {
    JsonObject users = userData["users"];

    // Check if user exists
    if (!users.containsKey(code)) {
        LL_Log.println("User not found");
        return false;
    }

    // Remove the user
    users.remove(code);
    LL_Log.println("User deleted successfully");
    return true;
}

// Function to save the current JSON data back to the file
bool UserDatabase::saveUserData() {
    File file = LittleFS.open(filename, "w");
    if (!file) {
        LL_Log.println("Error opening the file for writing");
        return false;
    }

    // Serialize and write the JSON data to the file
    if (serializeJsonPretty(userData, file) == 0) {
        LL_Log.println("Failed to write to file");
        file.close();
        return false;
    }

    file.close();
    LL_Log.println("User data saved successfully");
    return true;
}

// default instance
UserDatabase UserDB("/db.json");

/*  cjsontest-w
.c 7/30/2026   */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../cJSON.h"

void readJSONFile(const char *filename) {
    // Open the JSON file for reading
    FILE *file = fopen(filename, "r");
    
    if (!file) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return;
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read the entire file into a buffer
    char *buffer = (char *)malloc(fileSize + 1);
    fread(buffer, 1, fileSize, file);
    buffer[fileSize] = '\0'; // Null-terminate the string

    // Close the file
    fclose(file);

    // Parse the JSON data
    cJSON *json = cJSON_Parse(buffer);

    // Check if parsing was successful
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        cJSON_Delete(json);
        free(buffer);
        return;
    }

    // Process the JSON data (modify as needed)
    // ...

    // Clean up
    cJSON_Delete(json);
    free(buffer);
}

void write_json_to_file(const char *filename)
{                                       // example for h316 firmware
    cJSON *json = cJSON_CreateObject(); // empty top-level object
    cJSON *ptr;                         // work pointer
    cJSON *ptr2;                        // work pointer
    cJSON *ptr3;                        // work pointer
    cJSON *new; //
    cJSON *array;
    int wint = 13;                      // dummy register value
    int wint1 = 14;
    int wint2 = 15;

    ptr = cJSON_AddStringToObject(json, "Button", "Start"); // report button press
    cJSON_AddNumberToObject(ptr, "State", 123456);
    cJSON_AddNumberToObject(ptr, "Value", 000000);


printf("%s\n",cJSON_Print(json));

    array = cJSON_AddArrayToObject(json, "Registers");   // add array of register values

printf("%s\n",cJSON_Print(json));

    new = cJSON_CreateObject(); // object to be added to array
    ptr2 = cJSON_AddNumberToObject(new, "A", wint);

printf("register item\n%s\n",cJSON_Print(ptr2));
    cJSON_AddItemToArray(array,new);
printf("new:\n%s\n",cJSON_Print(array));

    new = cJSON_CreateObject();
    ptr2 = cJSON_AddNumberToObject(new, "B", wint1);
    cJSON_AddItemToArray(array,new);

    new = cJSON_CreateObject();
    ptr3 = cJSON_AddNumberToObject(new, "X", wint2);
    cJSON_AddItemToArray(array,new);

printf("final:\n%s\n",cJSON_Print(json));

    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "Could not open %s for writing\n", filename);
        cJSON_Delete(json);
        return;
    }

    char *json_string = cJSON_Print(json);  // output all objects as text
    fprintf(file, "%s", json_string);

    fclose(file);
    cJSON_Delete(json); // cleanup
    free(json_string);
}

int main() {

    write_json_to_file("example.json");

    const char *filename = "example.json";
    readJSONFile(filename);

    return 0;
};